    // ---- G4 ledger chunk 9: LockManager + WebMIDI family ----------------
    // (formerly the JS LockManager / MIDIAccess / MIDIPort / MIDIInput /
    // MIDIOutput classes in polyfills4). promiseTask semantics = a native
    // microtask that resolves/rejects.

    v8::Local<v8::Private> LocksHeldKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.locks.held"));
    }
    v8::Local<v8::Private> MidiInputsKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.midi.inputs"));
    }
    v8::Local<v8::Private> MidiOutputsKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.midi.outputs"));
    }

    void LockThenResolve(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> resolver_value, held_value, key;
      if (!data->Get(context, 0).ToLocal(&resolver_value) ||
          !data->Get(context, 1).ToLocal(&held_value) ||
          !data->Get(context, 2).ToLocal(&key))
        return;
      (void)held_value.As<v8::Map>()->Delete(context, key).FromMaybe(false);
      v8::Local<v8::Value> value =
          info.Length() >= 1 ? info[0] : v8::Undefined(isolate);
      (void)resolver_value.As<v8::Promise::Resolver>()
          ->Resolve(context, value)
          .FromMaybe(false);
    }

    void LockThenReject(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> resolver_value, held_value, key;
      if (!data->Get(context, 0).ToLocal(&resolver_value) ||
          !data->Get(context, 1).ToLocal(&held_value) ||
          !data->Get(context, 2).ToLocal(&key))
        return;
      (void)held_value.As<v8::Map>()->Delete(context, key).FromMaybe(false);
      v8::Local<v8::Value> reason =
          info.Length() >= 1 ? info[0] : v8::Undefined(isolate);
      (void)resolver_value.As<v8::Promise::Resolver>()
          ->Reject(context, reason)
          .FromMaybe(false);
    }

    void MicroResolve(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Promise::Resolver> resolver,
                      v8::Local<v8::FunctionTemplate> task_template)
    {
      v8::Local<v8::Value> queue;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "queueMicrotask"))
              .ToLocal(&queue) &&
          queue->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {
            task_template->GetFunction(context).ToLocalChecked()};
        v8::TryCatch try_catch(isolate);
        (void)queue.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    // resolve a promise with value in a queued microtask
    void MicroResolveWithValue(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Promise::Resolver> resolver,
                               v8::Local<v8::Value> value)
    {
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 2);
      (void)payload->Set(context, 0, resolver).FromMaybe(false);
      (void)payload->Set(context, 1, value).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> data = args.Data().As<v8::Array>();
            v8::Local<v8::Value> res, value;
            if (data->Get(ctx, 0).ToLocal(&res) &&
                data->Get(ctx, 1).ToLocal(&value))
              (void)res.As<v8::Promise::Resolver>()
                  ->Resolve(ctx, value)
                  .FromMaybe(false);
          },
          payload);
      v8::Local<v8::Value> queue;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "queueMicrotask"))
              .ToLocal(&queue) &&
          queue->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {
            runner->GetFunction(context).ToLocalChecked()};
        v8::TryCatch try_catch(isolate);
        (void)queue.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    // ---- LockManager ----------------------------------------------------

    void LockRequest(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      info.GetReturnValue().Set(resolver->GetPromise());
      v8::Local<v8::Function> handler;
      v8::Local<v8::Object> options = v8::Object::New(isolate);
      if (info.Length() >= 2 && info[1]->IsFunction())
      {
        handler = info[1].As<v8::Function>();
        if (info.Length() >= 3 && info[2]->IsObject())
          options = info[2].As<v8::Object>();
      }
      else if (info.Length() >= 2 && info[1]->IsObject())
      {
        options = info[1].As<v8::Object>();
        if (info.Length() >= 3 && info[2]->IsFunction())
          handler = info[2].As<v8::Function>();
      }
      auto reject = [&](v8::Local<v8::Value> reason) {
        (void)resolver->Reject(context, reason).FromMaybe(false);
      };
      if (handler.IsEmpty())
      {
        v8::Local<v8::Value> ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "TypeError"))
                .ToLocal(&ctor) &&
            ctor->IsFunction())
        {
          v8::Local<v8::Value> argv[] = {
              ToV8Str(isolate, "lock callback must be a function")};
          v8::Local<v8::Value> error;
          if (ctor.As<v8::Function>()
                  ->NewInstance(context, 1, argv)
                  .ToLocal(&error) &&
              !error.IsEmpty())
          {
            reject(error);
            return;
          }
        }
        reject(v8::Undefined(isolate));
        return;
      }
      // options.signal abort fast-path
      v8::Local<v8::Value> signal_value;
      if (options->Get(context, ToV8Str(isolate, "signal")).ToLocal(&signal_value) &&
          signal_value->IsObject())
      {
        v8::Local<v8::Value> aborted;
        if (signal_value.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "aborted"))
                .ToLocal(&aborted) &&
            aborted->BooleanValue(isolate))
        {
          v8::Local<v8::Value> reason;
          if (signal_value.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "reason"))
                  .ToLocal(&reason))
            reject(reason);
          else
            reject(v8::Undefined(isolate));
          return;
        }
      }
      std::string key = info.Length() >= 1 ? ToUtf8(isolate, info[0]) : "";
      v8::Local<v8::Value> held_value;
      if (!info.This()
                ->GetPrivate(context, LocksHeldKey(isolate))
                .ToLocal(&held_value) ||
          !held_value->IsMap())
      {
        reject(v8::Undefined(isolate));
        return;
      }
      v8::Local<v8::Value> if_available;
      bool if_avail =
          options->Get(context, ToV8Str(isolate, "ifAvailable"))
              .ToLocal(&if_available) &&
          if_available->BooleanValue(isolate);
      v8::Local<v8::Value> mode;
      std::string mode_text;
      if (options->Get(context, ToV8Str(isolate, "mode")).ToLocal(&mode))
        mode_text = ToUtf8(isolate, mode);
      std::string mode_final = mode_text == "shared" ? "shared" : "exclusive";
      v8::Local<v8::Array> task_data = v8::Array::New(isolate, 7);
      (void)task_data->Set(context, 0, info.This()).FromMaybe(false);
      (void)task_data->Set(context, 1, handler).FromMaybe(false);
      (void)task_data->Set(context, 2, options).FromMaybe(false);
      (void)task_data->Set(context, 3, ToV8Str(isolate, key))
          .FromMaybe(false);
      (void)task_data->Set(context, 4, ToV8Str(isolate, mode_final))
          .FromMaybe(false);
      (void)task_data->Set(context, 5, v8::Boolean::New(isolate, if_avail))
          .FromMaybe(false);
      (void)task_data->Set(context, 6, resolver).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> d = args.Data().As<v8::Array>();
            v8::Local<v8::Value> self, handler, options, key, mode, ifav,
                resolver_value;
            if (!d->Get(ctx, 0).ToLocal(&self) ||
                !d->Get(ctx, 1).ToLocal(&handler) ||
                !d->Get(ctx, 2).ToLocal(&options) ||
                !d->Get(ctx, 3).ToLocal(&key) ||
                !d->Get(ctx, 4).ToLocal(&mode) ||
                !d->Get(ctx, 5).ToLocal(&ifav) ||
                !d->Get(ctx, 6).ToLocal(&resolver_value) ||
                !self->IsObject() || !handler->IsFunction() ||
                !options->IsObject() || !mode->IsString() ||
                !resolver_value->IsPromise())
              return;
            v8::Local<v8::Promise::Resolver> resolver =
                resolver_value.As<v8::Promise::Resolver>();
            v8::Local<v8::Value> held_value;
            if (!self.As<v8::Object>()
                      ->GetPrivate(ctx,
                                   v8::Private::ForApi(
                                       iso, ToV8Str(iso, "lv8.locks.held")))
                      .ToLocal(&held_value) ||
                !held_value->IsMap())
              return;
            v8::Local<v8::Map> held = held_value.As<v8::Map>();
            auto call_handler =
                [&](v8::Local<v8::Value> arg, v8::Local<v8::Value>* out) {
                  v8::Local<v8::Value> argv[] = {arg};
                  v8::TryCatch try_catch(iso);
                  v8::Local<v8::Value> result;
                  if (handler.As<v8::Function>()
                          ->Call(ctx, self, 1, argv)
                          .ToLocal(&result))
                    *out = result;
                  else
                    try_catch.Reset();
                };
            if (ifav->BooleanValue(iso) &&
                held->Has(ctx, key).FromMaybe(false))
            {
              v8::Local<v8::Value> result;
              call_handler(v8::Null(iso), &result);
              (void)resolver->Resolve(ctx, result).FromMaybe(false);
              return;
            }
            v8::Local<v8::Object> lock = v8::Object::New(iso);
            (void)lock->Set(ctx, ToV8Str(iso, "name"), key).FromMaybe(false);
            (void)lock->Set(ctx, ToV8Str(iso, "mode"), mode).FromMaybe(false);
            (void)held->Set(ctx, key, lock).FromMaybe(false);
            v8::Local<v8::Value> result;
            bool called = false;
            {
              v8::Local<v8::Value> argv[] = {lock};
              v8::TryCatch try_catch(iso);
              called = handler.As<v8::Function>()->Call(ctx, self, 1, argv).ToLocal(&result);
              try_catch.Reset();
              if (!called)
              {
                (void)held->Delete(ctx, key).FromMaybe(false);
                v8::Local<v8::Value> reason;
                if (try_catch.HasCaught() &&
                    try_catch.Exception().ToLocal(&reason))
                  (void)resolver->Reject(ctx, reason).FromMaybe(false);
                else
                  (void)resolver->Reject(ctx, v8::Undefined(iso))
                      .FromMaybe(false);
                return;
              }
            }
            if (result->IsPromise())
            {
              v8::Local<v8::Value> then_data = v8::Array::New(iso, 3);
              (void)then_data->Set(ctx, 0, resolver).FromMaybe(false);
              (void)then_data->Set(ctx, 1, held).FromMaybe(false);
              (void)then_data->Set(ctx, 2, key).FromMaybe(false);
              v8::Local<v8::Value> then_fn;
              if (result.As<v8::Object>()
                      ->Get(ctx, ToV8Str(iso, "then"))
                      .ToLocal(&then_fn) &&
                  then_fn->IsFunction())
              {
                v8::Local<v8::FunctionTemplate> on_ok =
                    v8::FunctionTemplate::New(iso, LockThenResolve,
                                              then_data);
                on_ok->SetClassName(ToV8Str(iso, ""));
                v8::Local<v8::FunctionTemplate> on_fail =
                    v8::FunctionTemplate::New(iso, LockThenReject,
                                              then_data);
                on_fail->SetClassName(ToV8Str(iso, ""));
                v8::Local<v8::Value> then_argv[] = {
                    on_ok->GetFunction(ctx).ToLocalChecked(),
                    on_fail->GetFunction(ctx).ToLocalChecked()};
                v8::TryCatch try_catch2(iso);
                (void)then_fn.As<v8::Function>()
                    ->Call(ctx, result, 2, then_argv)
                    .FromMaybe(v8::Local<v8::Value>());
                try_catch2.Reset();
                return;
              }
              (void)resolver->Resolve(ctx, result).FromMaybe(false);
              return;
            }
            (void)held->Delete(ctx, key).FromMaybe(false);
            (void)resolver->Resolve(ctx, result).FromMaybe(false);
          },
          task_data);
      v8::Local<v8::Value> queue;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "queueMicrotask"))
              .ToLocal(&queue) &&
          queue->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {
            runner->GetFunction(context).ToLocalChecked()};
        v8::TryCatch try_catch(isolate);
        (void)queue.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    void LockQuery(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      info.GetReturnValue().Set(resolver->GetPromise());
      v8::Local<v8::Value> held_value;
      if (!info.This()
                ->GetPrivate(context, LocksHeldKey(isolate))
                .ToLocal(&held_value) ||
          !held_value->IsMap())
      {
        (void)resolver->Resolve(context, v8::Undefined(isolate))
            .FromMaybe(false);
        return;
      }
      v8::Local<v8::Array> held_list = held_value.As<v8::Map>()->AsArray();
      v8::Local<v8::Array> held = v8::Array::New(isolate);
      uint32_t n = 0;
      for (uint32_t i = 1; i < held_list->Length(); i += 2)
      {
        v8::Local<v8::Value> value;
        if (held_list->Get(context, i).ToLocal(&value))
          (void)held->Set(context, n++, value).FromMaybe(false);
      }
      v8::Local<v8::Object> out = v8::Object::New(isolate);
      (void)out->Set(context, ToV8Str(isolate, "held"), held)
          .FromMaybe(false);
      (void)out->Set(context, ToV8Str(isolate, "pending"),
                     v8::Array::New(isolate))
          .FromMaybe(false);
      (void)resolver->Resolve(context, out).FromMaybe(false);
    }

    void LockManagerConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->lockmanager_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: LockManager constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->lockmanager_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, LocksHeldKey(isolate),
                             v8::Map::New(isolate))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void InstallLockManager(v8::Isolate* isolate,
                            v8::Local<v8::Context> context,
                            ContextState* state)
    {
      if (!state->lockmanager_tpl.IsEmpty()) return;
      v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
          isolate, LockManagerConstructor);
      ctor->SetClassName(ToV8Str(isolate, "LockManager"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      inst->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "LockManager"),
                v8::PropertyAttribute::ReadOnly);
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      method("request", LockRequest);
      method("query", LockQuery);
      state->lockmanager_tpl.Reset(isolate, ctor);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "LockManager"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontDelete |
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
      // navigator.locks: replace the native placeholder with the full
      // LockManager instance
      v8::Local<v8::Value> navigator;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "navigator"))
              .ToLocal(&navigator) &&
          navigator->IsObject())
      {
        v8::Local<v8::Value> manager;
        if (state->lockmanager_tpl.Get(isolate)
                ->GetFunction(context)
                .ToLocal(&manager))
        {
          v8::Local<v8::Value> instance;
          if (manager.As<v8::Function>()
                  ->NewInstance(context, 0, nullptr)
                  .ToLocal(&instance) &&
              instance->IsObject())
            (void)navigator.As<v8::Object>()
                ->DefineOwnProperty(context, ToV8Str(isolate, "locks"),
                                    instance,
                                    static_cast<v8::PropertyAttribute>(
                                        v8::PropertyAttribute::DontEnum))
                .FromMaybe(false);
        }
      }
    }
