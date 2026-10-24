// lv8 G1 events: a native global EventTarget (addEventListener/
// removeEventListener/dispatchEvent) plus minimal Event-family constructors
// (Event, CustomEvent, PopStateEvent, HashChangeEvent). Navigation code in
// LV8Location.cpp uses DispatchEvent to fire popstate/hashchange.

#include "LV8.h"

#include <cstring>
#include <string>
#include <utility>

namespace LV8
  {
  namespace
    {
    v8::Local<v8::Private> DataKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8:event-data"));
    }

    // Accessor for one event property; the property name arrives in Data().
    void EventPropertyGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();

      v8::Local<v8::Value> data;
      if (!info.This()->GetPrivate(context, DataKey(isolate)).ToLocal(&data))
      {
        info.GetReturnValue().SetUndefined();
        return;
      }

      v8::Local<v8::String> prop = info.Data().As<v8::String>();
      v8::Local<v8::Value> value;
      if (data->IsObject() &&
          data.As<v8::Object>()->Get(context, prop).ToLocal(&value))
        info.GetReturnValue().Set(value);
      else
        info.GetReturnValue().SetUndefined();
    }

    // Writes through to the private props object (the reference's events
    // are plain JS objects; some oracle flows assign event fields).
    void EventPropertySetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data;
      if (info.Length() < 1 ||
          !info.This()->GetPrivate(context, DataKey(isolate)).ToLocal(&data) ||
          !data->IsObject())
        return;
      (void)data.As<v8::Object>()
          ->Set(context, info.Data(), info[0])
          .FromMaybe(false);
    }

    void AddEventListener(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction())
        return;  // browsers silently ignore malformed addEventListener calls

      ContextState* state = GetState(isolate->GetCurrentContext());
      if (state == nullptr)
        return;

      std::string type = ToUtf8(isolate, info[0]);
      v8::Global<v8::Function> fn(isolate, info[1].As<v8::Function>());

      // Same (type, function) pair registers at most once, like browsers.
      for (auto& entry : state->listeners)
      {
        if (entry.first == type && entry.second == fn)
          return;
      }
      state->listeners.emplace_back(std::move(type), std::move(fn));
    }

    void RemoveEventListener(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction())
        return;

      ContextState* state = GetState(isolate->GetCurrentContext());
      if (state == nullptr)
        return;

      std::string type = ToUtf8(isolate, info[0]);
      v8::Global<v8::Function> fn(isolate, info[1].As<v8::Function>());
      for (auto it = state->listeners.begin(); it != state->listeners.end();
           ++it)
      {
        if (it->first == type && it->second == fn)
        {
          state->listeners.erase(it);
          return;
        }
      }
    }

    // Expose dispatchEvent for JS callers; internal navigation code uses
    // DispatchEvent directly (so its exceptions surface the same way).
    void DispatchEventJs(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsObject())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: Failed to execute 'dispatchEvent'"));
        return;
      }

      v8::Local<v8::Value> type;
      if (!info[0].As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "type"))
               .ToLocal(&type) ||
          !type->IsString())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: Event type missing on dispatchEvent argument"));
        return;
      }

      ContextState* state = GetState(context);
      if (state == nullptr)
      {
        info.GetReturnValue().Set(true);
        return;
      }

      std::string event_type = ToUtf8(isolate, type);
      v8::Local<v8::Value> event = info[0];
      for (auto& entry : state->listeners)
      {
        if (entry.first != event_type)
          continue;
        v8::Local<v8::Function> fn =
            v8::Local<v8::Function>::New(isolate, entry.second);
        (void)fn->Call(context, context->Global(), 1, &event).FromMaybe(
            v8::Local<v8::Value>());
      }
      info.GetReturnValue().Set(true);
    }

    void EventStopMethod(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data;
      if (info.This()
              ->GetPrivate(context, DataKey(isolate))
              .ToLocal(&data) &&
          data->IsObject())
      {
        (void)data.As<v8::Object>()
            ->Set(context, info.Data().As<v8::String>(),
                  v8::Boolean::New(isolate, true))
            .FromMaybe(false);
      }
    }

    // G4.278: per-class prototype accessors (generated from the reference
    // measurement).  Instances stay thin ({isTrusted} own) and inherit the
    // generic Event members, matching the reference event family face.
    struct LV8EventClassSpec
    {
      const char* name;
      const char* props[24];
    };

