// lv8 triage: in-process V8 Inspector wiring.
//
// Purpose: non-invasive closure-variable visibility at exception pauses.
// Unlike source rewriting (which shifts the challenge VM's timing-dependent
// branches), the inspector observes the running program without changing it.
//
// Threading model: single-threaded, no WebSocket.  On every pause V8 calls
// runMessageLoopOnPause, which executes a CANNED sequence pre-registered
// from Python: evaluateOnCallFrame(watch expression) on frame 0, then
// Debugger.resume.  Responses/notifications accumulate in |outgoing| and are
// collected by Python via InspectPoll() after the run.

#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "v8-inspector.h"
#include "v8.h"

struct InspectorState
{
    bool created = false;
    bool paused = false;
    int next_id = 100;
    int trace_max = 0;         // 0 = resume at pauses; >0 = step-trace budget
    int trace_steps = 0;       // steps issued so far
    std::string last_call_frame_id = "0"; // from the latest Debugger.paused
    std::string last_local_scope_id;      // top frame's local scope objectId
    bool scope_dump = false;              // watch "__SCOPE__" => dump scope vars
    long long notifications_discarded = 0;
    std::string watch;                 // canned evaluateOnCallFrame expression
    std::vector<std::string> outgoing; // protocol messages from V8 (ring-capped)
    v8::Persistent<v8::Context> context;
    std::unique_ptr<v8_inspector::V8InspectorClient> client;
    std::unique_ptr<v8_inspector::V8Inspector> inspector;
    std::unique_ptr<v8_inspector::V8Inspector::Channel> channel;
    std::unique_ptr<v8_inspector::V8InspectorSession> session;
};

inline std::string StringBufferToUtf8(
    const v8_inspector::StringBuffer& buffer)
{
    v8_inspector::StringView view = buffer.string();
    if (view.is8Bit())
        return std::string(reinterpret_cast<const char*>(view.characters8()),
                           view.length());
    // UTF-16 backedge: encode minimally (BMP chars as-is, else '?')
    std::string out;
    out.reserve(view.length());
    for (size_t i = 0; i < view.length(); i++)
    {
        uint16_t ch = view.characters16()[i];
        if (ch < 0x80)
            out.push_back(static_cast<char>(ch));
        else
            out.push_back('?');
    }
    return out;
}

class InspectChannel : public v8_inspector::V8Inspector::Channel
{
public:
    explicit InspectChannel(InspectorState* state) : state_(state) {}

    void sendResponse(int, std::unique_ptr<v8_inspector::StringBuffer> message)
        override
    {
        // keep watch-evaluation responses (small); hard ring cap bounds
        // memory when the step budget is large
        state_->outgoing.push_back(StringBufferToUtf8(*message));
        if (state_->outgoing.size() > 400000)
            state_->outgoing.erase(state_->outgoing.begin(),
                                   state_->outgoing.begin() + 100000);
    }
    void sendNotification(
        std::unique_ptr<v8_inspector::StringBuffer> message) override
    {
        // paused events are the memory hogs (full callFrames): extract only
        // the callFrameId and discard the body
        std::string text = StringBufferToUtf8(*message);
        if (text.find("Debugger.paused") != std::string::npos)
        {
            auto pos = text.find("\"callFrameId\":\"");
            if (pos != std::string::npos)
            {
                auto begin = pos + 15;
                auto end = text.find('"', begin);
                if (end != std::string::npos)
                    state_->last_call_frame_id =
                        text.substr(begin, end - begin);
            }
            // G4.131: surface the pause to Python with the live callFrameId
            // and the top frame's function name, so the driver can issue
            // additional evaluateOnCallFrame requests against this frame
            // (closure-level reads / write-backs) while V8 is paused.
            // G4.134: surface up to 4 frames (id + function name) so the
            // driver can walk the whole paused call chain.
            {
                std::string fn;
                auto fpos = text.find("\"functionName\":\"");
                if (fpos != std::string::npos)
                {
                    auto fb = fpos + 16;
                    auto fe = text.find('"', fb);
                    if (fe != std::string::npos)
                        fn = text.substr(fb, fe - fb);
                }
                std::string frames_json = "[";
                size_t scan = 0;
                int taken = 0;
                while (taken < 4)
                {
                    size_t cpos = text.find("\"callFrameId\":\"", scan);
                    if (cpos == std::string::npos)
                        break;
                    size_t cb = cpos + 15;
                    std::string cf;
                    bool esc = false;
                    size_t k = cb;
                    for (; k < text.size(); k++)
                    {
                        char ch = text[k];
                        if (esc) { cf += ch; esc = false; continue; }
                        if (ch == '\\') { cf += ch; esc = true; continue; }
                        if (ch == '"') break;
                        cf += ch;
                    }
                    scan = k + 1;
                    std::string fname;
                    size_t nfpos = text.find("\"functionName\":\"", scan);
                    if (nfpos != std::string::npos && nfpos - scan < 400)
                    {
                        size_t nb = nfpos + 16;
                        size_t ne = text.find('"', nb);
                        if (ne != std::string::npos)
                            fname = text.substr(nb, ne - nb);
                    }
                    std::string item = "{\"cf\":\"" + cf + "\",\"fn\":\"" +
                                       fname + "\"}";
                    if (!frames_json.empty() && frames_json != "[")
                        frames_json += ",";
                    frames_json += item;
                    taken++;
                    if (taken >= 4) break;
                    if (nfpos == std::string::npos) break;
                    scan = nfpos;
                }
                frames_json += "]";
                state_->outgoing.push_back(
                    "{\"__lv8_paused\":true,\"cf\":\"" +
                    state_->last_call_frame_id + "\",\"fn\":\"" + fn +
                    "\",\"frames\":" + frames_json + "}");
            }
            // scope enumeration: the paused event's callFrames[0].scopeChain
            // starts with the local scope; remember its objectId so the
            // canned handler can dispatch Runtime.getProperties on it.
            {
                auto scpos = text.find("\"scopeChain\"");
                if (scpos != std::string::npos)
                {
                    auto oid = text.find("\"objectId\"", scpos);
                    if (oid != std::string::npos)
                    {
                        auto b = text.find('"', oid + 11);
                        if (b != std::string::npos)
                        {
                            auto e = text.find('"', b + 1);
                            if (e != std::string::npos)
                                state_->last_local_scope_id =
                                    text.substr(b + 1, e - b - 1);
                        }
                    }
                }
            }
            // exception payload triage: keep the data object (the body's
            // callFrames stay discarded) so the Python driver can map every
            // swallowed exception of the challenge stage.
            auto dpos = text.find("\"data\":{");
            if (dpos != std::string::npos)
            {
                std::string desc = text.substr(dpos + 8, 260);
                state_->outgoing.push_back("{\"__lv8_exc\":\"" + desc +
                                           "\"}");
                if (state_->outgoing.size() > 400000)
                    state_->outgoing.erase(
                        state_->outgoing.begin(),
                        state_->outgoing.begin() + 100000);
            }
        }
        // pass script compilations through (url resolves //# sourceURL, so
        // direct-eval'd generated loaders are enumerable by tooling)
        if (text.find("\"Debugger.scriptParsed\"") != std::string::npos)
        {
            state_->outgoing.push_back(text);
            if (state_->outgoing.size() > 400000)
                state_->outgoing.erase(state_->outgoing.begin(),
                                       state_->outgoing.begin() + 100000);
        }
        state_->notifications_discarded++;
    }
    void flushProtocolNotifications() override {}

private:
    InspectorState* state_;
};

