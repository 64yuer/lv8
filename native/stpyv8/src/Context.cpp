#include "Context.h"
#include "Wrapper.h"
#include "Engine.h"
#include "lv8/LV8.h"

#include "libplatform/libplatform.h"

void CContext::InstallLV8(const std::string& profile_json)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    LV8::InstallHostObjects(isolate, Handle(), profile_json);
}

// lv8 triage: in-process V8 Inspector ------------------------------------
void CContext::InspectEnable(const std::string& watch_expr)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = Handle();
    if (!m_inspect)
        m_inspect.reset(new InspectorState());
    if (!m_inspect->created)
    {
        m_inspect->context.Reset(isolate, context);
        m_inspect->client.reset(new InspectClient(m_inspect.get()));
        m_inspect->channel.reset(new InspectChannel(m_inspect.get()));
        m_inspect->inspector = v8_inspector::V8Inspector::create(
            isolate, m_inspect->client.get());
        v8_inspector::V8ContextInfo info(
            context, 1,
            v8_inspector::StringView(
                reinterpret_cast<const uint8_t*>("lv8"), 3));
        m_inspect->inspector->contextCreated(info);
        v8_inspector::StringView empty_state;
        m_inspect->session = m_inspect->inspector->connect(
            1, m_inspect->channel.get(), empty_state,
            v8_inspector::V8Inspector::kFullyTrusted);
        m_inspect->created = true;
    }
    m_inspect->watch = watch_expr;
}

void CContext::InspectDispatch(const std::string& msg)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    if (m_inspect && m_inspect->session)
        m_inspect->session->dispatchProtocolMessage(
            v8_inspector::StringView(
                reinterpret_cast<const uint8_t*>(msg.data()), msg.size()));
}

py::list CContext::InspectPoll(void)
{
    py::list out;
    if (m_inspect)
    {
        for (const auto& message : m_inspect->outgoing)
            out.append(message);
        m_inspect->outgoing.clear();
    }
    return out;
}

std::string CContext::InspectLastCallFrame(void)
{
    if (m_inspect)
        return m_inspect->last_call_frame_id;
    return std::string();
}

void CContext::InspectSetTrace(int max_steps)
{
    if (!m_inspect)
        m_inspect.reset(new InspectorState());
    m_inspect->trace_max = max_steps;
    m_inspect->trace_steps = 0;
}

void CContext::SetTimeZoneOverride(const std::string& iana_name)
{
    // Read the isolate from the persistent handle instead of
    // Isolate::GetCurrent() so entering the context is not required.
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    LV8::SetTimeZoneOverride(isolate, iana_name);
}

void CContext::SetLocale(const std::string& locale)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    LV8::SetDefaultLocale(isolate, locale);
}

void CContext::ResetTimeZone(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    LV8::RestoreHostTimeZone(isolate);
}

void CContext::ResetLocale(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    LV8::RestoreHostLocale(isolate);
}

int CContext::RunTimers(double advance_ms)
{
    // Timers run JS, so the context must be entered (stpyv8 contract).
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    return LV8::RunTimers(isolate, Handle(), advance_ms);
}

int CContext::PendingTimers(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    return LV8::PendingTimers(isolate, Handle());
}

double CContext::ClockMs(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    return LV8::LogicalClock(isolate, Handle());
}

void CContext::DrainMicrotasks(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    isolate->PerformMicrotaskCheckpoint();
    if (!m_last_promise.IsEmpty())
    {
        v8::Local<v8::Context> context = Handle();
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Value> promise = m_last_promise.Get(isolate);
        m_last_promise.Reset();
        if (promise.As<v8::Promise>()->State() ==
            v8::Promise::kFulfilled)
        {
            m_last_promise_result = CJavascriptObject::Wrap(
                promise.As<v8::Promise>()->Result());
        }
    }
}

py::object CContext::LastPromiseResult(void)
{
    return m_last_promise_result;
}