#include "LV8EventFamily.inc"

    const LV8EventClassSpec* FindEventClassSpec(const char* class_name)
    {
      for (const LV8EventClassSpec* s = kLV8EventClasses; s->name != nullptr;
           ++s)
      {
        if (std::strcmp(s->name, class_name) == 0) return s;
      }
      return nullptr;
    }

    // Shared constructor body for the Event family: builds an object of
    // |class_name| whose accessors read |props| through the private data
    // object. Real constructors live on the global; instances carry a
    // Symbol.toStringTag so toString() renders "[object PopStateEvent]".
    v8::Local<v8::Object> BuildEvent(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const char* class_name,
                                     v8::Local<v8::Object> props)
    {
      v8::Local<v8::String> name = ToV8Str(isolate, class_name);
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(name);
      v8::Local<v8::ObjectTemplate> instance_tmpl = tpl->InstanceTemplate();
      instance_tmpl->SetInternalFieldCount(0);
      instance_tmpl->Set(v8::Symbol::GetToStringTag(isolate), name,
                         v8::PropertyAttribute::ReadOnly);

      const char* const known[] = {"type",     "state",     "oldURL",
                                   "newURL",   "detail",    "bubbles",
                                   "cancelable", "isTrusted", "target",
                                   "currentTarget", "eventPhase",
                                   "data",     "origin",    "ports",
                                   "code",     "reason",    "wasClean"};
      // G4.231: ErrorEvent instances carry the uncaught-script details
      // (message + error) - only this class exposes those accessors.
      std::vector<const char*> known_props(known, known + sizeof(known) / sizeof(known[0]));
      if (std::strcmp(class_name, "ErrorEvent") == 0)
      {
        known_props.push_back("message");
        known_props.push_back("error");
      }
      for (const char* prop_name : known_props)
      {
        v8::Local<v8::String> prop = ToV8Str(isolate, prop_name);
        v8::Local<v8::FunctionTemplate> getter =
            v8::FunctionTemplate::New(isolate, EventPropertyGetter, prop);
        getter->SetClassName(ToV8Str(isolate, std::string("get ") + prop_name));
        v8::Local<v8::FunctionTemplate> setter =
            v8::FunctionTemplate::New(isolate, EventPropertySetter, prop);
        setter->SetClassName(ToV8Str(isolate, std::string("set ") + prop_name));
        // G4.289: the reference exposes exactly one enumerable own member
        // (isTrusted); every other data accessor is non-enumerable.
        v8::PropertyAttribute attrs =
            std::strcmp(prop_name, "isTrusted") == 0
                ? static_cast<v8::PropertyAttribute>(0)
                : static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::DontEnum);
        instance_tmpl->SetAccessorProperty(prop, getter, setter, attrs);
      }
      // G4.278: additionally expose class-specific members on the class
      // prototype (the reference keeps them there and inherits the generic
      // Event members).  The instance accessors above remain the
      // compatibility path for the private props store.
      const LV8EventClassSpec* spec = FindEventClassSpec(class_name);
      if (spec != nullptr)
      {
        for (const char* const* p = spec->props; *p != nullptr; ++p)
        {
          const char* prop_name = *p;
          v8::Local<v8::String> prop = ToV8Str(isolate, prop_name);
          v8::Local<v8::FunctionTemplate> getter =
              v8::FunctionTemplate::New(isolate, EventPropertyGetter, prop);
          getter->SetClassName(ToV8Str(isolate, std::string("get ") + prop_name));
          v8::Local<v8::FunctionTemplate> setter =
              v8::FunctionTemplate::New(isolate, EventPropertySetter, prop);
          setter->SetClassName(ToV8Str(isolate, std::string("set ") + prop_name));
          tpl->PrototypeTemplate()->SetAccessorProperty(prop, getter, setter);
        }
      }
      // propagation control
      const char* const stop_methods[] = {"stopPropagation",
                                          "stopImmediatePropagation"};
      for (const char* method_name : stop_methods)
      {
        v8::Local<v8::String> key = ToV8Str(isolate, method_name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, EventStopMethod, key);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      }

      v8::Local<v8::Object> instance =
          instance_tmpl->NewInstance(context).ToLocalChecked();
      (void)instance->SetPrivate(context, DataKey(isolate), props)
          .FromMaybe(false);
      return instance;
    }

    // Factory for `new XxxEvent(type, init)` constructors. Properties come
    // from init (init.type is ignored in favor of the positional argument,
    // matching the DOM).

    // G4 ledger chunk 3: typed default fields for the rich event family
    // (kind: 0 = raw, 1 = Number, 2 = Boolean, 3 = String), replicating
    // the reference bootstrap's per-class coercions and fallbacks.
    struct EventFieldSpec
    {
      const char* name;
      int kind;
      double number;
      const char* text;
    };

    double NumCoerce(v8::Local<v8::Context> context,
                     v8::Local<v8::Value> value, double fallback)
    {
      if (value.IsEmpty() || value->IsNullOrUndefined()) return 0.0;
      return value->NumberValue(context).FromMaybe(fallback);
    }

    void ApplyFieldSpec(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> props,
                        const EventFieldSpec* fields, size_t count,
                        v8::Local<v8::Object> init)
    {
      for (size_t i = 0; i < count; i++)
      {
        v8::Local<v8::String> key = ToV8Str(isolate, fields[i].name);
        v8::Local<v8::Value> raw;
        bool provided = false;
        if (!init.IsEmpty())
        {
          v8::Local<v8::Value> candidate;
          if (init->Get(context, key).ToLocal(&candidate) &&
              !candidate->IsUndefined())
          {
            raw = candidate;
            provided = true;
          }
        }
        v8::Local<v8::Value> out;
        switch (fields[i].kind)
        {
          case 1:
            out = v8::Number::New(
                isolate,
                provided ? NumCoerce(context, raw, 0.0) : fields[i].number);
            break;
          case 2:
            out = v8::Boolean::New(
                isolate,
                provided ? raw->BooleanValue(isolate)
                         : (fields[i].number != 0));
            break;
          case 3:
          {
            if (!provided || raw->IsNullOrUndefined())
            {
              out = ToV8Str(isolate, fields[i].text);
            }
            else
            {
              v8::Local<v8::String> text;
              out = raw->ToString(context).ToLocal(&text)
                  ? v8::Local<v8::Value>(text)
                  : v8::Local<v8::Value>(ToV8Str(isolate, fields[i].text));
            }
            break;
          }
          default:
            out = provided ? raw
                           : v8::Local<v8::Value>(v8::Null(isolate));
            break;
        }
        (void)props->Set(context, key, out).FromMaybe(false);
      }
    }


    // G4.278: install the class-specific accessors on the *class prototype*
    // exactly once (the reference instance face is thin: props in a private
    // slot + own isTrusted).  Getter/setter read the receiver's private props,
    // so inheritance works for every instance of the class.
    void InstallEventClassAccessors(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    const char* class_name)
    {
      const LV8EventClassSpec* spec = FindEventClassSpec(class_name);
      if (spec == nullptr) return;
      v8::Local<v8::Value> ctor_value;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, class_name))
               .ToLocal(&ctor_value) ||
          !ctor_value->IsFunction())
      {
        return;
      }
      v8::Local<v8::Object> proto;
      if (!ctor_value.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "prototype"))
               .ToLocal(&ctor_value) ||
          !ctor_value->IsObject())
      {
        return;
      }
      proto = ctor_value.As<v8::Object>();
      for (const char* const* p = spec->props; *p != nullptr; ++p)
      {
        const char* prop_name = *p;
        v8::Local<v8::String> key = ToV8Str(isolate, prop_name);
        if (proto->HasOwnProperty(context, key).FromMaybe(false)) continue;
        v8::Local<v8::FunctionTemplate> getter_tpl =
            v8::FunctionTemplate::New(isolate, EventPropertyGetter, key);
        getter_tpl->SetClassName(
            ToV8Str(isolate, std::string("get ") + prop_name));
        v8::Local<v8::FunctionTemplate> setter_tpl =
            v8::FunctionTemplate::New(isolate, EventPropertySetter, key);
        setter_tpl->SetClassName(
            ToV8Str(isolate, std::string("set ") + prop_name));
        proto->SetAccessorProperty(
            key, getter_tpl->GetFunction(context).ToLocalChecked(),
            setter_tpl->GetFunction(context).ToLocalChecked(),
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::DontEnum));
      }
    }

    void MakeEventConstructorEx(const char* class_name,
                                const EventFieldSpec* fields, size_t count,
                                const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();

      v8::Local<v8::Object> props = v8::Object::New(isolate);
      if (info.Length() > 0 && info[0]->IsString())
      {
        (void)props->Set(context, ToV8Str(isolate, "type"), info[0])
            .FromMaybe(false);
      }
      ApplyFieldSpec(isolate, context, props, fields, count,
                     info.Length() > 1 && info[1]->IsObject()
                         ? info[1].As<v8::Object>()
                         : v8::Local<v8::Object>());
      if (info.Length() > 1 && info[1]->IsObject())
      {
        // keys outside the spec table still flow through (raw copy)
        v8::Local<v8::Array> keys =
            info[1].As<v8::Object>()->GetOwnPropertyNames(context)
                .ToLocalChecked();
        for (uint32_t i = 0; i < keys->Length(); i++)
        {
          v8::Local<v8::Value> key = keys->Get(context, i).ToLocalChecked();
          if (props->Has(context, key).FromMaybe(false)) continue;
          v8::Local<v8::Value> value;
          if (info[1].As<v8::Object>()->Get(context, key).ToLocal(&value))
            (void)props->Set(context, key, value).FromMaybe(false);
        }
      }
      // dispatch-time defaults (browser semantics)
      if (!props->Has(context, ToV8Str(isolate, "isTrusted")).FromMaybe(false))
        (void)props->Set(context, ToV8Str(isolate, "isTrusted"),
                         v8::Boolean::New(isolate, false))
            .FromMaybe(false);
      if (!props->Has(context, ToV8Str(isolate, "target")).FromMaybe(false))
        (void)props->Set(context, ToV8Str(isolate, "target"),
                         v8::Null(isolate))
            .FromMaybe(false);
      if (!props->Has(context, ToV8Str(isolate, "currentTarget"))
               .FromMaybe(false))
        (void)props->Set(context, ToV8Str(isolate, "currentTarget"),
                         v8::Null(isolate))
            .FromMaybe(false);
      if (!props->Has(context, ToV8Str(isolate, "eventPhase")).FromMaybe(false))
        (void)props->Set(context, ToV8Str(isolate, "eventPhase"),
                         v8::Integer::New(isolate, 0))
            .FromMaybe(false);

      if (info.IsConstructCall())
      {
        // invoked via super() from a JS subclass: install LIVE accessors
        // backed by the props object (so dispatch-time updates like
        // eventPhase stay visible) instead of substituting the instance
        v8::Local<v8::String> src = ToV8Str(
            isolate,
            // G4.289: mirror the reference own-member face - exactly one
            // enumerable own member (isTrusted), the rest hidden.
            "(function (obj, props, keys) {"
            " for (const k of keys) {"
            "  Object.defineProperty(obj, k, {"
            "   get() { return props[k]; },"
            "   set(v) { props[k] = v; },"
            "   configurable: true, enumerable: (k === 'isTrusted') });"
            " }"
            " if (!('isTrusted' in props)) {"
            "  Object.defineProperty(obj, 'isTrusted', {"
            "   get() { return false; }, set(v) {},"
            "   configurable: true, enumerable: true }); }"
            " Object.defineProperty(obj, 'preventDefault', {"
            "  value: function () {"
            "   if (props.cancelable) props.defaultPrevented = true; },"
            "  configurable: true, enumerable: false });"
            " Object.defineProperty(obj, 'stopPropagation', {"
            "  value: function () { props.stopPropagation = true; },"
            "  configurable: true, enumerable: false });"
            " Object.defineProperty(obj, 'stopImmediatePropagation', {"
            "  value: function () { props.stopImmediatePropagation = true; },"
            "  configurable: true, enumerable: false }); })");
        v8::ScriptOrigin origin(ToV8Str(isolate, "<anonymous>"));
        v8::Local<v8::Script> script =
            v8::Script::Compile(context, src, &origin).ToLocalChecked();
        v8::Local<v8::Value> keys =
            props->GetOwnPropertyNames(context).ToLocalChecked();
        v8::Local<v8::Value> args[] = {info.This(), props, keys};
        (void)script->Run(context)
            .ToLocalChecked()
            .As<v8::Function>()
            ->Call(context, context->Global(), 3, args)
            .FromMaybe(v8::Local<v8::Value>());
        (void)info.This()
            ->SetPrivate(context, DataKey(isolate), props)
            .FromMaybe(false);
        // G4.278: best-effort prototype install of the class-specific
        // members (template-shaped prototypes reject post-hoc accessor
        // installs; the instance accessors above stay authoritative).
        InstallEventClassAccessors(isolate, context, class_name);
        return;
      }
      info.GetReturnValue().Set(
          BuildEvent(isolate, context, class_name, props));
    }

    void MakeEventConstructor(const char* class_name,
                              const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MakeEventConstructorEx(class_name, nullptr, 0, info);
    }

