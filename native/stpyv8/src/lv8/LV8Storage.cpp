// lv8 G3: web storage (localStorage/sessionStorage) and the cookie jar.
//
// Storage objects are proxy-like host surfaces over per-context,
// insertion-ordered key/value lists: named property reads/writes go through
// interceptors while getItem/setItem/... live on Storage.prototype, and
// `length` is a read-only own accessor. Values are always strings (Web
// Storage semantics). The cookie jar models document.cookie for the context
// origin: insertion-ordered, "Max-Age<=0" deletes an entry (an expired
// "Expires" would too -- date parsing arrives with the network bridge), and
// reads serialize "name=value" pairs joined by "; ".

#include "LV8.h"

#include "../Exception.h"

#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace LV8
  {
  namespace
    {
    // ----------------------------------------------------- storage model

    using StorageMap = std::vector<std::pair<std::string, std::string>>;

    enum StorageKind
    {
      kLocalStorage = 0,
      kSessionStorage = 1,
    };

    int StorageIndexOf(const StorageMap& storage, const std::string& key)
    {
      for (size_t i = 0; i < storage.size(); i++)
      {
        if (storage[i].first == key)
          return static_cast<int>(i);
      }
      return -1;
    }

    StorageMap& StorageFor(v8::Isolate* isolate,
                           v8::Local<v8::Context> context, StorageKind kind)
    {
      (void)isolate;
      ContextState* state = GetState(context);
      return kind == kLocalStorage ? state->local_storage
                                   : state->session_storage;
    }

    // The instance/method |data| payload carries the storage kind.
    StorageKind KindFromData(v8::Local<v8::Value> data)
    {
      if (data->IsExternal())
        return static_cast<StorageKind>(
            reinterpret_cast<intptr_t>(data.As<v8::External>()->Value()));
      return kLocalStorage;
    }

    // localStorage and sessionStorage share one real Storage.prototype.
    // Native member callbacks therefore select their backing store from the
    // receiver, rather than from a per-template callback payload.
    StorageKind KindForReceiver(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                v8::Local<v8::Object> receiver,
                                StorageKind fallback)
    {
      ContextState* state = GetState(context);
      if (state != nullptr)
      {
        if (!state->session_storage_object.IsEmpty() &&
            receiver->StrictEquals(state->session_storage_object.Get(isolate)))
          return kSessionStorage;
        if (!state->local_storage_object.IsEmpty() &&
            receiver->StrictEquals(state->local_storage_object.Get(isolate)))
          return kLocalStorage;
      }
      return fallback;
    }

    // Names that must reach the real accessors/prototype, not the data
    // interceptor.
    bool IsReservedStorageName(const std::string& key)
    {
      return key == "length" || key == "getItem" || key == "setItem" ||
             key == "removeItem" || key == "clear" || key == "key";
    }

    v8::Intercepted StoragePropertyGetter(
        v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
      if (!name->IsString())
        return v8::Intercepted::kNo;
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string key = ToUtf8(isolate, name);
      if (IsReservedStorageName(key))
        return v8::Intercepted::kNo;
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      int idx = StorageIndexOf(storage, key);
      if (idx < 0)
        return v8::Intercepted::kNo;
      info.GetReturnValue().Set(ToV8Str(isolate, storage[idx].second));
      return v8::Intercepted::kYes;
    }

    v8::Intercepted StoragePropertySetter(
        v8::Local<v8::Name> name, v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
      if (!name->IsString())
        return v8::Intercepted::kNo;
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string key = ToUtf8(isolate, name);
      if (IsReservedStorageName(key))
        return v8::Intercepted::kNo;
      std::string val = ToUtf8(isolate, value);
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      int idx = StorageIndexOf(storage, key);
      if (idx >= 0)
        storage[idx].second = val;
      else
        storage.push_back({key, val});
      info.GetReturnValue().Set(value);
      return v8::Intercepted::kYes;
    }

    v8::Intercepted StoragePropertyQuery(
        v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Integer>& info)
    {
      if (!name->IsString())
        return v8::Intercepted::kNo;
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string key = ToUtf8(isolate, name);
      if (IsReservedStorageName(key))
        return v8::Intercepted::kNo;
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      if (StorageIndexOf(storage, key) < 0)
        return v8::Intercepted::kNo;
      info.GetReturnValue().Set(
          static_cast<int32_t>(v8::PropertyAttribute::None));
      return v8::Intercepted::kYes;
    }

    v8::Intercepted StoragePropertyDeleter(
        v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Boolean>& info)
    {
      if (!name->IsString())
        return v8::Intercepted::kNo;
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string key = ToUtf8(isolate, name);
      if (IsReservedStorageName(key))
        return v8::Intercepted::kNo;
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      int idx = StorageIndexOf(storage, key);
      if (idx < 0)
        return v8::Intercepted::kNo;
      storage.erase(storage.begin() + idx);
      info.GetReturnValue().Set(true);
      return v8::Intercepted::kYes;
    }

    void StoragePropertyEnumerator(
        const v8::PropertyCallbackInfo<v8::Array>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      v8::Local<v8::Array> names = v8::Array::New(isolate);
      for (size_t i = 0; i < storage.size(); i++)
      {
        (void)names
            ->Set(context, static_cast<uint32_t>(i),
                  ToV8Str(isolate, storage[i].first))
            .FromMaybe(false);
      }
      info.GetReturnValue().Set(names);
    }

    void StorageGetItem(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      std::string key = ToUtf8(isolate, info[0]);
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      int idx = StorageIndexOf(storage, key);
      if (idx < 0)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(ToV8Str(isolate, storage[idx].second));
    }

    void StorageSetItem(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2)
        return;
      std::string key = ToUtf8(isolate, info[0]);
      std::string val = ToUtf8(isolate, info[1]);
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      int idx = StorageIndexOf(storage, key);
      if (idx >= 0)
        storage[idx].second = val;
      else
        storage.push_back({key, val});
    }

    void StorageRemoveItem(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1)
        return;
      std::string key = ToUtf8(isolate, info[0]);
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      int idx = StorageIndexOf(storage, key);
      if (idx >= 0)
        storage.erase(storage.begin() + idx);
    }

    void StorageClear(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      StorageFor(isolate, context,
                 KindForReceiver(isolate, context, info.This(),
                                 KindFromData(info.Data()))).clear();
    }

    void StorageKey(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsNumber())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      int32_t index = info[0]->Int32Value(context).FromMaybe(0);
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      if (index < 0 || static_cast<size_t>(index) >= storage.size())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(
          ToV8Str(isolate, storage[static_cast<size_t>(index)].first));
    }

    void StorageLength(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      StorageMap& storage =
          StorageFor(isolate, context,
                     KindForReceiver(isolate, context, info.This(),
                                     KindFromData(info.Data())));
      info.GetReturnValue().Set(static_cast<uint32_t>(storage.size()));
    }

    void StorageIllegalConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetIsolate()->ThrowException(
          ToV8Str(info.GetIsolate(), "Illegal constructor"));
    }

    v8::Local<v8::FunctionTemplate> MakeStorageTemplate(
        v8::Isolate* isolate, v8::Local<v8::Context> context)
    {
      v8::Local<v8::External> data = v8::External::New(isolate, nullptr);
      v8::Local<v8::FunctionTemplate> tpl =
          v8::FunctionTemplate::New(isolate, StorageIllegalConstructor, data);
      tpl->SetClassName(ToV8Str(isolate, "Storage"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Storage"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      tpl->PrototypeTemplate()->Set(v8::Symbol::GetToStringTag(isolate),
                                    ToV8Str(isolate, "Storage"),
                                    v8::PropertyAttribute::ReadOnly);
      inst->SetHandler(v8::NamedPropertyHandlerConfiguration(
          StoragePropertyGetter, StoragePropertySetter, StoragePropertyQuery,
          StoragePropertyDeleter, StoragePropertyEnumerator, data,
          v8::PropertyHandlerFlags::kNone));
      // reference's Storage.prototype enumeration order is observable to the
      // collector: length, clear, getItem, key, removeItem, setItem.
      {
        v8::Local<v8::String> key = ToV8Str(isolate, "length");
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, StorageLength, data);
        fn->SetClassName(ToV8Str(isolate, "get length"));
        tpl->PrototypeTemplate()->SetAccessorProperty(
            key, fn, v8::Local<v8::FunctionTemplate>());
      }
      // The shared template's callbacks select a backing map from |this|.
      auto method = [&](const char* name, v8::FunctionCallback cb, int length) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb, data);
        fn->SetClassName(key);
        fn->SetLength(length);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      method("clear", StorageClear, 0);
      method("getItem", StorageGetItem, 1);
      method("key", StorageKey, 1);
      method("removeItem", StorageRemoveItem, 1);
      method("setItem", StorageSetItem, 2);
      return tpl;
    }
    } // namespace

  void InstallStorage(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> profile)
  {
    ContextState* state = EnsureState(isolate, context);
    state->local_storage.clear();
    state->session_storage.clear();

    // navigator.storage profile values (quota/usage/persistent)
    {
      v8::Local<v8::Value> cfg;
      if (profile
              ->Get(context, ToV8Str(isolate, "storage"))
              .ToLocal(&cfg) &&
          cfg->IsObject())
      {
        v8::Local<v8::Value> field;
        if (cfg.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "quota"))
                .ToLocal(&field) &&
            field->IsNumber())
          state->storage_quota = field->NumberValue(context).FromMaybe(0.0);
        if (cfg.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "usage"))
                .ToLocal(&field) &&
            field->IsNumber())
          state->storage_usage = field->NumberValue(context).FromMaybe(0.0);
        if (cfg.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "persistent"))
                .ToLocal(&field))
          state->storage_persistent = field->BooleanValue(isolate);
      }
    }

    // pre-configured values from the facade profile ("localStorage" /
    // "sessionStorage" JSON objects); insertion order is preserved
    const char* keys[2] = {"localStorage", "sessionStorage"};
    for (int kind = 0; kind < 2; kind++)
    {
      v8::Local<v8::Value> cfg;
      if (!profile->Get(context, ToV8Str(isolate, keys[kind])).ToLocal(&cfg) ||
          !cfg->IsObject())
        continue;
      StorageMap& storage = kind == 0 ? state->local_storage
                                      : state->session_storage;
      v8::Local<v8::Object> cfg_obj = cfg.As<v8::Object>();
      v8::Local<v8::Array> names =
          cfg_obj->GetOwnPropertyNames(context).ToLocalChecked();
      for (uint32_t i = 0; i < names->Length(); i++)
      {
        v8::Local<v8::Value> key =
            names->Get(context, i).FromMaybe(v8::Local<v8::Value>());
        v8::Local<v8::Value> value;
        if (!key->IsString() ||
            !cfg_obj->Get(context, key).ToLocal(&value))
          continue;
        storage.push_back({ToUtf8(isolate, key), ToUtf8(isolate, value)});
      }
    }

    v8::Local<v8::FunctionTemplate> storage_tpl =
        MakeStorageTemplate(isolate, context);
    v8::Local<v8::Function> storage_ctor =
        storage_tpl->GetFunction(context).ToLocalChecked();
    v8::Local<v8::Value> storage_proto;
    if (storage_ctor->Get(context, ToV8Str(isolate, "prototype"))
            .ToLocal(&storage_proto))
      (void)storage_ctor
          ->DefineOwnProperty(context, ToV8Str(isolate, "prototype"),
                              storage_proto,
                              v8::PropertyAttribute::ReadOnly)
          .FromMaybe(false);
    (void)context
        ->Global()
        ->DefineOwnProperty(context, ToV8Str(isolate, "Storage"), storage_ctor,
                            static_cast<v8::PropertyAttribute>(
                                v8::PropertyAttribute::DontEnum))
        .FromMaybe(false);
    v8::Local<v8::ObjectTemplate> storage_inst =
        storage_tpl->InstanceTemplate();
    v8::Local<v8::Object> local_storage =
        storage_inst->NewInstance(context).ToLocalChecked();
    v8::Local<v8::Object> session_storage =
        storage_inst->NewInstance(context).ToLocalChecked();
    state->local_storage_object.Reset(isolate, local_storage);
    state->session_storage_object.Reset(isolate, session_storage);

    (void)context
        ->Global()
        ->DefineOwnProperty(context, ToV8Str(isolate, "localStorage"),
                            local_storage,
                            static_cast<v8::PropertyAttribute>(0))
        .FromMaybe(false);
    (void)context
        ->Global()
        ->DefineOwnProperty(context, ToV8Str(isolate, "sessionStorage"),
                            session_storage,
                            static_cast<v8::PropertyAttribute>(0))
        .FromMaybe(false);

    InstallCookieStore(isolate, context);
    InstallBucketsAndCredentials(isolate, context);
  }

  // ------------------------------------------------------------ cookie jar

  namespace
    {
    std::string TrimCopy(const std::string& text)
    {
      size_t begin = text.find_first_not_of(" \t\r\n");
      if (begin == std::string::npos)
        return "";
      size_t end = text.find_last_not_of(" \t\r\n");
      return text.substr(begin, end - begin + 1);
    }

    // RFC 1123 "Wdy, DD Mon YYYY HH:MM:SS GMT" (weekday and the legacy
    // dashed forms tolerated); month names are ASCII so no locale needed.
    // Returns false when the text is not a parsable cookie date.
    bool ParseHttpDate(const std::string& text, std::tm* out)
    {
      static const char* const kMonths[] = {
          "Jan", "Feb", "Mar", "Apr", "May", "Jun",
          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      std::string body = text;
      size_t comma = body.find(',');
      if (comma != std::string::npos)
        body = TrimCopy(body.substr(comma + 1));
      std::istringstream in(body);
      std::string month;
      int day = 0, year = 0, hour = 0, minute = 0, second = 0;
      char colon1 = '\0', colon2 = '\0';
      in >> day;
      in >> month;
      in >> year;
      in >> hour;
      in >> colon1;
      in >> minute;
      in >> colon2;
      in >> second;
      if (in.fail() || colon1 != ':' || colon2 != ':')
        return false;
      int month_index = -1;
      for (int i = 0; i < 12; i++)
      {
        if (month.size() >= 3 &&
            month.compare(0, 3, kMonths[i]) == 0)
        {
          month_index = i;
          break;
        }
      }
      if (month_index < 0)
        return false;
      if (year < 70)
        year += 2000;
      else if (year < 100)
        year += 1900;
      out->tm_year = year - 1900;
      out->tm_mon = month_index;
      out->tm_mday = day;
      out->tm_hour = hour;
      out->tm_min = minute;
      out->tm_sec = second;
      out->tm_isdst = 0;
      return true;
    }
    } // namespace

  std::string GetCookieString(v8::Isolate* isolate,
                              v8::Local<v8::Context> context)
  {
    (void)isolate;
    ContextState* state = GetState(context);
    if (state == nullptr)
      return "";
    std::string out;
    for (const auto& cookie : state->cookies)
    {
      if (!out.empty())
        out += "; ";
      out += cookie.name + "=" + cookie.value;
    }
    return out;
  }

  void SetCookieFromDocument(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             const std::string& text)
  {
    ContextState* state = GetState(context);
    if (state == nullptr)
      return;

    // first segment is "name=value"; the rest are attributes
    size_t semi = text.find(';');
    std::string pair = TrimCopy(semi == std::string::npos ? text
                                                          : text.substr(0, semi));
    size_t eq = pair.find('=');
    if (eq == std::string::npos || eq == 0)
      return; // browsers ignore invalid set-cookie strings
    std::string name = TrimCopy(pair.substr(0, eq));
    std::string value = TrimCopy(pair.substr(eq + 1));

    bool remove = false;
    std::string path = "/";
    size_t pos = semi;
    while (pos != std::string::npos)
    {
      size_t next = text.find(';', pos + 1);
      std::string attr = TrimCopy(
          text.substr(pos + 1,
                      next == std::string::npos ? std::string::npos
                                                : next - pos - 1));
      size_t aeq = attr.find('=');
      std::string key = TrimCopy(aeq == std::string::npos
                                     ? attr
                                     : attr.substr(0, aeq));
      std::string val = aeq == std::string::npos
                            ? ""
                            : TrimCopy(attr.substr(aeq + 1));
      for (auto& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (key == "max-age")
      {
        // "0" (or negative) deletes; a non-numeric value is ignored
        if (!val.empty())
        {
          char* endp = nullptr;
          long seconds = std::strtol(val.c_str(), &endp, 10);
          if (endp != val.c_str())
            remove = seconds <= 0;
        }
      }
      else if (key == "expires" && !val.empty())
      {
        // lv8 fork patch (G4.92): the delete form
        // "name=; expires=<past-date>" must remove the entry.  Parse the
        // RFC 1123 date WAFs emit ("Tue, 22 Sep 2026 19:25:33 GMT", and
        // the legacy no-weekday form); an unparsable date is ignored the
        // way browsers ignore it (the old cookie survives).
        std::tm tmv{};
        if (ParseHttpDate(val, &tmv))
        {
#if defined(_WIN32) || defined(_WIN64)
          std::time_t when = _mkgmtime(&tmv);
#else
          std::time_t when = timegm(&tmv);
#endif
          if (when != static_cast<std::time_t>(-1))
            remove = when <= std::time(nullptr);
        }
      }
      else if (key == "path" && !val.empty())
      {
        path = val;
      }
      // "Expires" parsing arrives with the network bridge (G3); "Secure",
      // "HttpOnly" and "SameSite" are stored implicitly by keeping the
      // entry (the oracle only exercises removal via Max-Age).
      pos = next;
    }

    // replace-or-append by name (single origin context: name is unique)
    for (size_t i = 0; i < state->cookies.size(); i++)
    {
      if (state->cookies[i].name == name)
      {
        state->cookies.erase(state->cookies.begin() + i);
        break;
      }
    }
    if (!remove)
      state->cookies.push_back({name, value, path});
  }

  // ------------------------------------------------------ cookie store

  namespace
    {
    // {name, value, path, secure, sameSite} cookie info object
    v8::Local<v8::Object> MakeCookieInfo(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context,
                                         const ContextState::Cookie& cookie)
    {
      v8::Local<v8::Object> obj = v8::Object::New(isolate);
      (void)obj->Set(context, ToV8Str(isolate, "name"),
                     ToV8Str(isolate, cookie.name)).FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "value"),
                     ToV8Str(isolate, cookie.value)).FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "path"),
                     ToV8Str(isolate, cookie.path)).FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "secure"),
                     v8::Boolean::New(isolate, false)).FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "sameSite"),
                     ToV8Str(isolate, "lax")).FromMaybe(false);
      return obj;
    }

    v8::Local<v8::Object> MakeCookieChangeEvent(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::vector<ContextState::Cookie>& changed,
        const std::vector<ContextState::Cookie>& deleted)
    {
      auto list = [](v8::Isolate* iso, v8::Local<v8::Context> ctx,
                     const std::vector<ContextState::Cookie>& cookies) {
        v8::Local<v8::Array> arr = v8::Array::New(iso);
        for (size_t i = 0; i < cookies.size(); i++)
          (void)arr->Set(ctx, static_cast<uint32_t>(i),
                         MakeCookieInfo(iso, ctx, cookies[i]))
              .FromMaybe(false);
        return arr;
      };
      v8::Local<v8::Object> obj = v8::Object::New(isolate);
      (void)obj->Set(context, ToV8Str(isolate, "changed"),
                     list(isolate, context, changed)).FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "deleted"),
                     list(isolate, context, deleted)).FromMaybe(false);
      return obj;
    }

    // resolve |name| from the jar
    const ContextState::Cookie* JarFind(ContextState* state,
                                        const std::string& name)
    {
      for (const auto& cookie : state->cookies)
      {
        if (cookie.name == name)
          return &cookie;
      }
      return nullptr;
    }

    // fire onchange with |changed|/|deleted| when a handler is set
    void FireCookieChange(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> self,
                          const std::vector<ContextState::Cookie>& changed,
                          const std::vector<ContextState::Cookie>& deleted)
    {
      v8::Local<v8::Value> handler;
      if (self->Get(context, ToV8Str(isolate, "onchange")).ToLocal(&handler) &&
          handler->IsFunction())
      {
        v8::Local<v8::Value> argv[1] = {MakeCookieChangeEvent(
            isolate, context, changed, deleted)};
        v8::TryCatch try_catch(isolate);
        (void)handler.As<v8::Function>()
            ->Call(context, self, 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        if (try_catch.HasCaught())
          CJavascriptException::ThrowIf(isolate, try_catch);
      }
    }
    } // namespace

  void InstallCookieStore(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    // global cookieStore: async face over the shared cookie jar
    v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
    tpl->SetClassName(ToV8Str(isolate, "CookieStore"));
    v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
    inst->SetInternalFieldCount(0);
    // M10: zero own symbols on instances (tag on prototype)
    tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "CookieStore"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));
    auto method = [&](const char* name, v8::FunctionCallback cb) {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, cb);
      fn->SetClassName(key);
      tpl->PrototypeTemplate()->Set(key, fn);
    };

    // set(name|opts, value?): writes the jar then fires onchange
    method("set", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      ContextState* st = GetState(ctx);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(ctx).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      if (st == nullptr || info.Length() < 1)
      {
        info.GetReturnValue().Set(promise);
        return;
      }
      std::string name;
      std::string value;
      std::string path = "/";
      if (info[0]->IsObject())
      {
        v8::Local<v8::Object> opts = info[0].As<v8::Object>();
        v8::Local<v8::Value> field;
        if (opts->Get(ctx, ToV8Str(iso, "name")).ToLocal(&field) &&
            field->IsString())
          name = ToUtf8(iso, field);
        if (opts->Get(ctx, ToV8Str(iso, "value")).ToLocal(&field) &&
            field->IsString())
          value = ToUtf8(iso, field);
        if (opts->Get(ctx, ToV8Str(iso, "path")).ToLocal(&field) &&
            field->IsString())
          path = ToUtf8(iso, field);
      }
      else if (info[0]->IsString())
      {
        name = ToUtf8(iso, info[0]);
        if (info.Length() > 1 && info[1]->IsString())
          value = ToUtf8(iso, info[1]);
      }
      if (name.empty())
      {
        info.GetReturnValue().Set(promise);
        return;
      }
      // replace-or-append by name
      std::vector<ContextState::Cookie> changed;
      for (size_t i = 0; i < st->cookies.size(); i++)
      {
        if (st->cookies[i].name == name)
        {
          changed.push_back(st->cookies[i]);
          changed.back().value = value;
          st->cookies[i].value = value;
          break;
        }
      }
      if (changed.empty())
      {
        st->cookies.push_back({name, value, path});
        changed.push_back({name, value, path});
      }
      FireCookieChange(iso, ctx, info.This(), changed, {});
      (void)resolver->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
      info.GetReturnValue().Set(promise);
    });

    // get(name): cookie info or null
    method("get", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      ContextState* st = GetState(ctx);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(ctx).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      std::string name;
      if (info.Length() >= 1 && info[0]->IsString())
        name = ToUtf8(iso, info[0]);
      const ContextState::Cookie* hit =
          st == nullptr || name.empty() ? nullptr : JarFind(st, name);
      if (hit == nullptr)
        (void)resolver->Resolve(ctx, v8::Null(iso)).FromMaybe(false);
      else
        (void)resolver
            ->Resolve(ctx, MakeCookieInfo(iso, ctx, *hit))
            .FromMaybe(false);
      info.GetReturnValue().Set(promise);
    });

    // has(name): boolean
    method("has", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      ContextState* st = GetState(ctx);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(ctx).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      std::string name;
      if (info.Length() >= 1 && info[0]->IsString())
        name = ToUtf8(iso, info[0]);
      bool found = st != nullptr && !name.empty() &&
                   JarFind(st, name) != nullptr;
      (void)resolver->Resolve(ctx, v8::Boolean::New(iso, found))
          .FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    });

    // delete(name): removes from the jar and fires onchange
    method("delete", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      ContextState* st = GetState(ctx);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(ctx).ToLocalChecked();
      std::string name;
      if (info.Length() >= 1 && info[0]->IsString())
        name = ToUtf8(iso, info[0]);
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      std::vector<ContextState::Cookie> deleted;
      if (st != nullptr && !name.empty())
      {
        for (size_t i = 0; i < st->cookies.size(); i++)
        {
          if (st->cookies[i].name == name)
          {
            deleted.push_back(st->cookies[i]);
            st->cookies.erase(st->cookies.begin() + i);
            break;
          }
        }
      }
      if (!deleted.empty())
        FireCookieChange(iso, ctx, info.This(), {}, deleted);
      (void)resolver->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
      info.GetReturnValue().Set(promise);
    });

    // getAll(name?): every matching cookie info
    method("getAll", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      ContextState* st = GetState(ctx);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(ctx).ToLocalChecked();
      v8::Local<v8::Array> arr = v8::Array::New(iso);
      if (st != nullptr)
      {
        std::string wanted = info.Length() >= 1 && info[0]->IsString()
                                 ? ToUtf8(iso, info[0])
                                 : std::string();
        uint32_t n = 0;
        for (const auto& cookie : st->cookies)
        {
          if (!wanted.empty() && cookie.name != wanted)
            continue;
          (void)arr->Set(ctx, n++, MakeCookieInfo(iso, ctx, cookie))
              .FromMaybe(false);
        }
      }
      (void)resolver->Resolve(ctx, arr).FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    });

    v8::Local<v8::Object> cookie_store =
        inst->NewInstance(context).ToLocalChecked();
    (void)context
        ->Global()
        ->DefineOwnProperty(context, ToV8Str(isolate, "cookieStore"),
                            cookie_store,
                            static_cast<v8::PropertyAttribute>(0))
        .FromMaybe(false);

    // global StorageManager + navigator.storage
    v8::Local<v8::FunctionTemplate> sm_tpl = v8::FunctionTemplate::New(isolate);
    sm_tpl->SetClassName(ToV8Str(isolate, "StorageManager"));
    v8::Local<v8::ObjectTemplate> sm_inst = sm_tpl->InstanceTemplate();
    sm_inst->SetInternalFieldCount(0);
    // M10: zero own symbols on instances (tag on prototype)
    sm_tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "StorageManager"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));
    auto sm_method = [&](const char* name, v8::FunctionCallback cb) {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, cb);
      fn->SetClassName(key);
      sm_tpl->PrototypeTemplate()->Set(key, fn);
    };
    // estimate(): {quota, usage} from the profile
    sm_method("estimate", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      ContextState* st = GetState(ctx);
      double quota = st != nullptr ? st->storage_quota : 2147483648.0;
      double usage = st != nullptr ? st->storage_usage : 0.0;
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(ctx).ToLocalChecked();
      v8::Local<v8::Object> estimate = v8::Object::New(iso);
      (void)estimate->Set(ctx, ToV8Str(iso, "quota"),
                          v8::Number::New(iso, quota)).FromMaybe(false);
      (void)estimate->Set(ctx, ToV8Str(iso, "usage"),
                          v8::Number::New(iso, usage)).FromMaybe(false);
      (void)resolver->Resolve(ctx, estimate).FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    });
    // persisted()/persist(): profile-driven boolean promises
    sm_method("persisted",
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                ContextState* st = GetState(ctx);
                bool value = st != nullptr ? st->storage_persistent : false;
                v8::Local<v8::Promise::Resolver> resolver =
                    v8::Promise::Resolver::New(ctx).ToLocalChecked();
                (void)resolver
                    ->Resolve(ctx, v8::Boolean::New(iso, value))
                    .FromMaybe(false);
                info.GetReturnValue().Set(resolver->GetPromise());
              });
    sm_method("persist",
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                ContextState* st = GetState(ctx);
                if (st != nullptr)
                  st->storage_persistent = true;
                v8::Local<v8::Promise::Resolver> resolver =
                    v8::Promise::Resolver::New(ctx).ToLocalChecked();
                (void)resolver
                    ->Resolve(ctx, v8::Boolean::New(iso, true))
                    .FromMaybe(false);
                info.GetReturnValue().Set(resolver->GetPromise());
              });
    v8::Local<v8::Function> sm_ctor =
        sm_tpl->GetFunction(context).ToLocalChecked();
    (void)context
        ->Global()
        ->DefineOwnProperty(context,
                            ToV8Str(isolate, "StorageManager"), sm_ctor,
                            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
        .FromMaybe(false);
    // navigator.storage: own property on the navigator host object
    v8::Local<v8::Value> navigator;
    if (context->Global()
            ->Get(context, ToV8Str(isolate, "navigator"))
            .ToLocal(&navigator) &&
        navigator->IsObject())
    {
      v8::Local<v8::Object> storage_obj =
          sm_inst->NewInstance(context).ToLocalChecked();
      (void)navigator.As<v8::Object>()
          ->DefineOwnProperty(context, ToV8Str(isolate, "storage"),
                              storage_obj,
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
  }

  // set the callback return value from a lambda body
  void info_get_return(const v8::FunctionCallbackInfo<v8::Value>& info,
                       v8::Local<v8::Value> value)
  {
    info.GetReturnValue().Set(value);
  }

  // ----------------------------------- storage buckets + credentials

  namespace
    {
    v8::Local<v8::Private> RespKey(v8::Isolate* isolate, const char* name)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, name));
    }

    ContextState::BucketStore* BucketFind(ContextState* state,
                                          const std::string& name)
    {
      for (auto& bucket : state->buckets)
      {
        if (bucket.name == name)
          return &bucket;
      }
      return nullptr;
    }

    // a bucket's cache namespace: open/match/keys over bucket->caches
    void BucketCachesOpen(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      ContextState* st = GetState(ctx);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(ctx).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      v8::Local<v8::Value> bucket_name_v;
      std::string bucket_name;
      if (info.This()
              ->GetPrivate(ctx, RespKey(iso, "lv8.buckets.name"))
              .ToLocal(&bucket_name_v) &&
          bucket_name_v->IsString())
        bucket_name = ToUtf8(iso, bucket_name_v);
      ContextState::BucketStore* bucket =
          st != nullptr ? BucketFind(st, bucket_name) : nullptr;
      if (bucket == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().Set(promise);
        return;
      }
      std::string cache_name = ToUtf8(iso, info[0]);
      bucket->caches[cache_name];

      // a Cache bound to (bucket, cache) through Private symbols
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(iso);
      tpl->SetClassName(ToV8Str(iso, "Cache"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(iso),
                ToV8Str(iso, "Cache"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(iso, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(iso, cb);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      // put(url, response): body/headers ride on the response Privates
      method("put", [](const v8::FunctionCallbackInfo<v8::Value>& cb_info) {
        v8::Isolate* iso2 = cb_info.GetIsolate();
        v8::Local<v8::Context> ctx2 = iso2->GetCurrentContext();
        ContextState* st2 = GetState(ctx2);
        v8::Local<v8::Promise::Resolver> res =
            v8::Promise::Resolver::New(ctx2).ToLocalChecked();
        v8::Local<v8::Promise> p = res->GetPromise();
        std::string bname;
        std::string cname;
        v8::Local<v8::Value> bv;
        v8::Local<v8::Value> cv;
        if (cb_info.This()
                ->GetPrivate(ctx2, RespKey(iso2, "lv8.cache.bucket"))
                .ToLocal(&bv) &&
            bv->IsString())
          bname = ToUtf8(iso2, bv);
        if (cb_info.This()
                ->GetPrivate(ctx2, RespKey(iso2, "lv8.cache.name"))
                .ToLocal(&cv) &&
            cv->IsString())
          cname = ToUtf8(iso2, cv);
        ContextState::BucketStore* b =
            st2 != nullptr ? BucketFind(st2, bname) : nullptr;
        if (b == nullptr || cb_info.Length() < 2 ||
            !cb_info[0]->IsString() || !cb_info[1]->IsObject())
        {
          info_get_return(cb_info, p);
          return;
        }
        std::string url = ToUtf8(iso2, cb_info[0]);
        v8::Local<v8::Object> response = cb_info[1].As<v8::Object>();
        v8::Local<v8::Value> body;
        if (!response
                 ->GetPrivate(ctx2, RespKey(iso2, "lv8.resp.body"))
                 .ToLocal(&body) ||
            !body->IsString())
          body = v8::String::Empty(iso2);
        ContextState::CacheEntry entry;
        entry.url = url;
        entry.body = ToUtf8(iso2, body);
        v8::Local<v8::Value> raw;
        if (response
                ->GetPrivate(ctx2, RespKey(iso2, "lv8.resp.headers"))
                .ToLocal(&raw) &&
            raw->IsArray())
        {
          v8::Local<v8::Array> list = raw.As<v8::Array>();
          for (uint32_t i = 0; i < list->Length(); i++)
          {
            v8::Local<v8::Value> pair =
                list->Get(ctx2, i).FromMaybe(v8::Local<v8::Value>());
            if (!pair->IsArray())
              continue;
            v8::Local<v8::Value> k = pair.As<v8::Array>()
                                         ->Get(ctx2, 0)
                                         .FromMaybe(v8::Local<v8::Value>());
            v8::Local<v8::Value> v = pair.As<v8::Array>()
                                         ->Get(ctx2, 1)
                                         .FromMaybe(v8::Local<v8::Value>());
            if (k->IsString() && v->IsString())
              entry.headers.push_back(
                  {ToUtf8(iso2, k), ToUtf8(iso2, v)});
          }
        }
        auto& entries = b->caches[cname];
        bool replaced = false;
        for (auto& e : entries)
        {
          if (e.url == url)
          {
            e = entry;
            replaced = true;
            break;
          }
        }
        if (!replaced)
          entries.push_back(entry);
        (void)res->Resolve(ctx2, v8::Undefined(iso2)).FromMaybe(false);
        info_get_return(cb_info, p);
      });
      // match(url): response built from the stored entry
      method("match", [](const v8::FunctionCallbackInfo<v8::Value>& cb_info) {
        v8::Isolate* iso2 = cb_info.GetIsolate();
        v8::Local<v8::Context> ctx2 = iso2->GetCurrentContext();
        ContextState* st2 = GetState(ctx2);
        v8::Local<v8::Promise::Resolver> res =
            v8::Promise::Resolver::New(ctx2).ToLocalChecked();
        v8::Local<v8::Promise> p = res->GetPromise();
        std::string bname;
        std::string cname;
        v8::Local<v8::Value> bv;
        v8::Local<v8::Value> cv;
        if (cb_info.This()
                ->GetPrivate(ctx2, RespKey(iso2, "lv8.cache.bucket"))
                .ToLocal(&bv) &&
            bv->IsString())
          bname = ToUtf8(iso2, bv);
        if (cb_info.This()
                ->GetPrivate(ctx2, RespKey(iso2, "lv8.cache.name"))
                .ToLocal(&cv) &&
            cv->IsString())
          cname = ToUtf8(iso2, cv);
        const ContextState::CacheEntry* hit = nullptr;
        if (st2 != nullptr && cb_info.Length() >= 1 &&
            cb_info[0]->IsString())
        {
          ContextState::BucketStore* b = BucketFind(st2, bname);
          if (b != nullptr)
          {
            auto cit = b->caches.find(cname);
            if (cit != b->caches.end())
            {
              std::string url = ToUtf8(iso2, cb_info[0]);
              for (const auto& e : cit->second)
              {
                if (e.url == url)
                {
                  hit = &e;
                  break;
                }
              }
            }
          }
        }
        if (hit == nullptr)
          (void)res->Resolve(ctx2, v8::Undefined(iso2)).FromMaybe(false);
        else
          (void)res
              ->Resolve(ctx2, MakeResponse(iso2, ctx2, 200, hit->headers,
                                           hit->body))
              .FromMaybe(false);
        info_get_return(cb_info, p);
      });
      // keys(): urls in this bucket cache
      method("keys", [](const v8::FunctionCallbackInfo<v8::Value>& cb_info) {
        v8::Isolate* iso2 = cb_info.GetIsolate();
        v8::Local<v8::Context> ctx2 = iso2->GetCurrentContext();
        ContextState* st2 = GetState(ctx2);
        v8::Local<v8::Promise::Resolver> res =
            v8::Promise::Resolver::New(ctx2).ToLocalChecked();
        v8::Local<v8::Array> urls = v8::Array::New(iso2);
        std::string bname;
        std::string cname;
        v8::Local<v8::Value> bv;
        v8::Local<v8::Value> cv;
        if (cb_info.This()
                ->GetPrivate(ctx2, RespKey(iso2, "lv8.cache.bucket"))
                .ToLocal(&bv) &&
            bv->IsString())
          bname = ToUtf8(iso2, bv);
        if (cb_info.This()
                ->GetPrivate(ctx2, RespKey(iso2, "lv8.cache.name"))
                .ToLocal(&cv) &&
            cv->IsString())
          cname = ToUtf8(iso2, cv);
        if (st2 != nullptr)
        {
          ContextState::BucketStore* b = BucketFind(st2, bname);
          if (b != nullptr)
          {
            auto cit = b->caches.find(cname);
            if (cit != b->caches.end())
            {
              uint32_t n = 0;
              for (const auto& e : cit->second)
                (void)urls->Set(ctx2, n++, ToV8Str(iso2, e.url))
                    .FromMaybe(false);
            }
          }
        }
        (void)res->Resolve(ctx2, urls).FromMaybe(false);
        info_get_return(cb_info, res->GetPromise());
      });

      v8::Local<v8::Object> cache = inst->NewInstance(ctx).ToLocalChecked();
      (void)cache
          ->SetPrivate(ctx, RespKey(iso, "lv8.cache.bucket"),
                       ToV8Str(iso, bucket_name))
          .FromMaybe(false);
      (void)cache
          ->SetPrivate(ctx, RespKey(iso, "lv8.cache.name"),
                       ToV8Str(iso, cache_name))
          .FromMaybe(false);
      (void)resolver->Resolve(ctx, cache).FromMaybe(false);
      info.GetReturnValue().Set(promise);
    }
    } // namespace

  void InstallBucketsAndCredentials(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context)
  {
    // global StorageBucket constructor (instanceof target)
    v8::Local<v8::FunctionTemplate> sb_tpl = v8::FunctionTemplate::New(isolate);
    sb_tpl->SetClassName(ToV8Str(isolate, "StorageBucket"));
    (void)context
        ->Global()
        ->DefineOwnProperty(context, ToV8Str(isolate, "StorageBucket"),
                            sb_tpl->GetFunction(context).ToLocalChecked(),
                            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
        .FromMaybe(false);

    // navigator.storageBuckets: open/keys/delete over per-context buckets
    v8::Local<v8::Value> navigator;
    if (context->Global()
            ->Get(context, ToV8Str(isolate, "navigator"))
            .ToLocal(&navigator) &&
        navigator->IsObject())
    {
      // manager surface
      v8::Local<v8::FunctionTemplate> mgr_tpl =
          v8::FunctionTemplate::New(isolate);
      mgr_tpl->SetClassName(ToV8Str(isolate, "StorageBucketManager"));
      v8::Local<v8::ObjectTemplate> mgr_inst = mgr_tpl->InstanceTemplate();
      mgr_inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      mgr_tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "StorageBucketManager"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto mgr_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        mgr_tpl->PrototypeTemplate()->Set(key, fn);
      };
      // open(name, opts): create-or-return a bucket object
      mgr_method("open",
                 [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                   v8::Isolate* iso = info.GetIsolate();
                   v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                   ContextState* st = GetState(ctx);
                   v8::Local<v8::Promise::Resolver> resolver =
                       v8::Promise::Resolver::New(ctx).ToLocalChecked();
                   v8::Local<v8::Promise> promise = resolver->GetPromise();
                   if (st == nullptr || info.Length() < 1 ||
                       !info[0]->IsString())
                   {
                     info.GetReturnValue().Set(promise);
                     return;
                   }
                   std::string name = ToUtf8(iso, info[0]);
                   std::string durability = "relaxed";
                   if (info.Length() > 1 && info[1]->IsObject())
                   {
                     v8::Local<v8::Value> field;
                     if (info[1]
                             .As<v8::Object>()
                             ->Get(ctx, ToV8Str(iso, "durability"))
                             .ToLocal(&field) &&
                         field->IsString())
                       durability = ToUtf8(iso, field);
                   }
                   ContextState::BucketStore* bucket =
                       BucketFind(st, name);
                   if (bucket == nullptr)
                   {
                     st->buckets.push_back({name, durability, {}});
                     bucket = BucketFind(st, name);
                   }
                   // bucket object over a StorageBucket-derived template:
                   // instanceof works against the global constructor
                   v8::Local<v8::FunctionTemplate> tpl =
                       v8::FunctionTemplate::New(iso);
                   tpl->SetClassName(ToV8Str(iso, "StorageBucket"));
                   v8::Local<v8::ObjectTemplate> inst =
                       tpl->InstanceTemplate();
                   inst->SetInternalFieldCount(0);
                   // M10: zero own symbols on instances (tag on prototype)
                   tpl->PrototypeTemplate()
                       ->Set(v8::Symbol::GetToStringTag(iso),
                             ToV8Str(iso, "StorageBucket"),
                             static_cast<v8::PropertyAttribute>(
                                 v8::PropertyAttribute::ReadOnly |
                                 v8::PropertyAttribute::DontEnum));
                   v8::Local<v8::Object> bucket_obj =
                       inst->NewInstance(ctx).ToLocalChecked();
                   // point the prototype at StorageBucket.prototype so
                   // `bucket instanceof StorageBucket` holds (capturing
                   // the template in a plain function-pointer callback is
                   // impossible)
                   v8::Local<v8::Value> sb_ctor;
                   if (ctx->Global()
                           ->Get(ctx, ToV8Str(iso, "StorageBucket"))
                           .ToLocal(&sb_ctor) &&
                       sb_ctor->IsFunction())
                   {
                     v8::Local<v8::Value> sb_proto;
                     if (sb_ctor.As<v8::Function>()
                             ->Get(ctx, ToV8Str(iso, "prototype"))
                             .ToLocal(&sb_proto) &&
                         sb_proto->IsObject())
                       (void)bucket_obj
                           ->SetPrototype(ctx, sb_proto.As<v8::Object>())
                           .FromMaybe(false);
                   }
                   (void)bucket_obj
                       ->Set(ctx, ToV8Str(iso, "name"),
                             ToV8Str(iso, name))
                       .FromMaybe(false);
                   (void)bucket_obj
                       ->Set(ctx, ToV8Str(iso, "durability"),
                             ToV8Str(iso, durability))
                       .FromMaybe(false);
                   // bucket.caches: an isolated CacheStorage whose open()
                   // yields caches bound to this bucket
                   v8::Local<v8::FunctionTemplate> cs_tpl =
                       v8::FunctionTemplate::New(iso);
                   cs_tpl->SetClassName(ToV8Str(iso, "CacheStorage"));
                   v8::Local<v8::ObjectTemplate> cs_inst =
                       cs_tpl->InstanceTemplate();
                   cs_inst->SetInternalFieldCount(0);
                   // M10: zero own symbols on instances (tag on prototype)
                   cs_tpl->PrototypeTemplate()
                       ->Set(v8::Symbol::GetToStringTag(iso),
                             ToV8Str(iso, "CacheStorage"),
                             static_cast<v8::PropertyAttribute>(
                                 v8::PropertyAttribute::ReadOnly |
                                 v8::PropertyAttribute::DontEnum));
                   {
                     v8::Local<v8::String> key =
                         ToV8Str(iso, "open");
                     v8::Local<v8::FunctionTemplate> fn =
                         v8::FunctionTemplate::New(iso, BucketCachesOpen);
                     fn->SetClassName(key);
                     cs_tpl->PrototypeTemplate()->Set(key, fn);
                   }
                   v8::Local<v8::Object> caches =
                       cs_inst->NewInstance(ctx).ToLocalChecked();
                   (void)caches
                       ->SetPrivate(ctx,
                                    RespKey(iso, "lv8.buckets.name"),
                                    ToV8Str(iso, name))
                       .FromMaybe(false);
                   (void)bucket_obj
                       ->Set(ctx, ToV8Str(iso, "caches"), caches)
                       .FromMaybe(false);
                   (void)resolver->Resolve(ctx, bucket_obj).FromMaybe(false);
                   info.GetReturnValue().Set(resolver->GetPromise());
                 });
      // keys(): bucket names
      mgr_method("keys",
                 [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                   v8::Isolate* iso = info.GetIsolate();
                   v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                   ContextState* st = GetState(ctx);
                   v8::Local<v8::Promise::Resolver> resolver =
                       v8::Promise::Resolver::New(ctx).ToLocalChecked();
                   v8::Local<v8::Array> names = v8::Array::New(iso);
                   if (st != nullptr)
                   {
                     uint32_t n = 0;
                     for (const auto& bucket : st->buckets)
                       (void)names
                           ->Set(ctx, n++, ToV8Str(iso, bucket.name))
                           .FromMaybe(false);
                   }
                   (void)resolver->Resolve(ctx, names).FromMaybe(false);
                   info.GetReturnValue().Set(resolver->GetPromise());
                 });
      // delete(name): drop the bucket, report whether it existed
      mgr_method("delete",
                 [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                   v8::Isolate* iso = info.GetIsolate();
                   v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                   ContextState* st = GetState(ctx);
                   v8::Local<v8::Promise::Resolver> resolver =
                       v8::Promise::Resolver::New(ctx).ToLocalChecked();
                   bool existed = false;
                   if (st != nullptr && info.Length() >= 1 &&
                       info[0]->IsString())
                   {
                     std::string name = ToUtf8(iso, info[0]);
                     for (size_t i = 0; i < st->buckets.size(); i++)
                     {
                       if (st->buckets[i].name == name)
                       {
                         st->buckets.erase(st->buckets.begin() + i);
                         existed = true;
                         break;
                       }
                     }
                   }
                   (void)resolver
                       ->Resolve(ctx, v8::Boolean::New(iso, existed))
                       .FromMaybe(false);
                   info.GetReturnValue().Set(resolver->GetPromise());
                 });
      v8::Local<v8::Object> manager =
          mgr_inst->NewInstance(context).ToLocalChecked();
      (void)navigator.As<v8::Object>()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "storageBuckets"), manager,
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // global PasswordCredential + CredentialsContainer
    {
      v8::Local<v8::FunctionTemplate> pc_tpl =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                if (info.Length() >= 1 && info[0]->IsObject())
                {
                  v8::Local<v8::Object> opts = info[0].As<v8::Object>();
                  for (const char* field :
                       {"id", "password", "name", "type"})
                  {
                    v8::Local<v8::Value> value;
                    if (opts->Get(ctx, ToV8Str(iso, field)).ToLocal(&value) &&
                        !value->IsUndefined())
                      (void)info.This()
                          ->Set(ctx, ToV8Str(iso, field), value)
                          .FromMaybe(false);
                  }
                }
                v8::Local<v8::Value> type;
                if (!info.This()
                         ->Get(ctx, ToV8Str(iso, "type"))
                         .ToLocal(&type) ||
                    !type->IsString())
                  (void)info.This()
                      ->Set(ctx, ToV8Str(iso, "type"),
                            ToV8Str(iso, "password"))
                      .FromMaybe(false);
              });
      pc_tpl->SetClassName(ToV8Str(isolate, "PasswordCredential"));
      v8::Local<v8::ObjectTemplate> pc_inst = pc_tpl->InstanceTemplate();
      pc_inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      pc_tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "PasswordCredential"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "PasswordCredential"),
                              pc_tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);

      v8::Local<v8::FunctionTemplate> cc_tpl =
          v8::FunctionTemplate::New(isolate);
      cc_tpl->SetClassName(ToV8Str(isolate, "CredentialsContainer"));
      v8::Local<v8::ObjectTemplate> cc_inst = cc_tpl->InstanceTemplate();
      cc_inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      cc_tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "CredentialsContainer"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto cc_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        cc_tpl->PrototypeTemplate()->Set(key, fn);
      };
      cc_method("store", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        ContextState* st = GetState(ctx);
        v8::Local<v8::Promise::Resolver> resolver =
            v8::Promise::Resolver::New(ctx).ToLocalChecked();
        if (st != nullptr && info.Length() >= 1 && info[0]->IsObject())
          st->stored_credential.Reset(iso, info[0].As<v8::Object>());
        (void)resolver->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
        info.GetReturnValue().Set(resolver->GetPromise());
      });
      cc_method("get", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        ContextState* st = GetState(ctx);
        v8::Local<v8::Promise::Resolver> resolver =
            v8::Promise::Resolver::New(ctx).ToLocalChecked();
        if (st != nullptr && !st->stored_credential.IsEmpty())
          (void)resolver
              ->Resolve(ctx, st->stored_credential.Get(iso))
              .FromMaybe(false);
        else
          (void)resolver->Resolve(ctx, v8::Null(iso)).FromMaybe(false);
        info.GetReturnValue().Set(resolver->GetPromise());
      });
      v8::Local<v8::Function> cc_ctor =
          cc_tpl->GetFunction(context).ToLocalChecked();
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "CredentialsContainer"),
                              cc_ctor,
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
      v8::Local<v8::Value> navigator;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "navigator"))
              .ToLocal(&navigator) &&
          navigator->IsObject())
      {
        v8::Local<v8::Object> container =
            cc_inst->NewInstance(context).ToLocalChecked();
        (void)navigator.As<v8::Object>()
            ->DefineOwnProperty(context,
                                ToV8Str(isolate, "credentials"), container,
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
    }
  }

} // namespace LV8