py::object CContext::EvaluateStructured(const std::string& src,
                                        const std::string name, int line,
                                        int col)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = Handle();
    v8::Context::Scope context_scope(context);

    LV8::SetLastEvalWasPromise(isolate, context, false);
    v8::TryCatch try_catch(isolate);
    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(isolate, src.c_str()).ToLocalChecked();
    v8::Local<v8::Value> origin_name =
        v8::String::NewFromUtf8(
            isolate, name.empty() ? "<anonymous>" : name.c_str())
            .ToLocalChecked();
    v8::ScriptOrigin origin(origin_name, line >= 0 ? line : 0,
                            col >= 0 ? col : 0);
    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(context, source, &origin).ToLocal(&script))
    {
        if (try_catch.HasCaught())
            CJavascriptException::ThrowIf(isolate, try_catch);
        return py::object();
    }
    v8::Local<v8::Value> result;
    if (!script->Run(context).ToLocal(&result))
    {
        if (try_catch.HasCaught())
            CJavascriptException::ThrowIf(isolate, try_catch);
        result = v8::Null(isolate);
    }
    // remember eval promises so a later drain can expose their result
    // (reference-parity .get(timeout) handles on the facade)
    m_last_promise.Reset();
    m_last_promise_result = py::object();
    if (result->IsPromise())
    {
        m_last_promise.Reset(isolate, result);
        LV8::SetLastEvalWasPromise(isolate, context, true);
    }
    // JSON-safe arrays/plain objects convert to native python lists/dicts
    // (single evaluation: stringify the already-computed result); anything
    // else (functions, host objects, cyclic graphs) stays a wrapper
    if (result->IsArray())
    {
        v8::Local<v8::String> encoded;
        if (v8::JSON::Stringify(context, result).ToLocal(&encoded))
        {
            std::string text = *v8::String::Utf8Value(isolate, encoded);
            if (!text.empty() && text[0] == '[')
            {
                py::object json = py::import("json");
                return json.attr("loads")(text);
            }
        }
    }
    return CJavascriptObject::Wrap(result);
}

void CContext::SetGlobalValue(const std::string& path, py::object value)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = Handle();
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Object> obj = context->Global();
    std::string segment;
    std::vector<std::string> segments;
    for (char c : path)
    {
        if (c == '.')
        {
            segments.push_back(segment);
            segment.clear();
        }
        else
        {
            segment.push_back(c);
        }
    }
    segments.push_back(segment);

    for (size_t i = 0; i + 1 < segments.size(); i++)
    {
        v8::Local<v8::String> key =
            v8::String::NewFromUtf8(isolate, segments[i].c_str())
                .ToLocalChecked();
        v8::Local<v8::Value> next;
        if (!obj->Get(context, key).ToLocal(&next) || !next->IsObject())
        {
            next = v8::Object::New(isolate);
            (void)obj->Set(context, key, next).FromMaybe(false);
        }
        obj = next.As<v8::Object>();
    }
    v8::Local<v8::String> last =
        v8::String::NewFromUtf8(isolate, segments.back().c_str())
            .ToLocalChecked();
    (void)obj
        ->Set(context, last,
              CPythonObject::Wrap(value))
        .FromMaybe(false);
}

bool CContext::LastEvalWasPromise(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    return LV8::LastEvalWasPromise(isolate, Handle());
}

int CContext::PendingNetworkTasks(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    return LV8::PendingNetworkTasks(isolate, Handle());
}

void CContext::SetBridgeEnabled(bool enabled)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    LV8::SetBridgeEnabled(isolate, Handle(), enabled);
}

std::string CContext::PendingBridgedRequests(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    return LV8::PendingBridgedRequests(isolate, Handle());
}

void CContext::ResolveBridged(const std::string& url,
                              const std::string& outcome,
                              const std::string& body,
                              const std::string& headers_json, int status)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    LV8::ResolveBridged(isolate, Handle(), url, outcome, body, headers_json,
                        status);
}

int CContext::DispatchPending(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    return LV8::DispatchPendingResponses(isolate, Handle());
}

int CContext::AddResource(const std::string& url, py::object body,
                          const std::string& headers_json, int status)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = Handle();
    v8::Context::Scope context_scope(context);

    // bytes bodies pass through raw (str extraction re-encodes as UTF-8)
    std::string body_bytes;
    if (PyBytes_Check(body.ptr()))
    {
        char* data = nullptr;
        Py_ssize_t size = 0;
        PyBytes_AsStringAndSize(body.ptr(), &data, &size);
        if (data != nullptr && size > 0) body_bytes.assign(data, static_cast<size_t>(size));
    }
    else
    {
        body_bytes = py::extract<std::string>(body)();
    }

    std::vector<std::pair<std::string, std::string>> headers;
    v8::Local<v8::Value> parsed;
    if (!headers_json.empty() &&
        v8::JSON::Parse(context, v8::String::NewFromUtf8(isolate, headers_json.c_str()).ToLocalChecked())
            .ToLocal(&parsed) &&
        parsed->IsArray())
    {
        v8::Local<v8::Array> arr = parsed.As<v8::Array>();
        for (uint32_t i = 0; i < arr->Length(); i++)
        {
            v8::Local<v8::Value> pair = arr->Get(context, i).ToLocalChecked();
            if (!pair->IsArray())
                continue;
            v8::Local<v8::Array> kv = pair.As<v8::Array>();
            v8::String::Utf8Value key(isolate, kv->Get(context, 0).ToLocalChecked());
            v8::String::Utf8Value value(isolate, kv->Get(context, 1).ToLocalChecked());
            headers.emplace_back(*key ? *key : "", *value ? *value : "");
        }
    }
    return LV8::AddResource(isolate, context, url, body_bytes, headers,
                            status);
}

