    // ---- WebMIDI family -------------------------------------------------

    std::string MidiRandomSuffix(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context)
    {
      v8::Local<v8::Value> math;
      double r = 0.5;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "Math"))
              .ToLocal(&math) &&
          math->IsObject())
      {
        v8::Local<v8::Value> rnd;
        if (math.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "random"))
                .ToLocal(&rnd) &&
            rnd->IsFunction())
        {
          v8::Local<v8::Value> result;
          if (rnd.As<v8::Function>()
                  ->Call(context, math, 0, nullptr)
                  .ToLocal(&result) &&
              result->IsNumber())
            r = result->NumberValue(context).FromMaybe(0.5);
        }
      }
      static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
      std::string out;
      for (int i = 0; i < 8; i++)
      {
        r *= 36;
        int digit = static_cast<int>(r);
        if (digit > 35) digit = 35;
        out += digits[digit];
        r -= digit;
      }
      return out;
    }

    v8::Local<v8::Object> MakeMidiPort(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       ContextState* state,
                                       v8::Local<v8::Object> values,
                                       const std::string& type)
    {
      v8::Local<v8::FunctionTemplate> shape =
          type == "input" && !state->midiinput_tpl.IsEmpty()
              ? state->midiinput_tpl.Get(isolate)
              : type == "output" && !state->midioutput_tpl.IsEmpty()
              ? state->midioutput_tpl.Get(isolate)
              : state->midiport_tpl.Get(isolate);
      v8::Local<v8::Object> inst = shape->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      auto get_str = [&](const char* key, const std::string& fallback) {
        v8::Local<v8::Value> value;
        if (!values.IsEmpty() &&
            values->Get(context, ToV8Str(isolate, key)).ToLocal(&value) &&
            !value->IsUndefined())
          return ToUtf8(isolate, value);
        return fallback;
      };
      std::string id = get_str("id", "");
      if (id.empty())
        id = type + "-" + MidiRandomSuffix(isolate, context);
      (void)inst->Set(context, ToV8Str(isolate, "id"), ToV8Str(isolate, id))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "manufacturer"),
                      ToV8Str(isolate, get_str("manufacturer", "")))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "name"),
                      ToV8Str(isolate, get_str("name", "")))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "type"),
                      ToV8Str(isolate, type))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "version"),
                      ToV8Str(isolate, get_str("version", "")))
          .FromMaybe(false);
      std::string st = get_str("state", "connected");
      (void)inst->Set(context, ToV8Str(isolate, "state"),
                      ToV8Str(isolate, st))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "connection"),
                      ToV8Str(isolate, "closed"))
          .FromMaybe(false);
      return inst;
    }

    void MidiPortSetConnection(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> self, connection;
      if (!data->Get(context, 0).ToLocal(&self) ||
          !data->Get(context, 1).ToLocal(&connection) ||
          !self->IsObject())
        return;
      (void)self.As<v8::Object>()
          ->Set(context, ToV8Str(isolate, "connection"), connection)
          .FromMaybe(false);
      info.GetReturnValue().Set(self);
    }

    void MidiPortToggle(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 2);
      (void)payload->Set(context, 0, info.This()).FromMaybe(false);
      (void)payload->Set(context, 1, info.Data()).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
          isolate, MidiPortSetConnection, payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      MicroResolve(isolate, context, resolver, runner);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void MidiInputReceive(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> event = v8::Object::New(isolate);
      v8::Local<v8::Value> data =
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
      v8::Local<v8::Array> bytes = v8::Array::New(isolate);
      if (data->IsArray())
        bytes = data.As<v8::Array>();
      v8::Local<v8::Uint8Array> arr = v8::Uint8Array::New(
          v8::ArrayBuffer::New(isolate,
                               static_cast<size_t>(bytes->Length())),
          0, static_cast<size_t>(bytes->Length()));
      for (uint32_t i = 0; i < bytes->Length(); i++)
      {
        v8::Local<v8::Value> byte;
        if (bytes->Get(context, i).ToLocal(&byte))
          (void)arr->Set(context, i, byte).FromMaybe(false);
      }
      (void)event->Set(context, ToV8Str(isolate, "data"), arr)
          .FromMaybe(false);
      double stamp = info.Length() >= 2
          ? info[1]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      (void)event->Set(context, ToV8Str(isolate, "receivedTime"),
                       v8::Number::New(isolate, stamp))
          .FromMaybe(false);
      v8::Local<v8::Value> handler;
      if (info.This()
              ->Get(context, ToV8Str(isolate, "onmidimessage"))
              .ToLocal(&handler) &&
          handler->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {event};
        v8::TryCatch try_catch(isolate);
        (void)handler.As<v8::Function>()
            ->Call(context, info.This(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    void MidiOutputSend(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> sent;
      if (!info.This()
                ->Get(context, ToV8Str(isolate, "sent"))
                .ToLocal(&sent) ||
          !sent->IsArray())
        return;
      v8::Local<v8::Object> record = v8::Object::New(isolate);
      v8::Local<v8::Array> data_copy = v8::Array::New(isolate);
      uint32_t n = 0;
      if (info.Length() >= 1 && info[0]->IsArray())
      {
        v8::Local<v8::Array> data = info[0].As<v8::Array>();
        for (uint32_t i = 0; i < data->Length(); i++)
        {
          v8::Local<v8::Value> byte;
          if (data->Get(context, i).ToLocal(&byte))
            (void)data_copy->Set(context, n++, byte).FromMaybe(false);
        }
      }
      double stamp = info.Length() >= 2
          ? info[1]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      (void)record->Set(context, ToV8Str(isolate, "data"), data_copy)
          .FromMaybe(false);
      (void)record->Set(context, ToV8Str(isolate, "timestamp"),
                        v8::Number::New(isolate, stamp))
          .FromMaybe(false);
      v8::Local<v8::Array> sent_list = sent.As<v8::Array>();
      (void)sent_list->Set(context, sent_list->Length(), record)
          .FromMaybe(false);
    }

    void MidiOutputClear(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      (void)info.This()->Set(context, ToV8Str(isolate, "sent"),
                             v8::Array::New(isolate))
          .FromMaybe(false);
    }

    void MidiAccessConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->midiaccess_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: MIDIAccess constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->midiaccess_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      v8::Local<v8::Object> values =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0].As<v8::Object>()
              : v8::Object::New(isolate);
      bool sysex = false;
      {
        v8::Local<v8::Value> raw;
        if (values->Get(context, ToV8Str(isolate, "sysexEnabled"))
                .ToLocal(&raw))
          sysex = raw->BooleanValue(isolate);
      }
      (void)inst->Set(context, ToV8Str(isolate, "sysexEnabled"),
                      v8::Boolean::New(isolate, sysex))
          .FromMaybe(false);
      v8::Local<v8::Map> inputs = v8::Map::New(isolate);
      v8::Local<v8::Map> outputs = v8::Map::New(isolate);
      const char* groups[] = {"inputs", "outputs"};
      v8::Local<v8::Map>* maps[] = {&inputs, &outputs};
      const char* types[] = {"input", "output"};
      for (int g = 0; g < 2; g++)
      {
        v8::Local<v8::Value> list;
        if (!values->Get(context, ToV8Str(isolate, groups[g])).ToLocal(&list) ||
            !list->IsArray())
          continue;
        v8::Local<v8::Array> items = list.As<v8::Array>();
        for (uint32_t i = 0; i < items->Length(); i++)
        {
          v8::Local<v8::Value> values_entry;
          if (!items->Get(context, i).ToLocal(&values_entry) ||
              !values_entry->IsObject())
            continue;
          v8::Local<v8::Object> port = MakeMidiPort(
              isolate, context, state, values_entry.As<v8::Object>(),
              types[g]);
          v8::Local<v8::Value> id;
          if (port->Get(context, ToV8Str(isolate, "id")).ToLocal(&id))
            (void)maps[g]
                ->As<v8::Map>()
                ->Set(context, id, port)
                .IsEmpty();
        }
      }
      (void)inst->SetPrivate(context, MidiInputsKey(isolate), inputs)
          .FromMaybe(false);
      (void)inst->SetPrivate(context, MidiOutputsKey(isolate), outputs)
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "onstatechange"),
                      v8::Null(isolate))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void MidiAccessInputs(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> inputs;
      if (info.This()
              ->GetPrivate(context, MidiInputsKey(isolate))
              .ToLocal(&inputs) &&
          inputs->IsMap())
        info.GetReturnValue().Set(inputs);
    }

    void MidiAccessOutputs(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> outputs;
      if (info.This()
              ->GetPrivate(context, MidiOutputsKey(isolate))
              .ToLocal(&outputs) &&
          outputs->IsMap())
        info.GetReturnValue().Set(outputs);
    }

    void InstallWebMidiFamily(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              ContextState* state)
    {
      if (state->midiport_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              ContextState* st = GetState(ctx);
              if (!info.IsConstructCall() || st == nullptr ||
                  st->midiport_tpl.IsEmpty())
              {
                iso->ThrowException(ToV8Str(
                    iso, "TypeError: MIDIPort constructor requires new"));
                return;
              }
              v8::Local<v8::Object> values =
                  info.Length() >= 1 && info[0]->IsObject()
                      ? info[0].As<v8::Object>()
                      : v8::Object::New(iso);
              info.GetReturnValue().Set(MakeMidiPort(iso, ctx, st, values,
                                                     "input"));
            });
        ctor->SetClassName(ToV8Str(isolate, "MIDIPort"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MIDIPort"),
                  v8::PropertyAttribute::ReadOnly);
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        method("open", MidiPortOpen);
        method("close", MidiPortClose);
        state->midiport_tpl.Reset(isolate, ctor);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "MIDIPort"),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontDelete |
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      if (state->midiinput_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, [](const v8::FunctionCallbackInfo<v8::Value>& info) {});
        ctor->SetClassName(ToV8Str(isolate, "MIDIInput"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MIDIInput"),
                  v8::PropertyAttribute::ReadOnly);
        ctor->Inherit(state->midiport_tpl.Get(isolate));
        v8::Local<v8::FunctionTemplate> receive =
            v8::FunctionTemplate::New(isolate, MidiInputReceive);
        receive->SetClassName(ToV8Str(isolate, "_receive"));
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "_receive"), receive);
        state->midiinput_tpl.Reset(isolate, ctor);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "MIDIInput"),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontDelete |
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      if (state->midioutput_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, [](const v8::FunctionCallbackInfo<v8::Value>& info) {});
        ctor->SetClassName(ToV8Str(isolate, "MIDIOutput"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MIDIOutput"),
                  v8::PropertyAttribute::ReadOnly);
        ctor->Inherit(state->midiport_tpl.Get(isolate));
        state->midioutput_tpl.Reset(isolate, ctor);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "MIDIOutput"),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontDelete |
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      if (state->midiaccess_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, MidiAccessConstructor);
        ctor->SetClassName(ToV8Str(isolate, "MIDIAccess"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MIDIAccess"),
                  v8::PropertyAttribute::ReadOnly);
        auto getter = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          ctor->PrototypeTemplate()->SetAccessorProperty(
              ToV8Str(isolate, name), fn, v8::Local<v8::FunctionTemplate>());
        };
        getter("inputs", MidiAccessInputs);
        getter("outputs", MidiAccessOutputs);
        state->midiaccess_tpl.Reset(isolate, ctor);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "MIDIAccess"),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontDelete |
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
    }