class InspectClient : public v8_inspector::V8InspectorClient
{
public:
    explicit InspectClient(InspectorState* state) : state_(state) {}

    // Canned pause handling: pick up the callFrameId from the paused event,
    // evaluate the watch expression on frame 0 (closure variables of the
    // throwing function are in scope), then resume.  Never blocks, so the
    // engine.py caller proceeds without any threading.
    void runMessageLoopOnPause(int contextGroupId) override
    {
        (void)contextGroupId;
        state_->paused = true;
        if (state_->watch.rfind("__SCOPE__", 0) == 0 &&
            !state_->last_local_scope_id.empty())
        {
            // scope-dump mode: fetch every variable of the top frame's
            // local scope; the raw getProperties response accumulates in
            // |outgoing| for Python-side parsing.
            Dispatch("{\"id\":" + std::to_string(state_->next_id++) +
                     ",\"method\":\"Runtime.getProperties\",\"params\":"
                     "{\"objectId\":\"" + state_->last_local_scope_id +
                     "\",\"generatePreview\":true}}");
            SendResume();
            state_->paused = false;
            return;
        }
        std::string call_frame_id = state_->last_call_frame_id;
        std::string escaped;
        escaped.reserve(state_->watch.size() + 16);
        for (char ch : state_->watch)
        {
            if (ch == '"' || ch == '\\')
                escaped.push_back('\\');
            if (ch == '\n')
            {
                escaped += "\\n";
                continue;
            }
            escaped.push_back(ch);
        }
        SendEvaluate(call_frame_id, escaped);
        if (state_->trace_max > 0 &&
            state_->trace_steps < state_->trace_max)
        {
            // step-trace budget: advance one statement; the resulting pause
            // re-enters this handler with the next register snapshot.
            state_->trace_steps++;
            Dispatch("{\"id\":" + std::to_string(state_->next_id++) +
                     ",\"method\":\"Debugger.stepInto\"}");
        }
        else
        {
            SendResume();
        }
        state_->paused = false;
    }
    void quitMessageLoopOnPause() override {}

private:
    void SendEvaluate(const std::string& call_frame_id,
                      const std::string& escaped_expression)
    {
        std::string msg = "{\"id\":" + std::to_string(state_->next_id++) +
                          ",\"method\":\"Debugger.evaluateOnCallFrame\","
                          "\"params\":{\"callFrameId\":\"" +
                          call_frame_id +
                          "\",\"expression\":\"" + escaped_expression +
                          "\",\"returnByValue\":true}}";
        Dispatch(msg);
    }
    void SendResume()
    {
        Dispatch("{\"id\":" + std::to_string(state_->next_id++) +
                 ",\"method\":\"Debugger.resume\"}");
    }
    void Dispatch(const std::string& msg)
    {
        if (state_->session)
            state_->session->dispatchProtocolMessage(
                v8_inspector::StringView(
                    reinterpret_cast<const uint8_t*>(msg.data()), msg.size()));
    }

    InspectorState* state_;
};
