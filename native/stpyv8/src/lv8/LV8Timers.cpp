// lv8 G1 timers: logical-clock setTimeout/setInterval with clear functions
// and RunTimers (the tick entry point the Python facade exposes). The clock
// only moves when the host advances it, so timer behavior is deterministic
// and testable - matching the reference implementation's logical clock.

#include "LV8.h"

#include "../Exception.h"

#include <algorithm>
#include <string>
#include <vector>

namespace LV8
  {
  namespace
    {
    // G4.369 EXPERIMENT (default OFF): real-time tracking of the system
    // timer clock (base 200ms + elapsed) reproduced the reference D5
    // observable (headChildren 91/91, trackIframe 0/0) but let timers fire
    // mid-battery, reordering the challenge feeds (id88 match=False).
    // The reference wheel's clock is a burst-and-freeze virtual clock, not
    // real time - model that from the archived G4.363 tms evidence before
    // enabling.  Default stays the logical clock the host advances.
    double EffectiveTimerClockMs(ContextState* state)
    {
      if (state == nullptr) return 0.0;
      if (!state->system_time) return state->clock_ms;
      const char* mode = std::getenv("LV8_TIMER_CLOCK");
      if (mode == nullptr || std::string(mode) != "real")
        return state->clock_ms;
      return state->system_clock_base +
             static_cast<double>(
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() -
                     state->system_origin_steady)
                     .count());
    }

    // setTimeout/clearTimeout share ids with their setInterval twins, like
    // browsers (one id space, four entry points).
    int ScheduleTimer(const v8::FunctionCallbackInfo<v8::Value>& info,
                      bool repeating)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction())
        return 0;

      double delay = 0.0;
      if (info.Length() > 1 && info[1]->IsNumber())
        delay = info[1]->NumberValue(context).FromMaybe(0.0);
      if (delay < 0.0)
        delay = 0.0;

      Timer timer;
      timer.id = state->next_timer_id++;
      timer.when_ms = EffectiveTimerClockMs(state) + delay;
      timer.interval_ms = repeating ? delay : 0.0;
      timer.repeating = repeating;
      timer.fn.Reset(isolate, info[0].As<v8::Function>());
      for (int i = 2; i < info.Length(); i++)
        timer.args.emplace_back(isolate, info[i]);

      state->timers.push_back(std::move(timer));
      return timer.id;
    }

    void ClearTimer(v8::Isolate* isolate, v8::Local<v8::Value> id_value)
    {
      ContextState* state = GetState(isolate->GetCurrentContext());
      if (state == nullptr || id_value.IsEmpty())
        return;

      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      int32_t id = id_value->IsNumber()
                       ? id_value->Int32Value(context).FromMaybe(0)
                       : 0;
      if (id <= 0)
        return;

      auto& timers = state->timers;
      timers.erase(
          std::remove_if(
              timers.begin(), timers.end(),
              [id](const Timer& t) { return t.id == id; }),
          timers.end());
    }

    void SetTimeout(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(ScheduleTimer(info, false));
    }

    void SetInterval(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(ScheduleTimer(info, true));
    }

    void ClearTimeout(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ClearTimer(info.GetIsolate(),
                 info.Length() > 0 ? info[0] : v8::Local<v8::Value>());
    }

    void ClearInterval(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ClearTimer(info.GetIsolate(),
                 info.Length() > 0 ? info[0] : v8::Local<v8::Value>());
    }

    void InstallGlobalMethod(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             const char* name, v8::FunctionCallback callback)
    {
      // Window.prototype carries the timer entry points, matching Chrome.
      v8::Local<v8::Value> proto_value =
          context->Global()->GetPrototype();
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> tpl =
          v8::FunctionTemplate::New(isolate, callback);
      tpl->SetClassName(key);
      // G4.393j: the reference wheel's window methods are native-method
      // form (no .prototype property); the template default would expose
      // one.
      tpl->RemovePrototype();
      v8::Local<v8::Function> fn = tpl->GetFunction(context).ToLocalChecked();

      if (proto_value->IsObject())
      {
        (void)proto_value.As<v8::Object>()
            ->DefineOwnProperty(
                context, key, fn,
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
        return;
      }
      (void)context->Global()->DefineOwnProperty(
                 context, key, fn,
                 static_cast<v8::PropertyAttribute>(
                     v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    // requestIdleCallback: schedule the callback on the logical clock with
    // an IdleDeadline argument ({didTimeout, timeRemaining() -> 50});
    // firing at/after the timeout boundary counts as timed out
    void RequestIdleCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction())
        return;

      double timeout = 0.0;
      if (info.Length() > 1 && info[1]->IsObject())
      {
        v8::Local<v8::Value> t;
        if (info[1].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "timeout"))
                .ToLocal(&t) &&
            t->IsNumber())
          timeout = t->NumberValue(context).FromMaybe(0.0);
      }
      if (timeout < 0.0)
        timeout = 0.0;
      bool did_timeout = timeout > 0.0;

      v8::Local<v8::Array> payload = v8::Array::New(isolate, 2);
      (void)payload->Set(context, 0, info[0]).FromMaybe(false);
      (void)payload->Set(context, 1, v8::Boolean::New(isolate, did_timeout))
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> wrapper = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> data = args.Data().As<v8::Array>();
            v8::Local<v8::Value> cb, did;
            if (!data->Get(ctx, 0).ToLocal(&cb) ||
                !data->Get(ctx, 1).ToLocal(&did) ||
                !cb->IsFunction())
              return;
            v8::Local<v8::Object> deadline = v8::Object::New(iso);
            (void)deadline
                ->Set(ctx, ToV8Str(iso, "didTimeout"), did)
                .FromMaybe(false);
            v8::Local<v8::FunctionTemplate> remaining =
                v8::FunctionTemplate::New(
                    iso,
                    [](const v8::FunctionCallbackInfo<v8::Value>& rargs) {
                      rargs.GetReturnValue().Set(
                          v8::Number::New(rargs.GetIsolate(), 50.0));
                    });
            remaining->SetClassName(ToV8Str(iso, "timeRemaining"));
            v8::Local<v8::Function> remaining_fn =
                remaining->GetFunction(ctx).ToLocalChecked();
            (void)deadline
                ->Set(ctx, ToV8Str(iso, "timeRemaining"), remaining_fn)
                .FromMaybe(false);
            v8::Local<v8::Value> argv[1] = {deadline};
            v8::TryCatch try_catch(iso);
            (void)cb.As<v8::Function>()
                ->Call(ctx, ctx->Global(), 1, argv)
                .FromMaybe(v8::Local<v8::Value>());
            try_catch.Reset();
          },
          payload);
      wrapper->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Function> fn =
          wrapper->GetFunction(context).ToLocalChecked();

      Timer timer;
      timer.id = state->next_timer_id++;
      timer.when_ms = EffectiveTimerClockMs(state) + timeout;
      timer.interval_ms = 0.0;
      timer.repeating = false;
      timer.fn.Reset(isolate, fn);
      state->timers.push_back(std::move(timer));
      info.GetReturnValue().Set(v8::Integer::New(isolate, timer.id));
    }

    void CancelIdleCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ClearTimer(info.GetIsolate(),
                 info.Length() > 0 ? info[0] : v8::Local<v8::Value>());
    }
    } // namespace

  int RunTimers(v8::Isolate* isolate, v8::Local<v8::Context> context,
                double advance_ms)
  {
    ContextState* state = GetState(context);
    if (state == nullptr)
      return 0;

    if (advance_ms < 0.0)
      advance_ms = 0.0;
    state->clock_ms += advance_ms;

    int runs = 0;
    // Safety cap: an interval with delay 0 must not hang the host.
    const int kMaxRuns = 10000;

    while (runs < kMaxRuns)
    {
      // earliest due timer wins; ties break FIFO by id
      Timer* due = nullptr;
      for (Timer& t : state->timers)
      {
        if (t.when_ms > EffectiveTimerClockMs(state))
          continue;
        if (due == nullptr || t.when_ms < due->when_ms ||
            (t.when_ms == due->when_ms && t.id < due->id))
        {
          due = &t;
        }
      }
      if (due == nullptr)
        break;

      // copy out: the callback may clear itself (or reschedule)
      v8::Local<v8::Function> fn =
          v8::Local<v8::Function>::New(isolate, due->fn);
      std::vector<v8::Local<v8::Value>> args;
      args.reserve(due->args.size());
      for (v8::Global<v8::Value>& arg : due->args)
        args.push_back(v8::Local<v8::Value>::New(isolate, arg));

      int id = due->id;
      bool repeating = due->repeating;
      double interval = due->interval_ms;
      if (repeating)
        due->when_ms += interval;
      else
      {
        auto& timers = state->timers;
        timers.erase(std::remove_if(
                         timers.begin(), timers.end(),
                         [id](const Timer& t) { return t.id == id; }),
                     timers.end());
      }

      runs++;
      v8::TryCatch try_catch(isolate);
      (void)fn->Call(context, context->Global(),
                     static_cast<int>(args.size()),
                     args.empty() ? nullptr : args.data())
          .FromMaybe(v8::Local<v8::Value>());
      if (try_catch.HasCaught())
      {
        // Browser semantics: an uncaught exception in one task is reported
        // (console + window "error" event) and never cancels the queue.
        // lv8 previously rethrew out of the tick loop, which aborted the
        // whole drain — a later task (the WAF signer) never ran.
        v8::Local<v8::Value> exception = try_catch.Exception();
        std::string text = "Uncaught";
        if (!try_catch.Message().IsEmpty())
        {
          v8::String::Utf8Value msg(isolate, try_catch.Message()->Get());
          text = "Uncaught " + std::string(*msg ? *msg : "", msg.length());
        }
        else if (!exception.IsEmpty())
        {
          v8::String::Utf8Value msg(isolate, exception);
          text = "Uncaught " + std::string(*msg ? *msg : "", msg.length());
        }
        {
          v8::TryCatch quiet(isolate);
          v8::Local<v8::Value> console;
          if (context->Global()
                  ->Get(context, ToV8Str(isolate, "console"))
                  .ToLocal(&console) &&
              console->IsObject())
          {
            v8::Local<v8::Value> err_fn;
            if (console.As<v8::Object>()
                    ->Get(context, ToV8Str(isolate, "error"))
                    .ToLocal(&err_fn) &&
                err_fn->IsFunction())
            {
              v8::Local<v8::Value> arg = ToV8Str(isolate, text);
              (void)err_fn.As<v8::Function>()
                  ->Call(context, console, 1, &arg)
                  .FromMaybe(v8::Local<v8::Value>());
            }
          }
          std::vector<std::pair<std::string, v8::Local<v8::Value>>> props;
          props.emplace_back("type", ToV8Str(isolate, "error"));
          props.emplace_back("message", ToV8Str(isolate, text));
          if (!exception.IsEmpty())
            props.emplace_back("error", exception);
          LV8::DispatchEvent(isolate, context, "ErrorEvent", props);
        }
      }
    }
    // clock advanced: animation frames scheduled before the tick fire
    // with the new timestamp (never on a plain drain of due timers);
    // frame fires count toward the returned run total
    if (advance_ms > 0.0)
      runs += DispatchAnimationFrames(isolate, context);
    return runs;
  }

  // advance the logical clock WITHOUT running timers (eventLoop.tick)
  void AdvanceClock(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    double ms)
  {
    ContextState* state = GetState(context);
    if (state == nullptr) return;
    if (ms < 0.0) ms = 0.0;
    state->clock_ms += ms;
  }

  // run at most |limit| due timers (no clock movement); returns the count
  int RunDueTimersLimit(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        int limit)
  {
    ContextState* state = GetState(context);
    if (state == nullptr) return 0;
    int runs = 0;
    while (runs < limit)
    {
      Timer* due = nullptr;
      for (Timer& t : state->timers)
      {
        if (t.when_ms > EffectiveTimerClockMs(state)) continue;
        if (due == nullptr || t.when_ms < due->when_ms ||
            (t.when_ms == due->when_ms && t.id < due->id))
        {
          due = &t;
        }
      }
      if (due == nullptr) break;
      v8::Local<v8::Function> fn =
          v8::Local<v8::Function>::New(isolate, due->fn);
      std::vector<v8::Local<v8::Value>> args;
      args.reserve(due->args.size());
      for (v8::Global<v8::Value>& arg : due->args)
        args.push_back(v8::Local<v8::Value>::New(isolate, arg));
      int id = due->id;
      bool repeating = due->repeating;
      double interval = due->interval_ms;
      if (repeating) due->when_ms += interval;
      else
      {
        auto& timers = state->timers;
        timers.erase(std::remove_if(
                         timers.begin(), timers.end(),
                         [id](const Timer& t) { return t.id == id; }),
                     timers.end());
      }
      runs++;
      v8::TryCatch try_catch(isolate);
      (void)fn->Call(context, context->Global(),
                     static_cast<int>(args.size()),
                     args.empty() ? nullptr : args.data())
          .FromMaybe(v8::Local<v8::Value>());
      if (try_catch.HasCaught())
      {
        // Browser semantics: an uncaught exception in one task is reported
        // (console + window "error" event) and never cancels the queue.
        // lv8 previously rethrew out of the tick loop, which aborted the
        // whole drain — a later task (the WAF signer) never ran.
        v8::Local<v8::Value> exception = try_catch.Exception();
        std::string text = "Uncaught";
        if (!try_catch.Message().IsEmpty())
        {
          v8::String::Utf8Value msg(isolate, try_catch.Message()->Get());
          text = "Uncaught " + std::string(*msg ? *msg : "", msg.length());
        }
        else if (!exception.IsEmpty())
        {
          v8::String::Utf8Value msg(isolate, exception);
          text = "Uncaught " + std::string(*msg ? *msg : "", msg.length());
        }
        {
          v8::TryCatch quiet(isolate);
          v8::Local<v8::Value> console;
          if (context->Global()
                  ->Get(context, ToV8Str(isolate, "console"))
                  .ToLocal(&console) &&
              console->IsObject())
          {
            v8::Local<v8::Value> err_fn;
            if (console.As<v8::Object>()
                    ->Get(context, ToV8Str(isolate, "error"))
                    .ToLocal(&err_fn) &&
                err_fn->IsFunction())
            {
              v8::Local<v8::Value> arg = ToV8Str(isolate, text);
              (void)err_fn.As<v8::Function>()
                  ->Call(context, console, 1, &arg)
                  .FromMaybe(v8::Local<v8::Value>());
            }
          }
          std::vector<std::pair<std::string, v8::Local<v8::Value>>> props;
          props.emplace_back("type", ToV8Str(isolate, "error"));
          props.emplace_back("message", ToV8Str(isolate, text));
          if (!exception.IsEmpty())
            props.emplace_back("error", exception);
          LV8::DispatchEvent(isolate, context, "ErrorEvent", props);
        }
      }
    }
    return runs;
  }

  void InstallTimers(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    InstallGlobalMethod(isolate, context, "setTimeout", SetTimeout);
    InstallGlobalMethod(isolate, context, "setInterval", SetInterval);
    InstallGlobalMethod(isolate, context, "clearTimeout", ClearTimeout);
    InstallGlobalMethod(isolate, context, "clearInterval", ClearInterval);
    InstallGlobalMethod(isolate, context, "requestIdleCallback",
                        RequestIdleCallback);
    InstallGlobalMethod(isolate, context, "cancelIdleCallback",
                        CancelIdleCallback);
  }

  int PendingTimers(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    ContextState* state = GetState(context);
    return state == nullptr
        ? 0
        : static_cast<int>(state->timers.size() + state->raf_queue.size());
  }

  double LogicalClock(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    ContextState* state = GetState(context);
    return state == nullptr ? 0.0 : state->clock_ms;
  }
} // namespace LV8