#define LV8_EVENT_CTOR(name)                                                 \
    void name##Constructor(const v8::FunctionCallbackInfo<v8::Value>& info)  \
    {                                                                        \
      MakeEventConstructor(#name, info);                                     \
    }

    LV8_EVENT_CTOR(Event)
    LV8_EVENT_CTOR(CustomEvent)
    LV8_EVENT_CTOR(PopStateEvent)
    LV8_EVENT_CTOR(HashChangeEvent)
    LV8_EVENT_CTOR(MessageEvent)
    LV8_EVENT_CTOR(ErrorEvent)
    LV8_EVENT_CTOR(CloseEvent)
#undef LV8_EVENT_CTOR

    struct FamilyCtorSpec
    {
      const char* name;
      const EventFieldSpec* fields;
      size_t count;
    };

    const FamilyCtorSpec* FamilyCtorLookup(const char* name)
    {
      static const EventFieldSpec kUi[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr}};
      static const EventFieldSpec kMouse[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr},
          {"screenX", 1, 0, nullptr}, {"screenY", 1, 0, nullptr},
          {"clientX", 1, 0, nullptr}, {"clientY", 1, 0, nullptr},
          {"button", 1, 0, nullptr}, {"buttons", 1, 0, nullptr},
          {"movementX", 1, 0, nullptr}, {"movementY", 1, 0, nullptr},
          {"ctrlKey", 2, 0, nullptr}, {"shiftKey", 2, 0, nullptr},
          {"altKey", 2, 0, nullptr}, {"metaKey", 2, 0, nullptr},
          {"relatedTarget", 0, 0, nullptr}};
      static const EventFieldSpec kPointer[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr},
          {"screenX", 1, 0, nullptr}, {"screenY", 1, 0, nullptr},
          {"clientX", 1, 0, nullptr}, {"clientY", 1, 0, nullptr},
          {"button", 1, 0, nullptr}, {"buttons", 1, 0, nullptr},
          {"movementX", 1, 0, nullptr}, {"movementY", 1, 0, nullptr},
          {"ctrlKey", 2, 0, nullptr}, {"shiftKey", 2, 0, nullptr},
          {"altKey", 2, 0, nullptr}, {"metaKey", 2, 0, nullptr},
          {"relatedTarget", 0, 0, nullptr},
          {"pointerId", 1, 0, nullptr}, {"pointerType", 3, 0, ""},
          {"isPrimary", 2, 0, nullptr}};
      static const EventFieldSpec kKeyboard[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr},
          {"key", 3, 0, ""}, {"code", 3, 0, ""},
          {"ctrlKey", 2, 0, nullptr}, {"shiftKey", 2, 0, nullptr},
          {"altKey", 2, 0, nullptr}, {"metaKey", 2, 0, nullptr},
          {"repeat", 2, 0, nullptr}};
      static const EventFieldSpec kInput[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr},
          {"data", 0, 0, nullptr}, {"inputType", 3, 0, ""}};
      static const EventFieldSpec kFocus[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr},
          {"relatedTarget", 0, 0, nullptr}};
      static const EventFieldSpec kWheel[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr},
          {"screenX", 1, 0, nullptr}, {"screenY", 1, 0, nullptr},
          {"clientX", 1, 0, nullptr}, {"clientY", 1, 0, nullptr},
          {"button", 1, 0, nullptr}, {"buttons", 1, 0, nullptr},
          {"movementX", 1, 0, nullptr}, {"movementY", 1, 0, nullptr},
          {"ctrlKey", 2, 0, nullptr}, {"shiftKey", 2, 0, nullptr},
          {"altKey", 2, 0, nullptr}, {"metaKey", 2, 0, nullptr},
          {"relatedTarget", 0, 0, nullptr},
          {"deltaX", 1, 0, nullptr}, {"deltaY", 1, 0, nullptr},
          {"deltaZ", 1, 0, nullptr}, {"deltaMode", 1, 0, nullptr}};
      static const EventFieldSpec kDrag[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr},
          {"screenX", 1, 0, nullptr}, {"screenY", 1, 0, nullptr},
          {"clientX", 1, 0, nullptr}, {"clientY", 1, 0, nullptr},
          {"button", 1, 0, nullptr}, {"buttons", 1, 0, nullptr},
          {"movementX", 1, 0, nullptr}, {"movementY", 1, 0, nullptr},
          {"ctrlKey", 2, 0, nullptr}, {"shiftKey", 2, 0, nullptr},
          {"altKey", 2, 0, nullptr}, {"metaKey", 2, 0, nullptr},
          {"relatedTarget", 0, 0, nullptr},
          {"dataTransfer", 0, 0, nullptr}};
      static const EventFieldSpec kClipboard[] = {
          {"clipboardData", 0, 0, nullptr}};
      static const EventFieldSpec kComposition[] = {
          {"view", 0, 0, nullptr}, {"detail", 1, 0, nullptr},
          {"data", 3, 0, ""}};
      static const EventFieldSpec kAnimation[] = {
          {"animationName", 3, 0, ""}, {"elapsedTime", 1, 0, nullptr},
          {"pseudoElement", 3, 0, ""}};
      static const EventFieldSpec kTransition[] = {
          {"propertyName", 3, 0, ""}, {"elapsedTime", 1, 0, nullptr},
          {"pseudoElement", 3, 0, ""}};
      static const EventFieldSpec kSubmit[] = {
          {"submitter", 0, 0, nullptr}};
      static const EventFieldSpec kProgress[] = {
          {"lengthComputable", 2, 0, nullptr},
          {"loaded", 1, 0, nullptr}, {"total", 1, 0, nullptr}};

      static const FamilyCtorSpec kTables[] = {
          {"UIEvent", kUi, sizeof(kUi) / sizeof(kUi[0])},
          {"MouseEvent", kMouse, sizeof(kMouse) / sizeof(kMouse[0])},
          {"PointerEvent", kPointer, sizeof(kPointer) / sizeof(kPointer[0])},
          {"KeyboardEvent", kKeyboard, sizeof(kKeyboard) / sizeof(kKeyboard[0])},
          {"InputEvent", kInput, sizeof(kInput) / sizeof(kInput[0])},
          {"FocusEvent", kFocus, sizeof(kFocus) / sizeof(kFocus[0])},
          {"WheelEvent", kWheel, sizeof(kWheel) / sizeof(kWheel[0])},
          {"DragEvent", kDrag, sizeof(kDrag) / sizeof(kDrag[0])},
          {"ClipboardEvent", kClipboard, sizeof(kClipboard) / sizeof(kClipboard[0])},
          {"CompositionEvent", kComposition, sizeof(kComposition) / sizeof(kComposition[0])},
          {"AnimationEvent", kAnimation, sizeof(kAnimation) / sizeof(kAnimation[0])},
          {"TransitionEvent", kTransition, sizeof(kTransition) / sizeof(kTransition[0])},
          {"SubmitEvent", kSubmit, sizeof(kSubmit) / sizeof(kSubmit[0])},
          {"ProgressEvent", kProgress, sizeof(kProgress) / sizeof(kProgress[0])},
      };
      for (const FamilyCtorSpec& entry : kTables)
      {
        if (std::strcmp(entry.name, name) == 0) return &entry;
      }
      return nullptr;
    }

    void FamilyCtorCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data = info.Data();
      if (data.IsEmpty() || !data->IsString()) return;
      std::string class_name = ToUtf8(isolate, data);
      const FamilyCtorSpec* spec = FamilyCtorLookup(class_name.c_str());
      MakeEventConstructorEx(class_name.c_str(),
                             spec ? spec->fields : nullptr,
                             spec ? spec->count : 0, info);
    }

    void TouchConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: Touch constructor requires new"));
        return;
      }
      // M11 (reference ground truth, _m11_values.json): the reference Touch ctor
      // IGNORES its options object entirely - every accessor reads 0 and
      // target reads undefined, the instance is zero-own, and the object
      // must be the template instance so the tag/prototype face apply
      // (returning a bare Object::New gave [object Object] with 11 own).
      info.GetReturnValue().Set(info.This());
    }

    void TouchListItem(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double index = info.Length() >= 1
          ? NumCoerce(context, info[0], 0.0)
          : 0.0;
      v8::Local<v8::Value> item =
          info.This()->Get(context, index).FromMaybe(v8::Local<v8::Value>());
      if (item.IsEmpty() || !item->BooleanValue(isolate))
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(item);
    }

    void TouchListConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->touchlist_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: TouchList constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->touchlist_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      // M11 (reference ground truth): the reference TouchList ctor IGNORES its
      // argument - `new TouchList([t1, t2]).length` is 0 there and the
      // instance is zero-own (length resolves through the prototype
      // accessor).  The event-side materialization (TouchEventCtor) still
      // plants indexed items + length on the lists it builds itself.
      (void)inst;
      info.GetReturnValue().Set(inst);
    }

    void TouchEventCtor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MakeEventConstructorEx("TouchEvent", nullptr, 0, info);
      // materialize typed touch lists (defaults: empty TouchList)
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || state->touchlist_tpl.IsEmpty()) return;
      v8::Local<v8::Object> options =
          info.Length() > 1 && info[1]->IsObject()
              ? info[1].As<v8::Object>()
              : v8::Local<v8::Object>();
      const char* names[] = {"touches", "targetTouches", "changedTouches"};
      for (const char* name : names)
      {
        v8::Local<v8::Value> list;
        v8::Local<v8::Array> values = v8::Array::New(isolate);
        if (!options.IsEmpty() &&
            options->Get(context, ToV8Str(isolate, name)).ToLocal(&list) &&
            list->IsArray())
          values = list.As<v8::Array>();
        v8::Local<v8::Object> touch_list =
            state->touchlist_tpl.Get(isolate)
                ->InstanceTemplate()
                ->NewInstance(context)
                .ToLocalChecked();
        uint32_t count = values->Length();
        for (uint32_t i = 0; i < count; i++)
        {
          v8::Local<v8::Value> value;
          if (values->Get(context, i).ToLocal(&value))
            (void)touch_list->Set(context, i, value).FromMaybe(false);
        }
        (void)touch_list
            ->Set(context, ToV8Str(isolate, "length"),
                  v8::Integer::New(isolate, static_cast<int32_t>(count)))
            .FromMaybe(false);
        (void)info.This()
            ->Set(context, ToV8Str(isolate, name), touch_list)
            .FromMaybe(false);
      }
    }

    // family installer: registers the constructors with real Inherit()
    // chains rooted at the native Event constructor template
    void InstallEventFamily(v8::Isolate* isolate,
                            v8::Local<v8::Context> context,
                            ContextState* state)
    {
      v8::Local<v8::FunctionTemplate> event_tpl =
          state->event_ctor_tpl.Get(isolate);
      if (event_tpl.IsEmpty()) return;

      auto make_family = [&](const char* name,
                             v8::Local<v8::FunctionTemplate> parent) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> tpl =
            v8::FunctionTemplate::New(isolate, FamilyCtorCallback, key);
        tpl->SetClassName(key);
        v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate), key,
                  v8::PropertyAttribute::ReadOnly);
        tpl->Inherit(parent);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, key,
                                tpl->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
        return tpl;
      };

      v8::Local<v8::FunctionTemplate> ui =
          make_family("UIEvent", event_tpl);
      v8::Local<v8::FunctionTemplate> mouse =
          make_family("MouseEvent", ui);
      make_family("PointerEvent", mouse);
      make_family("KeyboardEvent", ui);
      make_family("InputEvent", ui);
      make_family("FocusEvent", ui);
      v8::Local<v8::FunctionTemplate> wheel =
          make_family("WheelEvent", mouse);
      make_family("DragEvent", mouse);
      make_family("ClipboardEvent", event_tpl);
      make_family("CompositionEvent", ui);
      make_family("AnimationEvent", event_tpl);
      make_family("TransitionEvent", event_tpl);
      make_family("SubmitEvent", event_tpl);
      make_family("ProgressEvent", event_tpl);
      // WheelEvent delta-mode constants
      v8::Local<v8::Function> wheel_fn = wheel->GetFunction(context)
                                             .ToLocalChecked();
      const char* modes[] = {"DOM_DELTA_PIXEL", "DOM_DELTA_LINE",
                             "DOM_DELTA_PAGE"};
      for (int i = 0; i < 3; i++)
        (void)wheel_fn
            ->Set(context, ToV8Str(isolate, modes[i]),
                  v8::Integer::New(isolate, i))
            .FromMaybe(false);

      // Touch / TouchList / TouchEvent
      if (state->touchlist_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, TouchListConstructor);
        ctor->SetClassName(ToV8Str(isolate, "TouchList"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "TouchList"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        v8::Local<v8::FunctionTemplate> item_fn =
            v8::FunctionTemplate::New(isolate, TouchListItem);
        item_fn->SetClassName(ToV8Str(isolate, "item"));
        // G4.383: the reference's TouchList.item carries arity 1 and is
        // non-enumerable (the ledger's ":NE" row mark).
        item_fn->SetLength(1);
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "item"), item_fn,
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::DontEnum));
        state->touchlist_tpl.Reset(isolate, ctor);
      }
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, TouchConstructor);
        ctor->SetClassName(ToV8Str(isolate, "Touch"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "Touch"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "Touch"),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      {
        // M11: expose the SAME template the constructor returns instances
        // of - the previous bare duplicate template made
        // window.TouchList.prototype diverge from the instances' real
        // prototype (tag/item on one, face accessors on the other), so
        // `new TouchList().length` missed the face accessor
        if (!state->touchlist_tpl.IsEmpty())
        {
          v8::Local<v8::Function> tl_fn = state->touchlist_tpl.Get(isolate)
                                              ->GetFunction(context)
                                              .ToLocalChecked();
          (void)context
              ->Global()
              ->DefineOwnProperty(context, ToV8Str(isolate, "TouchList"),
                                  tl_fn,
                                  static_cast<v8::PropertyAttribute>(
                                      v8::PropertyAttribute::DontEnum))
              .FromMaybe(false);
        }
      }
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, TouchEventCtor);
        ctor->SetClassName(ToV8Str(isolate, "TouchEvent"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "TouchEvent"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        ctor->Inherit(ui);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "TouchEvent"),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
    }

    void AddGlobalFunction(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           const char* name, v8::FunctionCallback callback,
                           bool enumerable)
    {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> tpl =
          v8::FunctionTemplate::New(isolate, callback);
      tpl->SetClassName(key);
      v8::Local<v8::Function> fn = tpl->GetFunction(context).ToLocalChecked();
      v8::PropertyAttribute attrs = static_cast<v8::PropertyAttribute>(
          (enumerable ? 0 : v8::PropertyAttribute::DontEnum));
      (void)context->Global()->DefineOwnProperty(context, key, fn, attrs)
          .FromMaybe(false);
    }
    } // namespace

  void DispatchEvent(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     const std::string& class_name,
                     const std::vector<std::pair<std::string, v8::Local<v8::Value>>>& props,
                     bool document_target_first)
  {
    v8::Local<v8::Object> props_obj = v8::Object::New(isolate);
    for (const auto& prop : props)
    {
      (void)props_obj
          ->Set(context, ToV8Str(isolate, prop.first), prop.second)
          .FromMaybe(false);
    }

    v8::Local<v8::Value> event =
        BuildEvent(isolate, context, class_name.c_str(), props_obj);

    ContextState* state = GetState(context);
    if (state == nullptr)
    {
      return;
    }

    v8::Local<v8::Value> type_value;
    std::string event_type;
    if (props_obj->Get(context, ToV8Str(isolate, "type")).ToLocal(&type_value))
      event_type = ToUtf8(isolate, type_value);

    // Document-anchored lifecycle events (DOMContentLoaded, readystatechange)
    // fire document listeners first: document.addEventListener registrations
    // land in the element-target table, which the global pass below never
    // consults.  Route through the document object's own dispatchEvent.
    if (document_target_first)
    {
      v8::Local<v8::Value> document_value;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "document"))
              .ToLocal(&document_value) &&
          document_value->IsObject())
      {
        v8::Local<v8::Value> dispatch_fn;
        if (document_value.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "dispatchEvent"))
                .ToLocal(&dispatch_fn) &&
            dispatch_fn->IsFunction())
        {
          v8::Local<v8::Value> event_arg = event;
          v8::TryCatch try_catch(isolate);
          (void)dispatch_fn.As<v8::Function>()
              ->Call(context, document_value, 1, &event_arg)
              .FromMaybe(v8::Local<v8::Value>());
        }
      }
    }

    for (auto& entry : state->listeners)
    {
      if (entry.first != event_type)
        continue;
      v8::Local<v8::Function> fn =
          v8::Local<v8::Function>::New(isolate, entry.second);
      // Listeners run with a fresh TryCatch so one failing handler does not
      // abort the remaining ones (browser semantics: uncaught, not fatal).
      v8::TryCatch try_catch(isolate);
      (void)fn->Call(context, context->Global(), 1, &event)
          .FromMaybe(v8::Local<v8::Value>());
    }
  }

  // -------------------------------------------------- G3.13 dispatch helpers

  void SetEventField(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> event, const char* name,
                     v8::Local<v8::Value> value)
  {
    v8::Local<v8::Value> data;
    if (!event->GetPrivate(context, DataKey(isolate)).ToLocal(&data) ||
        !data->IsObject())
      return;
    (void)data.As<v8::Object>()
        ->Set(context, ToV8Str(isolate, name), value)
        .FromMaybe(false);
  }

  bool EventStopRequested(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> event)
  {
    v8::Local<v8::Value> data;
    if (!event->GetPrivate(context, DataKey(isolate)).ToLocal(&data) ||
        !data->IsObject())
      return false;
    v8::Local<v8::Object> props = data.As<v8::Object>();
    v8::Local<v8::Value> flag;
    if (props->Get(context, ToV8Str(isolate, "stopPropagation"))
            .ToLocal(&flag) &&
        flag->BooleanValue(isolate))
      return true;
    if (props->Get(context, ToV8Str(isolate, "stopImmediatePropagation"))
            .ToLocal(&flag) &&
        flag->BooleanValue(isolate))
      return true;
    return false;
  }

  bool EventImmediateStopRequested(
      v8::Isolate* isolate, v8::Local<v8::Context> context,
      v8::Local<v8::Object> event)
  {
    v8::Local<v8::Value> data;
    if (!event->GetPrivate(context, DataKey(isolate)).ToLocal(&data) ||
        !data->IsObject())
      return false;
    v8::Local<v8::Value> flag;
    return data.As<v8::Object>()
        ->Get(context, ToV8Str(isolate, "stopImmediatePropagation"))
        .ToLocal(&flag) && flag->BooleanValue(isolate);
  }

  std::string EventTypeOf(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> event)
  {
    v8::Local<v8::Value> data;
    if (!event->GetPrivate(context, DataKey(isolate)).ToLocal(&data) ||
        !data->IsObject())
      return std::string();
    v8::Local<v8::Value> type;
    if (!data.As<v8::Object>()
                 ->Get(context, ToV8Str(isolate, "type"))
                 .ToLocal(&type) ||
        !type->IsString())
      return std::string();
    return ToUtf8(isolate, type);
  }

  bool EventBubbles(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Object> event)
  {
    v8::Local<v8::Value> data;
    if (!event->GetPrivate(context, DataKey(isolate)).ToLocal(&data) ||
        !data->IsObject())
      return false;
    v8::Local<v8::Value> bubbles;
    if (!data.As<v8::Object>()
                    ->Get(context, ToV8Str(isolate, "bubbles"))
                    .ToLocal(&bubbles))
      return false;
    return bubbles->BooleanValue(isolate);
  }

  void InstallEventTarget(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    // Listener registration lives on the global object's prototype, matching
    // Chrome where addEventListener comes from Window.prototype.
    v8::Local<v8::Object> global = context->Global();
    v8::Local<v8::Value> proto_value = global->GetPrototype();

    struct Entry
    {
      const char* name;
      v8::FunctionCallback callback;
    };
    const Entry methods[] = {
        {"addEventListener", AddEventListener},
        {"removeEventListener", RemoveEventListener},
        {"dispatchEvent", DispatchEventJs},
    };

    if (proto_value->IsObject())
    {
      v8::Local<v8::Object> proto = proto_value.As<v8::Object>();
      for (const Entry& method : methods)
      {
        v8::Local<v8::String> key = ToV8Str(isolate, method.name);
        v8::Local<v8::FunctionTemplate> tpl =
            v8::FunctionTemplate::New(isolate, method.callback);
        tpl->SetClassName(key);
        (void)proto->DefineOwnProperty(
                   context, key, tpl->GetFunction(context).ToLocalChecked(),
                   static_cast<v8::PropertyAttribute>(
                       v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
    }

    // Event constructors are own (non-enumerable) global properties.
    // The Event template is retained for the G4 family Inherit() chains.
    {
      ContextState* state = GetState(context);
      v8::Local<v8::String> key = ToV8Str(isolate, "Event");
      v8::Local<v8::FunctionTemplate> tpl =
          v8::FunctionTemplate::New(isolate, EventConstructor);
      tpl->SetClassName(key);
      v8::Local<v8::FunctionTemplate> instance_holder =
          v8::FunctionTemplate::New(isolate);
      (void)instance_holder;
      if (state != nullptr)
      {
        state->event_ctor_tpl.Reset(isolate, tpl);
        // register THIS template's function: family instances must satisfy
        // `x instanceof Event` (AddGlobalFunction would create a twin)
        v8::PropertyAttribute attrs = static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, key,
                                tpl->GetFunction(context).ToLocalChecked(),
                                attrs)
            .FromMaybe(false);
      }
      else
      {
        AddGlobalFunction(isolate, context, "Event", EventConstructor, false);
      }
    }
    AddGlobalFunction(isolate, context, "CustomEvent", CustomEventConstructor,
                      false);
    AddGlobalFunction(isolate, context, "PopStateEvent", PopStateEventConstructor,
                      false);
    AddGlobalFunction(isolate, context, "HashChangeEvent",
                      HashChangeEventConstructor, false);
    AddGlobalFunction(isolate, context, "MessageEvent", MessageEventConstructor,
                      false);
    AddGlobalFunction(isolate, context, "ErrorEvent", ErrorEventConstructor,
                      false);
    AddGlobalFunction(isolate, context, "CloseEvent", CloseEventConstructor,
                      false);
    // G4 ledger chunk 3: rich event family (needs the Event template)
    InstallEventFamily(isolate, context, GetState(context));
  }

  void DocumentAddEventListener(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    AddEventListener(info);
  }

  void DocumentRemoveEventListener(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    RemoveEventListener(info);
  }

  void DocumentDispatchEvent(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    DispatchEventJs(info);
  }

  void WindowEventAdd(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    AddEventListener(info);
  }

  void WindowEventRemove(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    RemoveEventListener(info);
  }

  void WindowEventDispatch(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    DispatchEventJs(info);
  }
} // namespace LV8