void CContext::LoadDocument(const std::string& html,
                            const std::string& base_url,
                            const std::string& headers_json)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = Handle();
    v8::Context::Scope context_scope(context);

    std::vector<std::pair<std::string, std::string>> page_headers;
    if (!headers_json.empty())
    {
        v8::Local<v8::Value> parsed;
        if (v8::JSON::Parse(context,
                            v8::String::NewFromUtf8(
                                isolate, headers_json.c_str())
                                .ToLocalChecked())
                .ToLocal(&parsed) &&
            parsed->IsArray())
        {
            v8::Local<v8::Array> arr = parsed.As<v8::Array>();
            for (uint32_t i = 0; i < arr->Length(); i++)
            {
                v8::Local<v8::Value> pair =
                    arr->Get(context, i).ToLocalChecked();
                if (!pair->IsArray())
                    continue;
                v8::String::Utf8Value key(
                    isolate, pair.As<v8::Array>()->Get(context, 0).ToLocalChecked());
                v8::String::Utf8Value value(
                    isolate, pair.As<v8::Array>()->Get(context, 1).ToLocalChecked());
                page_headers.emplace_back(*key ? *key : "",
                                          *value ? *value : "");
            }
        }
    }
    LV8::LoadDocument(isolate, context, html, base_url, page_headers);
}

