// lv8 G3: in-memory indexedDB and file system access handles. Both are
// fully deterministic and per-context: databases/records and the virtual
// file tree live in ContextState; callbacks settle when drain() runs the
// dispatch + microtask checkpoint.

#include "LV8.h"

#include "../Exception.h"

#include <cstring>
#include <string>
#include <cmath>
#include <vector>

namespace LV8
  {
  // forward: handle factory (public definition below)
  v8::Local<v8::Object> MakeFsHandle(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const std::string& path,
                                     const std::string& name, bool dir);

  namespace
    {
    ContextState::IdbTask PendingIdbTaskMake(
        v8::Isolate* iso, v8::Local<v8::Object> request, int kind,
        const std::string& db_name = "", double version = 0,
        bool need_upgrade = false)
    {
      ContextState::IdbTask task;
      task.request.Reset(iso, request);
      task.kind = kind;
      task.db_name = db_name;
      task.db_version = version;
      task.need_upgrade = need_upgrade;
      return task;
    }

    v8::Local<v8::Private> IdbKey(v8::Isolate* isolate, const char* name)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, name));
    }

    v8::Local<v8::Private> FsKey(v8::Isolate* isolate, const char* name)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, name));
    }

    v8::Local<v8::Object> MakeIdbRequest(v8::Isolate* isolate,
                                          v8::Local<v8::Context> context,
                                          bool open)
    {
      v8::Local<v8::Object> request = v8::Object::New(isolate);
      (void)request
          ->SetPrivate(context, IdbKey(isolate, "lv8.idb.rstate"),
                       ToV8Str(isolate, "pending"))
          .FromMaybe(false);
      v8::Local<v8::Value> ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate,
                                     open ? "IDBOpenDBRequest" : "IDBRequest"))
              .ToLocal(&ctor) && ctor->IsFunction())
      {
        v8::Local<v8::Value> proto;
        if (ctor.As<v8::Function>()
                ->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&proto) && proto->IsObject())
          (void)request->SetPrototype(context, proto.As<v8::Object>())
              .FromMaybe(false);
      }
      return request;
    }

    // IDB callbacks receive an Event whose target is the request itself.
    v8::Local<v8::Object> MakeIdbEvent(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> request,
                                       const char* handler,
                                       double old_version = 0.0,
                                       double new_version = 0.0)
    {
      v8::Local<v8::Object> event = v8::Object::New(isolate);
      const bool upgrade = std::strcmp(handler, "onupgradeneeded") == 0;
      const char* class_name = upgrade ? "IDBVersionChangeEvent" : "Event";
      v8::Local<v8::Value> ctor;
      if (context->Global()->Get(context, ToV8Str(isolate, class_name))
              .ToLocal(&ctor) && ctor->IsFunction())
      {
        v8::Local<v8::Value> proto;
        if (ctor.As<v8::Function>()->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&proto) && proto->IsObject())
          (void)event->SetPrototype(context, proto.As<v8::Object>())
              .FromMaybe(false);
      }
      (void)event->DefineOwnProperty(context, ToV8Str(isolate, "target"),
                                     request, v8::PropertyAttribute::DontEnum)
          .FromMaybe(false);
      (void)event->DefineOwnProperty(context, ToV8Str(isolate, "type"),
                                     ToV8Str(isolate, handler + 2),
                                     v8::PropertyAttribute::DontEnum)
          .FromMaybe(false);
      if (upgrade)
      {
        // IDBVersionChangeEvent carries the version transition (reference parity)
        (void)event
            ->DefineOwnProperty(context, ToV8Str(isolate, "oldVersion"),
                                v8::Number::New(isolate, old_version),
                                v8::PropertyAttribute::DontEnum)
            .FromMaybe(false);
        (void)event
            ->DefineOwnProperty(context, ToV8Str(isolate, "newVersion"),
                                v8::Number::New(isolate, new_version),
                                v8::PropertyAttribute::DontEnum)
            .FromMaybe(false);
      }
      return event;
    }

    void FireIdbHandler(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> request, const char* handler,
                        v8::Local<v8::Value> result, double old_version = 0.0,
                        double new_version = 0.0)
    {
      v8::Local<v8::Value> fn;
      if (request->Get(context, ToV8Str(isolate, handler)).ToLocal(&fn) &&
          fn->IsFunction())
      {
        // browsers settle IDBRequest.result/readyState before the success
        // event fires; WAF SDKs read request.result directly (not only
        // event.target.result)
        (void)request
            ->DefineOwnProperty(context, ToV8Str(isolate, "result"), result,
                v8::PropertyAttribute::DontEnum)
            .FromMaybe(false);
        (void)request
            ->DefineOwnProperty(context, ToV8Str(isolate, "readyState"),
                  ToV8Str(isolate, "done"), v8::PropertyAttribute::DontEnum)
            .FromMaybe(false);
        v8::Local<v8::Value> argv[1] = {MakeIdbEvent(
            isolate, context, request, handler, old_version, new_version)};
        v8::TryCatch try_catch(isolate);
        (void)fn.As<v8::Function>()
            ->Call(context, request, 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        if (try_catch.HasCaught())
          CJavascriptException::ThrowIf(isolate, try_catch);
      }
    }

    std::vector<ContextState::IdbRecord>* IdbRecords(
        ContextState::IdbDatabase* db, const std::string& store_name)
    {
      auto it = db->records.find(store_name);
      if (it == db->records.end())
      {
        auto inserted = db->records.emplace(
            store_name,
            std::make_shared<std::vector<ContextState::IdbRecord>>());
        it = inserted.first;
      }
      return it->second.get();
    }

    ContextState::IdbDatabase* IdbFind(ContextState* state,
                                       const std::string& name)
    {
      for (auto& db : state->idb_databases)
      {
        if (db.name == name)
          return &db;
      }
      return nullptr;
    }

    ContextState::FsNode* FsFind(ContextState::FsNode* root,
                                 const std::string& path)
    {
      ContextState::FsNode* node = root;
      size_t start = 0;
      while (start <= path.size())
      {
        size_t slash = path.find('/', start);
        std::string part = slash == std::string::npos
                               ? path.substr(start)
                               : path.substr(start, slash - start);
        if (!part.empty())
        {
          auto it = node->children.find(part);
          if (it == node->children.end())
            return nullptr;
          node = &it->second;
        }
        if (slash == std::string::npos)
          break;
        start = slash + 1;
      }
      return node;
    }

    // reference-parity: store/database/transaction/request instances share the
    // global interface prototype (Object.getPrototypeOf(store) ===
    // IDBObjectStore.prototype on the reference); the working
    // implementations and WebIDL attribute accessors live on that global
    // prototype (installed once in InstallIdbGlobalFaces), so instance
    // gOPN stays empty - both reference-measured.
    v8::Local<v8::Object> ChainToGlobalProto(v8::Isolate* isolate,
                                             v8::Local<v8::Context> context,
                                             v8::Local<v8::Object> obj,
                                             const char* iface)
    {
      v8::Local<v8::Value> ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, iface))
              .ToLocal(&ctor) &&
          ctor->IsFunction())
      {
        v8::Local<v8::Value> proto;
        if (ctor.As<v8::Function>()
                ->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&proto) &&
            proto->IsObject())
          (void)obj->SetPrototype(context, proto.As<v8::Object>())
              .FromMaybe(false);
      }
      return obj;
    }

    // One-time upgrade of the JSON-conformance IDB prototype faces to the
    // reference-measured working faces: real method implementations with the
    // reference arities, WebIDL attribute accessors over instance private
    // slots.  Called lazily before any store/database/transaction is
    // created (the JSON polyfill runs at boot, before page scripts).

    // capture-less callbacks: context rides in info.Data() as an External
    // wrapping a heap IdbPrivSpec (leaked - context-lifetime face data)
    struct IdbPrivSpec
    {
      const char* priv;
      bool null_default;
    };

    void IdbPrivGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      auto* spec =
          static_cast<IdbPrivSpec*>(info.Data().As<v8::External>()->Value());
      v8::Local<v8::Value> v;
      if (info.This()->GetPrivate(ctx, IdbKey(iso, spec->priv)).ToLocal(&v))
        info.GetReturnValue().Set(v);
      else
        info.GetReturnValue().Set(
            spec->null_default ? v8::Local<v8::Value>(v8::Null(iso))
                               : v8::Local<v8::Value>(v8::Undefined(iso)));
    }

    void IdbPrivSetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      auto* spec =
          static_cast<IdbPrivSpec*>(info.Data().As<v8::External>()->Value());
      if (info.Length() >= 1)
        (void)info.This()
            ->SetPrivate(ctx, IdbKey(iso, spec->priv), info[0])
            .FromMaybe(false);
    }

    void IdbStoreNames(const v8::FunctionCallbackInfo<v8::Value>& info,
                       std::string& db_name, std::string& store_name)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      v8::Local<v8::Value> db_v;
      v8::Local<v8::Value> store_v;
      if (info.This()
              ->GetPrivate(ctx, IdbKey(iso, "lv8.idb.db"))
              .ToLocal(&db_v) &&
          db_v->IsString())
        db_name = ToUtf8(iso, db_v);
      if (info.This()
              ->GetPrivate(ctx, IdbKey(iso, "lv8.idb.store"))
              .ToLocal(&store_v) &&
          store_v->IsString())
        store_name = ToUtf8(iso, store_v);
    }

    void IdbThisPrivStr(const v8::FunctionCallbackInfo<v8::Value>& info,
                        std::string& out, const char* priv)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      v8::Local<v8::Value> v;
      if (info.This()->GetPrivate(ctx, IdbKey(iso, priv)).ToLocal(&v) &&
          v->IsString())
        out = ToUtf8(iso, v);
    }

    v8::Local<v8::Object> MakeIdbStore(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       const std::string& db_name,
                                       const std::string& store_name);

    void EnsureIdbGlobalFaces(v8::Isolate* isolate,
                              v8::Local<v8::Context> context)
    {
      v8::Local<v8::Private> flag = IdbKey(isolate, "lv8.idb.faces");
      if (context->Global()->HasPrivate(context, flag).FromMaybe(false))
        return;
      auto proto_of = [&](const char* iface) -> v8::Local<v8::Object>
      {
        v8::Local<v8::Value> ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, iface))
                .ToLocal(&ctor) &&
            ctor->IsFunction())
        {
          v8::Local<v8::Value> proto;
          if (ctor.As<v8::Function>()
                  ->Get(context, ToV8Str(isolate, "prototype"))
                  .ToLocal(&proto) &&
              proto->IsObject())
            return proto.As<v8::Object>();
        }
        return v8::Local<v8::Object>();
      };
      auto method = [&](v8::Local<v8::Object> proto, const char* name,
                        v8::FunctionCallback cb, int length)
      {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb, v8::Local<v8::Value>(),
                                      v8::Local<v8::Signature>(), length);
        fn->SetClassName(key);
        (void)proto
            ->Set(context, key, fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      };
      auto priv_accessor = [&](v8::Local<v8::Object> proto, const char* name,
                               const char* priv, bool null_default,
                               bool read_only)
      {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        auto* spec = new IdbPrivSpec{priv, null_default};
        v8::Local<v8::FunctionTemplate> getter =
            v8::FunctionTemplate::New(isolate, IdbPrivGetter,
                                      v8::External::New(isolate, spec));
        getter->SetClassName(key);
        v8::Local<v8::Function> set_fn;
        if (!read_only)
        {
          v8::Local<v8::FunctionTemplate> setter =
              v8::FunctionTemplate::New(isolate, IdbPrivSetter,
                                        v8::External::New(isolate, spec));
          setter->SetClassName(key);
          set_fn = setter->GetFunction(context).ToLocalChecked();
        }
        (void)proto
            ->SetAccessorProperty(key,
                                  getter->GetFunction(context).ToLocalChecked(),
                                  set_fn, v8::PropertyAttribute::None);
      };

      // ---- IDBObjectStore.prototype ----
      v8::Local<v8::Object> store_proto = proto_of("IDBObjectStore");
      if (!store_proto.IsEmpty())
      {
        auto put_impl = [](const v8::FunctionCallbackInfo<v8::Value>& info) {
          v8::Isolate* iso = info.GetIsolate();
          v8::Local<v8::Context> ctx = iso->GetCurrentContext();
          ContextState* st = GetState(ctx);
          std::string db_name;
          std::string store_name;
          if (st != nullptr && info.Length() >= 1 && !info[0]->IsUndefined())
            IdbStoreNames(info, db_name, store_name);
          v8::Local<v8::Object> request = MakeIdbRequest(iso, ctx, false);
          ContextState::IdbDatabase* db =
              db_name.empty() ? nullptr : IdbFind(st, db_name);
          if (db != nullptr)
          {
            std::string key_path;
            for (auto& s : db->stores)
            {
              if (s.name == store_name)
              {
                key_path = s.key_path;
                break;
              }
            }
            v8::Local<v8::Value> value = info[0];
            double key = 0;
            std::string string_key;
            bool key_is_string = false;
            bool have_key = false;
            if (!key_path.empty() && info[0]->IsObject())
            {
              v8::Local<v8::Value> key_v;
              if (info[0]
                      .As<v8::Object>()
                      ->Get(ctx, ToV8Str(iso, key_path))
                      .ToLocal(&key_v))
              {
                if (key_v->IsNumber())
                {
                  key = key_v->NumberValue(ctx).FromMaybe(0);
                  have_key = true;
                }
                else if (key_v->IsString())
                {
                  // WAF SDKs keep state under string keyPath keys
                  // ({name:'...'} stores); match them like numbers
                  string_key = ToUtf8(iso, key_v);
                  key_is_string = true;
                  have_key = true;
                }
              }
            }
            // explicit out-of-line key (spec: put(value, key))
            if (!have_key && info.Length() >= 2)
            {
              if (info[1]->IsNumber())
              {
                key = info[1]->NumberValue(ctx).FromMaybe(0);
                have_key = true;
              }
              else if (info[1]->IsString())
              {
                string_key = ToUtf8(iso, info[1]);
                key_is_string = true;
                have_key = true;
              }
            }
            if (have_key)
            {
              auto* records = IdbRecords(db, store_name);
              bool replaced = false;
              for (auto& record : *records)
              {
                if (record.key_is_string == key_is_string &&
                    (key_is_string ? record.string_key == string_key
                                   : record.key == key))
                {
                  record.value.Reset(iso, value);
                  replaced = true;
                  break;
                }
              }
              if (!replaced)
              {
                ContextState::IdbRecord record;
                record.key = key;
                record.key_is_string = key_is_string;
                record.string_key = string_key;
                record.value.Reset(iso, value);
                records->push_back(std::move(record));
              }
            }
          }
          st->pending_idb.push_back(PendingIdbTaskMake(iso, request, 1));
          info.GetReturnValue().Set(request);
        };
        // get(key): a missing record resolves with undefined (spec; reference
        // parity) - the d8y2 storage layer distinguishes undefined from
        // null on first run, and the null return gated the fingerprint
        // stage on lv8.
        auto get_impl = [](const v8::FunctionCallbackInfo<v8::Value>& info) {
          v8::Isolate* iso = info.GetIsolate();
          v8::Local<v8::Context> ctx = iso->GetCurrentContext();
          ContextState* st = GetState(ctx);
          std::string db_name;
          std::string store_name;
          if (st != nullptr && info.Length() >= 1 &&
              (info[0]->IsNumber() || info[0]->IsString()))
            IdbStoreNames(info, db_name, store_name);
          v8::Local<v8::Object> request = MakeIdbRequest(iso, ctx, false);
          v8::Local<v8::Value> result = v8::Undefined(iso);
          double key = 0;
          std::string string_key;
          bool key_is_string = false;
          if (info.Length() >= 1)
          {
            if (info[0]->IsNumber())
              key = info[0]->NumberValue(ctx).FromMaybe(0);
            else if (info[0]->IsString())
            {
              string_key = ToUtf8(iso, info[0]);
              key_is_string = true;
            }
          }
          ContextState::IdbDatabase* db =
              db_name.empty() ? nullptr : IdbFind(st, db_name);
          if (db != nullptr)
          {
            auto* records = IdbRecords(db, store_name);
            if (records != nullptr)
            {
              for (const auto& record : *records)
              {
                if (record.key_is_string == key_is_string &&
                    (key_is_string ? record.string_key == string_key
                                   : record.key == key))
                {
                  result = record.value.Get(iso);
                  break;
                }
              }
            }
          }
          (void)request
              ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.result"), result)
              .FromMaybe(false);
          st->pending_idb.push_back(PendingIdbTaskMake(iso, request, 1));
          info.GetReturnValue().Set(request);
        };
        auto delete_impl = [](const v8::FunctionCallbackInfo<v8::Value>&
                                  info) {
          v8::Isolate* iso = info.GetIsolate();
          v8::Local<v8::Context> ctx = iso->GetCurrentContext();
          ContextState* st = GetState(ctx);
          std::string db_name;
          std::string store_name;
          IdbStoreNames(info, db_name, store_name);
          ContextState::IdbDatabase* db =
              db_name.empty() ? nullptr : IdbFind(st, db_name);
          if (db != nullptr && info.Length() >= 1)
          {
            auto* records = IdbRecords(db, store_name);
            for (auto it = records->begin(); it != records->end();)
            {
              bool match = false;
              if (info[0]->IsString() && it->key_is_string)
                match = it->string_key == ToUtf8(iso, info[0]);
              else if (info[0]->IsNumber() && !it->key_is_string)
                match = it->key == info[0]->NumberValue(ctx).FromMaybe(0);
              if (match)
                it = records->erase(it);
              else
                ++it;
            }
          }
          v8::Local<v8::Object> request = MakeIdbRequest(iso, ctx, false);
          (void)request
              ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.result"),
                           v8::Undefined(iso))
              .FromMaybe(false);
          st->pending_idb.push_back(PendingIdbTaskMake(iso, request, 1));
          info.GetReturnValue().Set(request);
        };
        method(store_proto, "put", put_impl, 1);
        method(store_proto, "get", get_impl, 1);
        method(store_proto, "add", put_impl, 1);
        method(store_proto, "delete", delete_impl, 1);
        method(store_proto, "getKey", get_impl, 1);
        priv_accessor(store_proto, "name", "lv8.idb.store", false, true);
        priv_accessor(store_proto, "transaction", "lv8.idb.txn", true, true);
        // keyPath: from the store schema (this' db/store privates)
        {
          v8::Local<v8::String> key = ToV8Str(isolate, "keyPath");
          v8::Local<v8::FunctionTemplate> getter = v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                std::string db_name;
                std::string store_name;
                IdbStoreNames(info, db_name, store_name);
                ContextState* st = GetState(ctx);
                ContextState::IdbDatabase* db =
                    st == nullptr || db_name.empty()
                        ? nullptr
                        : IdbFind(st, db_name);
                if (db != nullptr)
                {
                  for (auto& s : db->stores)
                  {
                    if (s.name == store_name)
                    {
                      if (!s.key_path.empty())
                        info.GetReturnValue().Set(ToV8Str(iso, s.key_path));
                      return;
                    }
                  }
                }
                info.GetReturnValue().Set(v8::Null(iso));
              });
          getter->SetClassName(key);
          (void)store_proto
              ->SetAccessorProperty(key,
                                    getter->GetFunction(context)
                                        .ToLocalChecked(),
                                    v8::Local<v8::Function>(),
                                    v8::PropertyAttribute::None);
        }
        {
          v8::Local<v8::String> key = ToV8Str(isolate, "autoIncrement");
          v8::Local<v8::FunctionTemplate> getter = v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                info.GetReturnValue().Set(
                    v8::Boolean::New(info.GetIsolate(), false));
              });
          getter->SetClassName(key);
          (void)store_proto
              ->SetAccessorProperty(key,
                                    getter->GetFunction(context)
                                        .ToLocalChecked(),
                                    v8::Local<v8::Function>(),
                                    v8::PropertyAttribute::None);
        }
      }

      // ---- IDBTransaction.prototype ----
      v8::Local<v8::Object> txn_proto = proto_of("IDBTransaction");
      if (!txn_proto.IsEmpty())
      {
        method(txn_proto, "objectStore",
               [](const v8::FunctionCallbackInfo<v8::Value>& cb_info) {
                 v8::Isolate* iso2 = cb_info.GetIsolate();
                 v8::Local<v8::Context> ctx2 = iso2->GetCurrentContext();
                 std::string db_name;
                 v8::Local<v8::Value> db_v;
                 if (cb_info.This()
                         ->GetPrivate(ctx2, IdbKey(iso2, "lv8.idb.db"))
                         .ToLocal(&db_v) &&
                     db_v->IsString())
                   db_name = ToUtf8(iso2, db_v);
                 std::string store_name =
                     cb_info.Length() >= 1 && cb_info[0]->IsString()
                         ? ToUtf8(iso2, cb_info[0])
                         : std::string();
                 if (!store_name.empty())
                 {
                   v8::Local<v8::Object> store =
                       MakeIdbStore(iso2, ctx2, db_name, store_name);
                   (void)store
                       ->SetPrivate(ctx2, IdbKey(iso2, "lv8.idb.txn"),
                                    cb_info.This())
                       .FromMaybe(false);
                   cb_info.GetReturnValue().Set(store);
                 }
               },
               1);
        priv_accessor(txn_proto, "mode", "lv8.idb.mode", false, true);
        priv_accessor(txn_proto, "db", "lv8.idb.dbobj", true, true);
        priv_accessor(txn_proto, "durability", "lv8.idb.durability", false,
                      true);
        priv_accessor(txn_proto, "error", "lv8.idb.err", true, true);
        priv_accessor(txn_proto, "onabort", "lv8.idb.onabort", true, false);
        priv_accessor(txn_proto, "oncomplete", "lv8.idb.oncomplete", true,
                      false);
        priv_accessor(txn_proto, "onerror", "lv8.idb.onerror", true, false);
      }

      // ---- IDBDatabase.prototype ----
      v8::Local<v8::Object> db_proto = proto_of("IDBDatabase");
      if (!db_proto.IsEmpty())
      {
        method(db_proto, "createObjectStore",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 ContextState* st = GetState(ctx);
                 std::string db_name;
                 if (st == nullptr || info.Length() < 1 ||
                     !info[0]->IsString())
                   return;
                 IdbThisPrivStr(info, db_name, "lv8.idb.db");
                 ContextState::IdbDatabase* db = IdbFind(st, db_name);
                 if (db == nullptr)
                   return;
                 std::string store_name = ToUtf8(iso, info[0]);
                 std::string key_path;
                 if (info.Length() > 1 && info[1]->IsObject())
                 {
                   v8::Local<v8::Value> field;
                   if (info[1]
                           .As<v8::Object>()
                           ->Get(ctx, ToV8Str(iso, "keyPath"))
                           .ToLocal(&field) &&
                       field->IsString())
                     key_path = ToUtf8(iso, field);
                 }
                 bool exists = false;
                 for (auto& s : db->stores)
                 {
                   if (s.name == store_name)
                   {
                     exists = true;
                     break;
                   }
                 }
                 if (!exists)
                   db->stores.push_back({store_name, key_path});
                 info.GetReturnValue().Set(
                     MakeIdbStore(iso, ctx, db_name, store_name));
               },
               1);
        method(db_proto, "deleteObjectStore",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 ContextState* st = GetState(ctx);
                 std::string db_name;
                 if (st == nullptr || info.Length() < 1 ||
                     !info[0]->IsString())
                   return;
                 IdbThisPrivStr(info, db_name, "lv8.idb.db");
                 ContextState::IdbDatabase* db = IdbFind(st, db_name);
                 if (db == nullptr)
                   return;
                 std::string store_name = ToUtf8(iso, info[0]);
                 for (auto it = db->stores.begin(); it != db->stores.end();
                      ++it)
                 {
                   if (it->name == store_name)
                   {
                     db->stores.erase(it);
                     break;
                   }
                 }
                 db->records.erase(store_name);
               },
               1);
        method(db_proto, "transaction",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 v8::Local<v8::Object> db_obj = info.This();
                 std::string db_name;
                 IdbThisPrivStr(info, db_name, "lv8.idb.db");
                 v8::Local<v8::Object> txn = v8::Object::New(iso);
                 ChainToGlobalProto(iso, ctx, txn, "IDBTransaction");
                 (void)txn
                     ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.db"),
                                  ToV8Str(iso, db_name))
                     .FromMaybe(false);
                 (void)txn
                     ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.dbobj"), db_obj)
                     .FromMaybe(false);
                 (void)txn
                     ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.mode"),
                                  info.Length() > 1 && info[1]->IsString()
                                      ? info[1]
                                      : v8::Local<v8::Value>(
                                            ToV8Str(iso, "readonly")))
                     .FromMaybe(false);
                 (void)txn
                     ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.durability"),
                                  ToV8Str(iso, "default"))
                     .FromMaybe(false);
                 info.GetReturnValue().Set(txn);
               },
               1);
        priv_accessor(db_proto, "name", "lv8.idb.db", false, true);
        priv_accessor(db_proto, "version", "lv8.idb.version", false, true);
        priv_accessor(db_proto, "onabort", "lv8.idb.onabort", true, false);
        priv_accessor(db_proto, "onclose", "lv8.idb.onclose", true, false);
        priv_accessor(db_proto, "onerror", "lv8.idb.onerror", true, false);
        priv_accessor(db_proto, "onversionchange", "lv8.idb.onvc", true,
                      false);
      }

      // ---- IDBRequest.prototype ----
      v8::Local<v8::Object> req_proto = proto_of("IDBRequest");
      if (!req_proto.IsEmpty())
      {
        priv_accessor(req_proto, "result", "lv8.idb.result", false, true);
        priv_accessor(req_proto, "readyState", "lv8.idb.rstate", false, true);
        priv_accessor(req_proto, "error", "lv8.idb.err", true, true);
        priv_accessor(req_proto, "source", "lv8.idb.source", true, true);
        priv_accessor(req_proto, "transaction", "lv8.idb.txn", true, true);
      }

      (void)context->Global()->SetPrivate(context, flag,
                                          v8::Boolean::New(isolate, true))
          .FromMaybe(false);
    }
    v8::Local<v8::Object> MakeIdbStore(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       const std::string& db_name,
                                       const std::string& store_name)
    {
      EnsureIdbGlobalFaces(isolate, context);
      // The store face (working methods + WebIDL attribute accessors)
      // lives on the global IDBObjectStore.prototype, installed once in
      // InstallIdbGlobalFaces.  Instances only carry the private slots so
      // Object.getPrototypeOf(store) === IDBObjectStore.prototype and
      // gOPN(instance) stays empty - both reference-measured.
      v8::Local<v8::Object> obj = v8::Object::New(isolate);
      ChainToGlobalProto(isolate, context, obj, "IDBObjectStore");
      (void)obj->SetPrivate(context, IdbKey(isolate, "lv8.idb.db"),
                            ToV8Str(isolate, db_name))
          .FromMaybe(false);
      (void)obj->SetPrivate(context, IdbKey(isolate, "lv8.idb.store"),
                            ToV8Str(isolate, store_name))
          .FromMaybe(false);
      return obj;
    }

    v8::Local<v8::Object> MakeIdbDatabase(v8::Isolate* isolate,
                                          v8::Local<v8::Context> context,
                                          const std::string& name,
                                          double version)
    {
      EnsureIdbGlobalFaces(isolate, context);
      // The database face (createObjectStore/transaction/deleteObjectStore
      // + attribute accessors) lives on the global IDBDatabase.prototype;
      // instances carry only the private slots and the measured own
      // objectStoreNames list below.
      v8::Local<v8::Object> obj = v8::Object::New(isolate);
      ChainToGlobalProto(isolate, context, obj, "IDBDatabase");
      (void)obj->SetPrivate(context, IdbKey(isolate, "lv8.idb.db"),
                            ToV8Str(isolate, name))
          .FromMaybe(false);
      (void)obj
          ->SetPrivate(context, IdbKey(isolate, "lv8.idb.version"),
                       v8::Number::New(isolate, version))
          .FromMaybe(false);
      // objectStoreNames: DOMStringList snapshot, built lazily on every
      // read so stores registered by an upgrade callback (which run on
      // this same object before the success handler) are visible.
      // reference-measured face (offline A/B): own members are the indexed
      // strings only - length/contains/item/constructor/toStringTag and
      // Symbol.iterator resolve through a DOMStringList prototype, and
      // Array.from/for-of enumerate the store names.
      {
        v8::Local<v8::String> key = ToV8Str(isolate, "objectStoreNames");
        (void)obj
            ->SetNativeDataProperty(
                context, key,
                [](v8::Local<v8::Name>,
                   const v8::PropertyCallbackInfo<v8::Value>& info) {
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  std::string db_name;
                  v8::Local<v8::Value> db_v;
                  if (info.This()
                          ->GetPrivate(ctx, IdbKey(iso, "lv8.idb.db"))
                          .ToLocal(&db_v) &&
                      db_v->IsString())
                    db_name = ToUtf8(iso, db_v);
                  uint32_t n = 0;
                  ContextState* st = GetState(ctx);
                  ContextState::IdbDatabase* db =
                      (st != nullptr && !db_name.empty())
                          ? IdbFind(st, db_name)
                          : nullptr;
                  if (db != nullptr)
                    n = static_cast<uint32_t>(db->stores.size());
                  // per-database cache: identity holds across reads while
                  // the store count is unchanged (reference semantics)
                  v8::Local<v8::Value> cached;
                  if (info.This()
                          ->GetPrivate(ctx, IdbKey(iso, "lv8.idb.namesList"))
                          .ToLocal(&cached) &&
                      cached->IsObject())
                  {
                    v8::Local<v8::Value> cnt;
                    if (cached.As<v8::Object>()
                            ->GetPrivate(
                                ctx, IdbKey(iso, "lv8.idb.namesCount"))
                            .ToLocal(&cnt) &&
                        cnt->IsNumber() &&
                        cnt->NumberValue(ctx).FromMaybe(-1) ==
                            static_cast<double>(n))
                    {
                      info.GetReturnValue().Set(cached);
                      return;
                    }
                  }
                  v8::Local<v8::Array> names = v8::Array::New(iso);
                  v8::Local<v8::Object> list = v8::Object::New(iso);
                  if (db != nullptr)
                  {
                    uint32_t i = 0;
                    for (const auto& s : db->stores)
                    {
                      v8::Local<v8::String> name = ToV8Str(iso, s.name);
                      (void)names->Set(ctx, i, name).FromMaybe(false);
                      // indexed own members: reference gOPN(names) answers ["0"]
                      (void)list
                          ->DefineOwnProperty(
                              ctx, ToV8Str(iso, std::to_string(i)), name,
                              static_cast<v8::PropertyAttribute>(0))
                          .FromMaybe(false);
                      i++;
                    }
                  }
                  (void)list
                      ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.names"), names)
                      .FromMaybe(false);
                  v8::Local<v8::Object> proto = v8::Object::New(iso);
                  (void)proto
                      ->Set(ctx, v8::Symbol::GetToStringTag(iso),
                            ToV8Str(iso, "DOMStringList"))
                      .FromMaybe(false);
                  {
                    v8::Local<v8::String> ckey = ToV8Str(iso, "constructor");
                    v8::Local<v8::FunctionTemplate> cfn =
                        v8::FunctionTemplate::New(iso);
                    cfn->SetClassName(ToV8Str(iso, "DOMStringList"));
                    (void)proto
                        ->Set(ctx, ckey,
                              cfn->GetFunction(ctx).ToLocalChecked())
                        .FromMaybe(false);
                  }
                  {
                    v8::Local<v8::String> lkey = ToV8Str(iso, "length");
                    v8::Local<v8::FunctionTemplate> lfn =
                        v8::FunctionTemplate::New(
                            iso,
                            [](const v8::FunctionCallbackInfo<v8::Value>&
                                   args) {
                              v8::Isolate* iso2 = args.GetIsolate();
                              v8::Local<v8::Context> ctx2 =
                                  iso2->GetCurrentContext();
                              uint32_t len = 0;
                              v8::Local<v8::Value> names_v;
                              if (args.This()
                                      ->GetPrivate(
                                          ctx2, IdbKey(iso2, "lv8.idb.names"))
                                      .ToLocal(&names_v) &&
                                  names_v->IsArray())
                                len = names_v.As<v8::Array>()->Length();
                              args.GetReturnValue().Set(
                                  v8::Integer::NewFromUnsigned(iso2, len));
                            });
                    lfn->SetClassName(ToV8Str(iso, "get length"));
                    (void)proto
                        ->SetAccessorProperty(
                            lkey, lfn->GetFunction(ctx).ToLocalChecked(),
                            v8::Local<v8::Function>(),
                            v8::PropertyAttribute::DontEnum);
                  }
                  {
                    v8::Local<v8::String> mkey = ToV8Str(iso, "contains");
                    v8::Local<v8::FunctionTemplate> mfn =
                        v8::FunctionTemplate::New(
                            iso,
                            [](const v8::FunctionCallbackInfo<v8::Value>&
                                   args) {
                              v8::Isolate* iso2 = args.GetIsolate();
                              v8::Local<v8::Context> ctx2 =
                                  iso2->GetCurrentContext();
                              bool found = false;
                              v8::Local<v8::Value> names_v;
                              if (args.Length() >= 1 &&
                                  args.This()
                                      ->GetPrivate(
                                          ctx2, IdbKey(iso2, "lv8.idb.names"))
                                      .ToLocal(&names_v) &&
                                  names_v->IsArray())
                              {
                                v8::Local<v8::Array> arr =
                                    names_v.As<v8::Array>();
                                for (uint32_t i = 0; i < arr->Length(); ++i)
                                {
                                  v8::Local<v8::Value> item;
                                  if (arr->Get(ctx2, i).ToLocal(&item) &&
                                      item->StrictEquals(args[0]))
                                  {
                                    found = true;
                                    break;
                                  }
                                }
                              }
                              args.GetReturnValue().Set(
                                  v8::Boolean::New(iso2, found));
                            });
                    mfn->SetClassName(mkey);
                    (void)proto
                        ->Set(ctx, mkey,
                              mfn->GetFunction(ctx).ToLocalChecked())
                        .FromMaybe(false);
                  }
                  {
                    v8::Local<v8::String> mkey = ToV8Str(iso, "item");
                    v8::Local<v8::FunctionTemplate> mfn =
                        v8::FunctionTemplate::New(
                            iso,
                            [](const v8::FunctionCallbackInfo<v8::Value>&
                                   args) {
                              v8::Isolate* iso2 = args.GetIsolate();
                              v8::Local<v8::Context> ctx2 =
                                  iso2->GetCurrentContext();
                              v8::Local<v8::Value> names_v;
                              if (args.Length() >= 1 && args[0]->IsNumber() &&
                                  args.This()
                                      ->GetPrivate(
                                          ctx2, IdbKey(iso2, "lv8.idb.names"))
                                      .ToLocal(&names_v) &&
                                  names_v->IsArray())
                              {
                                v8::Local<v8::Array> arr =
                                    names_v.As<v8::Array>();
                                uint32_t i = static_cast<uint32_t>(
                                    args[0]->NumberValue(ctx2).FromMaybe(-1));
                                v8::Local<v8::Value> item;
                                if (i < arr->Length() &&
                                    arr->Get(ctx2, i).ToLocal(&item))
                                {
                                  args.GetReturnValue().Set(item);
                                  return;
                                }
                              }
                              args.GetReturnValue().Set(v8::Null(iso2));
                            });
                    mfn->SetClassName(mkey);
                    (void)proto
                        ->Set(ctx, mkey,
                              mfn->GetFunction(ctx).ToLocalChecked())
                        .FromMaybe(false);
                  }
                  // Symbol.iterator: delegate to the private names array's
                  // own iterator (for-of / spread parity with the reference)
                  {
                    v8::Local<v8::Symbol> iter = v8::Symbol::GetIterator(iso);
                    v8::Local<v8::FunctionTemplate> ifn =
                        v8::FunctionTemplate::New(
                            iso,
                            [](const v8::FunctionCallbackInfo<v8::Value>&
                                   args) {
                              v8::Isolate* iso2 = args.GetIsolate();
                              v8::Local<v8::Context> ctx2 =
                                  iso2->GetCurrentContext();
                              v8::Local<v8::Symbol> iter =
                                  v8::Symbol::GetIterator(iso2);
                              v8::Local<v8::Value> names_v;
                              if (args.This()
                                      ->GetPrivate(
                                          ctx2, IdbKey(iso2, "lv8.idb.names"))
                                      .ToLocal(&names_v) &&
                                  names_v->IsArray())
                              {
                                v8::Local<v8::Value> array_ctor;
                                if (ctx2->Global()
                                        ->Get(ctx2, ToV8Str(iso2, "Array"))
                                        .ToLocal(&array_ctor) &&
                                    array_ctor->IsObject())
                                {
                                  v8::Local<v8::Value> arr_p;
                                  if (array_ctor.As<v8::Object>()
                                          ->Get(ctx2,
                                                ToV8Str(iso2, "prototype"))
                                          .ToLocal(&arr_p) &&
                                      arr_p->IsObject())
                                  {
                                    v8::Local<v8::Value> values_iter;
                                    if (arr_p.As<v8::Object>()
                                            ->Get(ctx2, iter)
                                            .ToLocal(&values_iter) &&
                                        values_iter->IsFunction())
                                    {
                                      v8::Local<v8::Value> argv[] = {names_v};
                                      v8::Local<v8::Value> result;
                                      if (values_iter.As<v8::Function>()
                                              ->Call(ctx2, names_v, 1, argv)
                                              .ToLocal(&result))
                                      {
                                        args.GetReturnValue().Set(result);
                                        return;
                                      }
                                    }
                                  }
                                }
                              }
                              args.GetReturnValue().Set(v8::Undefined(iso2));
                            });
                    ifn->SetClassName(ToV8Str(iso, "[Symbol.iterator]"));
                    (void)proto
                        ->Set(ctx, iter,
                              ifn->GetFunction(ctx).ToLocalChecked())
                        .FromMaybe(false);
                  }
                  (void)list->SetPrototype(ctx, proto).FromMaybe(false);
                  (void)list
                      ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.namesCount"),
                                   v8::Number::New(iso, n))
                      .FromMaybe(false);
                  (void)info.This()
                      ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.namesList"), list)
                      .FromMaybe(false);
                  info.GetReturnValue().Set(list);
                })
            .FromMaybe(false);
      }
      return obj;
    }

    // getDirectoryHandle/getFileHandle shared body (root defaults to the
    // virtual tree root when the handle path is empty)
    void FsChildHandleImpl(
        const v8::FunctionCallbackInfo<v8::Value>& info, bool child_dir)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      ContextState* st = GetState(ctx);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(ctx).ToLocalChecked();
      v8::Local<v8::Value> path_v;
      std::string base;
      if (st == nullptr || info.Length() < 1 || !info[0]->IsString() ||
          !info.This()
               ->GetPrivate(ctx, FsKey(iso, "lv8.fs.path"))
               .ToLocal(&path_v) ||
          !path_v->IsString())
      {
        info.GetReturnValue().Set(resolver->GetPromise());
        return;
      }
      base = ToUtf8(iso, path_v);
      std::string child_name = ToUtf8(iso, info[0]);
      bool create = info.Length() > 1 && info[1]->IsObject() &&
                    info[1]
                        .As<v8::Object>()
                        ->Get(ctx, ToV8Str(iso, "create"))
                        .FromMaybe(v8::Local<v8::Value>())
                        ->BooleanValue(iso);
      std::string child_path =
          base.empty() ? child_name : base + "/" + child_name;
      ContextState::FsNode* parent =
          base.empty() ? &st->fs_root : FsFind(&st->fs_root, base);
      ContextState::FsNode* node =
          parent != nullptr ? FsFind(parent, child_name) : nullptr;
      if (node == nullptr && create && parent != nullptr)
      {
        ContextState::FsNode fresh;
        fresh.dir = child_dir;
        parent->children[child_name] = std::move(fresh);
        node = FsFind(parent, child_name);
      }
      if (node != nullptr && node->dir != child_dir)
        node = nullptr;
      if (node == nullptr)
      {
        (void)resolver
            ->Reject(ctx, ToV8Str(iso, "NotFoundError: handle not found"))
            .FromMaybe(false);
      }
      else
      {
        (void)resolver
            ->Resolve(ctx, MakeFsHandle(iso, ctx, child_path, child_name,
                                        child_dir))
            .FromMaybe(false);
      }
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void FsChildHandleDirThunk(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      FsChildHandleImpl(info, true);
    }

    void FsChildHandleFileThunk(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      FsChildHandleImpl(info, false);
    }
    } // namespace

  // ---------------------------------------------------- indexedDB install

  void InstallIdbAndFs(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "IDBFactory"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "IDBFactory"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* name, v8::FunctionCallback cb, int length)
      {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb, v8::Local<v8::Value>(),
                                      v8::Local<v8::Signature>(), length);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      // cmp(a, b): key ordering per spec (numbers < strings); -1/0/1
      method("cmp",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               if (info.Length() < 2)
                 return;
               auto kind = [&](v8::Local<v8::Value> v) -> int
               { return v->IsNumber() ? 0 : (v->IsString() ? 1 : 2); };
               int ka = kind(info[0]), kb = kind(info[1]);
               if (info[0]->StrictEquals(info[1]))
                 info.GetReturnValue().Set(v8::Integer::New(iso, 0));
               else if (ka != kb)
                 info.GetReturnValue().Set(v8::Integer::New(iso, ka < kb ? -1 : 1));
               else if (ka == 0)
                 info.GetReturnValue().Set(v8::Integer::New(
                     iso,
                     info[0]->NumberValue(ctx).FromMaybe(0) <
                             info[1]->NumberValue(ctx).FromMaybe(0)
                         ? -1
                         : 1));
               else
               {
                 std::string a = ToUtf8(iso, info[0]);
                 std::string b = ToUtf8(iso, info[1]);
                 info.GetReturnValue().Set(v8::Integer::New(
                     iso, a < b ? -1 : 1));
               }
             },
             2);
      // deleteDatabase(name): drop the database; success settles async
      method("deleteDatabase",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               ContextState* st = GetState(ctx);
               v8::Local<v8::Object> request = MakeIdbRequest(iso, ctx, true);
               if (st != nullptr && info.Length() >= 1 &&
                   info[0]->IsString())
               {
                 std::string name = ToUtf8(iso, info[0]);
                 for (auto it = st->idb_databases.begin();
                      it != st->idb_databases.end(); ++it)
                 {
                   if (it->name == name)
                   {
                     st->idb_databases.erase(it);
                     break;
                   }
                 }
                 st->pending_idb.push_back(
                     PendingIdbTaskMake(iso, request, 0, name, 0, false));
               }
               info.GetReturnValue().Set(request);
             },
             1);
      method("open", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        ContextState* st = GetState(ctx);
        v8::Local<v8::Object> request = MakeIdbRequest(iso, ctx, true);
        if (st != nullptr && info.Length() >= 1 && info[0]->IsString())
        {
          std::string name = ToUtf8(iso, info[0]);
          double version = info.Length() > 1 && info[1]->IsNumber()
                               ? info[1]->NumberValue(ctx).FromMaybe(1)
                               : 1.0;
          ContextState::IdbDatabase* db = IdbFind(st, name);
          bool need_upgrade = db == nullptr || db->version < version;
          double old_version = db == nullptr ? 0.0 : db->version;
          (void)request
              ->SetPrivate(ctx, IdbKey(iso, "lv8.idb.oldVersion"),
                           v8::Number::New(iso, old_version))
              .FromMaybe(false);
          if (db == nullptr)
          {
            st->idb_databases.push_back({name, version, {}, {}});
            db = IdbFind(st, name);
          }
          else if (need_upgrade)
          {
            db->version = version;
          }
          if (db != nullptr)
            st->pending_idb.push_back(
                PendingIdbTaskMake(iso, request, 0, name, version,
                                   need_upgrade));
        }
        info.GetReturnValue().Set(request);
      },
      1);
      method("databases",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               ContextState* st = GetState(ctx);
               v8::Local<v8::Promise::Resolver> resolver =
                   v8::Promise::Resolver::New(ctx).ToLocalChecked();
               v8::Local<v8::Array> arr = v8::Array::New(iso);
               if (st != nullptr)
               {
                 uint32_t n = 0;
                 for (const auto& db : st->idb_databases)
                 {
                   v8::Local<v8::Object> entry = v8::Object::New(iso);
                   (void)entry
                       ->Set(ctx, ToV8Str(iso, "name"),
                             ToV8Str(iso, db.name))
                       .FromMaybe(false);
                   (void)entry
                       ->Set(ctx, ToV8Str(iso, "version"),
                             v8::Number::New(iso, db.version))
                       .FromMaybe(false);
                   (void)arr->Set(ctx, n++, entry).FromMaybe(false);
                 }
               }
               (void)resolver->Resolve(ctx, arr).FromMaybe(false);
               info.GetReturnValue().Set(resolver->GetPromise());
             },
             0);
      v8::Local<v8::Object> factory =
          inst->NewInstance(context).ToLocalChecked();
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "indexedDB"),
                              factory,
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // __lv8_clock_ms(): logical clock accessor for JS-side surfaces
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            info.GetReturnValue().Set(
                v8::Number::New(iso, LogicalClock(iso, ctx)));
          });
      tpl->SetClassName(ToV8Str(isolate, "__lv8_clock_ms"));
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "__lv8_clock_ms"),
                              tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // global queueMicrotask(cb): enqueue as a v8 microtask
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            if (info.Length() >= 1 && info[0]->IsFunction())
            {
              // true microtask deferral (reference parity): callbacks run
              // at the next checkpoint, so handlers assigned synchronously
              // after a constructor still observe the event
              iso->EnqueueMicrotask(info[0].As<v8::Function>());
            }
          });
      tpl->SetClassName(ToV8Str(isolate, "queueMicrotask"));
      // G4.393n: native-method form (no .prototype, like the reference).
      tpl->RemovePrototype();
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "queueMicrotask"),
                              tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // requestAnimationFrame / cancelAnimationFrame over the logical clock
    {
      v8::Local<v8::FunctionTemplate> raf = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            ContextState* st = GetState(ctx);
            if (st == nullptr || info.Length() < 1 ||
                !info[0]->IsFunction())
            {
              info.GetReturnValue().Set(0);
              return;
            }
            int id = LV8::RegisterAnimationFrame(
                iso, ctx, info[0].As<v8::Function>());
            info.GetReturnValue().Set(v8::Integer::New(iso, id));
          });
      raf->SetClassName(ToV8Str(isolate, "requestAnimationFrame"));
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "requestAnimationFrame"),
                              raf->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);

      v8::Local<v8::FunctionTemplate> caf =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                if (info.Length() >= 1 && info[0]->IsNumber())
                {
                  LV8::CancelAnimationFrame(
                      info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
                      info[0]->IntegerValue(
                          info.GetIsolate()->GetCurrentContext())
                          .FromMaybe(0));
                }
              });
      caf->SetClassName(ToV8Str(isolate, "cancelAnimationFrame"));
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "cancelAnimationFrame"),
                              caf->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // scheduler.postTask(fn, {delay}) -> Promise<fn()> (logical delays);
    // TaskController/AbortSignal -> abort() rejects with AbortError
    {
      v8::Local<v8::Object> scheduler = v8::Object::New(isolate);
      v8::Local<v8::FunctionTemplate> post = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            v8::Local<v8::Promise> promise = resolver->GetPromise();
            double delay = 0;
            v8::Local<v8::Value> signal = v8::Undefined(iso);
            if (info.Length() > 1 && info[1]->IsObject())
            {
              v8::Local<v8::Object> opts = info[1].As<v8::Object>();
              v8::Local<v8::Value> delay_v;
              if (opts->Get(ctx, ToV8Str(iso, "delay")).ToLocal(&delay_v) &&
                  delay_v->IsNumber())
                delay = delay_v->NumberValue(ctx).FromMaybe(0);
              v8::Local<v8::Value> signal_v;
              if (opts->Get(ctx, ToV8Str(iso, "signal")).ToLocal(&signal_v))
                signal = signal_v;
            }
            v8::Local<v8::Object> err = v8::Exception::Error(
                ToV8Str(iso, "The task was aborted")).As<v8::Object>();
            (void)err->Set(ctx, ToV8Str(iso, "name"),
                           ToV8Str(iso, "AbortError")).FromMaybe(false);
            bool aborted = false;
            if (signal->IsObject())
            {
              v8::Local<v8::Value> aborted_v;
              if (signal.As<v8::Object>()
                      ->Get(ctx, ToV8Str(iso, "aborted"))
                      .ToLocal(&aborted_v) && aborted_v->IsBoolean())
                aborted = aborted_v->BooleanValue(iso);
            }
            if (aborted || info.Length() < 1 || !info[0]->IsFunction())
            {
              (void)resolver->Reject(ctx, err).FromMaybe(false);
              info.GetReturnValue().Set(promise);
              return;
            }
            // schedule through the logical timer queue: advance(n) runs the
            // task once the delay has elapsed; abort() before the boundary
            // flips signal.aborted so the wrapper rejects instead. A JS
            // deferred provides callable resolve/reject.
            v8::Local<v8::String> deferred_src = ToV8Str(
                iso,
                "(function () {"
                " let resolve, reject;"
                " const promise = new Promise(function (res, rej) {"
                " resolve = res; reject = rej; });"
                " return { promise, resolve, reject }; })");
            v8::ScriptOrigin origin(ToV8Str(iso, "<anonymous>"));
            v8::Local<v8::Script> deferred_script =
                v8::Script::Compile(ctx, deferred_src, &origin)
                    .ToLocalChecked();
            v8::Local<v8::Function> make_deferred =
                deferred_script->Run(ctx).ToLocalChecked().As<v8::Function>();
            v8::Local<v8::Object> deferred = make_deferred
                ->Call(ctx, ctx->Global(), 0, nullptr)
                .ToLocalChecked()
                .As<v8::Object>();
            v8::Local<v8::Value> promise_v =
                deferred->Get(ctx, ToV8Str(iso, "promise")).ToLocalChecked();
            v8::Local<v8::Function> resolve_fn = deferred
                ->Get(ctx, ToV8Str(iso, "resolve"))
                .ToLocalChecked()
                .As<v8::Function>();
            v8::Local<v8::Function> reject_fn = deferred
                ->Get(ctx, ToV8Str(iso, "reject"))
                .ToLocalChecked()
                .As<v8::Function>();
            v8::Local<v8::String> wrapper_src = ToV8Str(
                iso,
                "(function (signal, taskFn, resolve, reject, err) {"
                " if (signal && signal.aborted) reject(err);"
                " else resolve(taskFn()); })");
            v8::Local<v8::Script> script =
                v8::Script::Compile(ctx, wrapper_src, &origin)
                    .ToLocalChecked();
            v8::Local<v8::Function> wrapper =
                script->Run(ctx).ToLocalChecked().As<v8::Function>();
            v8::Local<v8::Value> set_timeout =
                ctx->Global()->Get(ctx, ToV8Str(iso, "setTimeout"))
                    .ToLocalChecked();
            v8::Local<v8::Value> sargs[] = {
                wrapper, v8::Number::New(iso, delay), signal, info[0],
                resolve_fn, reject_fn, err};
            v8::TryCatch try_catch(iso);
            (void)set_timeout.As<v8::Function>()
                ->Call(ctx, ctx->Global(), 7, sargs)
                .FromMaybe(v8::Local<v8::Value>());
            if (try_catch.HasCaught())
              CJavascriptException::ThrowIf(iso, try_catch);
            info.GetReturnValue().Set(promise_v);
          });
      post->SetClassName(ToV8Str(isolate, "postTask"));
      (void)scheduler
          ->Set(context, ToV8Str(isolate, "postTask"),
                post->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "scheduler"),
                              scheduler,
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
      // TaskController: {signal: {aborted: false}} with abort()
      // M11: reference parity - instances are zero-own: `tc.signal` resolves
      // through the prototype accessor (constant null) and reference has NO
      // abort member at all (tc.abort is undefined there), so the
      // constructor must not plant own abort/signal on the instance
      v8::Local<v8::FunctionTemplate> tc_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Object> signal = v8::Object::New(iso);
            (void)signal
                ->Set(ctx, ToV8Str(iso, "aborted"),
                      v8::Boolean::New(iso, false))
                .FromMaybe(false);
            v8::Local<v8::Object> controller =
                info.This().As<v8::Object>();
            (void)controller
                ->SetPrivate(ctx,
                             v8::Private::ForApi(iso,
                                                 ToV8Str(iso, "lv8.tc.signal")),
                             signal)
                .FromMaybe(false);
          });
      tc_tpl->SetClassName(ToV8Str(isolate, "TaskController"));
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "TaskController"),
                              tc_tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // navigator.locks: provided by the native LockManager in LV8Net
    // (G4 ledger chunk 9); the LV8IdbFs placeholder was removed to avoid
    // overriding it.
    // navigator.storage.getDirectory() -> root handle
    {
      v8::Local<v8::Value> navigator;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "navigator"))
               .ToLocal(&navigator) ||
          !navigator->IsObject())
        return;
      v8::Local<v8::Value> storage;
      if (!navigator.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "storage"))
               .ToLocal(&storage) ||
          !storage->IsObject())
        return;
      v8::Local<v8::String> key = ToV8Str(isolate, "getDirectory");
      v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            (void)resolver->Resolve(ctx, MakeFsHandle(iso, ctx, "", "", true))
                .FromMaybe(false);
            info.GetReturnValue().Set(resolver->GetPromise());
          });
      fn->SetClassName(key);
      storage.As<v8::Object>()
          ->DefineOwnProperty(context, key,
                              fn->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // global handle constructors for instanceof checks
    for (int dir = 0; dir < 2; dir++)
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(
          ToV8Str(isolate, dir ? "FileSystemDirectoryHandle"
                               : "FileSystemFileHandle"));
      (void)context
          ->Global()
          ->DefineOwnProperty(
              context,
              ToV8Str(isolate, dir ? "FileSystemDirectoryHandle"
                                   : "FileSystemFileHandle"),
              tpl->GetFunction(context).ToLocalChecked(),
              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
  }

  // ------------------------------------------------------ FS handle body

  v8::Local<v8::Object> MakeFsHandle(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const std::string& path,
                                     const std::string& name, bool dir)
  {
    v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
    tpl->SetClassName(
        ToV8Str(isolate, dir ? "FileSystemDirectoryHandle"
                             : "FileSystemFileHandle"));
    v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
    inst->SetInternalFieldCount(0);
    // instantiate first; methods ride as own properties (the prototype
    // is swapped for the global constructor's prototype below so
    // instanceof holds, which would hide prototype-registered methods)
    v8::Local<v8::Object> obj = inst->NewInstance(context).ToLocalChecked();
    (void)obj->Set(context, ToV8Str(isolate, "kind"),
                   ToV8Str(isolate, dir ? "directory" : "file"))
        .FromMaybe(false);
    (void)obj->Set(context, ToV8Str(isolate, "name"), ToV8Str(isolate, name))
        .FromMaybe(false);
    (void)obj->SetPrivate(context, FsKey(isolate, "lv8.fs.path"),
                          ToV8Str(isolate, path))
        .FromMaybe(false);
    auto method = [&](const char* method_name, v8::FunctionCallback cb) {
      v8::Local<v8::String> key = ToV8Str(isolate, method_name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, cb);
      fn->SetClassName(key);
      (void)obj->Set(context, key, fn->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
    };

    if (dir)
    {
      // getDirectoryHandle/getFileHandle bodies (thunks defined after
      // FsChildHandleImpl below)
      method("getDirectoryHandle", FsChildHandleDirThunk);
      method("getFileHandle", FsChildHandleFileThunk);
      // resolve(target): path segments from this handle to the target
      method("resolve",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               v8::Local<v8::Promise::Resolver> resolver =
                   v8::Promise::Resolver::New(ctx).ToLocalChecked();
               v8::Local<v8::Array> segments = v8::Array::New(iso);
               if (info.Length() >= 1 && info[0]->IsObject())
               {
                 v8::Local<v8::Value> path;
                 if (info[0]
                         .As<v8::Object>()
                         ->GetPrivate(ctx, FsKey(iso, "lv8.fs.path"))
                         .ToLocal(&path) &&
                     path->IsString())
                 {
                   std::string p = ToUtf8(iso, path);
                   uint32_t n = 0;
                   size_t start = 0;
                   while (start <= p.size())
                   {
                     size_t slash = p.find('/', start);
                     std::string part =
                         slash == std::string::npos
                             ? p.substr(start)
                             : p.substr(start, slash - start);
                     if (!part.empty())
                       (void)segments->Set(ctx, n++, ToV8Str(iso, part))
                           .FromMaybe(false);
                     if (slash == std::string::npos)
                       break;
                     start = slash + 1;
                   }
                 }
               }
               (void)resolver->Resolve(ctx, segments).FromMaybe(false);
               info.GetReturnValue().Set(resolver->GetPromise());
             });
    }
    else
    {
      // createWritable(): writer applies writes to the file eagerly
      method("createWritable",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               v8::Local<v8::Promise::Resolver> resolver =
                   v8::Promise::Resolver::New(ctx).ToLocalChecked();
               v8::Local<v8::FunctionTemplate> w_tpl =
                   v8::FunctionTemplate::New(iso);
               w_tpl->SetClassName(
                   ToV8Str(iso, "FileSystemWritableFileStream"));
               v8::Local<v8::ObjectTemplate> w_inst =
                   w_tpl->InstanceTemplate();
               w_inst->SetInternalFieldCount(0);
               // M10: zero own symbols on instances (tag on prototype)
               w_tpl->PrototypeTemplate()
                   ->Set(v8::Symbol::GetToStringTag(iso),
                         ToV8Str(iso, "FileSystemWritableFileStream"),
                         static_cast<v8::PropertyAttribute>(
                             v8::PropertyAttribute::ReadOnly |
                             v8::PropertyAttribute::DontEnum));
               auto w_method = [&](const char* name2, v8::FunctionCallback cb) {
                 v8::Local<v8::String> key = ToV8Str(iso, name2);
                 v8::Local<v8::FunctionTemplate> wfn =
                     v8::FunctionTemplate::New(iso, cb);
                 wfn->SetClassName(key);
                 w_tpl->PrototypeTemplate()->Set(key, wfn);
               };
               // write(data): string replaces from 0; {position, data}
               // overwrites at the offset
               w_method("write",
                        [](const v8::FunctionCallbackInfo<v8::Value>&
                           cb_info) {
                          v8::Isolate* iso2 = cb_info.GetIsolate();
                          v8::Local<v8::Context> ctx2 =
                              iso2->GetCurrentContext();
                          ContextState* st2 = GetState(ctx2);
                          v8::Local<v8::Promise::Resolver> res =
                              v8::Promise::Resolver::New(ctx2)
                                  .ToLocalChecked();
                          v8::Local<v8::Value> path;
                          if (st2 == nullptr || cb_info.Length() < 1 ||
                              !cb_info.This()
                                   ->GetPrivate(ctx2,
                                                FsKey(iso2, "lv8.fs.path"))
                                   .ToLocal(&path) ||
                              !path->IsString())
                          {
                            cb_info.GetReturnValue().Set(res->GetPromise());
                            return;
                          }
                          ContextState::FsNode* file =
                              FsFind(&st2->fs_root, ToUtf8(iso2, path));
                          if (file == nullptr || file->dir)
                          {
                            cb_info.GetReturnValue().Set(res->GetPromise());
                            return;
                          }
                          std::string data;
                          double position = 0;
                          if (cb_info[0]->IsString())
                          {
                            data = ToUtf8(iso2, cb_info[0]);
                          }
                          else if (cb_info[0]->IsObject())
                          {
                            v8::Local<v8::Value> field;
                            if (cb_info[0]
                                    .As<v8::Object>()
                                    ->Get(ctx2, ToV8Str(iso2, "position"))
                                    .ToLocal(&field) &&
                                field->IsNumber())
                              position =
                                  field->NumberValue(ctx2).FromMaybe(0);
                            if (cb_info[0]
                                    .As<v8::Object>()
                                    ->Get(ctx2, ToV8Str(iso2, "data"))
                                    .ToLocal(&field) &&
                                field->IsString())
                              data = ToUtf8(iso2, field);
                          }
                          if (position >
                              static_cast<double>(file->content.size()))
                            position =
                                static_cast<double>(file->content.size());
                          if (file->content.size() <
                              static_cast<size_t>(position) + data.size())
                            file->content.resize(
                                static_cast<size_t>(position) + data.size());
                          file->content.replace(
                              static_cast<size_t>(position), data.size(),
                              data);
                          (void)res
                              ->Resolve(ctx2, v8::Undefined(iso2))
                              .FromMaybe(false);
                          cb_info.GetReturnValue().Set(res->GetPromise());
                        });
               w_method("close",
                        [](const v8::FunctionCallbackInfo<v8::Value>&
                           cb_info) {
                          v8::Isolate* iso2 = cb_info.GetIsolate();
                          v8::Local<v8::Context> ctx2 =
                              iso2->GetCurrentContext();
                          v8::Local<v8::Promise::Resolver> res =
                              v8::Promise::Resolver::New(ctx2)
                                  .ToLocalChecked();
                          (void)res
                              ->Resolve(ctx2, v8::Undefined(iso2))
                              .FromMaybe(false);
                          cb_info.GetReturnValue().Set(res->GetPromise());
                        });
               v8::Local<v8::Object> writer =
                   w_inst->NewInstance(ctx).ToLocalChecked();
               v8::Local<v8::Value> path;
               std::string path_str;
               if (info.This()
                       ->GetPrivate(ctx, FsKey(iso, "lv8.fs.path"))
                       .ToLocal(&path) &&
                   path->IsString())
                 path_str = ToUtf8(iso, path);
               (void)writer
                   ->SetPrivate(ctx, FsKey(iso, "lv8.fs.path"),
                                ToV8Str(iso, path_str))
                   .FromMaybe(false);
               (void)resolver->Resolve(ctx, writer).FromMaybe(false);
               info.GetReturnValue().Set(resolver->GetPromise());
             });
      // getFile(): text() reads the current content
      method("getFile",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               ContextState* st = GetState(ctx);
               v8::Local<v8::Promise::Resolver> resolver =
                   v8::Promise::Resolver::New(ctx).ToLocalChecked();
               v8::Local<v8::Object> file_obj = v8::Object::New(iso);
               std::string path_str;
               v8::Local<v8::Value> path;
               if (info.This()
                       ->GetPrivate(ctx, FsKey(iso, "lv8.fs.path"))
                       .ToLocal(&path) &&
                   path->IsString())
                 path_str = ToUtf8(iso, path);
               std::string content;
               if (st != nullptr)
               {
                 ContextState::FsNode* file =
                     FsFind(&st->fs_root, path_str);
                 if (file != nullptr && !file->dir)
                   content = file->content;
               }
               (void)file_obj
                   ->SetPrivate(ctx, FsKey(iso, "lv8.fs.content"),
                                ToV8Str(iso, content))
                   .FromMaybe(false);
               {
                 v8::Local<v8::String> key = ToV8Str(iso, "text");
                 v8::Local<v8::FunctionTemplate> tfn =
                     v8::FunctionTemplate::New(
                         iso,
                         [](const v8::FunctionCallbackInfo<v8::Value>&
                                cb_info) {
                           v8::Isolate* iso2 = cb_info.GetIsolate();
                           v8::Local<v8::Context> ctx2 =
                               iso2->GetCurrentContext();
                           v8::Local<v8::Promise::Resolver> res =
                               v8::Promise::Resolver::New(ctx2)
                                   .ToLocalChecked();
                           v8::Local<v8::Value> content;
                           std::string text;
                           if (cb_info.This()
                                   ->GetPrivate(ctx2,
                                                FsKey(iso2,
                                                      "lv8.fs.content"))
                                   .ToLocal(&content) &&
                               content->IsString())
                             text = ToUtf8(iso2, content);
                           (void)res
                               ->Resolve(ctx2, ToV8Str(iso2, text))
                               .FromMaybe(false);
                           cb_info.GetReturnValue().Set(res->GetPromise());
                         });
                 tfn->SetClassName(key);
                 file_obj->Set(ctx, key,
                               tfn->GetFunction(ctx).ToLocalChecked())
                     .FromMaybe(false);
               }
               (void)resolver->Resolve(ctx, file_obj).FromMaybe(false);
               info.GetReturnValue().Set(resolver->GetPromise());
             });
    }
    // prototype points at the global constructor's prototype so
    // instanceof holds (methods are own properties, unaffected)
    {
      v8::Local<v8::Value> ctor;
      if (context
              ->Global()
              ->Get(context,
                    ToV8Str(isolate, dir ? "FileSystemDirectoryHandle"
                                         : "FileSystemFileHandle"))
              .ToLocal(&ctor) &&
          ctor->IsFunction())
      {
        v8::Local<v8::Value> proto;
        if (ctor.As<v8::Function>()
                ->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&proto) &&
            proto->IsObject())
          (void)obj->SetPrototype(context, proto.As<v8::Object>())
              .FromMaybe(false);
      }
    }
    return obj;
  }

  int PendingIdbTasks(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    (void)isolate;
    ContextState* state = GetState(context);
    return state == nullptr ? 0 : static_cast<int>(state->pending_idb.size());
  }

  void DispatchIdbTasks(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        bool open_only)
  {
    ContextState* state = GetState(context);
    if (state == nullptr)
      return;
    struct DeferredOpenSuccess
    {
      v8::Global<v8::Object> request;
      v8::Global<v8::Object> database;
    };
    std::vector<DeferredOpenSuccess> deferred;
    size_t cursor = 0;
    while (cursor < state->pending_idb.size())
    {
      if (open_only && state->pending_idb[cursor].kind != 0)
      {
        ++cursor;
        continue;
      }
      ContextState::IdbTask task = std::move(state->pending_idb[cursor]);
      state->pending_idb.erase(state->pending_idb.begin() + cursor);
      v8::Local<v8::Object> request = task.request.Get(isolate);
      if (task.kind == 0)
      {
        v8::Local<v8::Object> db =
            MakeIdbDatabase(isolate, context, task.db_name,
                            task.db_version);
        if (task.need_upgrade)
        {
          double old_version = 0.0;
          v8::Local<v8::Value> ov;
          if (request
                  ->GetPrivate(context, IdbKey(isolate, "lv8.idb.oldVersion"))
                  .ToLocal(&ov) &&
              ov->IsNumber())
            old_version = ov->NumberValue(context).FromMaybe(0.0);
          FireIdbHandler(isolate, context, request, "onupgradeneeded", db,
                         old_version, task.db_version);
          // The reference lets other open requests settle before the
          // upgraded request's success event, while retaining the same
          // database object across its upgrade and success callbacks.
          DeferredOpenSuccess later;
          later.request.Reset(isolate, request);
          later.database.Reset(isolate, db);
          deferred.push_back(std::move(later));
        }
        else
          FireIdbHandler(isolate, context, request, "onsuccess", db);
      }
      else
      {
        v8::Local<v8::Value> result;
        if (request
                ->GetPrivate(context, IdbKey(isolate, "lv8.idb.result"))
                .ToLocal(&result))
          FireIdbHandler(isolate, context, request, "onsuccess", result);
        else
          FireIdbHandler(isolate, context, request, "onsuccess",
                         v8::Undefined(isolate));
      }
    }
    for (auto& later : deferred)
      FireIdbHandler(isolate, context, later.request.Get(isolate),
                     "onsuccess", later.database.Get(isolate));
  }
