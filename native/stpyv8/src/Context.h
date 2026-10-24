#pragma once

#include <cassert>
#include <memory>

#include "Isolate.h"
#include "Platform.h"
#include "Wrapper.h"
#include "Utils.h"
#include "Inspector.h"


class CContext;
class CIsolate;

typedef std::shared_ptr<CContext> CContextPtr;
typedef std::shared_ptr<CIsolate> CIsolatePtr;


class CContext
{
    py::object m_global;
    v8::Persistent<v8::Context> m_context;
    // lv8 G3: last eval promise (captured until the next structured eval)
    v8::Global<v8::Value> m_last_promise;
    py::object m_last_promise_result;
    // lv8 triage: in-process V8 Inspector (watch expression at pauses)
    std::unique_ptr<InspectorState> m_inspect;
public:
    CContext(v8::Handle<v8::Context> context);
    CContext(const CContext& context);
    CContext(py::object global);

    ~CContext()
    {
        m_context.Reset();
    }

    v8::Handle<v8::Context> Handle(void) const {
        return v8::Local<v8::Context>::New(v8::Isolate::GetCurrent(), m_context);
    }

    py::object GetGlobal(void);

    py::str GetSecurityToken(void);
    void SetSecurityToken(py::str token);

    bool IsEntered(void) {
        return !m_context.IsEmpty();
    }
    void Enter(void) {
        v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
        Handle()->Enter();
    }
    void Leave(void) {
        v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
        Handle()->Exit();
    }

    py::object Evaluate(const std::string& src, const std::string name = std::string(),
                        int line = -1, int col = -1);
    py::object EvaluateW(const std::wstring& src, const std::wstring name = std::wstring(),
                         int line = -1, int col = -1);

    // lv8 G1: install native host objects (navigator, console, timers,
    // location/history, event target...) from a profile JSON.
    void InstallLV8(const std::string& profile_json);

    // lv8 G1: override the ICU default time zone (IANA name, DST-aware).
    void SetTimeZoneOverride(const std::string& iana_name);

    // lv8 G4: restore the machine zone/locale captured before the first
    // override (multi-context isolation for unconfigured contexts).
    void ResetTimeZone(void);
    void ResetLocale(void);

    // lv8 G1: override the ICU default locale (BCP 47) for Intl resolution.
    void SetLocale(const std::string& locale);

    // lv8 G1: advance the logical clock and run due timers; returns the
    // number of timer callbacks executed. Requires the entered state.
    int RunTimers(double advance_ms);

    // lv8 G1: number of scheduled-but-not-yet-run timers.
    int PendingTimers(void);

    // lv8 G3: fetches issued but not yet consumed by a microtask checkpoint.
    int PendingNetworkTasks(void);

    // lv8 G3: deliver queued offline fetch responses (explicit drain).
    int DispatchPending(void);
    void SetBridgeEnabled(bool enabled);
    std::string PendingBridgedRequests(void);
    void ResolveBridged(const std::string& url, const std::string& outcome,
                        const std::string& body,
                        const std::string& headers_json, int status);

    // lv8 G1: current logical clock in milliseconds.
    double ClockMs(void);

    // lv8 G2: parse |html| as the page document, run inline scripts in
    // document order, fire DOMContentLoaded, set readyState to "complete".
    void LoadDocument(const std::string& html,
                      const std::string& base_url = std::string(),
                      const std::string& headers_json = std::string());

    // lv8 G1: run pending V8 microtasks (promise callbacks).
    void DrainMicrotasks(void);
    // lv8 G3: result of the last settled eval promise (read after a
    // drain); primitives come back native, objects as wrappers.
    py::object LastPromiseResult(void);

    // lv8 G3: evaluate and return JSON-safe arrays/objects as native
    // Python list/dict (reference-implementation parity); anything else
    // (functions, host objects, cyclic graphs) comes back as wrappers.
    py::object EvaluateStructured(const std::string& src,
                                  const std::string name = std::string(),
                                  int line = -1, int col = -1);

    // lv8 G3: seed the offline network bridge resource table; headers is a
    // JSON array of [name, value] pairs (order preserved for set-cookie).
    int AddResource(const std::string& url, py::object body,
                    const std::string& headers_json, int status = 200);

    // lv8 G3: set a (dot-path) global from a python object; callables
    // become callable JS functions.
    void SetGlobalValue(const std::string& path, py::object value);
    // lv8 G3: whether the last structured eval settled a promise.
    bool LastEvalWasPromise(void);

    // lv8 triage: in-process V8 Inspector.  InspectEnable installs the
    // agent and registers the canned evaluateOnCallFrame expression used at
    // every pause; InspectDispatch feeds protocol messages; InspectPoll
    // drains responses/notifications (each pause auto-appends the watch
    // result and resumes).
    void InspectEnable(const std::string& watch_expr);
    void InspectDispatch(const std::string& msg);
    py::list InspectPoll(void);
    // lv8 triage: step-trace budget — pauses evaluate the watch then step
    // into instead of resuming, until |max_steps| steps are spent.
    void InspectSetTrace(int max_steps);
    std::string InspectLastCallFrame(void);

    static py::object GetEntered(void);
    static py::object GetCurrent(void);
    static py::object GetCalling(void);
    static bool InContext(void) {
        return v8::Isolate::GetCurrent()->InContext();
    }

    static void Expose(void);
};
