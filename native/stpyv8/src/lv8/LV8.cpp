// lv8 G1 core: per-context state registry, profile-driven host objects and
// the console surface. Navigation (location/history) lives in LV8Location.cpp,
// the global event target in LV8Events.cpp, timers in LV8Timers.cpp.

#include "LV8.h"

#include <unicode/simpletz.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <v8-profiler.h>

#include <unicode/timezone.h>
#include <unicode/unistr.h>
#include <unicode/locid.h>

#include <cctype>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace LV8
  {
  namespace
    {
    const char* const kProfileSymbol = "lv8:profile";

    v8::Local<v8::Private> ProfileKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, kProfileSymbol));
    }

    std::string PascalClassName(const std::string& key)
    {
      if (key.empty()) return "Object";
      std::string out = key;
      out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
      return out;
    }

    // Real browser interface spelling for profile keys; nullptr falls back
    // to PascalClassName. Used for top-level installs and nested objects.
    const char* BrowserClassName(const std::string& key)
    {
      if (key == "navigator") return "Navigator";
      if (key == "screen") return "Screen";
      if (key == "performance") return "Performance";
      if (key == "crypto") return "Crypto";
      if (key == "userAgentData") return "NavigatorUAData";
      if (key == "plugins") return "PluginArray";
      if (key == "mimeTypes") return "MimeTypeArray";
      if (key == "orientation") return "ScreenOrientation";
      if (key == "connection") return "NetworkInformation";
      if (key == "history") return "History";
      if (key == "storage") return "StorageManager";
      return nullptr;
    }

    // ---------------------------------------------------------- state registry

    // State is attached to the context global via a Private symbol holding a
    // v8::External. Private lookups are handle-based, so identity survives
    // GC moves (a raw v8::Context* map key would not: compacting GC moves
    // V8 heap objects). A weak persistent of the External frees the state
    // when the context is collected.
    v8::Local<v8::Private> StateKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8:context-state"));
    }

    ContextState* GetStateImpl(v8::Local<v8::Context> context)
    {
      v8::Isolate* isolate = context->GetIsolate();
      v8::Local<v8::Private> key = StateKey(isolate);
      v8::Local<v8::Value> existing;
      bool got = context->Global()->GetPrivate(context, key).ToLocal(&existing);
      if (!got || !existing->IsExternal())
        return nullptr;
      return static_cast<ContextState*>(
          existing.As<v8::External>()->Value());
    }
    } // namespace

  ContextState* GetState(v8::Local<v8::Context> context)
  {
    return GetStateImpl(context);
  }

  // Owns the state plus the weak handle that triggers its cleanup.
  struct StateGuard
  {
    ContextState* state = nullptr;
    v8::Global<v8::External> handle;
    ~StateGuard()
    {
      handle.Reset();
      delete state;
    }
  };

  ContextState* EnsureState(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    v8::Local<v8::Private> key = StateKey(isolate);
    v8::Local<v8::Value> existing;
    if (context->Global()->GetPrivate(context, key).ToLocal(&existing) &&
        existing->IsExternal())
      return static_cast<ContextState*>(existing.As<v8::External>()->Value());

    auto* state = new ContextState();
    v8::Local<v8::External> ext = v8::External::New(isolate, state);
    if (!context->Global()->SetPrivate(context, key, ext).FromMaybe(false))
    {
      delete state;
      throw std::runtime_error("lv8: failed to attach context state");
    }

    // Weak persistent does NOT keep the External (or its context) alive;
    // when the context is collected the callback destroys the guard, which
    // in turn frees the state.
    auto* guard = new StateGuard();
    guard->state = state;
    guard->handle.Reset(isolate, ext);
    guard->handle.SetWeak(
        guard,
        [](const v8::WeakCallbackInfo<StateGuard>& info)
        {
          delete info.GetParameter();
        },
        v8::WeakCallbackType::kParameter);
    return state;
  }

  namespace
    {
    v8::Local<v8::Private> PathKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8:path"));
    }

    // Instance path inside the profile tree ("navigator", "location",
    // "navigator.userAgentData", ...). Empty for path-less objects.
    std::string ObjectPath(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Object> self)
    {
      v8::Local<v8::Value> path;
      if (!self->GetPrivate(context, PathKey(isolate)).ToLocal(&path) ||
          !path->IsString())
        return "";
      return ToUtf8(isolate, path);
    }

    void MonitorRecord(v8::Local<v8::Context> context, const std::string& path,
                       const char* operation, const std::string& value = "")
    {
      if (path.empty())
        return;
      ContextState* state = GetState(context);
      if (state == nullptr || !state->monitor_enabled ||
          state->monitor_suspended)
        return;
      state->monitor_entries.emplace_back(path, operation);
      state->monitor_values.push_back(value);
    }

    // compact value snapshot for the fingerprint comparison (strings up to
    // 120 chars kept verbatim; scalars printed; objects collapsed)
    std::string MonitorValueString(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Value> value)
    {
      if (value.IsEmpty())
        return "";
      if (value->IsUndefined())
        return "undefined";
      if (value->IsNull())
        return "null";
      if (value->IsBoolean())
        return value->BooleanValue(isolate) ? "true" : "false";
      if (value->IsNumber())
      {
        double number = value->NumberValue(context).FromMaybe(0);
        if (number == static_cast<long long>(number))
          return std::to_string(static_cast<long long>(number));
        std::ostringstream text;
        text << number;
        return text.str();
      }
      if (value->IsString())
      {
        std::string text = ToUtf8(isolate, value);
        return text.size() > 120 ? text.substr(0, 120) : text;
      }
      return "<object>";
    }

    // An ignore path hides a node and its whole subtree.
    bool PathIgnored(const ContextState* state, const std::string& path)
    {
      for (const std::string& ignore : state->ignore_paths)
      {
        if (ignore.empty())
          continue;
        if (ignore == path || path.rfind(ignore + ".", 0) == 0 ||
            ignore.rfind(path + ".", 0) == 0)
          return true;
      }
      return false;
    }

    // Forward decl: array-backed host object (PluginArray/MimeTypeArray);
    // defined after ProfileMethodCaller.
    v8::Local<v8::Object> ArrayHostFromProfile(v8::Isolate* isolate,
                                               v8::Local<v8::Context> context,
                                               const char* class_name,
                                               v8::Local<v8::Array> arr,
                                               const std::string& path);

    // (MonitorCall is declared in LV8.h; defined outside this namespace.)

    // Native accessor: reads the property of the same name from the profile
    // object attached (privately) to the host instance. Event objects reuse
    // the same mechanism for type/state/oldURL/... accessors.
    void ProfilePropertyGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> self = info.This();

      v8::Local<v8::Private> key = ProfileKey(isolate);
      v8::Local<v8::Value> data;
      if (!self->GetPrivate(context, key).ToLocal(&data) || !data->IsObject())
      {
        info.GetReturnValue().SetUndefined();
        return;
      }

      v8::Local<v8::String> prop = info.Data().As<v8::String>();
      std::string obj_path = ObjectPath(isolate, context, self);
      std::string prop_name = ToUtf8(isolate, prop);
      std::string full_path =
          obj_path.empty() ? prop_name
                           : obj_path + "." + prop_name;

      v8::Local<v8::Value> value;
      if (!data.As<v8::Object>()->Get(context, prop).ToLocal(&value))
      {
        MonitorRecord(context, full_path, "get", "undefined");
        info.GetReturnValue().SetUndefined();
        return;
      }
      MonitorRecord(context, full_path, "get",
                    MonitorValueString(isolate, context, value));
      if (value->IsObject() && !value->IsArray() && !value->IsFunction())
      {
        // Already a host object (cached from a previous access) - return
        // as-is so object identity holds across reads.
        if (value.As<v8::Object>()
                ->HasPrivate(context, ProfileKey(isolate))
                .FromMaybe(false))
        {
          info.GetReturnValue().Set(value);
          return;
        }
        // Nested profile objects are surfaced as native host objects too;
        // the wrapped instance is cached back so identity keeps holding.
        v8::Local<v8::Object> child_data = value.As<v8::Object>();
        const char* known = BrowserClassName(prop_name);
        std::string child_class = known ? known : PascalClassName(prop_name);
        v8::Local<v8::Object> child = HostObjectFromProfile(
            isolate, context, child_class.c_str(), child_data, full_path);
        (void)data.As<v8::Object>()->Set(context, prop, child).FromMaybe(false);
        info.GetReturnValue().Set(child);
        return;
      }
      if (value->IsArray())
      {
        // Plugin/MimeType collections surface as browser-like array host
        // objects (toStringTag, length, item) instead of raw JS arrays.
        const char* known = BrowserClassName(prop_name);
        if (known && (prop_name == "plugins" || prop_name == "mimeTypes"))
        {
          v8::Local<v8::Object> host = ArrayHostFromProfile(
              isolate, context, known, value.As<v8::Array>(), full_path);
          (void)data.As<v8::Object>()->Set(context, prop, host).FromMaybe(false);
          info.GetReturnValue().Set(host);
          return;
        }
      }
      info.GetReturnValue().Set(value);
    }

    // Native method declared through "$methods": the fixed return value is
    // stored under the method name inside the "$methods" sub-object of the
    // instance's private profile data.
    void ProfileMethodCaller(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();

      std::string obj_path = ObjectPath(isolate, context, info.This());
      MonitorRecord(context,
                    obj_path.empty()
                        ? ToUtf8(isolate, info.Data().As<v8::String>())
                        : obj_path + "." +
                              ToUtf8(isolate, info.Data().As<v8::String>()),
                    "call");

      v8::Local<v8::Private> key = ProfileKey(isolate);
      v8::Local<v8::Value> data;
      if (!info.This()->GetPrivate(context, key).ToLocal(&data) ||
          !data->IsObject())
      {
        info.GetReturnValue().SetUndefined();
        return;
      }

      v8::Local<v8::Value> methods;
      if (!data.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "$methods"))
               .ToLocal(&methods) ||
          !methods->IsObject())
      {
        info.GetReturnValue().SetUndefined();
        return;
      }

      v8::Local<v8::Value> value;
      if (!methods.As<v8::Object>()
               ->Get(context, info.Data().As<v8::String>())
               .ToLocal(&value))
      {
        info.GetReturnValue().SetUndefined();
        return;
      }
      info.GetReturnValue().Set(value);
    }

    // Array-backed collection object (navigator.plugins / mimeTypes):
    // Symbol.toStringTag + native length accessor + item(i) method + own
    // index data properties. Backed by the profile array via a private.
    void ArrayLengthGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data;
      if (!info.This()
               ->GetPrivate(context, ProfileKey(isolate))
               .ToLocal(&data) ||
          !data->IsArray())
      {
        info.GetReturnValue().Set(0);
        return;
      }
      info.GetReturnValue().Set(
          static_cast<int32_t>(data.As<v8::Array>()->Length()));
    }

    void ArrayItemMethod(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data;
      if (!info.This()
               ->GetPrivate(context, ProfileKey(isolate))
               .ToLocal(&data) ||
          !data->IsArray() || info.Length() < 1 || !info[0]->IsNumber())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      uint32_t i = info[0]
                       ->Uint32Value(context)
                       .FromMaybe(0);
      v8::Local<v8::Array> arr = data.As<v8::Array>();
      if (i >= arr->Length())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      v8::Local<v8::Value> item =
          arr->Get(context, i).FromMaybe(v8::Local<v8::Value>(v8::Null(isolate)));
      info.GetReturnValue().Set(item);
    }

    // namedItem() over the private array; |field| selects the item key
    // ("name" for PluginArray, "type" for MimeTypeArray).
    void ArrayNamedItemMethod(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data;
      std::string field = ToUtf8(isolate, info.Data().As<v8::String>());
      if (!info.This()->GetPrivate(context, ProfileKey(isolate)).ToLocal(&data) ||
          !data->IsArray() || info.Length() < 1)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      std::string wanted = ToUtf8(isolate, info[0]);
      v8::Local<v8::Array> arr = data.As<v8::Array>();
      for (uint32_t i = 0; i < arr->Length(); i++)
      {
        v8::Local<v8::Value> item = arr->Get(context, i).FromMaybe(
            v8::Local<v8::Value>(v8::Undefined(isolate)));
        if (!item->IsObject())
          continue;
        v8::Local<v8::Value> candidate;
        if (item.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, field))
                .ToLocal(&candidate) &&
            candidate->IsString() && ToUtf8(isolate, candidate) == wanted)
        {
          info.GetReturnValue().Set(item);
          return;
        }
      }
      info.GetReturnValue().SetNull();
    }

    v8::Local<v8::Object> ArrayHostFromProfile(v8::Isolate* isolate,
                                               v8::Local<v8::Context> context,
                                               const char* class_name,
                                               v8::Local<v8::Array> arr,
                                               const std::string& path)
    {
      v8::Local<v8::String> name = ToV8Str(isolate, class_name);
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(name);
      v8::Local<v8::ObjectTemplate> instance_tmpl = tpl->InstanceTemplate();
      instance_tmpl->SetInternalFieldCount(0);
      instance_tmpl->Set(v8::Symbol::GetToStringTag(isolate), name,
                         v8::PropertyAttribute::ReadOnly);

      v8::Local<v8::String> length_key = ToV8Str(isolate, "length");
      v8::Local<v8::FunctionTemplate> length_fn =
          v8::FunctionTemplate::New(isolate, ArrayLengthGetter);
      length_fn->SetClassName(ToV8Str(isolate, "get length"));
      instance_tmpl->SetAccessorProperty(
          length_key, length_fn, v8::Local<v8::FunctionTemplate>());

      v8::Local<v8::String> item_key = ToV8Str(isolate, "item");
      v8::Local<v8::FunctionTemplate> item_fn =
          v8::FunctionTemplate::New(isolate, ArrayItemMethod);
      item_fn->SetClassName(item_key);
      tpl->PrototypeTemplate()->Set(item_key, item_fn);

      std::string name_field =
          (std::string(class_name) == "MimeTypeArray") ? "type" : "name";
      v8::Local<v8::String> named_key = ToV8Str(isolate, "namedItem");
      v8::Local<v8::FunctionTemplate> named_fn = v8::FunctionTemplate::New(
          isolate, ArrayNamedItemMethod, ToV8Str(isolate, name_field));
      named_fn->SetClassName(named_key);
      tpl->PrototypeTemplate()->Set(named_key, named_fn);

      if (!context->Global()
               ->DefineOwnProperty(context, name,
                                   tpl->GetFunction(context).ToLocalChecked(),
                                   static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
               .FromMaybe(false))
      {
        // non-fatal
      }

      v8::Local<v8::Object> instance =
          instance_tmpl->NewInstance(context).ToLocalChecked();
      (void)instance->SetPrivate(context, ProfileKey(isolate), arr)
          .FromMaybe(false);
      if (!path.empty())
        (void)instance->SetPrivate(context, PathKey(isolate),
                                   ToV8Str(isolate, path))
            .FromMaybe(false);
      for (uint32_t i = 0; i < arr->Length(); i++)
      {
        (void)instance
            ->Set(context, i,
                  arr->Get(context, i)
                      .FromMaybe(v8::Local<v8::Value>(v8::Undefined(isolate))))
            .FromMaybe(false);
      }
      // named access: plugins["PDF Viewer"], mimeTypes["application/pdf"]
      for (uint32_t i = 0; i < arr->Length(); i++)
      {
        v8::Local<v8::Value> item = arr->Get(context, i).FromMaybe(
            v8::Local<v8::Value>(v8::Undefined(isolate)));
        if (!item->IsObject())
          continue;
        v8::Local<v8::Value> named;
        if (item.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, name_field))
                .ToLocal(&named) &&
            named->IsString())
        {
          (void)instance->Set(context, named, item).FromMaybe(false);
        }
      }
      return instance;
    }

    // Promise resolved with |value|; .then continuations run at the next
    // microtask checkpoint (facade eval/advance/drain call one).
    v8::Local<v8::Value> ResolvedPromise(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context,
                                         v8::Local<v8::Value> value)
    {
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      (void)resolver->Resolve(context, value).FromMaybe(false);
      return resolver->GetPromise();
    }

    v8::Local<v8::Value> RejectedPromise(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context,
                                         v8::Local<v8::Value> reason)
    {
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      (void)resolver->Reject(context, reason).FromMaybe(false);
      return resolver->GetPromise();
    }

    // Browser class spelling for async-resolved payloads.
    const char* AsyncPayloadClassName(const std::string& method)
    {
      if (method == "getBattery") return "BatteryManager";
      return nullptr;
    }

    // Generic "$async" method: name -> payload data object held under the
    // navigator profile's private "$async" sub-object. The payload becomes
    // a native host object passed to the resolver.
    void AsyncMethodCaller(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string obj_path = ObjectPath(isolate, context, info.This());
      MonitorRecord(context,
                    obj_path.empty()
                        ? ToUtf8(isolate, info.Data().As<v8::String>())
                        : obj_path + "." +
                              ToUtf8(isolate, info.Data().As<v8::String>()),
                    "call");
      v8::Local<v8::Value> data;
      if (!info.This()->GetPrivate(context, ProfileKey(isolate)).ToLocal(&data) ||
          !data->IsObject())
      {
        info.GetReturnValue().Set(
            RejectedPromise(isolate, context, v8::Null(isolate)));
        return;
      }
      v8::Local<v8::Value> async_map;
      if (!data.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "$async"))
               .ToLocal(&async_map) ||
          !async_map->IsObject())
      {
        info.GetReturnValue().Set(
            RejectedPromise(isolate, context, v8::Null(isolate)));
        return;
      }
      v8::Local<v8::String> method = info.Data().As<v8::String>();
      v8::Local<v8::Value> payload;
      if (!async_map.As<v8::Object>()
               ->Get(context, method)
               .ToLocal(&payload) ||
          !payload->IsObject())
      {
        info.GetReturnValue().Set(
            RejectedPromise(isolate, context, v8::Null(isolate)));
        return;
      }
      std::string method_name = ToUtf8(isolate, method);
      const char* known = AsyncPayloadClassName(method_name);
      std::string class_name = known ? known : PascalClassName(method_name);
      v8::Local<v8::Object> wrapped = HostObjectFromProfile(
          isolate, context, class_name.c_str(), payload.As<v8::Object>(),
          method_name);
      info.GetReturnValue().Set(ResolvedPromise(isolate, context, wrapped));
    }

    // navigator.userAgentData.getHighEntropyValues(hints) -> Promise<object>
    void GetHighEntropyValues(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      MonitorRecord(context,
                    ObjectPath(isolate, context, info.This()) +
                        ".getHighEntropyValues",
                    "call");
      v8::Local<v8::Value> data;
      if (!info.This()->GetPrivate(context, ProfileKey(isolate)).ToLocal(&data) ||
          !data->IsObject() || info.Length() < 1 || !info[0]->IsArray())
      {
        info.GetReturnValue().Set(
            RejectedPromise(isolate, context, v8::Null(isolate)));
        return;
      }
      v8::Local<v8::Object> ua_data = data.As<v8::Object>();
      v8::Local<v8::Array> hints = info[0].As<v8::Array>();
      v8::Local<v8::Object> out = v8::Object::New(isolate);
      for (uint32_t i = 0; i < hints->Length(); i++)
      {
        v8::Local<v8::Value> hint = hints->Get(context, i).FromMaybe(
            v8::Local<v8::Value>(v8::Undefined(isolate)));
        if (!hint->IsString())
          continue;
        v8::Local<v8::Value> value;
        if (ua_data->Get(context, hint).ToLocal(&value) &&
            !value->IsUndefined())
          (void)out->Set(context, hint, value).FromMaybe(false);
      }
      info.GetReturnValue().Set(ResolvedPromise(isolate, context, out));
    }

    // navigator.permissions.query({name}) -> Promise<PermissionStatus>
    void PermissionsQuery(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string obj_path = ObjectPath(isolate, context, info.This());
      MonitorRecord(context,
                    obj_path.empty() ? "query" : obj_path + ".query",
                    "call");
      v8::Local<v8::Value> data;
      if (!info.This()->GetPrivate(context, ProfileKey(isolate)).ToLocal(&data) ||
          !data->IsObject() || info.Length() < 1 || !info[0]->IsObject())
      {
        info.GetReturnValue().Set(
            RejectedPromise(isolate, context, v8::Null(isolate)));
        return;
      }
      v8::Local<v8::Value> name_value;
      if (!info[0].As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "name"))
               .ToLocal(&name_value) ||
          !name_value->IsString())
      {
        info.GetReturnValue().Set(
            RejectedPromise(isolate, context, v8::Null(isolate)));
        return;
      }
      v8::Local<v8::Value> state;
      if (!data.As<v8::Object>()
               ->Get(context, name_value)
               .ToLocal(&state) ||
          state->IsUndefined())
      {
        // reference state-map shape: permissions {state: {name: granted}}
        state = ToV8Str(isolate, "prompt");
        v8::Local<v8::Value> state_map;
        if (data.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "state"))
                .ToLocal(&state_map) &&
            state_map->IsObject())
        {
          v8::Local<v8::Value> mapped;
          if (state_map.As<v8::Object>()
                  ->Get(context, name_value)
                  .ToLocal(&mapped) &&
              !mapped->IsUndefined())
            state = mapped;
        }
      }
      else if (state->IsObject())
      {
        // reference object shape: permissions {name: {state: granted}}
        v8::Local<v8::Value> inner;
        if (state.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "state"))
                .ToLocal(&inner) &&
            !inner->IsUndefined())
          state = inner;
      }
      v8::Local<v8::Object> status_data = v8::Object::New(isolate);
      (void)status_data->Set(context, ToV8Str(isolate, "name"), name_value)
          .FromMaybe(false);
      (void)status_data->Set(context, ToV8Str(isolate, "state"), state)
          .FromMaybe(false);
      v8::Local<v8::Object> status = HostObjectFromProfile(
          isolate, context, "PermissionStatus", status_data,
          obj_path + ".query");
      info.GetReturnValue().Set(ResolvedPromise(isolate, context, status));
    }

    int OrientationAngleFor(const std::string& type)
    {
      if (type == "landscape-primary") return 90;
      if (type == "landscape-secondary") return 270;
      if (type == "portrait-secondary") return 180;
      return 0;  // portrait-primary
    }

    // screen.orientation.lock(type) -> Promise<void>; updates the private
    // orientation data synchronously so later getters observe the change.
    void OrientationLock(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data;
      MonitorRecord(context,
                    ObjectPath(isolate, context, info.This()) + ".lock",
                    "call");
      if (!info.This()->GetPrivate(context, ProfileKey(isolate)).ToLocal(&data) ||
          !data->IsObject() || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().Set(
            RejectedPromise(isolate, context, v8::Null(isolate)));
        return;
      }
      std::string type = ToUtf8(isolate, info[0]);
      if (type != "portrait-primary" && type != "portrait-secondary" &&
          type != "landscape-primary" && type != "landscape-secondary")
      {
        info.GetReturnValue().Set(RejectedPromise(
            isolate, context,
            ToV8Str(isolate,
                    "TypeError: unknown orientation type: " + type)));
        return;
      }
      v8::Local<v8::Object> orientation = data.As<v8::Object>();
      (void)orientation
          ->Set(context, ToV8Str(isolate, "type"), info[0])
          .FromMaybe(false);
      (void)orientation
          ->Set(context, ToV8Str(isolate, "angle"),
                v8::Integer::New(isolate, OrientationAngleFor(type)))
          .FromMaybe(false);
      // prod parity: primitives ride as own data properties on the host
      // instance, so mirror the mutation there as well
      (void)info.This()
          ->Set(context, ToV8Str(isolate, "type"), info[0])
          .FromMaybe(false);
      (void)info.This()
          ->Set(context, ToV8Str(isolate, "angle"),
                v8::Integer::New(isolate, OrientationAngleFor(type)))
          .FromMaybe(false);
      info.GetReturnValue().Set(
          ResolvedPromise(isolate, context, v8::Undefined(isolate)));
    }

    void OrientationUnlock(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      // offline locking: unlock keeps the current orientation
    }

    // Attach the per-interface async surface onto the prototype template.
    void AttachAsyncMethods(v8::Isolate* isolate,
                            const std::string& class_name,
                            v8::Local<v8::Object> data,
                            v8::Local<v8::ObjectTemplate> proto_tmpl)
    {
      auto add = [&](const char* name, v8::FunctionCallback callback) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, callback);
        fn->SetClassName(key);
        proto_tmpl->Set(key, fn);
      };

      if (class_name == "Navigator")
      {
        // "$async" payloads drive the generic promise methods
        v8::Local<v8::Value> async_map;
        if (data->Get(isolate->GetCurrentContext(),
                      ToV8Str(isolate, "$async"))
                .ToLocal(&async_map) &&
            async_map->IsObject())
        {
          v8::Local<v8::Context> context = isolate->GetCurrentContext();
          v8::Local<v8::Array> names = async_map.As<v8::Object>()
                                           ->GetOwnPropertyNames(context)
                                           .ToLocalChecked();
          for (uint32_t i = 0; i < names->Length(); i++)
          {
            v8::Local<v8::Value> name_value = names->Get(context, i)
                                                  .FromMaybe(v8::Local<v8::Value>(
                                                      v8::Undefined(isolate)));
            if (!name_value->IsString())
              continue;
            v8::Local<v8::String> method = name_value.As<v8::String>();
            v8::Local<v8::FunctionTemplate> fn =
                v8::FunctionTemplate::New(isolate, AsyncMethodCaller, method);
            fn->SetClassName(method);
            proto_tmpl->Set(method, fn);
          }
        }
      }
      else if (class_name == "NavigatorUAData")
      {
        add("getHighEntropyValues", GetHighEntropyValues);
      }
      else if (class_name == "Permissions")
      {
        add("query", PermissionsQuery);
      }
      else if (class_name == "ScreenOrientation")
      {
        add("lock", OrientationLock);
        add("unlock", OrientationUnlock);
      }
    }
    } // namespace

  void MonitorCall(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> self, const char* method)
  {
    std::string obj_path = ObjectPath(isolate, context, self);
    MonitorRecord(context, obj_path.empty() ? std::string(method)
                                            : obj_path + "." + method,
                  "call");
  }

  void MonitorRead(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   const std::string& path, v8::Local<v8::Value> value)
  {
    MonitorRecord(context, path, "get",
                  MonitorValueString(isolate, context, value));
  }

  // Mirror each own key of |data| as a native accessor property on a fresh
  // instance of a FunctionTemplate named |class_name|. Getters are real
  // FunctionTemplate functions: toString() renders
  // "function get <key>() { [native code] }" like real browsers. Optional
  // "$methods" entries (name -> fixed return value) become prototype methods
  // with "function <name>() { [native code] }" appearance, so they do not
  // show up in Object.keys(instance) - matching real browsers, where
  // navigator.javaEnabled lives on Navigator.prototype.
  v8::Local<v8::Object> HostObjectFromProfile(v8::Isolate* isolate,
                                              v8::Local<v8::Context> context,
                                              const char* class_name,
                                              v8::Local<v8::Object> data,
                                              const std::string& path)
  {
    v8::Local<v8::String> name = ToV8Str(isolate, class_name);

    v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
    tpl->SetClassName(name);
    v8::Local<v8::ObjectTemplate> instance_tmpl = tpl->InstanceTemplate();
    instance_tmpl->SetInternalFieldCount(0);

    // "$methods"/"$async" are consumed here, never mirrored as data props.
    v8::Local<v8::String> methods_key = ToV8Str(isolate, "$methods");
    v8::Local<v8::Value> methods;
    bool has_methods =
        data->Get(context, methods_key).ToLocal(&methods) && methods->IsObject();
    v8::Local<v8::String> async_key = ToV8Str(isolate, "$async");
    v8::Local<v8::Value> async_map;
    bool has_async =
        data->Get(context, async_key).ToLocal(&async_map) &&
        async_map->IsObject();

    v8::Local<v8::Private> key = ProfileKey(isolate);

    // primitive/array values collected here, installed as own data
    // properties on the instance after instantiation (reference parity)
    std::vector<
        std::pair<v8::Local<v8::String>, v8::Local<v8::Value>>>
        data_props;

    v8::Local<v8::Array> keys =
        data->GetOwnPropertyNames(context).ToLocalChecked();
    for (uint32_t i = 0; i < keys->Length(); i++)
    {
      v8::Local<v8::Value> key_value = keys->Get(context, i).ToLocalChecked();
      v8::Local<v8::String> key_str =
          key_value->ToString(context).ToLocalChecked();
      if (has_methods && key_str->Equals(context, methods_key).FromMaybe(false))
        continue;
      if (has_async && key_str->Equals(context, async_key).FromMaybe(false))
        continue;

      std::string full_path =
          path.empty() ? ToUtf8(isolate, key_str)
                       : path + "." + ToUtf8(isolate, key_str);
      // ignore_apis hides the node (and subtree) at install time: no
      // accessor, no Object.keys entry, no monitor record.
      ContextState* state = GetState(context);
      if (state != nullptr && PathIgnored(state, full_path))
        continue;

      // Reference parity (prod): primitive profile values surface as own
      // DATA properties (getOwnPropertyDescriptor(...).get is undefined).
      // Debug mode keeps accessors so reads feed the monitor; arrays keep
      // the accessor so collection reads wrap in their host object;
      // objects keep it so internal "$" keys stay hidden.
      v8::Local<v8::Value> probe;
      const bool monitor_mode =
          state != nullptr && state->monitor_enabled;
      if (data->Get(context, key_str).ToLocal(&probe) && !monitor_mode &&
          (probe->IsString() || probe->IsNumber() || probe->IsBoolean() ||
           probe->IsNull() || probe->IsUndefined()))
      {
        data_props.emplace_back(key_str, probe);
        continue;
      }

      std::string getter_name = std::string("get ") + ToUtf8(isolate, key_str);
      v8::Local<v8::FunctionTemplate> getter =
          v8::FunctionTemplate::New(isolate, ProfilePropertyGetter, key_str);
      getter->SetClassName(ToV8Str(isolate, getter_name));
      instance_tmpl->SetAccessorProperty(key_str, getter,
                                         v8::Local<v8::FunctionTemplate>());
    }


    v8::Local<v8::ObjectTemplate> proto_tmpl = tpl->PrototypeTemplate();
    if (has_methods)
    {
      v8::Local<v8::Object> methods_obj = methods.As<v8::Object>();
      v8::Local<v8::Array> method_names =
          methods_obj->GetOwnPropertyNames(context).ToLocalChecked();
      for (uint32_t i = 0; i < method_names->Length(); i++)
      {
        v8::Local<v8::String> method_name =
            method_names->Get(context, i).ToLocalChecked()
                ->ToString(context).ToLocalChecked();
        v8::Local<v8::FunctionTemplate> method =
            v8::FunctionTemplate::New(isolate, ProfileMethodCaller,
                                      method_name);
        method->SetClassName(method_name);
        proto_tmpl->Set(method_name, method);
      }
    }

    AttachAsyncMethods(isolate, std::string(class_name), data, proto_tmpl);

    instance_tmpl->Set(v8::Symbol::GetToStringTag(isolate), name,
                       v8::PropertyAttribute::ReadOnly);

    if (!context->Global()
               ->DefineOwnProperty(context, name,
                                   tpl->GetFunction(context).ToLocalChecked(),
                                   static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
               .FromMaybe(false))
    {
      // Constructor exposure failure is non-fatal for older engines.
    }

    v8::Local<v8::Object> instance =
        instance_tmpl->NewInstance(context).ToLocalChecked();
    if (!instance->SetPrivate(context, key, data).FromMaybe(false))
      throw std::runtime_error("lv8: failed to attach lv8 profile");
    if (!path.empty())
      (void)instance->SetPrivate(context, PathKey(isolate),
                                 ToV8Str(isolate, path))
          .FromMaybe(false);
    for (auto& prop : data_props)
      (void)instance->Set(context, prop.first, prop.second).FromMaybe(false);

    return instance;
  }

  void SetTimeZoneOverride(v8::Isolate* isolate, const std::string& iana_name)
  {
    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString canonical;
    icu::TimeZone::getCanonicalID(
        icu::UnicodeString::fromUTF8(iana_name), canonical, status);
    // custom offset ids ("123") are not canonical but the reference
    // accepts them, so fall back to the raw id and let ICU decide
    icu::UnicodeString zone_id =
        (U_FAILURE(status) || canonical.isEmpty())
            ? icu::UnicodeString::fromUTF8(iana_name)
            : canonical;
    if (zone_id.isEmpty())
      throw std::runtime_error("lv8: unknown IANA time zone: " + iana_name);

    icu::UnicodeString id;
    icu::TimeZone* zone = icu::TimeZone::createTimeZone(zone_id);
    zone->getID(id);
    if (id != zone_id)
    {
      delete zone;
      // digits-only ids are custom GMT-offset minutes ("123" -> +02:03);
      // the reference accepts them as configured zones
      const bool all_digits = !iana_name.empty() &&
          std::all_of(iana_name.begin(), iana_name.end(),
                      [](unsigned char c) { return std::isdigit(c) != 0; });
      if (all_digits)
      {
        zone = new icu::SimpleTimeZone(
            static_cast<int32_t>(std::atoi(iana_name.c_str())) * 60000,
            icu::UnicodeString::fromUTF8(iana_name));
        zone->getID(id);
      }
      if (id != zone_id)
      {
        delete zone;
        throw std::runtime_error("lv8: unknown IANA time zone: " +
                                 iana_name);
      }
    }
    icu::TimeZone::adoptDefault(zone);
    isolate->DateTimeConfigurationChangeNotification(
        v8::Isolate::TimeZoneDetection::kSkip);
  }

  void SetDefaultLocale(v8::Isolate* isolate, const std::string& locale_name)
  {
    UErrorCode status = U_ZERO_ERROR;
    icu::Locale locale = icu::Locale::createCanonical(locale_name.c_str());
    if (locale.isBogus() || locale_name.empty())
      throw std::runtime_error("lv8: unknown locale: " + locale_name);

    icu::Locale::setDefault(locale, status);
    if (U_FAILURE(status))
      throw std::runtime_error("lv8: cannot set locale: " + locale_name);

    // Invalidates the per-isolate cached default locale (public API); Intl
    // re-reads icu::Locale::getDefault() on next use.
    isolate->LocaleConfigurationChangeNotification();
  }

  // Host ICU defaults captured once, before the first context overrides
  // them. The ICU default time zone / locale are process-wide state that
  // Date and Intl read live, so contexts re-apply their own override when
  // entered (engine facade) and unconfigured contexts restore these.
  namespace
  {
    icu::TimeZone* g_host_time_zone = nullptr;
    icu::Locale g_host_locale = icu::Locale::getRoot();
    bool g_host_defaults_saved = false;
  }

  void SaveHostDefaults()
  {
    if (g_host_defaults_saved)
      return;
    g_host_defaults_saved = true;
    g_host_time_zone = icu::TimeZone::createDefault();
    g_host_locale = icu::Locale::getDefault();
  }

  void RestoreHostTimeZone(v8::Isolate* isolate)
  {
    SaveHostDefaults();
    icu::TimeZone::adoptDefault(g_host_time_zone->clone());
    isolate->DateTimeConfigurationChangeNotification(
        v8::Isolate::TimeZoneDetection::kSkip);
  }

  void RestoreHostLocale(v8::Isolate* isolate)
  {
    SaveHostDefaults();
    UErrorCode status = U_ZERO_ERROR;
    icu::Locale::setDefault(g_host_locale, status);
    if (U_FAILURE(status))
      return;
    isolate->LocaleConfigurationChangeNotification();
  }

  // --------------------------------------------------------------- console

  namespace
    {
    // Chrome-style formatting approximation: strings as-is, objects
    // JSON-ish, args joined by a single space. The detection surface cares
    // about native appearance, not about pretty-printing parity.
    std::string FormatConsoleArgsFrom(
        const v8::FunctionCallbackInfo<v8::Value>& info, int start)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string out;
      for (int i = start; i < info.Length(); i++)
      {
        if (!out.empty()) out += " ";
        v8::Local<v8::Value> arg = info[i];
        if (arg->IsObject() && !arg->IsFunction() && !arg->IsDate() &&
            !arg->IsRegExp())
        {
          v8::Local<v8::String> json;
          if (v8::JSON::Stringify(context, arg).ToLocal(&json))
            out += ToUtf8(isolate, json);
          else
            out += "[object Object]";
        }
        else
        {
          v8::Local<v8::String> str;
          if (arg->ToString(context).ToLocal(&str))
            out += ToUtf8(isolate, str);
        }
      }
      return out;
    }

    std::string FormatConsoleArgs(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      return FormatConsoleArgsFrom(info, 0);
    }

    void ConsoleWrite(const char* level, const std::string& text)
    {
      // JS-observable console behavior is fully synchronous/no-op; the
      // stdout sink is a dev convenience and is opt-in so page chatter
      // (e.g. the challenge SDK's console probing) does not flood tools.
      static const int verbose =
          []() {
            const char* env = std::getenv("LV8_CONSOLE_VERBOSE");
            return env != nullptr && env[0] != '\0' && std::strcmp(env, "0") != 0;
          }();
      if (!verbose)
        return;
      if (level[0] == '\0')
        std::fprintf(stdout, "%s\n", text.c_str());
      else
        std::fprintf(stdout, "[%s] %s\n", level, text.c_str());
      std::fflush(stdout);
    }

    void ConsoleLog(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleWrite("", FormatConsoleArgs(info));
    }

    void ConsoleInfo(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleWrite("info", FormatConsoleArgs(info));
    }

    void ConsoleWarn(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleWrite("warn", FormatConsoleArgs(info));
    }

    void ConsoleError(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleWrite("error", FormatConsoleArgs(info));
    }

    void ConsoleDebug(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleWrite("debug", FormatConsoleArgs(info));
    }

    void ConsoleTrace(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleWrite("trace", FormatConsoleArgs(info));
    }

    void ConsoleDir(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleWrite("", FormatConsoleArgs(info));
    }

    void ConsoleNoop(const v8::FunctionCallbackInfo<v8::Value>& info) {}

    // time/timeEnd/timeLog + count/countReset use process-wide maps; console
    // semantics are per-context in a browser, but console methods are rare
    // in automation payloads and this keeps the G1 surface simple.
    double NowMs()
    {
      return std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now().time_since_epoch())
          .count();
    }

    std::map<std::string, double>& ConsoleTimes()
    {
      static std::map<std::string, double> times;
      return times;
    }

    std::map<std::string, int>& ConsoleCounts()
    {
      static std::map<std::string, int> counts;
      return counts;
    }

    std::string ConsoleLabel(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      return info.Length() > 0 ? ToUtf8(info.GetIsolate(), info[0])
                               : std::string("default");
    }

    void ConsoleTime(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleTimes()[ConsoleLabel(info)] = NowMs();
    }

    void ConsoleTimeLog(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      std::string label = ConsoleLabel(info);
      auto it = ConsoleTimes().find(label);
      if (it == ConsoleTimes().end())
      {
        ConsoleWrite("error", "Timer '" + label + "' does not exist");
        return;
      }
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.3f", NowMs() - it->second);
      ConsoleWrite("", label + ": " + buf + " ms");
    }

    void ConsoleTimeEnd(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      std::string label = ConsoleLabel(info);
      auto it = ConsoleTimes().find(label);
      if (it == ConsoleTimes().end())
      {
        ConsoleWrite("error", "Timer '" + label + "' does not exist");
        return;
      }
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.3f", NowMs() - it->second);
      ConsoleWrite("", label + ": " + buf + " ms - timer ended");
      ConsoleTimes().erase(it);
    }

    void ConsoleCount(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      std::string label = ConsoleLabel(info);
      int n = ++ConsoleCounts()[label];
      ConsoleWrite("", label + ": " + std::to_string(n));
    }

    void ConsoleCountReset(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ConsoleCounts().erase(ConsoleLabel(info));
    }

    void ConsoleAssert(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (info.Length() > 0 && info[0]->BooleanValue(info.GetIsolate()))
        return;
      // Chrome semantics: the condition argument is not part of the message
      ConsoleWrite("error", "Assertion failed" +
                                (info.Length() > 1
                                     ? ": " + FormatConsoleArgsFrom(info, 1)
                                     : std::string()));
    }

    // Install one console method with "function <name>() { [native code] }"
    // appearance on the console prototype.
    void AddConsoleMethod(v8::Isolate* isolate,
                          v8::Local<v8::ObjectTemplate> proto_tmpl,
                          const char* method_name,
                          v8::FunctionCallback callback)
    {
      v8::Local<v8::String> name = ToV8Str(isolate, method_name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, callback);
      fn->SetClassName(name);
      proto_tmpl->Set(name, fn);
    }
    } // namespace

  void InstallConsole(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    // Methods go on Console.prototype; "console" itself is a plain own
    // property of the global object whose Object.keys() stays empty, like a
    // real browser.
    v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
    tpl->SetClassName(ToV8Str(isolate, "Console"));

    v8::Local<v8::ObjectTemplate> proto_tmpl = tpl->PrototypeTemplate();
    AddConsoleMethod(isolate, proto_tmpl, "log", ConsoleLog);
    AddConsoleMethod(isolate, proto_tmpl, "info", ConsoleInfo);
    AddConsoleMethod(isolate, proto_tmpl, "warn", ConsoleWarn);
    AddConsoleMethod(isolate, proto_tmpl, "error", ConsoleError);
    AddConsoleMethod(isolate, proto_tmpl, "debug", ConsoleDebug);
    AddConsoleMethod(isolate, proto_tmpl, "trace", ConsoleTrace);
    AddConsoleMethod(isolate, proto_tmpl, "dir", ConsoleDir);
    AddConsoleMethod(isolate, proto_tmpl, "dirxml", ConsoleNoop);
    AddConsoleMethod(isolate, proto_tmpl, "table", ConsoleNoop);
    AddConsoleMethod(isolate, proto_tmpl, "group", ConsoleNoop);
    AddConsoleMethod(isolate, proto_tmpl, "groupCollapsed", ConsoleNoop);
    AddConsoleMethod(isolate, proto_tmpl, "groupEnd", ConsoleNoop);
    AddConsoleMethod(isolate, proto_tmpl, "time", ConsoleTime);
    AddConsoleMethod(isolate, proto_tmpl, "timeLog", ConsoleTimeLog);
    AddConsoleMethod(isolate, proto_tmpl, "timeEnd", ConsoleTimeEnd);
    AddConsoleMethod(isolate, proto_tmpl, "count", ConsoleCount);
    AddConsoleMethod(isolate, proto_tmpl, "countReset", ConsoleCountReset);
    AddConsoleMethod(isolate, proto_tmpl, "assert", ConsoleAssert);
    AddConsoleMethod(isolate, proto_tmpl, "clear", ConsoleNoop);

    v8::Local<v8::ObjectTemplate> console_tmpl = tpl->InstanceTemplate();
    // M10: zero own symbols on instances (tag on prototype)
    tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "Console"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));
    v8::Local<v8::Object> console = console_tmpl->NewInstance(context).ToLocalChecked();

    (void)context->Global()->DefineOwnProperty(
        context, ToV8Str(isolate, "console"), console,
        static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));
  }

  // ------------------------------------------------------------ media queries

  namespace
  {
    v8::Local<v8::Value> ProfilePathGet(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        v8::Local<v8::Object> profile,
                                        const char* group, const char* field)
    {
      if (field == nullptr)
      {
        v8::Local<v8::Value> top;
        if (!profile->Get(context, ToV8Str(isolate, group)).ToLocal(&top))
          return v8::Undefined(isolate);
        return top;
      }
      v8::Local<v8::Value> subgroup;
      if (!profile->Get(context, ToV8Str(isolate, group)).ToLocal(&subgroup) ||
          !subgroup->IsObject())
        return v8::Undefined(isolate);
      v8::Local<v8::Value> value;
      if (!subgroup.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, field))
               .ToLocal(&value))
        return v8::Undefined(isolate);
      return value;
    }

    double AsNumber(v8::Local<v8::Context> context, v8::Local<v8::Value> value)
    {
      if (value->IsNumber())
        return value->NumberValue(context).FromMaybe(0.0);
      return 0.0;
    }

    std::string AsString(v8::Isolate* isolate, v8::Local<v8::Value> value)
    {
      if (value->IsString())
        return ToUtf8(isolate, value);
      return "";
    }

    // Media-query evaluation over the profile.  G4.238: this is an exact
    // behavioural port of the reference wheel's (reference) matcher, measured
    // host-level in work/_mq_hostab2.py: pointer-like features read the
    // profile, color-gamut/display-mode are fixed single values, and every
    // other feature (width/height/resolution/orientation/...) reports
    // no-match.  The business payload captures matchMedia into its VM host
    // table at boot, so JS-level prelude overrides never reach it - the
    // parity has to live here.
    bool EvalMediaQuery(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> profile, const std::string& raw)
    {
      auto str_field = [&](const char* group, const char* field) {
        return AsString(
            isolate, ProfilePathGet(isolate, context, profile, group, field));
      };
      const std::string pointer = str_field("media", "pointer");
      const std::string hover = str_field("media", "hover");
      // Reference-wheel parser (measured in tools/_mq_host_ab.py): every
      // parenthesised group is evaluated and the results are OR-ed; a group
      // without the exact ": " separator is a bare feature check against
      // the truthy set below; features/values are matched case-sensitively
      // with no trimming on the left and only trailing whitespace ignored.
      bool any = false;
      size_t pos = 0;
      while (pos < raw.size())
      {
        size_t open = raw.find('(', pos);
        if (open == std::string::npos)
          break;
        size_t close = raw.find(')', open + 1);
        if (close == std::string::npos)
          break;
        std::string inner = raw.substr(open + 1, close - open - 1);
        pos = close + 1;
        size_t sep = inner.find(": ");
        if (sep == std::string::npos)
        {
          if (inner == "pointer" || inner == "any-pointer" ||
              inner == "hover" || inner == "any-hover" ||
              inner == "color-gamut")
            any = true;
          continue;
        }
        std::string feature = inner.substr(0, sep);
        std::string value = inner.substr(sep + 2);
        while (!value.empty() &&
               (value.back() == ' ' || value.back() == '\t'))
          value.pop_back();
        bool hit = false;
        if (feature == "prefers-color-scheme")
          hit = str_field("media", "prefersColorScheme") == value;
        else if (feature == "prefers-reduced-motion")
          hit = str_field("media", "prefersReducedMotion") == value;
        else if (feature == "hover" || feature == "any-hover")
          hit = hover == value;
        else if (feature == "pointer" || feature == "any-pointer")
          hit = pointer == value;
        else if (feature == "color-gamut")
          hit = value == "srgb";
        else if (feature == "display-mode")
          hit = value == "browser";
        if (hit)
          any = true;
      }
      return any;
    }

    void MatchMediaCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsString())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: matchMedia requires a query string"));
        return;
      }
      ContextState* state = GetState(context);
      v8::Local<v8::Object> profile =
          state == nullptr ? v8::Local<v8::Object>()
                           : state->profile_data.Get(isolate);
      bool matches = false;
      if (!profile.IsEmpty())
        matches = EvalMediaQuery(isolate, context, profile,
                                 ToUtf8(isolate, info[0]));

      // reference parity: media/matches live on the prototype reading internal
      // fields - the instance own-face stays empty
      if (state->mql_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  v8::Isolate *iso = info.GetIsolate();
                  iso->ThrowException(ToV8Str(
                      iso, "Illegal constructor"));
                });
        ctor->SetClassName(ToV8Str(isolate, "MediaQueryList"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(2);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MediaQueryList"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto accessor = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          ctor->PrototypeTemplate()->SetAccessorProperty(
              ToV8Str(isolate, name), fn,
              v8::Local<v8::FunctionTemplate>());
        };
        // field 0 = media (string), field 1 = matches (boolean)
        accessor("media",
                 [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                   v8::Local<v8::Object> self = info.This();
                   if (self->InternalFieldCount() > 0)
                     info.GetReturnValue().Set(
                         v8::Local<v8::Value>::Cast(
                             self->GetInternalField(0)));
                 });
        accessor("matches",
                 [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                   v8::Local<v8::Object> self = info.This();
                   if (self->InternalFieldCount() > 1)
                     info.GetReturnValue().Set(
                         v8::Local<v8::Value>::Cast(
                             self->GetInternalField(1)));
                 });
        state->mql_tpl.Reset(isolate, ctor);
      }
      v8::Local<v8::Object> mql = state->mql_tpl.Get(isolate)
                                      ->InstanceTemplate()
                                      ->NewInstance(context)
                                      .ToLocalChecked();
      (void)mql->SetInternalField(0, info[0]);
      (void)mql->SetInternalField(
          1, v8::Boolean::New(isolate, matches));
      info.GetReturnValue().Set(mql);
      return;
    }

  } // namespace

  namespace
  {
    // __lv8__.monitor.enabled
    void MonitorEnabledGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ContextState* state = GetState(info.GetIsolate()->GetCurrentContext());
      info.GetReturnValue().Set(
          v8::Boolean::New(info.GetIsolate(),
                           state != nullptr && state->monitor_enabled));
    }

    // __lv8__.monitor.entries -> [{path, operation}, ...]
    void MonitorEntriesGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::Array> entries = v8::Array::New(isolate);
      if (state != nullptr)
      {
        uint32_t i = 0;
        for (const auto& entry : state->monitor_entries)
        {
          v8::Local<v8::Object> item = v8::Object::New(isolate);
          (void)item
              ->Set(context, ToV8Str(isolate, "path"),
                    ToV8Str(isolate, entry.first))
              .FromMaybe(false);
          (void)item
              ->Set(context, ToV8Str(isolate, "operation"),
                    ToV8Str(isolate, entry.second))
              .FromMaybe(false);
          if (i < state->monitor_values.size())
            (void)item
                ->Set(context, ToV8Str(isolate, "value"),
                      ToV8Str(isolate, state->monitor_values[i]))
                .FromMaybe(false);
          (void)entries->Set(context, i++, item).FromMaybe(false);
        }
      }
      info.GetReturnValue().Set(entries);
    }

    // __lv8__.monitor.clear()
    void MonitorClear(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ContextState* state = GetState(info.GetIsolate()->GetCurrentContext());
      if (state != nullptr)
        state->monitor_entries.clear();
    }

    void MonitorGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ContextState* state =
          GetState(info.GetIsolate()->GetCurrentContext());
      if (state == nullptr)
      {
        info.GetReturnValue().SetUndefined();
        return;
      }
      info.GetReturnValue().Set(state->monitor_object.Get(info.GetIsolate()));
    }
  } // namespace

  void InstallMonitorSurface(v8::Isolate* isolate,
                             v8::Local<v8::Context> context)
  {
    // monitor object: enabled / entries / clear()
    ContextState* state = EnsureState(isolate, context);
    v8::Local<v8::Object> monitor = v8::Object::New(isolate);
    state->monitor_object.Reset(isolate, monitor);
    v8::Local<v8::String> enabled_key = ToV8Str(isolate, "enabled");
    v8::Local<v8::FunctionTemplate> enabled_fn =
        v8::FunctionTemplate::New(isolate, MonitorEnabledGetter);
    enabled_fn->SetClassName(ToV8Str(isolate, "get enabled"));
    (void)monitor->SetAccessorProperty(
        enabled_key, enabled_fn->GetFunction(context).ToLocalChecked(),
        v8::Local<v8::Function>(),
        static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum |
                                           v8::PropertyAttribute::DontDelete));

    v8::Local<v8::String> entries_key = ToV8Str(isolate, "entries");
    v8::Local<v8::FunctionTemplate> entries_fn =
        v8::FunctionTemplate::New(isolate, MonitorEntriesGetter);
    entries_fn->SetClassName(ToV8Str(isolate, "get entries"));
    (void)monitor->SetAccessorProperty(
        entries_key, entries_fn->GetFunction(context).ToLocalChecked(),
        v8::Local<v8::Function>(),
        static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum |
                                           v8::PropertyAttribute::DontDelete));

    v8::Local<v8::String> clear_key = ToV8Str(isolate, "clear");
    v8::Local<v8::FunctionTemplate> clear_fn =
        v8::FunctionTemplate::New(isolate, MonitorClear);
    clear_fn->SetClassName(clear_key);
    (void)monitor
        ->DefineOwnProperty(
            context, clear_key, clear_fn->GetFunction(context).ToLocalChecked(),
            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum |
                                               v8::PropertyAttribute::DontDelete))
        .FromMaybe(false);

    // control surface object with a monitor accessor
    v8::Local<v8::Object> surface = v8::Object::New(isolate);
    v8::Local<v8::String> monitor_key = ToV8Str(isolate, "monitor");
    v8::Local<v8::FunctionTemplate> monitor_fn =
        v8::FunctionTemplate::New(isolate, MonitorGetter);
    monitor_fn->SetClassName(ToV8Str(isolate, "get monitor"));
    (void)surface->SetAccessorProperty(
        monitor_key, monitor_fn->GetFunction(context).ToLocalChecked(),
        v8::Local<v8::Function>(),
        static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum |
                                           v8::PropertyAttribute::DontDelete));

    // setMonitorSuspend(bool): park monitor recording while bootstrap
    // scripts snapshot their own surfaces (page-invisible reads)
    {
      v8::Local<v8::FunctionTemplate> suspend_tpl =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                ContextState* st = GetState(ctx);
                if (st != nullptr && info.Length() >= 1)
                  st->monitor_suspended = info[0]->BooleanValue(iso);
              });
      suspend_tpl->SetClassName(ToV8Str(isolate, "setMonitorSuspend"));
      (void)surface
          ->Set(context, ToV8Str(isolate, "setMonitorSuspend"),
                suspend_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
    }

    // __lv8__.help(): control-surface documentation (oracle checks
    // help().eventLoop)
    {
      v8::Local<v8::FunctionTemplate> help_tpl =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Object> doc = v8::Object::New(iso);
                (void)doc
                    ->Set(ctx, ToV8Str(iso, "eventLoop"),
                          ToV8Str(iso,
                                  "advance/sleep/drain control logical "
                                  "time"))
                    .FromMaybe(false);
                info.GetReturnValue().Set(doc);
              });
      help_tpl->SetClassName(ToV8Str(isolate, "help"));
      (void)surface
          ->DefineOwnProperty(context, ToV8Str(isolate, "help"),
                              help_tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // __lv8__.eventLoop: phase-exact control over microtasks and timers
    // (tick() runs microtasks + at most one due timer; drainTimers(n)
    // runs timers without microtasks; drain() empties both phases)
    {
      v8::Local<v8::Object> loop = v8::Object::New(isolate);
      auto loop_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        (void)loop->Set(context, key,
                        fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      };
      loop_method(
          "tick",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            double ms = info.Length() >= 1 && info[0]->IsNumber()
                ? info[0].As<v8::Number>()->Value() : 0.0;
            iso->PerformMicrotaskCheckpoint();
            AdvanceClock(iso, ctx, ms);
            info.GetReturnValue().Set(
                RunDueTimersLimit(iso, ctx, 1));
          });
      loop_method(
          "advance",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            double ms = info.Length() >= 1 && info[0]->IsNumber()
                ? info[0].As<v8::Number>()->Value() : 0.0;
            iso->PerformMicrotaskCheckpoint();
            info.GetReturnValue().Set(RunTimers(iso, ctx, ms));
          });
      loop_method(
          "sleep",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            double ms = info.Length() >= 1 && info[0]->IsNumber()
                ? info[0].As<v8::Number>()->Value() : 0.0;
            iso->PerformMicrotaskCheckpoint();
            info.GetReturnValue().Set(RunTimers(iso, ctx, ms));
          });
      loop_method(
          "drainTimers",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            int limit = info.Length() >= 1 && info[0]->IsInt32()
                ? info[0].As<v8::Int32>()->Value() : 10000;
            info.GetReturnValue().Set(RunDueTimersLimit(iso, ctx, limit));
          });
      loop_method(
          "drainMicrotasks",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            iso->PerformMicrotaskCheckpoint();
            info.GetReturnValue().Set(0);
          });
      loop_method(
          "drain",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            int limit = info.Length() >= 1 && info[0]->IsInt32()
                ? info[0].As<v8::Int32>()->Value() : 10000;
            int runs = 0;
            while (runs < limit)
            {
              iso->PerformMicrotaskCheckpoint();
              int batch = RunDueTimersLimit(iso, ctx, limit - runs);
              if (batch == 0) break;
              runs += batch;
            }
            iso->PerformMicrotaskCheckpoint();
            info.GetReturnValue().Set(runs);
          });
      loop_method(
          "setAutoAdvanceStep",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            ContextState* st = GetState(ctx);
            if (st != nullptr && info.Length() >= 1 && info[0]->IsNumber())
              st->perf_advance_step = info[0].As<v8::Number>()->Value();
          });
      loop_method(
          "setDateAdvanceStep",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            ContextState* st = GetState(ctx);
            if (st != nullptr && info.Length() >= 1 && info[0]->IsNumber())
              st->date_advance_step = info[0].As<v8::Number>()->Value();
          });
      // date-read step probes for the JS Date shim
      {
        v8::Local<v8::Object> global = context->Global();
        auto install_helper = [&](const char* name,
                                  v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          (void)global
              ->DefineOwnProperty(context, key,
                                  fn->GetFunction(context).ToLocalChecked(),
                                  static_cast<v8::PropertyAttribute>(
                                      v8::PropertyAttribute::DontEnum))
              .FromMaybe(false);
        };
        install_helper("__lv8_heap_dump",
                       [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                         v8::Isolate* iso = info.GetIsolate();
                         v8::Local<v8::Context> ctx =
                             iso->GetCurrentContext();
                         ContextState* st = GetState(ctx);
                         if (st == nullptr)
                           return;
                         if (st->heap_strings.empty())
                         {
                           v8::HeapProfiler *hp = iso->GetHeapProfiler();
                           if (hp == nullptr)
                             return;
                           const v8::HeapSnapshot *snap =
                               hp->TakeHeapSnapshot();
                           if (snap == nullptr)
                             return;
                           int count = snap->GetNodesCount();
                           for (int i = 0; i < count; i++)
                           {
                             const v8::HeapGraphNode *node =
                                 snap->GetNode(i);
                             if (node->GetType() !=
                                 v8::HeapGraphNode::kString)
                               continue;
                             v8::String::Utf8Value s(iso,
                                                     node->GetName());
                             if (s.length() >= 20 &&
                                 st->heap_strings.size() < 20000)
                               st->heap_strings.emplace_back(*s,
                                                             s.length());
                           }
                           const_cast<v8::HeapSnapshot *>(snap)->Delete();
                         }
                         std::string joined;
                         for (const auto &e : st->heap_strings)
                         {
                           if (!joined.empty()) joined.push_back('\x01');
                           joined += e;
                         }
                         info.GetReturnValue().Set(ToV8Str(iso, joined));
                       });
        install_helper("__lv8_set_fn_name",
                       [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                         v8::Isolate *iso = info.GetIsolate();
                         if (info.Length() < 2 || !info[0]->IsFunction() ||
                             !info[1]->IsString())
                           return;
                         v8::Local<v8::Function> fn =
                             info[0].As<v8::Function>();
                         v8::String::Utf8Value name_utf8(
                             iso, info[1].As<v8::String>());
                         fn->SetName(ToV8Str(iso, std::string(
                             *name_utf8, name_utf8.length())));
                       });
        install_helper("__lv8_stack_log",
                       [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                         v8::Isolate* iso = info.GetIsolate();
                         v8::Local<v8::Context> ctx =
                             iso->GetCurrentContext();
                         ContextState* st = GetState(ctx);
                         if (st == nullptr || info.Length() < 1 ||
                             !info[0]->IsString())
                           return;
                         v8::String::Utf8Value s(iso, info[0]);
                         if (st->stack_log.size() < 4096)
                           st->stack_log.emplace_back(*s, s.length());
                       });
        install_helper("__lv8_stack_dump",
                       [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                         v8::Isolate* iso = info.GetIsolate();
                         v8::Local<v8::Context> ctx =
                             iso->GetCurrentContext();
                         ContextState* st = GetState(ctx);
                         std::string joined;
                         if (st != nullptr)
                         {
                           for (const auto& e : st->stack_log)
                           {
                             if (!joined.empty()) joined.push_back('\x01');
                             joined += e;
                           }
                         }
                         info.GetReturnValue().Set(ToV8Str(iso, joined));
                       });
        install_helper("__lv8_date_step",
                       [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                         v8::Isolate* iso = info.GetIsolate();
                         v8::Local<v8::Context> ctx =
                             iso->GetCurrentContext();
                         ContextState* st = GetState(ctx);
                         info.GetReturnValue().Set(v8::Number::New(
                             iso,
                             st != nullptr ? st->date_advance_step : 0.0));
                       });
        install_helper("__lv8_date_read",
                       [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                         v8::Isolate* iso = info.GetIsolate();
                         v8::Local<v8::Context> ctx =
                             iso->GetCurrentContext();
                         ContextState* st = GetState(ctx);
                         if (st != nullptr && st->date_advance_step > 0.0)
                           st->clock_ms += st->date_advance_step;
                         info.GetReturnValue().Set(v8::Number::New(
                             iso, LogicalClock(iso, ctx)));
                       });
      }
      (void)surface->DefineOwnProperty(
          context, ToV8Str(isolate, "eventLoop"), loop,
          static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum |
                                             v8::PropertyAttribute::DontDelete));
    }

    // Only reference's control name is page-visible.  The historical lv8 alias is
    // configurable so the post-bootstrap bridge can remove it from window.
    (void)context->Global()->DefineOwnProperty(
        context, ToV8Str(isolate, "__lv8__"), surface,
        static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
        .FromMaybe(false);
    (void)context->Global()->DefineOwnProperty(
        context, ToV8Str(isolate, "__iv8__"), surface,
        static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum |
                                           v8::PropertyAttribute::DontDelete))
        .FromMaybe(false);
  }

  void InstallMediaQuery(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    // matchMedia: own, writable+configurable+enumerable (Window interface)
    v8::Local<v8::String> key = ToV8Str(isolate, "matchMedia");
    v8::Local<v8::FunctionTemplate> tpl =
        v8::FunctionTemplate::New(isolate, MatchMediaCallback);
    tpl->SetClassName(key);
    // G4.393l: native-method form - the reference exposes matchMedia
    // without a .prototype property.
    tpl->RemovePrototype();
    (void)context->Global()
        ->DefineOwnProperty(context, key,
                            tpl->GetFunction(context).ToLocalChecked(),
                            static_cast<v8::PropertyAttribute>(0))
        .FromMaybe(false);

    // window.orientation: reference has no such member (reference-measured);
    // only screen.orientation exists there
  }

  namespace
  {
    // Chrome window-surface completion: members a real Chromium window owns
    // that WAF environment surveys probe (typeof / value / callability)
    // before the fingerprint is sealed.  Geometry mirrors the screen
    // profile so window and screen stay coherent.  This is the native
    // counterpart of the guarded Python bootstrap, which remains only for
    // extensions built before these accessors existed.
    double WindowScreenDim(v8::Isolate* isolate,
                           v8::Local<v8::Context> context,
                           const char* primary, const char* fallback)
    {
      ContextState* state = GetState(context);
      if (state == nullptr)
        return 0.0;
      v8::Local<v8::Object> profile = state->profile_data.Get(isolate);
      v8::Local<v8::Value> value = ProfilePathGet(isolate, context, profile,
                                                  "screen", primary);
      if (!value->IsNumber())
        value = ProfilePathGet(isolate, context, profile, "screen", fallback);
      return AsNumber(context, value);
    }

    void InnerWidthGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      info.GetReturnValue().Set(v8::Number::New(
          iso, WindowScreenDim(iso, iso->GetCurrentContext(), "availWidth",
                               "width")));
    }

    void InnerHeightGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      info.GetReturnValue().Set(v8::Number::New(
          iso, WindowScreenDim(iso, iso->GetCurrentContext(), "availHeight",
                               "height")));
    }

    void OuterWidthGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      info.GetReturnValue().Set(v8::Number::New(
          iso, WindowScreenDim(iso, iso->GetCurrentContext(), "availWidth",
                               "width")));
    }

    void OuterHeightGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      info.GetReturnValue().Set(v8::Number::New(
          iso, WindowScreenDim(iso, iso->GetCurrentContext(), "availHeight",
                               "height")));
    }

    // window.clientInformation: Chrome aliases the navigator instance
    void ClientInformationGetter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      v8::Local<v8::Value> nav;
      if (ctx->Global()->Get(ctx, ToV8Str(iso, "navigator")).ToLocal(&nav))
        info.GetReturnValue().Set(nav);
      else
        info.GetReturnValue().Set(v8::Undefined(iso));
    }

    // open(): popups are blocked in the offline runtime; Chrome returns
    // null for blocked popups, so report that shape
    void WindowOpenCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      (void)info;
      info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    void WindowAlertCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      (void)info;
      info.GetReturnValue().Set(v8::Undefined(info.GetIsolate()));
    }

    void WindowConfirmCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      (void)info;
      info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), false));
    }

    void WindowPromptCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      (void)info;
      info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    // WebIDL constructor surfaces (Gamepad, MediaSource, legacy exception
    // interfaces, MediaController): constructible check targets; calling
    // them is an IllegalConstructor TypeError as in Chrome
    void IllegalConstructorCallback(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetIsolate()->ThrowException(
          ToV8Str(info.GetIsolate(), "Illegal constructor"));
    }

    void IsTypeSupportedCallback(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      // G4.235: the reference engine answers FALSE for every probe (30/30
      // codec/type probes verified empty -> false).  lv8's constant true
      // flipped the page SDK's media branch ("video/mp4; codecs=\"avc1.42E01E,
      // mp4a.40.2\"") and with it the whole unit-set/variant selection that
      // poisons the ESS state fields 50/51.
      (void)info;
      info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), false));
    }

    // navigator.webkit{Persistent,Temporary}Storage: legacy Chrome quota
    // handles; usage reports 0 and quota mirrors the storage profile
    double StorageQuota(v8::Isolate* isolate,
                        v8::Local<v8::Context> context)
    {
      ContextState* state = GetState(context);
      if (state == nullptr)
        return 1073741824.0;
      v8::Local<v8::Object> profile = state->profile_data.Get(isolate);
      v8::Local<v8::Value> value =
          ProfilePathGet(isolate, context, profile, "storage", "quota");
      return value->IsNumber()
                 ? value->NumberValue(context).FromMaybe(1073741824.0)
                 : 1073741824.0;
    }

    void StorageQueryUsageCallback(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      double quota = StorageQuota(iso, ctx);
      if (info.Length() >= 1 && info[0]->IsFunction())
      {
        v8::Local<v8::Value> argv[2] = {v8::Number::New(iso, 0.0),
                                        v8::Number::New(iso, quota)};
        (void)info[0]
            .As<v8::Function>()
            ->Call(ctx, ctx->Global(), 2, argv)
            .FromMaybe(v8::Local<v8::Value>());
      }
      info.GetReturnValue().Set(v8::Undefined(iso));
    }

    void StorageRequestQuotaCallback(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      if (info.Length() >= 2 && info[0]->IsNumber() && info[1]->IsFunction())
      {
        v8::Local<v8::Value> argv[1] = {info[0]};
        (void)info[1]
            .As<v8::Function>()
            ->Call(ctx, ctx->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
      }
      info.GetReturnValue().Set(v8::Undefined(iso));
    }
  } // namespace

  // B2 surface: window position/scroll/status/origin family
  void WindowZeroNumberGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> context = iso->GetCurrentContext();
    v8::Local<v8::Value> value = v8::Number::New(iso, 0);
    if (info.Data()->IsString())
      MonitorRead(iso, context,
                  "window." + ToUtf8(iso, info.Data().As<v8::String>()),
                  value);
    info.GetReturnValue().Set(value);
  }
  void WindowFalseGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> context = iso->GetCurrentContext();
    v8::Local<v8::Value> value = v8::Boolean::New(iso, false);
    if (info.Data()->IsString())
      MonitorRead(iso, context,
                  "window." + ToUtf8(iso, info.Data().As<v8::String>()),
                  value);
    info.GetReturnValue().Set(value);
  }
  void WindowEmptyStringGetter(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> context = iso->GetCurrentContext();
    v8::Local<v8::Value> value = ToV8Str(iso, "");
    if (info.Data()->IsString())
      MonitorRead(iso, context,
                  "window." + ToUtf8(iso, info.Data().As<v8::String>()),
                  value);
    info.GetReturnValue().Set(value);
  }
  void WindowDevicePixelRatioGetter(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> context = iso->GetCurrentContext();
    v8::Local<v8::Value> value = v8::Number::New(iso, 1);
    MonitorRead(iso, context, "window.devicePixelRatio", value);
    info.GetReturnValue().Set(value);
  }
  void WindowIsSecureContextGetter(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> context = iso->GetCurrentContext();
    std::string href = ResolveUrlString(iso, context, "");
    bool secure = href.rfind("https:", 0) == 0;
    v8::Local<v8::Value> value = v8::Boolean::New(iso, secure);
    MonitorRead(iso, context, "window.isSecureContext", value);
    info.GetReturnValue().Set(value);
  }
  void WindowOriginGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> context = iso->GetCurrentContext();
    std::string href = ResolveUrlString(iso, context, "");
    size_t scheme = href.find("://");
    if (scheme == std::string::npos)
    {
      v8::Local<v8::Value> value = ToV8Str(iso, "null");
      MonitorRead(iso, context, "window.origin", value);
      info.GetReturnValue().Set(value);
      return;
    }
    std::string rest = href.substr(scheme + 3);
    size_t path = rest.find_first_of("/?#");
    std::string origin = href.substr(0, scheme + 3) +
                         (path == std::string::npos ? rest
                                                    : rest.substr(0, path));
    v8::Local<v8::Value> value = ToV8Str(iso, origin);
    MonitorRead(iso, context, "window.origin", value);
    info.GetReturnValue().Set(value);
  }

  void InstallWindowSurface(v8::Isolate* isolate,
                            v8::Local<v8::Context> context)
  {
    // browser brand: the main global reports "[object Window]" via
    // Object.prototype.toString (Symbol.toStringTag).  The WAF SDK branches
    // on this brand during bootstrap; a misbranded window sent its state
    // machine down the wrong generation path.
    {
      v8::Local<v8::Object> global = context->Global();
      (void)global
          ->DefineOwnProperty(
              context, v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "Window"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    auto define_fn = [&](const char* name, v8::FunctionCallback cb) {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> tpl =
          v8::FunctionTemplate::New(isolate, cb);
      tpl->SetClassName(key);
      // Chrome owns these as configurable (deletable) so page scripts may
      // redeclare them; DontDelete would break `function open(){}` in eval
      (void)context
          ->Global()
          ->DefineOwnProperty(
              context, key, tpl->GetFunction(context).ToLocalChecked(),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    };
    auto define_getter = [&](const char* name, v8::FunctionCallback cb) {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> tpl =
          v8::FunctionTemplate::New(isolate, cb, key);
      tpl->SetClassName(ToV8Str(isolate, (std::string("get ") + name).c_str()));
      (void)context->Global()->SetAccessorProperty(
          key, tpl->GetFunction(context).ToLocalChecked(),
          v8::Local<v8::Function>(),
          static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));
    };
    auto define_ctor = [&](const char* name) {
      define_fn(name, IllegalConstructorCallback);
    };

    define_getter("innerWidth", InnerWidthGetter);
    define_getter("innerHeight", InnerHeightGetter);
    define_getter("outerWidth", OuterWidthGetter);
    define_getter("outerHeight", OuterHeightGetter);
    define_getter("clientInformation", ClientInformationGetter);
    // B2 surface: window position/scroll/status/origin family
    define_getter("screenLeft", WindowZeroNumberGetter);
    define_getter("screenTop", WindowZeroNumberGetter);
    define_getter("screenX", WindowZeroNumberGetter);
    define_getter("screenY", WindowZeroNumberGetter);
    define_getter("scrollX", WindowZeroNumberGetter);
    define_getter("scrollY", WindowZeroNumberGetter);
    define_getter("pageXOffset", WindowZeroNumberGetter);
    define_getter("pageYOffset", WindowZeroNumberGetter);
    define_getter("devicePixelRatio", WindowDevicePixelRatioGetter);
    define_getter("origin", WindowOriginGetter);
    define_getter("isSecureContext", WindowIsSecureContextGetter);
    define_getter("closed", WindowFalseGetter);
    define_getter("credentialless", WindowFalseGetter);
    define_getter("crossOriginIsolated", WindowFalseGetter);
    define_getter("hasOpener", WindowFalseGetter);
    define_getter("originAgentCluster", WindowFalseGetter);
    define_getter("name", WindowEmptyStringGetter);
    define_getter("status", WindowEmptyStringGetter);
    define_getter("defaultStatus", WindowEmptyStringGetter);
    define_fn("open", WindowOpenCallback);
    define_fn("alert", WindowAlertCallback);
    define_fn("confirm", WindowConfirmCallback);
    define_fn("prompt", WindowPromptCallback);
    define_ctor("Gamepad");
    define_ctor("SVGException");
    define_ctor("XPathException");
    define_ctor("MediaController");
    // MediaSource carries the static isTypeSupported(codec) predicate
    {
      v8::Local<v8::String> key = ToV8Str(isolate, "MediaSource");
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
          isolate, IllegalConstructorCallback);
      tpl->SetClassName(key);
      v8::Local<v8::Function> fn =
          tpl->GetFunction(context).ToLocalChecked();
      v8::Local<v8::String> its_key = ToV8Str(isolate, "isTypeSupported");
      v8::Local<v8::FunctionTemplate> its = v8::FunctionTemplate::New(
          isolate, IsTypeSupportedCallback);
      its->SetClassName(its_key);
      (void)fn
          ->Set(context, its_key,
                its->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, key, fn,
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    // navigator additions: the legacy webkit quota handles (getGamepads
    // lives in InstallNet)
    v8::Local<v8::Value> nav_v;
    if (!context->Global()->Get(context, ToV8Str(isolate, "navigator"))
             .ToLocal(&nav_v) ||
        !nav_v->IsObject())
      return;
    v8::Local<v8::Object> nav = nav_v.As<v8::Object>();
    v8::Local<v8::Value> probe;
    for (const char* storage_name :
         {"webkitPersistentStorage", "webkitTemporaryStorage"})
    {
      if (!nav->Get(context, ToV8Str(isolate, storage_name)).ToLocal(&probe) ||
          !probe->IsUndefined())
        continue;
      v8::Local<v8::Object> handle = v8::Object::New(isolate);
      auto handle_fn = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> tpl =
            v8::FunctionTemplate::New(isolate, cb);
        tpl->SetClassName(key);
        (void)handle
            ->DefineOwnProperty(
                context, key, tpl->GetFunction(context).ToLocalChecked(),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      };
      handle_fn("queryUsageAndQuota", StorageQueryUsageCallback);
      handle_fn("requestQuota", StorageRequestQuotaCallback);
      (void)nav
          ->DefineOwnProperty(context, ToV8Str(isolate, storage_name), handle,
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
  }

  // ------------------------------------------------------- native appearance
  // Polyfill and shell sources are compiled under 'lv8-bootstrap*' script
  // names; they must never surface in Error stacks or in
  // Function.prototype.toString (the reference exposes these surfaces as
  // built-ins). Stacks are formatted exactly like the V8 default
  // ("header\n    at fn (script:line:col)" / "at script:line:col") with
  // bootstrap frames dropped.
  // Function.prototype.toString: functions defined in bootstrap scripts
  // report the native format; everything else falls through to the
  // original implementation (stashed on Function.prototype via a private
  // symbol at install time).
  void Lv8FunctionToString(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> target = info.This();
    if (target->IsFunction())
    {
      v8::Local<v8::Function> fn = target.As<v8::Function>();
      v8::ScriptOrigin origin = fn->GetScriptOrigin();
      v8::Local<v8::Value> rname = origin.ResourceName();
      std::string script =
          rname.IsEmpty() ? std::string() : ToUtf8(isolate, rname);
      bool native_like = script.rfind("lv8-bootstrap", 0) == 0;
      if (!native_like)
      {
        // G4.306: a few reference interfaces are materialised outside the
        // bootstrap scripts (page-injected binder).  They may register their
        // names as "native-looking" so Function.prototype.toString matches
        // the reference wheel.
        v8::Local<v8::Value> registry;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "__LV8_NATIVE_FN_NAMES__"))
                .ToLocal(&registry) &&
            registry->IsObject())
        {
          v8::Local<v8::Value> name_v;
          std::string name;
          if (fn->Get(context, ToV8Str(isolate, "name")).ToLocal(&name_v) &&
              name_v->IsString())
          {
            name = ToUtf8(isolate, name_v);
          }
          v8::Local<v8::Value> hit;
          if (!name.empty() &&
              registry.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, name))
                  .ToLocal(&hit) &&
              hit->BooleanValue(isolate))
          {
            native_like = true;
          }
        }
      }
      if (native_like)
      {
        v8::Local<v8::Value> name_v;
        std::string name;
        if (fn->Get(context, ToV8Str(isolate, "name")).ToLocal(&name_v) &&
            name_v->IsString())
          name = ToUtf8(isolate, name_v);
        info.GetReturnValue().Set(
            ToV8Str(isolate, "function " + name + "() { [native code] }"));
        return;
      }
    }
    v8::Local<v8::Object> fp = context->Global()
                                   ->Get(context, ToV8Str(isolate, "Function"))
                                   .ToLocalChecked()
                                   .As<v8::Function>()
                                   ->GetPrototype()
                                   .As<v8::Object>();
    v8::Local<v8::Private> slot = v8::Private::ForApi(
        isolate, ToV8Str(isolate, "lv8.orig.fnToString"));
    v8::Local<v8::Value> orig;
    if (fp->GetPrivate(context, slot).ToLocal(&orig) && orig->IsFunction())
    {
      v8::Local<v8::Value> result;
      if (orig.As<v8::Function>()
              ->Call(context, target, 0, nullptr)
              .ToLocal(&result))
        info.GetReturnValue().Set(result);
    }
  }

  void InstallNativeAppearance(v8::Isolate* isolate,
                               v8::Local<v8::Context> context)
  {

    v8::Local<v8::Object> fp =
        context->Global()
            ->Get(context, ToV8Str(isolate, "Function"))
            .ToLocalChecked()
            .As<v8::Function>()
            ->GetPrototype()
            .As<v8::Object>();
    v8::Local<v8::String> key = ToV8Str(isolate, "toString");
    v8::Local<v8::Function> orig =
        fp->Get(context, key).ToLocalChecked().As<v8::Function>();
    (void)fp
        ->SetPrivate(context,
                     v8::Private::ForApi(
                         isolate, ToV8Str(isolate, "lv8.orig.fnToString")),
                     orig)
        .FromMaybe(false);
    v8::Local<v8::FunctionTemplate> tpl =
        v8::FunctionTemplate::New(isolate, Lv8FunctionToString);
    tpl->SetClassName(ToV8Str(isolate, "toString"));
    v8::Local<v8::Function> fn = tpl->GetFunction(context).ToLocalChecked();
    (void)fp->Set(context, key, fn).FromMaybe(false);
  }

  // ------------------------------------------------------------------ install

  void InstallHostObjects(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          const std::string& profile_json)
  {
    // Capture the machine zone/locale before any context can override the
    // process-wide ICU defaults (must run before the first set_timezone).
    SaveHostDefaults();

    v8::Local<v8::String> json = ToV8Str(isolate, profile_json);
    v8::Local<v8::Value> parsed;
    if (!v8::JSON::Parse(context, json).ToLocal(&parsed) || !parsed->IsObject())
      throw std::runtime_error("lv8: profile is not valid JSON object");

    v8::Local<v8::Object> profile = parsed.As<v8::Object>();

    // Infrastructure first; a re-install resets per-context state.
    ContextState* state = EnsureState(isolate, context);
    state->clock_ms = 0.0;
    // real-clock anchors are always captured: performance.timeOrigin
    // reports the real creation epoch (with sub-millisecond fraction)
    // in every time mode, matching the reference wheel
    state->system_origin_steady = std::chrono::steady_clock::now();
    state->system_origin_ms = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    state->system_origin_ms_precise =
        std::chrono::duration<double, std::milli>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    // reference wheel renders timeOrigin with millisecond-precision
    // fractions ("...918.715"); keep the double at 3 decimals
    state->system_origin_ms_precise =
        std::floor(state->system_origin_ms_precise * 1000.0) / 1000.0;
    state->next_timer_id = 1;
    state->timers.clear();
    state->entries.clear();
    state->perf_entries.clear();
    state->perf_observers.clear();
    state->index = 0;
    state->listeners.clear();
    // raw profile kept for data-only consumers (matchMedia, orientation)
    state->profile_data.Reset(isolate, profile);
    {
      v8::Local<v8::Value> interval;
      if (profile->Get(context, ToV8Str(isolate, "$animation_frame_interval"))
              .ToLocal(&interval) && interval->IsNumber())
        state->animation_frame_interval = interval->NumberValue(context)
            .FromMaybe(16.0);
      v8::Local<v8::Value> time_mode;
      if (profile->Get(context, ToV8Str(isolate, "$time_mode"))
              .ToLocal(&time_mode) && time_mode->IsString())
      {
        const std::string tm = ToUtf8(isolate, time_mode);
        if (tm == "system" || tm == "hybrid")
        {
          // hybrid: Date stays logical while performance.now runs on the
          // real steady clock - the reference wheel's combination (its Date.now
          // advances ~1ms per read; performance.now reports real elapsed
          // milliseconds since the wheel started)
          state->perf_real = tm == "hybrid";
          state->system_time = tm == "system";
          state->system_origin_steady = std::chrono::steady_clock::now();
          // G4.369: the reference wheel's load pushes ~200ms of virtual
          // time before page scripts run (reference-measured, PROGRESS G4.363);
          // due timers run against this base plus real drain time.
          state->system_clock_base = 200.0;
          state->system_origin_ms = static_cast<double>(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count());
          if (const char *pace_env = std::getenv("LV8_DATE_PACE_MS"))
          {
            try
            {
              double pace = std::stod(pace_env);
              if (pace >= 0.0)
                state->date_pace_ms = pace;
            }
            catch (...)
            {
            }
          }
        }
        if (tm == "system")
        {
          // G4.375: the reference wheel's timer clock is driven by the
          // page's own Date reads (dateAdvanceStep 1ms/read, wheel-exposed
          // knob measured in _g375_clock_probe.json) - that is what fires
          // its delayed DOM cleanup (the head IFRAME removal) while the
          // battery runs.  lv8 kept clock_ms frozen, so that timer never
          // became due (rc 6 vs 7, headChildren 92 vs 91).  Date VALUES
          // stay paced real time (ESS-proven); only the clock bookkeeping
          // tracks reads.  The LV8_TIMER_CLOCK=real experiment keeps its
          // G4.369 semantics and does not opt in.
          const char *clock_env = std::getenv("LV8_TIMER_CLOCK");
          if (clock_env == nullptr || std::string(clock_env) != "real")
            state->date_advance_step = 1.0;
          // Reference-wheel pace for Date.now() reads: the challenge's
          // interpreter polls the clock at ~1ms granularity and its encoded
          // phase durations reflect that real work time; a bare
          // re-implementation finishes the same phases ~10x faster and the
          // WAF's plausibility window rejects the cookie.  Native shape
          // (native code string, name "now", length 0) is identical to the
          // builtin, so the substitution stays invisible.
          v8::Local<v8::Object> global = context->Global();
          v8::Local<v8::Value> date_val;
          if (global->Get(context, ToV8Str(isolate, "Date"))
                  .ToLocal(&date_val) && date_val->IsObject())
          {
            v8::Local<v8::FunctionTemplate> now_tpl =
                v8::FunctionTemplate::New(
                    isolate,
                    [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                      v8::Isolate* iso = info.GetIsolate();
                      v8::Local<v8::Context> ctx =
                          iso->GetCurrentContext();
                      ContextState* st = GetState(ctx);
                      auto real_ms = []() {
                        return static_cast<double>(
                            std::chrono::duration_cast<
                                std::chrono::milliseconds>(
                                std::chrono::system_clock::now()
                                    .time_since_epoch())
                                .count());
                      };
                      // G4.377: LV8_DATE_MODE=virtual mirrors the wheel's
                      // Date VALUE semantics (epoch-at-origin + the shared
                      // clock, +date_advance_step per read, no real-time
                      // pacing) - the SDK's phase-wait loops then consume
                      // read counts instead of real milliseconds.  Default
                      // stays the ESS-proven paced real time.
                      const char *dmode = std::getenv("LV8_DATE_MODE");
                      bool virtual_dates = dmode != nullptr &&
                                           std::string(dmode) == "virtual";
                      double now_ms;
                      if (virtual_dates && st != nullptr)
                      {
                        if (st->date_advance_step > 0.0)
                          st->clock_ms += st->date_advance_step;
                        now_ms = st->system_origin_ms + st->clock_ms;
                      }
                      else
                      {
                        now_ms = real_ms();
                        if (st != nullptr)
                        {
                          double pace = st->date_pace_ms;
                          while (now_ms - st->last_paced_date_ms < pace)
                          {
                            std::this_thread::yield();
                            now_ms = real_ms();
                          }
                          st->last_paced_date_ms = now_ms;
                          // G4.375: each returned read advances the shared
                          // timer clock by the step (reference dateAdvanceStep
                          // model) - the SDK's own read cadence decides when
                          // its delayed cleanup timers become due.
                          if (st->date_advance_step > 0.0)
                            st->clock_ms += st->date_advance_step;
                        }
                      }
                      info.GetReturnValue().Set(
                          v8::Number::New(iso, now_ms));
                    });
            now_tpl->SetClassName(ToV8Str(isolate, "now"));
            v8::Local<v8::Function> now_fn =
                now_tpl->GetFunction(context).ToLocalChecked();
            now_fn->SetName(ToV8Str(isolate, "now"));
            (void)date_val.As<v8::Object>()
                ->DefineOwnProperty(context, ToV8Str(isolate, "now"),
                                    now_fn,
                                    static_cast<v8::PropertyAttribute>(
                                        v8::PropertyAttribute::None))
                .FromMaybe(false);
          }
        }
      }
    }

    // "$monitor": {"enabled": bool, "ignore": [path, ...]}
    v8::Local<v8::Value> monitor_cfg;
    if (profile->Get(context, ToV8Str(isolate, "$monitor"))
            .ToLocal(&monitor_cfg) &&
        monitor_cfg->IsObject())
    {
      v8::Local<v8::Value> enabled;
      if (monitor_cfg.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "enabled"))
              .ToLocal(&enabled))
        state->monitor_enabled = enabled->BooleanValue(isolate);
      v8::Local<v8::Value> ignore;
      if (monitor_cfg.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "ignore"))
              .ToLocal(&ignore) &&
          ignore->IsArray())
      {
        v8::Local<v8::Array> paths = ignore.As<v8::Array>();
        for (uint32_t i = 0; i < paths->Length(); i++)
        {
          v8::Local<v8::Value> path_value =
              paths->Get(context, i).FromMaybe(
                  v8::Local<v8::Value>(v8::Undefined(isolate)));
          if (path_value->IsString())
            state->ignore_paths.push_back(ToUtf8(isolate, path_value));
        }
      }
    }

    // The single browsing context is its own parent/top-level frame.  The
    // window alias is installed before every other enumerable surface so it
    // stays the first key of Object.keys(window) (the reference engine's
    // enumeration order; the WAF VM fingerprints that order).  window keeps
    // its [LegacyUnforgeable] non-configurable accessor; the other aliases
    // are configurable so the enumeration-normalization tranche can slot
    // them at their reference positions.
    struct SelfGetter
    {
      static void Get(const v8::FunctionCallbackInfo<v8::Value>& info)
      {
        info.GetReturnValue().Set(
            info.GetIsolate()->GetCurrentContext()->Global());
      }
    };
    for (const char* alias : {"window", "self", "top", "parent"})
    {
      v8::Local<v8::String> key = ToV8Str(isolate, alias);
      v8::Local<v8::FunctionTemplate> getter_tpl =
          v8::FunctionTemplate::New(isolate, SelfGetter::Get);
      getter_tpl->SetClassName(ToV8Str(isolate, std::string("get ") + alias));
      v8::Local<v8::Function> getter =
          getter_tpl->GetFunction(context).ToLocalChecked();
      v8::PropertyAttribute alias_attr =
        (std::string(alias) == "window")
            ? v8::PropertyAttribute::DontDelete
            : static_cast<v8::PropertyAttribute>(0);
      (void)context->Global()->SetAccessorProperty(
          key, getter, v8::Local<v8::Function>(),
          static_cast<v8::PropertyAttribute>(alias_attr));
    }

    InstallEventTarget(isolate, context);
    InstallConsole(isolate, context);
    InstallTimers(isolate, context);

    // location/history: stateful native objects (URL session history).
    std::string initial_url = "about:blank";
    v8::Local<v8::Value> location_data;
    if (profile->Get(context, ToV8Str(isolate, "location")).ToLocal(&location_data) &&
        location_data->IsObject())
    {
      v8::Local<v8::Value> href;
      if (location_data.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "href"))
              .ToLocal(&href) &&
          href->IsString())
      {
        initial_url = ToUtf8(isolate, href);
      }
    }
    InstallNavigation(isolate, context, initial_url);

    // Remaining profile keys become profile-driven host objects. Keys with
    // dedicated native surfaces are skipped.
    v8::Local<v8::Array> keys =
        profile->GetOwnPropertyNames(context).ToLocalChecked();
    for (uint32_t i = 0; i < keys->Length(); i++)
    {
      std::string key =
          ToUtf8(isolate, keys->Get(context, i).ToLocalChecked());

      // G4.231: the reference window walk hits "document" between
      // "navigation" and "history" (measured: own-key index 225 of the
      // business-page for-in).  Place the document accessor at exactly that
      // insertion point so the SDK's key battery walks both engines in one
      // order.  This must run BEFORE the data-only skip below - "history" is
      // itself data-only, so the check would otherwise never be reached.
      if (key == "history")
        EnsureDocumentAccessorProperty(isolate, context);

      // location/history/console have native surfaces; the rest of these
      // groups are data-only (matchMedia / orientation / async payloads
      // read them through state->profile_data) and never become globals -
      // browsers have no window.battery / window.media either.
      static const char* const kDataOnlyKeys[] = {
          "location", "history",     "console", "media",
          "devicePixelRatio", "battery", "chrome",  "speech",
          "storage",  "webgl",       "webgpu",  "webrtc",
          "midi",     "audio",       "canvas",  "mediaDevices",
          "permissions", "$monitor", "$methods", "$async"};
      bool data_only = false;
      for (const char* data_key : kDataOnlyKeys)
      {
        if (key == data_key)
        {
          data_only = true;
          break;
        }
      }
      if (data_only)
        continue;

      v8::Local<v8::Value> value;
      if (!profile->Get(context, ToV8Str(isolate, key)).ToLocal(&value) ||
          !value->IsObject() || value->IsArray())
        continue;

      // Class name: known interfaces keep their real browser spelling.
      const char* known = BrowserClassName(key);
      std::string class_name = known ? known : PascalClassName(key);

      v8::Local<v8::Object> obj = HostObjectFromProfile(
          isolate, context, class_name.c_str(), value.As<v8::Object>(), key);
      (void)context->Global()->DefineOwnProperty(
          context, ToV8Str(isolate, key), obj,
          static_cast<v8::PropertyAttribute>(0));
    }

    // Safety net: contexts whose profile carries no "history" key still get
    // the reference descriptor face (the placement pass is idempotent and a
    // no-op once the walk has already relocated the accessor).
    EnsureDocumentAccessorProperty(isolate, context);

    InstallMediaQuery(isolate, context);
    InstallWindowSurface(isolate, context);
    InstallMonitorSurface(isolate, context);
    InstallStorage(isolate, context, profile);
    InstallCrypto(isolate, context);
    InstallNet(isolate, context);
    InstallDocumentShell(isolate, context);
    InstallOfflineDeclarations(isolate, context);
    InstallIdbAndFs(isolate, context);
    InstallBrowserChrome(isolate, context);
    InstallDomExtras(isolate, context);
    InstallCanvasSurface(isolate, context);
    InstallNativeAppearance(isolate, context);
  }
} // namespace LV8