int RegisterAnimationFrame(v8::Isolate* isolate,
                           v8::Local<v8::Context> context,
                           v8::Local<v8::Function> callback)
{
  ContextState* state = GetState(context);
  if (state == nullptr)
    return 0;
  int id = state->next_raf_id++;
  double interval = state->animation_frame_interval > 0.0
      ? state->animation_frame_interval : 16.0;
  double clock = LogicalClock(isolate, context);
  // fire at the next interval boundary strictly after the current clock
  double fire_at = (std::floor(clock / interval) + 1.0) * interval;
  state->raf_queue.push_back(
      {id, fire_at, v8::Global<v8::Function>(isolate, callback)});
  return id;
}

void CancelAnimationFrame(v8::Isolate* isolate,
                          v8::Local<v8::Context> context, int id)
{
  ContextState* state = GetState(context);
  if (state == nullptr)
    return;
  for (auto it = state->raf_queue.begin(); it != state->raf_queue.end();
       ++it)
  {
    if (it->id == id)
    {
      state->raf_queue.erase(it);
      return;
    }
  }
}

int DispatchAnimationFrames(v8::Isolate* isolate,
                            v8::Local<v8::Context> context)
{
  ContextState* state = GetState(context);
  if (state == nullptr || state->raf_queue.empty())
    return 0;
  int fired = 0;
  std::vector<ContextState::RafEntry> batch;
  batch.swap(state->raf_queue);
  double now = LogicalClock(isolate, context);
  for (auto& entry : batch)
  {
    if (entry.fire_at > now + 1e-9)
    {
      // boundary not reached: requeue (chained rAFs land here)
      state->raf_queue.push_back(std::move(entry));
      continue;
    }
    fired++;
    v8::Local<v8::Function> fn = entry.fn.Get(isolate);
    v8::Local<v8::Value> argv[1] = {v8::Number::New(isolate, entry.fire_at)};
    v8::TryCatch try_catch(isolate);
    (void)fn->Call(context, context->Global(), 1, argv)
        .FromMaybe(v8::Local<v8::Value>());
    if (try_catch.HasCaught())
      CJavascriptException::ThrowIf(isolate, try_catch);
  }
  return fired;
}

void DispatchSchedulerTasks(v8::Isolate* isolate,
                            v8::Local<v8::Context> context)
{
  ContextState* state = GetState(context);
  if (state == nullptr)
    return;
  while (!state->scheduler_tasks.empty())
  {
    ContextState::SchedulerTask task =
        std::move(state->scheduler_tasks.front());
    state->scheduler_tasks.erase(state->scheduler_tasks.begin());
    v8::Local<v8::Function> fn = task.fn.Get(isolate);
    v8::TryCatch try_catch(isolate);
    (void)fn->Call(context, context->Global(), 0, nullptr)
        .FromMaybe(v8::Local<v8::Value>());
    if (try_catch.HasCaught())
      CJavascriptException::ThrowIf(isolate, try_catch);
  }
}
} // namespace LV8