void CContext::Expose(void)
{
    py::class_<CPlatform, boost::noncopyable>("JSPlatform", "JSPlatform allows the V8 platform to be initialized", py::no_init)
    .def(py::init<std::string>((py::arg("argv") = std::string())))
    .def("init", &CPlatform::Init, "Initializes the platform")
    ;

    py::class_<CIsolate, boost::noncopyable>("JSIsolate", "JSIsolate is an isolated instance of the V8 engine.", py::no_init)
    .def(py::init<bool>((py::arg("owner") = false)))

    .add_static_property("current", &CIsolate::GetCurrent,
                         "Returns the entered isolate for the current thread or NULL in case there is no current isolate.")

    .add_property("locked", &CIsolate::IsLocked)

    .def("GetCurrentStackTrace", &CIsolate::GetCurrentStackTrace)

    .def("enter", &CIsolate::Enter,
         "Sets this isolate as the entered one for the current thread. "
         "Saves the previously entered one (if any), so that it can be "
         "restored when exiting.  Re-entering an isolate is allowed.")

    .def("leave", &CIsolate::Leave,
         "Exits this isolate by restoring the previously entered one in the current thread. "
         "The isolate may still stay the same, if it was entered more than once.")
    ;

    py::class_<CContext, boost::noncopyable>("JSContext", "JSContext is an execution context.", py::no_init)
    .def(py::init<const CContext&>("Create a new context based on a existing context"))
    .def(py::init<py::object>((py::arg("global") = py::object()),
                              "Create a new context based on global object"))

    .add_property("securityToken", &CContext::GetSecurityToken, &CContext::SetSecurityToken)

    .add_property("locals", &CContext::GetGlobal, "Local variables within context")

    .add_static_property("entered", &CContext::GetEntered,
                         "The last entered context.")
    .add_static_property("current", &CContext::GetCurrent,
                         "The context that is on the top of the stack.")
    .add_static_property("calling", &CContext::GetCalling,
                         "The context of the calling JavaScript code.")
    .add_static_property("inContext", &CContext::InContext,
                         "Returns true if V8 has a current context.")

    .def("eval_structured", &CContext::EvaluateStructured, (py::arg("source"),
                                       py::arg("name") = std::string(),
                                       py::arg("line") = -1,
                                       py::arg("col") = -1),
         "Evaluate and convert JSON-safe results to native python.")
    .def("eval", &CContext::Evaluate, (py::arg("source"),
                                       py::arg("name") = std::string(),
                                       py::arg("line") = -1,
                                       py::arg("col") = -1))
    .def("eval", &CContext::EvaluateW, (py::arg("source"),
                                        py::arg("name") = std::wstring(),
                                        py::arg("line") = -1,
                                        py::arg("col") = -1))

    .def("enter", &CContext::Enter, "Enter this context. "
         "After entering a context, all code compiled and "
         "run is compiled and run in this context.")
    .def("leave", &CContext::Leave, "Exit this context. "
         "Exiting the current context restores the context "
         "that was in place when entering the current context.")

    .def("install_lv8", &CContext::InstallLV8, "Install lv8 native host objects from a profile JSON string.")
    .def("set_timezone", &CContext::SetTimeZoneOverride, "Override the ICU default time zone (IANA name, DST-aware).")
    .def("reset_timezone", &CContext::ResetTimeZone, "Restore the machine time zone captured before the first override.")
    .def("set_locale", &CContext::SetLocale, "Override the ICU default locale (BCP 47) for Intl resolution.")
    .def("reset_locale", &CContext::ResetLocale, "Restore the machine locale captured before the first override.")
    .def("run_timers", &CContext::RunTimers, (py::arg("advance_ms") = 0.0),
         "Advance the logical clock and run due timers; returns the number of callbacks executed.")
    .def("pending_timers", &CContext::PendingTimers, "Number of scheduled-but-not-yet-run timers.")
    .def("pending_network_tasks", &CContext::PendingNetworkTasks, "Number of offline fetches not yet drained.")
    .def("set_bridge_enabled", &CContext::SetBridgeEnabled, (py::arg("enabled")), "Enable the Python-side network bridge for resource-table misses.")
    .def("pending_bridged_requests", &CContext::PendingBridgedRequests, "JSON list of fetches parked for the Python bridge.")
    .def("resolve_bridged", &CContext::ResolveBridged, (py::arg("url"), py::arg("outcome"), py::arg("body"), py::arg("headers_json"), py::arg("status")), "Resolve a bridged fetch: outcome in {response, bundle, reject}.")
    .def("dispatch_pending", &CContext::DispatchPending, "Deliver queued offline fetch responses; returns the number delivered.")
    .def("clock_ms", &CContext::ClockMs, "Current logical clock in milliseconds.")
    .def("drain_microtasks", &CContext::DrainMicrotasks, "Run pending V8 microtasks (promise callbacks).")
    .def("set_global_value", &CContext::SetGlobalValue, (py::arg("path"), py::arg("value")), "Set a dot-path global from a python object.")
    .def("last_eval_was_promise", &CContext::LastEvalWasPromise, "Whether the last structured eval settled a promise.")
    .def("inspect_enable", &CContext::InspectEnable, (py::arg("watch_expr")), "lv8 triage: install the in-process V8 Inspector and register the canned pause watch expression.")
    .def("inspect_dispatch", &CContext::InspectDispatch, (py::arg("msg")), "lv8 triage: feed a DevTools protocol message to the in-process inspector session.")
    .def("inspect_poll", &CContext::InspectPoll, "lv8 triage: drain inspector responses/notifications (one entry per protocol message; pauses auto-append the watch result and resume).")
    .def("inspect_last_call_frame", &CContext::InspectLastCallFrame, "lv8 triage: callFrameId of the most recent Debugger.paused event")
    .def("inspect_set_trace", &CContext::InspectSetTrace, (py::arg("max_steps")), "lv8 triage: step-trace budget - pauses evaluate the watch then step into until the budget is spent, then resume.")
    .def("last_promise_result", &CContext::LastPromiseResult, "Result of the last settled eval promise.")
    .def("add_resource", &CContext::AddResource, (py::arg("url"), py::arg("body"), py::arg("headers_json") = std::string(), py::arg("status") = 200), "Seed the offline network bridge resource table.")
    .def("load_document", &CContext::LoadDocument, (py::arg("html"), py::arg("base_url") = std::string(), py::arg("headers_json") = std::string()),
         "Parse HTML into the document, run scripts, fire DOMContentLoaded.")

    .def("__bool__", &CContext::IsEntered, "the context has been entered.")
    ;

    py::objects::class_value_wrapper<std::shared_ptr<CPlatform>,
    py::objects::make_ptr_instance<CPlatform,
    py::objects::pointer_holder<std::shared_ptr<CPlatform>,CPlatform> > >();

    py::objects::class_value_wrapper<std::shared_ptr<CIsolate>,
    py::objects::make_ptr_instance<CIsolate,
    py::objects::pointer_holder<std::shared_ptr<CIsolate>,CIsolate> > >();

    py::objects::class_value_wrapper<std::shared_ptr<CContext>,
    py::objects::make_ptr_instance<CContext,
    py::objects::pointer_holder<std::shared_ptr<CContext>,CContext> > >();
}

CContext::CContext(v8::Handle<v8::Context> context)
{
    v8::HandleScope handle_scope(v8::Isolate::GetCurrent());

    m_context.Reset(context->GetIsolate(), context);
}

CContext::CContext(const CContext& context)
{
    v8::HandleScope handle_scope(v8::Isolate::GetCurrent());

    m_context.Reset(context.Handle()->GetIsolate(), context.Handle());
}

CContext::CContext(py::object global)
    : m_global(global)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);

    // Global template carries lv8's Window named-property interceptor
    // (non-masking: inert until lv8 attaches a DomState to the global).
    v8::Handle<v8::Context> context =
        v8::Context::New(isolate, nullptr,
                         LV8::GlobalTemplateWithNamedAccess(isolate));

    m_context.Reset(isolate, context);

    v8::Context::Scope context_scope(Handle());

    if (!global.is_none())
    {
        v8::Maybe<bool> retcode =
            Handle()->Global()->Set(context,
                                    v8::String::NewFromUtf8(isolate, "__proto__").ToLocalChecked(),
                                    CPythonObject::Wrap(global));
        if(retcode.IsNothing()) {
            //TODO we need to do something if the set call failed
        }

        Py_DECREF(global.ptr());
    }
}

py::object CContext::GetGlobal(void)
{
    v8::HandleScope handle_scope(v8::Isolate::GetCurrent());

    return CJavascriptObject::Wrap(Handle()->Global());
}

py::str CContext::GetSecurityToken(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);

    v8::Handle<v8::Value> token = Handle()->GetSecurityToken();

    if (token.IsEmpty()) return py::str();

    v8::String::Utf8Value str(isolate, token->ToString(m_context.Get(isolate)).ToLocalChecked());

    return py::str(*str, str.length());
}

void CContext::SetSecurityToken(py::str token)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);

    if (token.is_none())
    {
        Handle()->UseDefaultSecurityToken();
    }
    else
    {
        Handle()->SetSecurityToken(v8::String::NewFromUtf8(isolate,
                                   py::extract<const char *>(token)()).ToLocalChecked());
    }
}

py::object CContext::GetEntered(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();

    if (!isolate->InContext())
        return py::object();

    v8::HandleScope handle_scope(isolate);

    v8::Handle<v8::Context> entered = isolate->GetEnteredOrMicrotaskContext();

    return (entered.IsEmpty()) ? py::object() :
           py::object(py::handle<>(boost::python::converter::shared_ptr_to_python<CContext>(CContextPtr(new CContext(entered)))));
}

py::object CContext::GetCurrent(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    if (!isolate->InContext())
        return py::object();

    v8::HandleScope handle_scope(isolate);

    v8::Handle<v8::Context> current = isolate->GetCurrentContext();

    return (current.IsEmpty()) ? py::object() :
           py::object(py::handle<>(boost::python::converter::shared_ptr_to_python<CContext>(CContextPtr(new CContext(current)))));
}

py::object CContext::GetCalling(void)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();

    if (!isolate->InContext())
        return py::object();

    v8::HandleScope handle_scope(isolate);

    v8::Handle<v8::Context> calling = isolate->GetCurrentContext();

    return (calling.IsEmpty()) ? py::object() :
           py::object(py::handle<>(boost::python::converter::shared_ptr_to_python<CContext>(CContextPtr(new CContext(calling)))));
}

py::object CContext::Evaluate(const std::string& src,
                              const std::string name,
                              int line, int col)
{
    CEngine engine(v8::Isolate::GetCurrent());

    CScriptPtr script = engine.Compile(src, name, line, col);

    return script->Run();
}

py::object CContext::EvaluateW(const std::wstring& src,
                               const std::wstring name,
                               int line, int col)
{
    CEngine engine(v8::Isolate::GetCurrent());

    CScriptPtr script = engine.CompileW(src, name, line, col);

    return script->Run();
}
