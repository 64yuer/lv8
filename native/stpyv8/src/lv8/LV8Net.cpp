// lv8 G3: offline network bridge. fetch/caches/performance never touch a
// socket: requests resolve against the per-context resource table
// (ContextState::resources, seeded from the facade's add_resource), are
// recorded in __lv8__.netLog with browser-default sec-ch-* headers, feed
// performance resource timings, and any "set-cookie" headers mutate the
// shared cookie jar. Response bodies arrive through text()/json()
// promises so the microtask checkpoint (drain) drives assertions.

#include "LV8.h"

#include "../Exception.h"

#include <chrono>
#include "lexbor/html/html.h"
#include "lexbor/dom/dom.h"
#include <algorithm>

#include <cmath>
#include <regex>
#include <cstring>
#include <limits>

#include <cstdio>
#include <cctype>
#include <string>
#include <vector>

namespace LV8
  {
  namespace
    {
    using HeaderList = std::vector<std::pair<std::string, std::string>>;

    std::string ToLower(std::string s)
    {
      for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    }

    // -------------------------------------------------------- net logging

    v8::Local<v8::Array> BuildNetEntries(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context,
                                         const std::vector<ContextState::NetEntry>& log)
    {
      v8::Local<v8::Array> arr = v8::Array::New(isolate);
      for (size_t i = 0; i < log.size(); i++)
      {
        v8::Local<v8::Object> entry = v8::Object::New(isolate);
        (void)entry
            ->Set(context, ToV8Str(isolate, "method"),
                  ToV8Str(isolate, log[i].method))
            .FromMaybe(false);
        (void)entry
            ->Set(context, ToV8Str(isolate, "url"),
                  ToV8Str(isolate, log[i].url))
            .FromMaybe(false);
        v8::Local<v8::Object> headers = v8::Object::New(isolate);
        for (const auto& h : log[i].headers)
        {
          (void)headers
              ->Set(context, ToV8Str(isolate, h.first),
                    ToV8Str(isolate, h.second))
              .FromMaybe(false);
        }
        (void)entry
            ->Set(context, ToV8Str(isolate, "headers"), headers)
            .FromMaybe(false);
        (void)entry
            ->Set(context, ToV8Str(isolate, "cookieHeader"),
                  ToV8Str(isolate, log[i].cookie_header))
            .FromMaybe(false);
        if (log[i].has_body)
        {
          // body key appears only when a request body exists
          (void)entry
              ->Set(context, ToV8Str(isolate, "body"),
                    ToV8Str(isolate, log[i].body))
              .FromMaybe(false);
        }
        (void)arr->Set(context, static_cast<uint32_t>(i), entry)
            .FromMaybe(false);
      }
      return arr;
    }

    // Rebuild the cached entries array so alias reads keep identity until
    // the next request lands.
    void RefreshNetLog(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       ContextState* state)
    {
      v8::Local<v8::Array> entries =
          BuildNetEntries(isolate, context, state->net_log);
      state->netlog_entries.Reset(isolate, entries);
      v8::Local<v8::Object> netlog = state->netlog_object.Get(isolate);
      if (!netlog.IsEmpty())
      {
        (void)netlog
            ->Set(context, ToV8Str(isolate, "entries"), entries)
            .FromMaybe(false);
      }
    }

    void AppendNetEntryImpl(v8::Isolate* isolate,
                            v8::Local<v8::Context> context,
                            ContextState* state,
                            const ContextState::NetEntry& entry)
    {
      state->net_log.push_back(entry);
      RefreshNetLog(isolate, context, state);
    }

    // Browser-default client hint headers derived from the profile's
    // navigator.userAgentData (falling back to Chrome-on-Windows values).
    HeaderList DefaultRequestHeaders(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     ContextState* state)
    {
      HeaderList headers;
      std::string brands =
          "\"Chromium\";v=\"127\", \"Not)A;Brand\";v=\"24\"";
      std::string mobile = "?0";
      std::string platform = "\"Windows\"";
      if (!state->profile_data.IsEmpty())
      {
        v8::Local<v8::Value> nav;
        if (state->profile_data
                .Get(isolate)
                ->Get(context, ToV8Str(isolate, "navigator"))
                .ToLocal(&nav) &&
            nav->IsObject())
        {
          v8::Local<v8::Value> ua_data;
          if (nav.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "userAgentData"))
                  .ToLocal(&ua_data) &&
              ua_data->IsObject())
          {
            v8::Local<v8::Object> uad = ua_data.As<v8::Object>();
            v8::Local<v8::Value> brand_list;
            if (uad->Get(context, ToV8Str(isolate, "brands"))
                    .ToLocal(&brand_list) &&
                brand_list->IsArray())
            {
              v8::Local<v8::Array> arr = brand_list.As<v8::Array>();
              std::string joined;
              for (uint32_t i = 0; i < arr->Length(); i++)
              {
                v8::Local<v8::Value> item = arr->Get(context, i)
                                                .FromMaybe(
                                                    v8::Local<v8::Value>());
                if (!item->IsObject())
                  continue;
                v8::Local<v8::Value> brand;
                v8::Local<v8::Value> version;
                if (!item.As<v8::Object>()
                         ->Get(context, ToV8Str(isolate, "brand"))
                         .ToLocal(&brand) ||
                    !brand->IsString())
                  continue;
                (void)item.As<v8::Object>()
                    ->Get(context, ToV8Str(isolate, "version"))
                    .ToLocal(&version);
                if (!joined.empty())
                  joined += ", ";
                joined += "\"" + ToUtf8(isolate, brand) + "\"";
                if (version->IsString())
                  joined += ";v=\"" + ToUtf8(isolate, version) + "\"";
              }
              if (!joined.empty())
                brands = joined;
            }
            v8::Local<v8::Value> is_mobile;
            if (uad->Get(context, ToV8Str(isolate, "mobile"))
                    .ToLocal(&is_mobile))
              mobile = is_mobile->BooleanValue(isolate) ? "?1" : "?0";
            v8::Local<v8::Value> plat;
            if (uad->Get(context, ToV8Str(isolate, "platform"))
                    .ToLocal(&plat) &&
                plat->IsString())
              platform = "\"" + ToUtf8(isolate, plat) + "\"";
          }
        }
      }
      headers.push_back({"sec-ch-ua", brands});
      headers.push_back({"sec-ch-ua-mobile", mobile});
      headers.push_back({"sec-ch-ua-platform", platform});
      return headers;
    }

    // -------------------------------------------------------- responses

    // Response/Headers carry their data in Private symbols (string body,
    // [[k, v], ...] header list, integer status).

    v8::Local<v8::Private> RespKey(v8::Isolate* isolate, const char* name)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, name));
    }

    HeaderList ResponseHeaders(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Object> response)
    {
      HeaderList headers;
      v8::Local<v8::Value> raw;
      if (!response
               ->GetPrivate(context, RespKey(isolate, "lv8.resp.headers"))
               .ToLocal(&raw) ||
          !raw->IsArray())
        return headers;
      v8::Local<v8::Array> arr = raw.As<v8::Array>();
      for (uint32_t i = 0; i < arr->Length(); i++)
      {
        v8::Local<v8::Value> pair =
            arr->Get(context, i).FromMaybe(v8::Local<v8::Value>());
        if (!pair->IsArray())
          continue;
        v8::Local<v8::Array> kv = pair.As<v8::Array>();
        v8::Local<v8::Value> k = kv->Get(context, 0)
                                     .FromMaybe(v8::Local<v8::Value>());
        v8::Local<v8::Value> v = kv->Get(context, 1)
                                     .FromMaybe(v8::Local<v8::Value>());
        if (k->IsString() && v->IsString())
          headers.push_back({ToUtf8(isolate, k), ToUtf8(isolate, v)});
      }
      return headers;
    }

    v8::Local<v8::Object> MakeHeaders(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const HeaderList& headers)
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "Headers"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M6: the reference instances carry zero own symbols - the tag lives
      // on the prototype (these internal instances never see the exposed
      // window.Headers.prototype, so the anonymous template needs its own)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Headers"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      v8::Local<v8::Array> raw = v8::Array::New(isolate);
      for (size_t i = 0; i < headers.size(); i++)
      {
        v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
        (void)pair->Set(context, 0, ToV8Str(isolate, headers[i].first))
            .FromMaybe(false);
        (void)pair->Set(context, 1, ToV8Str(isolate, headers[i].second))
            .FromMaybe(false);
        (void)raw->Set(context, static_cast<uint32_t>(i), pair)
            .FromMaybe(false);
      }

      // headers.get(name) is case-insensitive
      auto get_fn = [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        if (info.Length() < 1 || !info[0]->IsString())
        {
          info.GetReturnValue().SetNull();
          return;
        }
        std::string wanted =
            ToLower(ToUtf8(iso, info[0]));
        for (const auto& h : ResponseHeaders(iso, ctx, info.This()))
        {
          if (ToLower(h.first) == wanted)
          {
            info.GetReturnValue().Set(ToV8Str(iso, h.second));
            return;
          }
        }
        info.GetReturnValue().SetNull();
      };
      auto has_fn = [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        if (info.Length() < 1 || !info[0]->IsString())
        {
          info.GetReturnValue().Set(false);
          return;
        }
        std::string wanted = ToLower(ToUtf8(iso, info[0]));
        for (const auto& h : ResponseHeaders(iso, ctx, info.This()))
        {
          if (ToLower(h.first) == wanted)
          {
            info.GetReturnValue().Set(true);
            return;
          }
        }
        info.GetReturnValue().Set(false);
      };
      auto m = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      m("get", get_fn);
      m("has", has_fn);
      // instantiate only after the prototype is fully populated
      v8::Local<v8::Object> obj =
          inst->NewInstance(context).ToLocalChecked();
      (void)obj
          ->SetPrivate(context, RespKey(isolate, "lv8.resp.headers"), raw)
          .FromMaybe(false);
      return obj;
    }

    // Response.json(body, init): JSON.stringify(undefined -> null) as body
    void ResponseStaticJson(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      v8::Local<v8::Value> ctor;
      if (!ctx->Global()
                   ->Get(ctx, ToV8Str(iso, "Response"))
                   .ToLocal(&ctor) ||
          !ctor->IsFunction())
        return;
      v8::Local<v8::Value> json_value =
          info.Length() >= 1 && !info[0]->IsUndefined()
              ? v8::Local<v8::Value>(info[0])
              : v8::Local<v8::Value>(v8::Null(iso));
      v8::Local<v8::Value> json_obj;
      if (!ctx->Global()->Get(ctx, ToV8Str(iso, "JSON")).ToLocal(&json_obj) ||
          !json_obj->IsObject())
        return;
      v8::Local<v8::Value> stringify;
      if (!json_obj.As<v8::Object>()
               ->Get(ctx, ToV8Str(iso, "stringify"))
               .ToLocal(&stringify) ||
          !stringify->IsFunction())
        return;
      v8::Local<v8::Value> sargs[1] = {json_value};
      v8::Local<v8::Value> body;
      if (!stringify.As<v8::Function>()
               ->Call(ctx, json_obj, 1, sargs)
               .ToLocal(&body))
        return;
      v8::Local<v8::Value> cargs[2] = {
          body,
          info.Length() >= 2 ? v8::Local<v8::Value>(info[1])
                             : v8::Local<v8::Value>(v8::Undefined(iso))};
      v8::Local<v8::Value> response;
      if (ctor.As<v8::Function>()
              ->NewInstance(ctx, 2, cargs)
              .ToLocal(&response))
        info.GetReturnValue().Set(response);
    }

    // Response.error(): status-0 response with a non-mutable "error" type
    void ResponseStaticError(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      v8::Local<v8::Value> ctor;
      if (!ctx->Global()
                   ->Get(ctx, ToV8Str(iso, "Response"))
                   .ToLocal(&ctor) ||
          !ctor->IsFunction())
        return;
      v8::Local<v8::Object> init = v8::Object::New(iso);
      (void)init->Set(ctx, ToV8Str(iso, "status"),
                      v8::Integer::New(iso, 0))
          .FromMaybe(false);
      v8::Local<v8::Value> cargs[2] = {ToV8Str(iso, ""), init};
      v8::Local<v8::Value> response;
      if (!ctor.As<v8::Function>()
               ->NewInstance(ctx, 2, cargs)
               .ToLocal(&response) ||
          !response->IsObject())
        return;
      (void)response.As<v8::Object>()
          ->DefineOwnProperty(ctx, ToV8Str(iso, "type"),
                              ToV8Str(iso, "error"),
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::ReadOnly |
                                  v8::PropertyAttribute::DontEnum |
                                  v8::PropertyAttribute::DontDelete))
          .FromMaybe(false);
      info.GetReturnValue().Set(response);
    }

    // Response.redirect(url, status = 302): location header on a plain
    // response
    void ResponseStaticRedirect(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* iso = info.GetIsolate();
      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
      v8::Local<v8::Value> ctor;
      if (!ctx->Global()
                   ->Get(ctx, ToV8Str(iso, "Response"))
                   .ToLocal(&ctor) ||
          !ctor->IsFunction())
        return;
      int status = 302;
      if (info.Length() >= 2 && info[1]->IsNumber())
      {
        double raw = info[1]->NumberValue(ctx).FromMaybe(302.0);
        status = raw != 0.0 ? static_cast<int>(raw) : 302;
      }
      v8::Local<v8::Object> init = v8::Object::New(iso);
      (void)init->Set(ctx, ToV8Str(iso, "status"),
                      v8::Integer::New(iso, status))
          .FromMaybe(false);
      v8::Local<v8::Value> cargs[2] = {ToV8Str(iso, ""), init};
      v8::Local<v8::Value> response;
      if (!ctor.As<v8::Function>()
               ->NewInstance(ctx, 2, cargs)
               .ToLocal(&response) ||
          !response->IsObject())
        return;
      v8::Local<v8::Value> headers;
      if (response.As<v8::Object>()
              ->Get(ctx, ToV8Str(iso, "headers"))
              .ToLocal(&headers) &&
          headers->IsObject())
      {
        v8::Local<v8::Value> set_fn;
        if (headers.As<v8::Object>()
                ->Get(ctx, ToV8Str(iso, "set"))
                .ToLocal(&set_fn) &&
            set_fn->IsFunction())
        {
          v8::Local<v8::Value> sargs[2] = {ToV8Str(iso, "location"),
                                           info.Length() >= 1
                                               ? v8::Local<v8::Value>(info[0])
                                               : v8::Local<v8::Value>(
                                                     v8::Undefined(iso))};
          v8::TryCatch try_catch(iso);
          (void)set_fn.As<v8::Function>()
              ->Call(ctx, headers, 2, sargs)
              .FromMaybe(v8::Local<v8::Value>());
          try_catch.Reset();
        }
      }
      info.GetReturnValue().Set(response);
    }

    void ResponseOk(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> status;
      if (info.This()
              ->GetPrivate(context, RespKey(isolate, "lv8.resp.status"))
              .ToLocal(&status) &&
          status->IsInt32())
      {
        int32_t code = status->Int32Value(context).FromMaybe(0);
        info.GetReturnValue().Set(code >= 200 && code < 300);
        return;
      }
      info.GetReturnValue().Set(false);
    }

    void ResponseStatus(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> status;
      if (info.This()
              ->GetPrivate(context, RespKey(isolate, "lv8.resp.status"))
              .ToLocal(&status) &&
          status->IsInt32())
        info.GetReturnValue().Set(status);
      else
        info.GetReturnValue().Set(0);
    }

    void ResponseText(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Value> body;
      if (info.This()
              ->GetPrivate(context, RespKey(isolate, "lv8.resp.body"))
              .ToLocal(&body) &&
          body->IsString())
        (void)resolver->Resolve(context, body).FromMaybe(false);
      else
        (void)resolver->Resolve(context, ToV8Str(isolate, ""))
            .FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void ResponseJson(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Value> body;
      std::string text;
      if (info.This()
              ->GetPrivate(context, RespKey(isolate, "lv8.resp.body"))
              .ToLocal(&body) &&
          body->IsString())
        text = ToUtf8(isolate, body);
      v8::Local<v8::Value> parsed;
      if (!v8::JSON::Parse(context, ToV8Str(isolate, text)).ToLocal(&parsed))
      {
        (void)resolver
            ->Reject(context,
                     ToV8Str(isolate, "SyntaxError: response body is not JSON"))
            .FromMaybe(false);
      }
      else
      {
        (void)resolver->Resolve(context, parsed).FromMaybe(false);
      }
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    // latin-1-safe V8 string for binary bodies (UTF-8 encoding mangles
    // bytes >= 0x80; resource bodies are raw byte strings)
    v8::Local<v8::String> Latin1ToV8(v8::Isolate* isolate,
                                     const std::string& bytes)
    {
      if (bytes.empty()) return v8::String::Empty(isolate);
      return v8::String::NewFromOneByte(
                 isolate, reinterpret_cast<const uint8_t*>(bytes.data()),
                 v8::NewStringType::kNormal,
                 static_cast<int>(bytes.size()))
          .ToLocalChecked();
    }

    // arrayBuffer(): body bytes (latin-1 chars map 1:1 to bytes 0-255)
    void ResponseArrayBuffer(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Value> body;
      if (!info.This()
               ->GetPrivate(context, RespKey(isolate, "lv8.resp.body"))
               .ToLocal(&body) ||
          !body->IsString())
        body = ToV8Str(isolate, "");
      // stored bodies are one-byte (latin-1) strings; read bytes 1:1
      v8::Local<v8::String> text = body.As<v8::String>();
      int length = text->Length();
      v8::Local<v8::ArrayBuffer> buffer =
          v8::ArrayBuffer::New(isolate,
                               static_cast<size_t>(length));
      if (length > 0)
      {
        text->WriteOneByte(isolate,
                           static_cast<uint8_t*>(
                               buffer->GetBackingStore()->Data()),
                           0, length, v8::String::NO_NULL_TERMINATION);
      }
      (void)resolver->Resolve(context, buffer).FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    // ---- G4 ledger chunk 4: URL / URLSearchParams / URLPattern --------
    // Previously JS bootstrap classes (polyfills.py). The algorithms are a
    // faithful port of the reference bootstrap's regex-based parsing (the
    // LV8Location parser normalizes differently and would change observable
    // serialization).

    v8::Local<v8::Private> UspPairsKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.usp.pairs"));
    }
    v8::Local<v8::Private> UspOnChangeKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.usp.onchange"));
    }
    v8::Local<v8::Private> UrlPartsKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.url.parts"));
    }
    v8::Local<v8::Private> UrlParamsKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.url.params"));
    }

    double UrlNumOr(v8::Local<v8::Context> context,
                    v8::Local<v8::Value> value, double fallback)
    {
      if (value.IsEmpty()) return fallback;
      return value->NumberValue(context).FromMaybe(fallback);
    }

    // ---- percent encoding ----------------------------------------------

    int HexValue(char c)
    {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    }

    // decodeURIComponent semantics: '+' stays '+', UTF-8 percent runs
    // decode to code points, malformed input throws URIError
    bool PercentDecodeStrict(v8::Isolate* isolate, const std::string& in,
                             std::string& out)
    {
      std::vector<uint8_t> bytes;
      for (size_t i = 0; i < in.size(); i++)
      {
        if (in[i] == '%')
        {
          if (i + 2 >= in.size() || HexValue(in[i + 1]) < 0 ||
              HexValue(in[i + 2]) < 0)
          {
            isolate->ThrowException(ToV8Str(isolate, "URIError: URI malformed"));
            return false;
          }
          bytes.push_back(static_cast<uint8_t>(
              HexValue(in[i + 1]) * 16 + HexValue(in[i + 2])));
          i += 2;
        }
        else
        {
          bytes.push_back(static_cast<uint8_t>(in[i]));
        }
      }
      // validate UTF-8 well-formedness
      for (size_t i = 0; i < bytes.size();)
      {
        uint8_t b = bytes[i];
        size_t want = 1;
        if (b >= 0xF0) want = 4;
        else if (b >= 0xE0) want = 3;
        else if (b >= 0xC0) want = 2;
        else if (b > 0x7F)
        {
          isolate->ThrowException(ToV8Str(isolate, "URIError: URI malformed"));
          return false;
        }
        if (i + want > bytes.size())
        {
          isolate->ThrowException(ToV8Str(isolate, "URIError: URI malformed"));
          return false;
        }
        for (size_t k = 1; k < want; k++)
        {
          if ((bytes[i + k] & 0xC0) != 0x80)
          {
            isolate->ThrowException(
                ToV8Str(isolate, "URIError: URI malformed"));
            return false;
          }
        }
        i += want;
      }
      out.assign(bytes.begin(), bytes.end());
      return true;
    }

    std::string PercentEncodeComponent(const std::string& in)
    {
      static const char* hex = "0123456789ABCDEF";
      std::string out;
      for (unsigned char c : in)
      {
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                     c == '.' || c == '~' || c == '!' || c == '\'' ||
                     c == '(' || c == ')' || c == '*';
        if (plain) out += static_cast<char>(c);
        else
        {
          out += '%';
          out += hex[c >> 4];
          out += hex[c & 0xF];
        }
      }
      return out;
    }

    // ---- URLSearchParams -------------------------------------------------

    v8::Local<v8::Array> UspPairs(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> self)
    {
      v8::Local<v8::Value> pairs;
      if (self->GetPrivate(context, UspPairsKey(isolate)).ToLocal(&pairs) &&
          pairs->IsArray())
        return pairs.As<v8::Array>();
      return v8::Array::New(isolate);
    }

    void UspNotify(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> self)
    {
      v8::Local<v8::Value> onchange;
      if (self->GetPrivate(context, UspOnChangeKey(isolate)).ToLocal(&onchange) &&
          onchange->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {self};
        v8::TryCatch try_catch(isolate);
        (void)onchange.As<v8::Function>()
            ->Call(context, self, 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        if (try_catch.HasCaught())
          CJavascriptException::ThrowIf(isolate, try_catch);
      }
    }

    void UspAppendPair(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Array> pairs,
                       v8::Local<v8::Value> key, v8::Local<v8::Value> value)
    {
      v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
      (void)pair->Set(context, 0, key).FromMaybe(false);
      (void)pair->Set(context, 1, value).FromMaybe(false);
      (void)pairs->Set(context, pairs->Length(), pair).FromMaybe(false);
    }

    // JS: item.split("=", 2) -- value truncates at the second '='
    void SplitPair(const std::string& item, std::string& key,
                   std::string& value)
    {
      size_t eq = item.find('=');
      if (eq == std::string::npos)
      {
        key = item;
        value.clear();
        return;
      }
      key = item.substr(0, eq);
      size_t eq2 = item.find('=', eq + 1);
      value = eq2 == std::string::npos ? item.substr(eq + 1)
                                       : item.substr(eq + 1, eq2 - eq - 1);
    }

    void UspParse(v8::Isolate* isolate, v8::Local<v8::Context> context,
                  v8::Local<v8::Array> pairs, const std::string& raw)
    {
      std::string query = raw;
      if (!query.empty() && query[0] == '?') query.erase(0, 1);
      size_t start = 0;
      while (start <= query.size())
      {
        size_t amp = query.find('&', start);
        std::string item = query.substr(
            start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!item.empty())
        {
          std::string key, value;
          SplitPair(item, key, value);
          std::string key_dec, value_dec;
          std::string key_spaced = key;
          std::string value_spaced = value;
          for (auto& c : key_spaced)
            if (c == '+') c = ' ';
          for (auto& c : value_spaced)
            if (c == '+') c = ' ';
          if (PercentDecodeStrict(isolate, key_spaced, key_dec) &&
              PercentDecodeStrict(isolate, value_spaced, value_dec))
            UspAppendPair(isolate, context, pairs, ToV8Str(isolate, key_dec),
                          ToV8Str(isolate, value_dec));
          else
            return;  // URIError already thrown
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
      }
    }

    void UspAppend(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2) return;
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      UspAppendPair(isolate, context, pairs, info[0], info[1]);
      UspNotify(isolate, context, info.This());
    }

    void UspDelete(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1) return;
      std::string name = ToUtf8(isolate, info[0]);
      bool has_value = info.Length() >= 2 && !info[1]->IsUndefined();
      std::string value = has_value ? ToUtf8(isolate, info[1]) : "";
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      v8::Local<v8::Array> kept = v8::Array::New(isolate);
      uint32_t n = 0;
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk, pv;
        if (!pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) ||
            !pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv))
          continue;
        std::string pk_text = ToUtf8(isolate, pk);
        bool drop = pk_text == name &&
                    (!has_value || ToUtf8(isolate, pv) == value);
        if (drop) continue;
        (void)kept->Set(context, n++, pair).FromMaybe(false);
      }
      (void)info.This()->SetPrivate(context, UspPairsKey(isolate), kept)
          .FromMaybe(false);
      UspNotify(isolate, context, info.This());
    }

    void UspGet(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      std::string name = ToUtf8(isolate, info[0]);
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk, pv;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) &&
            pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv) &&
            ToUtf8(isolate, pk) == name)
        {
          info.GetReturnValue().Set(pv);
          return;
        }
      }
      info.GetReturnValue().SetNull();
    }

    void UspGetAll(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      if (info.Length() >= 1)
      {
        std::string name = ToUtf8(isolate, info[0]);
        v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
        for (uint32_t i = 0; i < pairs->Length(); i++)
        {
          v8::Local<v8::Value> pair;
          if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
            continue;
          v8::Local<v8::Value> pk, pv;
          if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) &&
              pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv) &&
              ToUtf8(isolate, pk) == name)
            (void)out->Set(context, n++, pv).FromMaybe(false);
        }
      }
      info.GetReturnValue().Set(out);
    }

    void UspHas(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1)
      {
        info.GetReturnValue().Set(v8::Boolean::New(isolate, false));
        return;
      }
      std::string name = ToUtf8(isolate, info[0]);
      bool has_value = info.Length() >= 2 && !info[1]->IsUndefined();
      std::string value = has_value ? ToUtf8(isolate, info[1]) : "";
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk, pv;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) &&
            pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv) &&
            ToUtf8(isolate, pk) == name &&
            (!has_value || ToUtf8(isolate, pv) == value))
        {
          info.GetReturnValue().Set(v8::Boolean::New(isolate, true));
          return;
        }
      }
      info.GetReturnValue().Set(v8::Boolean::New(isolate, false));
    }

    void UspSet(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2) return;
      std::string name = ToUtf8(isolate, info[0]);
      std::string value = ToUtf8(isolate, info[1]);
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      int32_t first_at = -1;
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) &&
            ToUtf8(isolate, pk) == name)
        {
          first_at = static_cast<int32_t>(i);
          break;
        }
      }
      v8::Local<v8::Array> kept = v8::Array::New(isolate);
      uint32_t n = 0;
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) &&
            ToUtf8(isolate, pk) == name)
          continue;
        (void)kept->Set(context, n++, pair).FromMaybe(false);
      }
      v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
      (void)pair->Set(context, 0, info[0]).FromMaybe(false);
      (void)pair->Set(context, 1, info[1]).FromMaybe(false);
      if (first_at < 0 || first_at > static_cast<int32_t>(n))
        (void)kept->Set(context, n, pair).FromMaybe(false);
      else
      {
        // splice at the first original position
        v8::Local<v8::Array> spliced = v8::Array::New(isolate);
        uint32_t m = 0;
        for (uint32_t i = 0; i < n + 1; i++)
        {
          if (i == static_cast<uint32_t>(first_at))
            (void)spliced->Set(context, m++, pair).FromMaybe(false);
          v8::Local<v8::Value> item;
          if (i < n && kept->Get(context, i).ToLocal(&item))
            (void)spliced->Set(context, m++, item).FromMaybe(false);
        }
        kept = spliced;
      }
      (void)info.This()->SetPrivate(context, UspPairsKey(isolate), kept)
          .FromMaybe(false);
      UspNotify(isolate, context, info.This());
    }

    void UspSort(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      std::vector<std::pair<std::string, v8::Local<v8::Value>>> rows;
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk, pv;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) &&
            pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv))
          rows.emplace_back(ToUtf8(isolate, pk), pv);
      }
      std::stable_sort(rows.begin(), rows.end(),
                       [](const auto& left, const auto& right) {
                         return left.first < right.first;
                       });
      v8::Local<v8::Array> sorted = v8::Array::New(isolate);
      uint32_t n = 0;
      for (auto& row : rows)
      {
        v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
        (void)pair->Set(context, 0, ToV8Str(isolate, row.first))
            .FromMaybe(false);
        (void)pair->Set(context, 1, row.second).FromMaybe(false);
        (void)sorted->Set(context, n++, pair).FromMaybe(false);
      }
      (void)info.This()->SetPrivate(context, UspPairsKey(isolate), sorted)
          .FromMaybe(false);
      UspNotify(isolate, context, info.This());
    }

    std::string UspToString(v8::Isolate* isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> self)
    {
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, self);
      std::string out;
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk, pv;
        if (!pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) ||
            !pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv))
          continue;
        if (!out.empty()) out += "&";
        std::string key = PercentEncodeComponent(ToUtf8(isolate, pk));
        std::string value = PercentEncodeComponent(ToUtf8(isolate, pv));
        auto plus = [](std::string t) {
          for (size_t k = 0; k + 3 <= t.size();)
          {
            if (t.compare(k, 3, "%20") == 0)
            {
              t[k] = '+';
              t.erase(k + 1, 2);
            }
            k += 1;
          }
          return t;
        };
        out += plus(key) + "=" + plus(value);
      }
      return out;
    }

    void UspToStringFn(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      info.GetReturnValue().Set(
          ToV8Str(isolate, UspToString(isolate, context, info.This())));
    }

    void UspSize(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      info.GetReturnValue().Set(v8::Number::New(
          isolate,
          static_cast<double>(UspPairs(isolate, context, info.This())
                                  ->Length())));
    }

    void UspEntries(const v8::FunctionCallbackInfo<v8::Value>& info);

    v8::Local<v8::FunctionTemplate> UspIteratorTemplate(v8::Isolate* isolate)
    {
      // Symbol.iterator delegates to entries()
      return v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            UspEntries(info);
          });
    }

    void UspEntries(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (pairs->Get(context, i).ToLocal(&pair))
          (void)out->Set(context, n++, pair).FromMaybe(false);
      }
      v8::Local<v8::Value> array_ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "Array"))
               .ToLocal(&array_ctor) ||
          !array_ctor->IsObject())
        return;
      v8::Local<v8::Value> array_proto;
      if (!array_ctor.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "prototype"))
               .ToLocal(&array_proto) ||
          !array_proto->IsObject())
        return;
      v8::Local<v8::Value> iterator_fn;
      if (!array_proto.As<v8::Object>()
               ->Get(context, v8::Symbol::GetIterator(isolate))
               .ToLocal(&iterator_fn) ||
          !iterator_fn->IsFunction())
        return;
      v8::Local<v8::Value> result =
          iterator_fn.As<v8::Function>()
              ->Call(context, out, 0, nullptr)
              .FromMaybe(v8::Local<v8::Value>());
      if (!result.IsEmpty())
        info.GetReturnValue().Set(result);
    }

    void UspKeys(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk))
          (void)out->Set(context, n++, pk).FromMaybe(false);
      }
      v8::Local<v8::Value> array_ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "Array"))
               .ToLocal(&array_ctor) ||
          !array_ctor->IsObject())
        return;
      v8::Local<v8::Value> array_proto;
      if (!array_ctor.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "prototype"))
               .ToLocal(&array_proto) ||
          !array_proto->IsObject())
        return;
      v8::Local<v8::Value> iterator_fn;
      if (!array_proto.As<v8::Object>()
               ->Get(context, v8::Symbol::GetIterator(isolate))
               .ToLocal(&iterator_fn) ||
          !iterator_fn->IsFunction())
        return;
      v8::Local<v8::Value> result =
          iterator_fn.As<v8::Function>()
              ->Call(context, out, 0, nullptr)
              .FromMaybe(v8::Local<v8::Value>());
      if (!result.IsEmpty())
        info.GetReturnValue().Set(result);
    }

    void UspValues(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pv;
        if (pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv))
          (void)out->Set(context, n++, pv).FromMaybe(false);
      }
      v8::Local<v8::Value> array_ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "Array"))
               .ToLocal(&array_ctor) ||
          !array_ctor->IsObject())
        return;
      v8::Local<v8::Value> array_proto;
      if (!array_ctor.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "prototype"))
               .ToLocal(&array_proto) ||
          !array_proto->IsObject())
        return;
      v8::Local<v8::Value> iterator_fn;
      if (!array_proto.As<v8::Object>()
               ->Get(context, v8::Symbol::GetIterator(isolate))
               .ToLocal(&iterator_fn) ||
          !iterator_fn->IsFunction())
        return;
      v8::Local<v8::Value> result =
          iterator_fn.As<v8::Function>()
              ->Call(context, out, 0, nullptr)
              .FromMaybe(v8::Local<v8::Value>());
      if (!result.IsEmpty())
        info.GetReturnValue().Set(result);
    }

    void UspForEach(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsFunction()) return;
      v8::Local<v8::Function> fn = info[0].As<v8::Function>();
      v8::Local<v8::Value> this_arg =
          info.Length() >= 2
              ? info[1]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
      v8::Local<v8::Array> pairs = UspPairs(isolate, context, info.This());
      for (uint32_t i = 0; i < pairs->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!pairs->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> pk, pv;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) &&
            pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv))
        {
          v8::Local<v8::Value> argv[] = {pv, pk, info.This()};
          v8::TryCatch try_catch(isolate);
          (void)fn->Call(context, this_arg, 3, argv)
              .FromMaybe(v8::Local<v8::Value>());
          if (try_catch.HasCaught())
            CJavascriptException::ThrowIf(isolate, try_catch);
        }
      }
    }

    void UspConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->usp_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: URLSearchParams constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->usp_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      v8::Local<v8::Array> pairs = v8::Array::New(isolate);
      (void)inst->SetPrivate(context, UspPairsKey(isolate), pairs)
          .FromMaybe(false);
      if (info.Length() >= 1 && !info[0]->IsNullOrUndefined())
      {
        if (info[0]->IsObject() && !info[0]->IsArray() &&
            state->usp_tpl.Get(isolate)->HasInstance(info[0].As<v8::Object>()))
        {
          v8::Local<v8::Array> other =
              UspPairs(isolate, context, info[0].As<v8::Object>());
          for (uint32_t i = 0; i < other->Length(); i++)
          {
            v8::Local<v8::Value> pair;
            if (other->Get(context, i).ToLocal(&pair))
              (void)pairs->Set(context, pairs->Length(), pair)
                  .FromMaybe(false);
          }
        }
        else if (info[0]->IsString())
        {
          UspParse(isolate, context, pairs, ToUtf8(isolate, info[0]));
        }
        else if (info[0]->IsArray())
        {
          v8::Local<v8::Array> items = info[0].As<v8::Array>();
          for (uint32_t i = 0; i < items->Length(); i++)
          {
            v8::Local<v8::Value> pair;
            if (!items->Get(context, i).ToLocal(&pair) || !pair->IsArray())
              continue;
            v8::Local<v8::Value> pk, pv;
            if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&pk) &&
                pair.As<v8::Array>()->Get(context, 1).ToLocal(&pv))
              UspAppendPair(isolate, context, pairs, pk, pv);
          }
        }
        else if (info[0]->IsObject())
        {
          v8::Local<v8::Array> keys =
              info[0].As<v8::Object>()
                  ->GetOwnPropertyNames(context)
                  .ToLocalChecked();
          for (uint32_t i = 0; i < keys->Length(); i++)
          {
            v8::Local<v8::Value> key = keys->Get(context, i).ToLocalChecked();
            v8::Local<v8::Value> value;
            if (info[0].As<v8::Object>()->Get(context, key).ToLocal(&value))
              UspAppendPair(isolate, context, pairs, key, value);
          }
        }
      }
      v8::Local<v8::Value> onchange =
          info.Length() >= 2
              ? info[1]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
      (void)inst->SetPrivate(context, UspOnChangeKey(isolate), onchange)
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    // ---- URL -----------------------------------------------------------

    void CurrentHref(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     std::string& out)
    {
      out = "about:blank";
      v8::Local<v8::Value> location;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "location"))
              .ToLocal(&location) &&
          location->IsObject())
      {
        v8::Local<v8::Value> href;
        if (location.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "href"))
                .ToLocal(&href) &&
            href->IsString())
          out = ToUtf8(isolate, href);
      }
    }

    bool HasScheme(const std::string& value)
    {
      if (value.empty() || !std::isalpha(static_cast<unsigned char>(value[0])))
        return false;
      for (size_t i = 1; i < value.size(); i++)
      {
        char c = value[i];
        if (c == ':') return true;
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' ||
              c == '-' || c == '.'))
          return false;
      }
      return false;
    }

    // reference resolveURL: scheme-absolute passes through, otherwise
    // resolve against the base origin + dot-segment normalization
    std::string ResolveUrlPoly(const std::string& url,
                               const std::string& base)
    {
      std::string value = url;
      if (HasScheme(value)) return value;
      std::string base_text = base;
      std::string origin, base_path;
      {
        // ^([a-z][a-z0-9+.-]*:\/\/[^/?#]+)([^?#]*)
        size_t colon = base_text.find(':');
        if (colon == std::string::npos) return value;
        bool scheme_ok =
            !base_text.empty() &&
            std::isalpha(static_cast<unsigned char>(base_text[0]));
        for (size_t i = 1; i < colon && scheme_ok; i++)
        {
          char c = base_text[i];
          if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' ||
                c == '-' || c == '.'))
            scheme_ok = false;
        }
        if (!scheme_ok || colon + 3 > base_text.size() ||
            base_text[colon + 1] != '/' || base_text[colon + 2] != '/')
          return value;
        size_t authority_start = colon + 3;
        size_t authority_end =
            base_text.find_first_of("/?#", authority_start);
        origin = base_text.substr(0, authority_end == std::string::npos
                                         ? std::string::npos
                                         : authority_end);
        std::string rest = authority_end == std::string::npos
            ? ""
            : base_text.substr(authority_end);
        size_t q = rest.find('?');
        base_path = q == std::string::npos ? rest : rest.substr(0, q);
      }
      if (value.rfind("//", 0) == 0)
      {
        // G4.354: preserve the double slash like the reference URL parser
        // (`new URL('//html', base)` -> 'https:////html', NOT
        // 'https://html' which would collapse the empty authority)
        std::string scheme = origin.substr(0, origin.find(':') + 1);
        return scheme + value;
      }
      size_t split = value.find_first_of("?#");
      std::string path = split == std::string::npos
          ? value
          : value.substr(0, split);
      std::string suffix = split == std::string::npos
          ? ""
          : value.substr(split);
      if (path.empty()) return origin + base_path + suffix;
      std::string candidate;
      if (path[0] == '/')
      {
        candidate = path;
      }
      else
      {
        size_t slash = base_path.rfind('/');
        std::string head = slash == std::string::npos
            ? "/"
            : base_path.substr(0, slash + 1);
        candidate = head + path;
      }
      std::string normalized;
      size_t start = 0;
      while (start <= candidate.size())
      {
        size_t slash = candidate.find('/', start);
        std::string part = candidate.substr(
            start, slash == std::string::npos ? std::string::npos
                                              : slash - start);
        if (!part.empty() && part != ".")
        {
          if (part == "..")
          {
            size_t pop = normalized.rfind('/');
            if (pop != std::string::npos) normalized.resize(pop);
          }
          else
          {
            if (!normalized.empty()) normalized += '/';
            normalized += part;
          }
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
      }
      return origin + "/" + normalized + suffix;
    }

    struct UrlPartsText
    {
      std::string protocol, host, pathname, search, hash;
    };

    // reference: ^([a-z][a-z0-9+.-]*:)(?:\/\/([^/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$  (i)
    void ParseHrefParts(const std::string& href, UrlPartsText& out)
    {
      out = UrlPartsText();
      size_t colon = href.find(':');
      if (colon == std::string::npos) return;
      bool scheme_ok =
          !href.empty() &&
          std::isalpha(static_cast<unsigned char>(href[0]));
      for (size_t i = 1; i < colon && scheme_ok; i++)
      {
        char c = href[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' ||
              c == '-' || c == '.'))
          scheme_ok = false;
      }
      if (!scheme_ok) return;
      out.protocol = href.substr(0, colon + 1);
      std::string rest = href.substr(colon + 1);
      size_t at = 0;
      if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/')
      {
        at = 2;
        while (at < rest.size() && rest[at] != '/' && rest[at] != '?' &&
               rest[at] != '#')
          at += 1;
        out.host = rest.substr(2, at - 2);
      }
      while (at < rest.size() && rest[at] != '?' && rest[at] != '#')
      {
        out.pathname += rest[at];
        at += 1;
      }
      // search keeps its '?' and hash keeps its '#' (regex group semantics)
      if (at < rest.size() && rest[at] == '?')
      {
        out.search += '?';
        at += 1;
        while (at < rest.size() && rest[at] != '#')
        {
          out.search += rest[at];
          at += 1;
        }
      }
      if (at < rest.size() && rest[at] == '#')
      {
        out.hash = rest.substr(at);
      }
    }

    void UrlRebuild(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Object> self)
    {
      v8::Local<v8::Value> parts_value;
      if (!self->GetPrivate(context, UrlPartsKey(isolate)).ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      v8::Local<v8::Object> parts = parts_value.As<v8::Object>();
      auto get = [&](const char* name) {
        v8::Local<v8::Value> value;
        return parts->Get(context, ToV8Str(isolate, name)).ToLocal(&value)
            ? ToUtf8(isolate, value)
            : std::string();
      };
      std::string protocol = get("_protocol");
      std::string host = get("_host");
      std::string pathname = get("_pathname");
      std::string search = get("_search");
      std::string hash = get("_hash");
      std::string href;
      if (!protocol.empty())
      {
        href = !host.empty() ? protocol + "//" + host : protocol;
        href += pathname + search + hash;
      }
      else
      {
        href = pathname + search + hash;
      }
      (void)parts->Set(context, ToV8Str(isolate, "_href"),
                       ToV8Str(isolate, href))
          .FromMaybe(false);
    }

    void UrlSetHref(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    ContextState* state, v8::Local<v8::Object> self,
                    const std::string& value)
    {
      v8::Local<v8::Value> parts_value;
      if (!self->GetPrivate(context, UrlPartsKey(isolate)).ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      v8::Local<v8::Object> parts = parts_value.As<v8::Object>();
      UrlPartsText text;
      ParseHrefParts(value, text);
      const char* names[] = {"_protocol", "_host", "_pathname", "_search",
                             "_hash"};
      const std::string* values[] = {&text.protocol, &text.host,
                                     &text.pathname, &text.search,
                                     &text.hash};
      for (int i = 0; i < 5; i++)
        (void)parts->Set(context, ToV8Str(isolate, names[i]),
                         ToV8Str(isolate, *values[i]))
            .FromMaybe(false);
      // searchParams rebinds to the new search string
      std::string search = text.search;
      std::string search_value =
          search.empty() ? "" : (search[0] == '?' ? search.substr(1) : search);
      v8::Local<v8::Object> params = state->usp_tpl.Get(isolate)
                                         ->InstanceTemplate()
                                         ->NewInstance(context)
                                         .ToLocalChecked();
      v8::Local<v8::Array> pairs = v8::Array::New(isolate);
      (void)params->SetPrivate(context, UspPairsKey(isolate), pairs)
          .FromMaybe(false);
      if (!search_value.empty())
        UspParse(isolate, context, pairs, search_value);
      // onchange closure: keep search/href in sync (Data carries the URL)
      v8::Local<v8::FunctionTemplate> onchange_tpl =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                v8::Isolate* iso = args.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Value> self_value = args.Data();
                if (self_value.IsEmpty() || !self_value->IsObject()) return;
                v8::Local<v8::Object> self = self_value.As<v8::Object>();
                v8::Local<v8::Value> params;
                if (!self->GetPrivate(ctx, UrlParamsKey(iso)).ToLocal(&params) ||
                    !params->IsObject())
                  return;
                std::string text =
                    UspToString(iso, ctx, params.As<v8::Object>());
                v8::Local<v8::Value> parts_value;
                if (!self->GetPrivate(ctx, UrlPartsKey(iso)).ToLocal(&parts_value) ||
                    !parts_value->IsObject())
                  return;
                std::string new_search =
                    text.empty() ? "" : "?" + text;
                (void)parts_value.As<v8::Object>()
                    ->Set(ctx, ToV8Str(iso, "_search"),
                          ToV8Str(iso, new_search))
                    .FromMaybe(false);
                UrlRebuild(iso, ctx, self);
              },
              self);
      onchange_tpl->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Function> onchange =
          onchange_tpl->GetFunction(context).ToLocalChecked();
      (void)params->SetPrivate(context, UspOnChangeKey(isolate), onchange)
          .FromMaybe(false);
      (void)self->SetPrivate(context, UrlParamsKey(isolate), params)
          .FromMaybe(false);
      UrlRebuild(isolate, context, self);
    }

    void UrlConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->url_tpl.IsEmpty())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: URL constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->url_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      v8::Local<v8::Object> parts = v8::Object::New(isolate);
      (void)inst->SetPrivate(context, UrlPartsKey(isolate), parts)
          .FromMaybe(false);
      std::string base;
      if (info.Length() >= 2 && !info[1]->IsUndefined())
        base = ToUtf8(isolate, info[1]);
      else
        CurrentHref(isolate, context, base);
      std::string input = info.Length() >= 1 && !info[0]->IsNullOrUndefined()
          ? ToUtf8(isolate, info[0])
          : "";
      UrlSetHref(isolate, context, state, inst,
                 ResolveUrlPoly(input, base));
      info.GetReturnValue().Set(inst);
    }

    // URL component accessor: reads/writes a parts field by name
    void UrlGetComponent(const v8::FunctionCallbackInfo<v8::Value>& info,
                         const char* field)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> parts_value;
      if (!info.This()
               ->GetPrivate(context, UrlPartsKey(isolate))
               .ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      v8::Local<v8::Value> value;
      if (parts_value.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, field))
              .ToLocal(&value))
        info.GetReturnValue().Set(value);
    }

    void UrlSetComponent(const v8::FunctionCallbackInfo<v8::Value>& info,
                         const char* field, const std::string& value)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> parts_value;
      if (!info.This()
               ->GetPrivate(context, UrlPartsKey(isolate))
               .ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      (void)parts_value.As<v8::Object>()
          ->Set(context, ToV8Str(isolate, field), ToV8Str(isolate, value))
          .FromMaybe(false);
      UrlRebuild(isolate, context, info.This());
    }

    void UrlGetHref(const v8::FunctionCallbackInfo<v8::Value>& info)
    { UrlGetComponent(info, "_href"); }

    void UrlSetHrefFn(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (info.Length() < 1 || state == nullptr) return;
      v8::Local<v8::Value> parts_value;
      std::string current;
      if (info.This()
              ->GetPrivate(context, UrlPartsKey(isolate))
              .ToLocal(&parts_value) &&
          parts_value->IsObject())
      {
        v8::Local<v8::Value> value;
        if (parts_value.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "_href"))
                .ToLocal(&value))
          current = ToUtf8(isolate, value);
      }
      if (current.empty()) CurrentHref(isolate, context, current);
      std::string input = ToUtf8(isolate, info[0]);
      UrlSetHref(isolate, context, state, info.This(),
                 ResolveUrlPoly(input, current));
    }

    void UrlGetProtocol(const v8::FunctionCallbackInfo<v8::Value>& info)
    { UrlGetComponent(info, "_protocol"); }

    void UrlSetProtocol(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (info.Length() < 1) return;
      v8::Isolate* isolate = info.GetIsolate();
      std::string value = ToUtf8(isolate, info[0]);
      while (!value.empty() && value.back() == ':') value.pop_back();
      UrlSetComponent(info, "_protocol", value + ":");
    }

    void UrlGetHost(const v8::FunctionCallbackInfo<v8::Value>& info)
    { UrlGetComponent(info, "_host"); }

    void UrlSetHost(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (info.Length() < 1) return;
      UrlSetComponent(info, "_host", ToUtf8(info.GetIsolate(), info[0]));
    }

    void UrlGetHostname(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> parts_value;
      if (!info.This()
               ->GetPrivate(context, UrlPartsKey(isolate))
               .ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      v8::Local<v8::Value> host;
      if (!parts_value.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "_host"))
               .ToLocal(&host))
        return;
      std::string text = ToUtf8(isolate, host);
      size_t colon = text.rfind(':');
      if (colon != std::string::npos)
      {
        bool digits = colon + 1 < text.size();
        for (size_t i = colon + 1; i < text.size() && digits; i++)
          digits = std::isdigit(static_cast<unsigned char>(text[i])) != 0;
        if (digits) text.resize(colon);
      }
      info.GetReturnValue().Set(ToV8Str(isolate, text));
    }

    void UrlSetHostname(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> parts_value;
      if (info.Length() < 1 ||
          !info.This()
               ->GetPrivate(context, UrlPartsKey(isolate))
               .ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      v8::Local<v8::Value> host;
      std::string host_text;
      if (parts_value.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "_host"))
              .ToLocal(&host))
        host_text = ToUtf8(isolate, host);
      size_t colon = host_text.rfind(':');
      std::string port = colon == std::string::npos
          ? ""
          : host_text.substr(colon + 1);
      std::string next = ToUtf8(isolate, info[0]) +
                         (port.empty() ? "" : ":" + port);
      UrlSetComponent(info, "_host", next);
    }

    void UrlGetPort(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> parts_value;
      if (!info.This()
               ->GetPrivate(context, UrlPartsKey(isolate))
               .ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      v8::Local<v8::Value> host;
      if (!parts_value.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "_host"))
               .ToLocal(&host))
        return;
      std::string text = ToUtf8(isolate, host);
      size_t colon = text.rfind(':');
      std::string port;
      if (colon != std::string::npos)
      {
        port = text.substr(colon + 1);
        for (char c : port)
          if (!std::isdigit(static_cast<unsigned char>(c)))
          {
            port.clear();
            break;
          }
      }
      info.GetReturnValue().Set(ToV8Str(isolate, port));
    }

    void UrlSetPort(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> parts_value;
      if (info.Length() < 1 ||
          !info.This()
               ->GetPrivate(context, UrlPartsKey(isolate))
               .ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      v8::Local<v8::Value> host;
      std::string host_text;
      if (parts_value.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "_host"))
              .ToLocal(&host))
        host_text = ToUtf8(isolate, host);
      size_t colon = host_text.rfind(':');
      std::string head = colon == std::string::npos
          ? host_text
          : host_text.substr(0, colon);
      std::string port = ToUtf8(isolate, info[0]);
      UrlSetComponent(info, "_host",
                      head + (port.empty() ? "" : ":" + port));
    }

    void UrlGetPathname(const v8::FunctionCallbackInfo<v8::Value>& info)
    { UrlGetComponent(info, "_pathname"); }

    void UrlSetPathname(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (info.Length() < 1) return;
      std::string value = ToUtf8(info.GetIsolate(), info[0]);
      if (value.empty() || value[0] != '/') value = "/" + value;
      UrlSetComponent(info, "_pathname", value);
    }

    void UrlGetSearch(const v8::FunctionCallbackInfo<v8::Value>& info)
    { UrlGetComponent(info, "_search"); }

    void UrlSetSearch(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (info.Length() < 1 || state == nullptr) return;
      std::string value = ToUtf8(isolate, info[0]);
      std::string search = value.empty()
          ? ""
          : (value[0] == '?' ? value : "?" + value);
      UrlSetComponent(info, "_search", search);
      // rebind searchParams to the new search string (reference behavior)
      std::string bare = search.empty() ? "" : search.substr(1);
      v8::Local<v8::Object> params = state->usp_tpl.Get(isolate)
                                         ->InstanceTemplate()
                                         ->NewInstance(context)
                                         .ToLocalChecked();
      v8::Local<v8::Array> pairs = v8::Array::New(isolate);
      (void)params->SetPrivate(context, UspPairsKey(isolate), pairs)
          .FromMaybe(false);
      if (!bare.empty()) UspParse(isolate, context, pairs, bare);
      v8::Local<v8::FunctionTemplate> onchange_tpl =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                v8::Isolate* iso = args.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Value> self_value = args.Data();
                if (self_value.IsEmpty() || !self_value->IsObject()) return;
                v8::Local<v8::Object> self = self_value.As<v8::Object>();
                v8::Local<v8::Value> params;
                if (!self->GetPrivate(ctx, UrlParamsKey(iso)).ToLocal(&params) ||
                    !params->IsObject())
                  return;
                std::string text = UspToString(iso, ctx, params.As<v8::Object>());
                v8::Local<v8::Value> parts_value;
                if (!self->GetPrivate(ctx, UrlPartsKey(iso)).ToLocal(&parts_value) ||
                    !parts_value->IsObject())
                  return;
                (void)parts_value.As<v8::Object>()
                    ->Set(ctx, ToV8Str(iso, "_search"),
                          ToV8Str(iso, text.empty() ? "" : "?" + text))
                    .FromMaybe(false);
                UrlRebuild(iso, ctx, self);
              },
              info.This());
      onchange_tpl->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Function> onchange =
          onchange_tpl->GetFunction(context).ToLocalChecked();
      (void)params->SetPrivate(context, UspOnChangeKey(isolate), onchange)
          .FromMaybe(false);
      (void)info.This()
          ->SetPrivate(context, UrlParamsKey(isolate), params)
          .FromMaybe(false);
      UrlRebuild(isolate, context, info.This());
    }

    void UrlGetHash(const v8::FunctionCallbackInfo<v8::Value>& info)
    { UrlGetComponent(info, "_hash"); }

    void UrlSetHash(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (info.Length() < 1) return;
      std::string value = ToUtf8(info.GetIsolate(), info[0]);
      std::string hash = value.empty()
          ? ""
          : (value[0] == '#' ? value : "#" + value);
      UrlSetComponent(info, "_hash", hash);
    }

    void UrlGetOrigin(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> parts_value;
      if (!info.This()
               ->GetPrivate(context, UrlPartsKey(isolate))
               .ToLocal(&parts_value) ||
          !parts_value->IsObject())
        return;
      v8::Local<v8::Value> protocol, host;
      std::string p, h;
      if (parts_value.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "_protocol"))
              .ToLocal(&protocol))
        p = ToUtf8(isolate, protocol);
      if (parts_value.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "_host"))
              .ToLocal(&host))
        h = ToUtf8(isolate, host);
      info.GetReturnValue().Set(
          ToV8Str(isolate, !p.empty() && !h.empty() ? p + "//" + h : "null"));
    }

    void UrlGetSearchParams(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> params;
      if (info.This()
              ->GetPrivate(context, UrlParamsKey(isolate))
              .ToLocal(&params) &&
          !params.IsEmpty())
        info.GetReturnValue().Set(params);
    }

    void UrlToStringFn(const v8::FunctionCallbackInfo<v8::Value>& info)
    { UrlGetComponent(info, "_href"); }

    void UrlCanParse(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      std::string value = info.Length() >= 1 ? ToUtf8(isolate, info[0]) : "";
      bool ok = HasScheme(value);
      if (!ok && info.Length() >= 2 && !info[1]->IsUndefined())
        ok = HasScheme(ToUtf8(isolate, info[1]));
      info.GetReturnValue().Set(v8::Boolean::New(isolate, ok));
    }

    void UrlParseStatic(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      std::string value = info.Length() >= 1 ? ToUtf8(isolate, info[0]) : "";
      bool ok = HasScheme(value);
      if (!ok && info.Length() >= 2 && !info[1]->IsUndefined())
        ok = HasScheme(ToUtf8(isolate, info[1]));
      if (!ok || state == nullptr || state->url_tpl.IsEmpty())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      // delegate to the constructor (construct call)
      v8::Local<v8::Function> ctor =
          state->url_tpl.Get(isolate)->GetFunction(context).ToLocalChecked();
      uint32_t argc = static_cast<uint32_t>(info.Length());
      v8::Local<v8::Value>* argv = new v8::Local<v8::Value>[argc ? argc : 1];
      for (uint32_t i = 0; i < argc; i++) argv[i] = info[i];
      v8::Local<v8::Value> result;
      bool ok_call =
          ctor->NewInstance(context, argc, argv).ToLocal(&result);
      delete[] argv;
      if (ok_call && !result.IsEmpty())
        info.GetReturnValue().Set(result);
    }

    // ---- URLPattern ------------------------------------------------------

    void CompileComponent(const std::string& pattern,
                          std::string& regex_source,
                          std::vector<std::string>& names)
    {
      static const char* specials = ".*+?^${}()|[]\\";
      std::string source;
      names.clear();
      size_t i = 0;
      while (i < pattern.size())
      {
        char c = pattern[i];
        if (c == '*')
        {
          source += ".*";
          i += 1;
          continue;
        }
        if (c == ':')
        {
          size_t name_start = ++i;
          while (i < pattern.size() &&
                 (std::isalnum(static_cast<unsigned char>(pattern[i])) ||
                  pattern[i] == '_' || pattern[i] == '$'))
            i += 1;
          names.push_back(pattern.substr(name_start, i - name_start));
          source += "([^/?#]+)";
          if (i < pattern.size() &&
              (pattern[i] == '+' || pattern[i] == '*' || pattern[i] == '?'))
            source += pattern[i++];
          continue;
        }
        if (std::strchr(specials, c) != nullptr) source += '\\';
        source += c;
        i += 1;
      }
      regex_source = "^" + source + "$";
    }

    // urlPatternParts: URL instance props / string parsed via URL / object
    v8::Local<v8::Object> PatternPartsOf(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context,
                                         ContextState* state,
                                         v8::Local<v8::Value> input,
                                         v8::Local<v8::Value> base)
    {
      v8::Local<v8::Object> parts = v8::Object::New(isolate);
      static const char* const keys[] = {"protocol", "username", "password",
                                         "hostname", "port", "pathname",
                                         "search", "hash"};
      auto setk = [&](const char* key, const std::string& value) {
        (void)parts->Set(context, ToV8Str(isolate, key),
                         ToV8Str(isolate, value))
            .FromMaybe(false);
      };
      for (const char* key : keys) setk(key, "");
      if (input.IsEmpty() || !input->IsObject())
      {
        if (!input.IsEmpty() && input->IsString())
        {
          v8::Local<v8::Value> url_ctor;
          if (context->Global()
                  ->Get(context, ToV8Str(isolate, "URL"))
                  .ToLocal(&url_ctor) &&
              url_ctor->IsFunction())
          {
            v8::Local<v8::Value> argv[2];
            uint32_t argc = 1;
            argv[0] = input;
            if (!base.IsEmpty())
              argv[argc++] = base;
            v8::Local<v8::Value> instance;
            if (url_ctor.As<v8::Function>()
                    ->NewInstance(context, argc, argv)
                    .ToLocal(&instance) &&
                instance->IsObject())
              return PatternPartsOf(isolate, context, state, instance,
                                    v8::Local<v8::Value>());
          }
        }
        return parts;
      }
      v8::Local<v8::Object> obj = input.As<v8::Object>();
      bool is_url = !state->url_tpl.IsEmpty() &&
                    state->url_tpl.Get(isolate)->HasInstance(obj);
      auto get = [&](const char* key) {
        v8::Local<v8::Value> value;
        return obj->Get(context, ToV8Str(isolate, key)).ToLocal(&value)
            ? ToUtf8(isolate, value)
            : std::string();
      };
      if (is_url)
      {
        std::string protocol = get("protocol");
        while (!protocol.empty() && protocol.back() == ':') protocol.pop_back();
        setk("protocol", protocol);
        setk("username", get("username"));
        setk("password", get("password"));
        setk("hostname", get("hostname"));
        setk("port", get("port"));
        setk("pathname", get("pathname"));
        std::string search = get("search");
        if (!search.empty() && search[0] == '?') search.erase(0, 1);
        setk("search", search);
        std::string hash = get("hash");
        if (!hash.empty() && hash[0] == '#') hash.erase(0, 1);
        setk("hash", hash);
        return parts;
      }
      for (const char* key : keys)
      {
        v8::Local<v8::Value> value;
        if (obj->Get(context, ToV8Str(isolate, key)).ToLocal(&value) &&
            !value->IsUndefined())
          setk(key, ToUtf8(isolate, value));
      }
      return parts;
    }

    v8::Local<v8::Object> UrlPatternExecImpl(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Object> self, v8::Local<v8::Value> input,
        v8::Local<v8::Value> base, bool* matched)
    {
      ContextState* state = GetState(context);
      *matched = false;
      v8::Local<v8::Value> re_value, names_value;
      if (state == nullptr ||
          !self->GetPrivate(
                    context,
                    v8::Private::ForApi(
                        isolate, ToV8Str(isolate, "lv8.urlpattern.re")))
               .ToLocal(&re_value) ||
          !re_value->IsObject() ||
          !self->GetPrivate(
                    context,
                    v8::Private::ForApi(
                        isolate, ToV8Str(isolate, "lv8.urlpattern.names")))
               .ToLocal(&names_value) ||
          !names_value->IsObject())
        return v8::Local<v8::Object>();
      v8::Local<v8::Object> parts =
          PatternPartsOf(isolate, context, state, input, base);
      static const char* const keys[] = {"protocol", "username", "password",
                                         "hostname", "port", "pathname",
                                         "search", "hash"};
      v8::Local<v8::Object> result = v8::Object::New(isolate);
      {
        v8::Local<v8::Array> inputs = v8::Array::New(isolate, 1);
        (void)inputs->Set(context, 0, input).FromMaybe(false);
        (void)result->Set(context, ToV8Str(isolate, "inputs"), inputs)
            .FromMaybe(false);
      }
      for (const char* key : keys)
      {
        v8::Local<v8::Value> regex_text;
        if (!re_value.As<v8::Object>()
                 ->Get(context, ToV8Str(isolate, key))
                 .ToLocal(&regex_text) ||
            !regex_text->IsString())
          return v8::Local<v8::Object>();
        v8::Local<v8::Value> part;
        if (!parts->Get(context, ToV8Str(isolate, key)).ToLocal(&part))
          return v8::Local<v8::Object>();
        std::string part_text =
            part->IsUndefined() ? "" : ToUtf8(isolate, part);
        std::regex compiled(ToUtf8(isolate, regex_text),
                            std::regex::ECMAScript);
        std::smatch matches;
        if (!std::regex_match(part_text, matches, compiled))
          return v8::Local<v8::Object>();
        v8::Local<v8::Value> names;
        if (!names_value.As<v8::Object>()
                 ->Get(context, ToV8Str(isolate, key))
                 .ToLocal(&names) ||
            !names->IsArray())
          return v8::Local<v8::Object>();
        v8::Local<v8::Array> name_list = names.As<v8::Array>();
        v8::Local<v8::Object> groups = v8::Object::New(isolate);
        for (uint32_t g = 0; g < name_list->Length(); g++)
        {
          v8::Local<v8::Value> name;
          if (!name_list->Get(context, g).ToLocal(&name) || !name->IsString())
            continue;
          std::string group_text =
              g + 1 < matches.size() && matches[g + 1].matched
              ? matches[g + 1].str()
              : "";
          (void)groups->Set(context, name, ToV8Str(isolate, group_text))
              .FromMaybe(false);
        }
        v8::Local<v8::Object> component = v8::Object::New(isolate);
        (void)component->Set(context, ToV8Str(isolate, "input"),
                             ToV8Str(isolate, part_text))
            .FromMaybe(false);
        (void)component->Set(context, ToV8Str(isolate, "groups"), groups)
            .FromMaybe(false);
        (void)result->Set(context, ToV8Str(isolate, key), component)
            .FromMaybe(false);
      }
      *matched = true;
      return result;
    }

    void UrlPatternExec(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      bool matched = false;
      v8::Local<v8::Object> result = UrlPatternExecImpl(
          isolate, context, info.This(),
          info.Length() >= 1 ? info[0] : v8::Local<v8::Value>(),
          info.Length() >= 2 ? info[1] : v8::Local<v8::Value>(), &matched);
      if (matched)
        info.GetReturnValue().Set(result);
      else
        info.GetReturnValue().SetNull();
    }

    void UrlPatternTest(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      bool matched = false;
      UrlPatternExecImpl(isolate, context, info.This(),
                         info.Length() >= 1 ? info[0] : v8::Local<v8::Value>(),
                         info.Length() >= 2 ? info[1] : v8::Local<v8::Value>(),
                         &matched);
      info.GetReturnValue().Set(v8::Boolean::New(isolate, matched));
    }

    void UrlPatternConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->urlpattern_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: URLPattern constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->urlpattern_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      // reference: string init -> URL-derived parts (or pathname-only for
      // patterns starting with / ? #), object init -> per-key values
      v8::Local<v8::Object> values;
      if (info.Length() >= 1 && info[0]->IsString())
      {
        std::string text = ToUtf8(isolate, info[0]);
        bool special = !text.empty() &&
                       (text[0] == '/' || text[0] == '?' || text[0] == '#');
        if (special && (info.Length() < 2 || info[1]->IsUndefined()))
        {
          values = v8::Object::New(isolate);
          std::string pathname =
              (text[0] == '?' || text[0] == '#') ? "/" : text;
          (void)values->Set(context, ToV8Str(isolate, "pathname"),
                            ToV8Str(isolate, pathname))
              .FromMaybe(false);
        }
        else
        {
          v8::Local<v8::Value> base =
              info.Length() >= 2 ? info[1] : v8::Local<v8::Value>();
          values = PatternPartsOf(isolate, context, state, info[0], base);
        }
      }
      else if (info.Length() >= 1 && info[0]->IsObject())
      {
        values = info[0].As<v8::Object>();
      }
      else
      {
        values = v8::Object::New(isolate);
      }
      static const char* const keys[] = {"protocol", "username", "password",
                                         "hostname", "port", "pathname",
                                         "search", "hash"};
      v8::Local<v8::Object> re_texts = v8::Object::New(isolate);
      v8::Local<v8::Object> names_map = v8::Object::New(isolate);
      bool any_groups = false;
      for (const char* key : keys)
      {
        std::string pattern_text = "*";
        v8::Local<v8::Value> raw;
        if (values->Get(context, ToV8Str(isolate, key)).ToLocal(&raw) &&
            !raw->IsUndefined())
          pattern_text = ToUtf8(isolate, raw);
        std::string regex_source;
        std::vector<std::string> names;
        CompileComponent(pattern_text, regex_source, names);
        if (!names.empty()) any_groups = true;
        (void)re_texts->Set(context, ToV8Str(isolate, key),
                            ToV8Str(isolate, regex_source))
            .FromMaybe(false);
        v8::Local<v8::Array> name_list = v8::Array::New(isolate);
        for (size_t i = 0; i < names.size(); i++)
          (void)name_list->Set(context, static_cast<uint32_t>(i),
                               ToV8Str(isolate, names[i]))
              .FromMaybe(false);
        (void)names_map->Set(context, ToV8Str(isolate, key), name_list)
            .FromMaybe(false);
      }
      (void)inst
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate, ToV8Str(isolate, "lv8.urlpattern.re")),
                       re_texts)
          .FromMaybe(false);
      (void)inst
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate,
                           ToV8Str(isolate, "lv8.urlpattern.names")),
                       names_map)
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "hasRegExpGroups"),
                      v8::Boolean::New(isolate, any_groups))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    // --------------------------------------------------------------- Blob / File

    // Blob payload: a GC-managed ArrayBuffer stored in a private slot
    v8::Local<v8::Private> BlobKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.blob.bytes"));
    }

    v8::Local<v8::ArrayBuffer> BlobBytesOf(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Object> blob)
    {
      v8::Local<v8::Value> bytes;
      if (blob->GetPrivate(context, BlobKey(isolate)).ToLocal(&bytes) &&
          bytes->IsArrayBuffer())
        return bytes.As<v8::ArrayBuffer>();
      return v8::Local<v8::ArrayBuffer>();
    }

    // byte range of an ArrayBuffer (GetBackingStore is the supported
    // accessor in this V8 version)
    const uint8_t* ArrayBufferBytes(v8::Local<v8::ArrayBuffer> bytes,
                                    size_t* length)
    {
      *length = 0;
      if (bytes.IsEmpty()) return nullptr;
      auto store = bytes->GetBackingStore();
      if (!store || !store->Data()) return nullptr;
      *length = store->ByteLength();
      return static_cast<const uint8_t*>(store->Data());
    }

    // HTTP token range + lower case, else empty (reference
    // normalizeMimeType semantics) is applied inline in the constructors

    // UTF-8 decode with U+FFFD replacement on ill-formed sequences
    std::string DecodeUtf8Replacement(const std::vector<uint8_t>& bytes)
    {
      std::string out;
      char repl[] = "\xEF\xBF\xBD";
      size_t i = 0;
      auto append = [&](uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800)
        {
          out += static_cast<char>(0xC0 | (cp >> 6));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
          out += static_cast<char>(0xE0 | (cp >> 12));
          out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
          out += static_cast<char>(0xF0 | (cp >> 18));
          out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
          out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        }
      };
      auto take = [&](size_t count, uint32_t& cp) -> bool {
        // |count| = total sequence length (2..4); bytes[i+1..i+count-1]
        // must be continuation bytes
        if (count < 2 || i + count > bytes.size()) return false;
        cp = bytes[i] & (count == 2 ? 0x1F : count == 3 ? 0x0F : 0x07);
        for (size_t k = 1; k < count; k++)
        {
          if ((bytes[i + k] & 0xC0) != 0x80) return false;
          cp = (cp << 6) | (bytes[i + k] & 0x3F);
        }
        i += count;
        return true;
      };
      while (i < bytes.size())
      {
        uint32_t b = bytes[i];
        if (b < 0x80) { out += static_cast<char>(b); i += 1; continue; }
        uint32_t cp = 0;
        size_t want = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 0;
        bool ok = want >= 2 && take(want, cp);
        // surrogate and out-of-range code points decode as replacement
        if (ok && (cp >= 0xD800 && cp <= 0xDFFF || cp > 0x10FFFF)) ok = false;
        if (ok) append(cp);
        else { out += repl; i += 1; }
      }
      return out;
    }

    bool IsBlobValue(v8::Isolate* isolate, ContextState* state,
                     v8::Local<v8::Value> value)
    {
      if (state == nullptr || !value->IsObject()) return false;
      if (!state->blob_tpl.IsEmpty() &&
          state->blob_tpl.Get(isolate)->HasInstance(
              value.As<v8::Object>()))
        return true;
      if (!state->file_tpl.IsEmpty() &&
          state->file_tpl.Get(isolate)->HasInstance(value.As<v8::Object>()))
        return true;
      return false;
    }

    void AppendPartBytes(v8::Isolate* isolate,
                         v8::Local<v8::Context> context,
                         ContextState* state,
                         v8::Local<v8::Value> part,
                         std::vector<uint8_t>& out)
    {
      if (IsBlobValue(isolate, state, part))
      {
        v8::Local<v8::ArrayBuffer> bytes =
            BlobBytesOf(isolate, context, part.As<v8::Object>());
        size_t length = 0;
        const uint8_t* data = ArrayBufferBytes(bytes, &length);
        if (data != nullptr) out.insert(out.end(), data, data + length);
        return;
      }
      if (part->IsArrayBuffer())
      {
        size_t length = 0;
        const uint8_t* data =
            ArrayBufferBytes(part.As<v8::ArrayBuffer>(), &length);
        out.insert(out.end(), data, data + length);
        return;
      }
      if (part->IsArrayBufferView())
      {
        v8::Local<v8::ArrayBufferView> view = part.As<v8::ArrayBufferView>();
        v8::Local<v8::ArrayBuffer> buffer = view->Buffer();
        size_t total = 0;
        const uint8_t* base = ArrayBufferBytes(buffer, &total);
        if (base != nullptr)
        {
          size_t start = static_cast<size_t>(view->ByteOffset());
          size_t length = static_cast<size_t>(view->ByteLength());
          if (start <= total)
          {
            size_t count = std::min(length, total - start);
            out.insert(out.end(), base + start, base + start + count);
          }
        }
        return;
      }
      // everything else stringifies (utf-8 encoded)
      v8::TryCatch try_catch(isolate);
      v8::Local<v8::String> text;
      if (part->ToString(context).ToLocal(&text))
      {
        v8::String::Utf8Value utf8(isolate, text);
        const char* data = *utf8 ? *utf8 : "";
        out.insert(out.end(), data, data + utf8.length());
      }
      else
      {
        CJavascriptException::ThrowIf(isolate, try_catch);
      }
    }

    v8::Local<v8::Object> MakeBlobFromBytes(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        ContextState* state, const std::vector<uint8_t>& bytes,
        const std::string& type, bool as_file = false)
    {
      v8::Local<v8::FunctionTemplate> shape =
          as_file && !state->file_tpl.IsEmpty()
              ? state->file_tpl.Get(isolate)
              : state->blob_tpl.Get(isolate);
      v8::Local<v8::ArrayBuffer> buffer =
          v8::ArrayBuffer::New(isolate, bytes.size());
      if (!bytes.empty())
        std::memcpy(buffer->GetBackingStore()->Data(), bytes.data(),
                    bytes.size());
      v8::Local<v8::Object> inst = shape->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, BlobKey(isolate), buffer)
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "type"),
                      ToV8Str(isolate, type))
          .FromMaybe(false);
      return inst;
    }

    void BlobConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->blob_tpl.IsEmpty())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: Blob constructor requires new"));
        return;
      }
      std::vector<uint8_t> bytes;
      if (info.Length() >= 1 && !info[0]->IsNullOrUndefined())
      {
        if (info[0]->IsArray())
        {
          v8::Local<v8::Array> parts = info[0].As<v8::Array>();
          for (uint32_t i = 0; i < parts->Length(); i++)
          {
            v8::Local<v8::Value> part;
            if (parts->Get(context, i).ToLocal(&part))
              AppendPartBytes(isolate, context, state, part, bytes);
          }
        }
        else if (info[0]->IsString())
        {
          AppendPartBytes(isolate, context, state, info[0], bytes);
        }
        else
        {
          isolate->ThrowException(ToV8Str(
              isolate, "TypeError: Blob parts must be iterable"));
          return;
        }
      }
      std::string type;
      if (info.Length() >= 2 && info[1]->IsObject())
      {
        v8::Local<v8::Value> raw;
        if (info[1].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "type"))
                .ToLocal(&raw))
        {
          std::string value =
              raw->IsNullOrUndefined() ? std::string() : ToUtf8(isolate, raw);
          bool token = true;
          for (unsigned char c : value)
            if (c < 0x20 || c > 0x7E) token = false;
          if (token)
          {
            for (char& c : value)
              c = static_cast<char>(
                  std::tolower(static_cast<unsigned char>(c)));
            type = value;
          }
        }
      }
      info.GetReturnValue().Set(
          MakeBlobFromBytes(isolate, context, state, bytes, type));
    }

    void BlobGetSize(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::ArrayBuffer> bytes =
          BlobBytesOf(isolate, context, info.This());
      double size = bytes.IsEmpty() ? 0.0
                                    : static_cast<double>(bytes->ByteLength());
      info.GetReturnValue().Set(v8::Number::New(isolate, size));
    }

    void BlobArrayBuffer(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::ArrayBuffer> bytes =
          BlobBytesOf(isolate, context, info.This());
      size_t length = 0;
      const uint8_t* data = ArrayBufferBytes(bytes, &length);
      v8::Local<v8::ArrayBuffer> copy =
          v8::ArrayBuffer::New(isolate, length);
      if (length > 0 && copy->GetBackingStore() &&
          copy->GetBackingStore()->Data())
        std::memcpy(copy->GetBackingStore()->Data(), data, length);
      (void)resolver->Resolve(context, copy).FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void BlobText(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::ArrayBuffer> bytes =
          BlobBytesOf(isolate, context, info.This());
      std::vector<uint8_t> raw;
      size_t length = 0;
      const uint8_t* data = ArrayBufferBytes(bytes, &length);
      if (data != nullptr) raw.assign(data, data + length);
      (void)resolver
          ->Resolve(context,
                    ToV8Str(isolate, DecodeUtf8Replacement(raw)))
          .FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void BlobSlice(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::ArrayBuffer> bytes =
          BlobBytesOf(isolate, context, info.This());
      double size = bytes.IsEmpty() ? 0.0
                                    : static_cast<double>(bytes->ByteLength());
      auto offset = [&](v8::Local<v8::Value> value, double fallback) {
        // empty local = the JS default parameter kicked in; Number(x) || 0
        double number = fallback;
        if (!value.IsEmpty())
          number = value->NumberValue(context).FromMaybe(fallback);
        if (std::isnan(number)) number = 0.0;
        return number < 0 ? std::max(size + number, 0.0)
                          : std::min(number, size);
      };
      double from = offset(info.Length() >= 1 ? info[0]
                                              : v8::Local<v8::Value>(),
                           0.0);
      double to = std::max(from, offset(info.Length() >= 2 ? info[1]
                                                           : v8::Local<v8::Value>(),
                                         size));
      std::vector<uint8_t> part;
      size_t total = 0;
      const uint8_t* data = ArrayBufferBytes(bytes, &total);
      if (data != nullptr && to > from)
      {
        size_t start = static_cast<size_t>(from);
        size_t count = static_cast<size_t>(to - from);
        if (start <= total)
        {
          count = std::min(count, total - start);
          part.assign(data + start, data + start + count);
        }
      }
      std::string type;
      if (info.Length() >= 3 && !info[2]->IsNullOrUndefined())
      {
        std::string value = ToUtf8(isolate, info[2]);
        bool token = true;
        for (unsigned char c : value)
          if (c < 0x20 || c > 0x7E) token = false;
        if (token)
        {
          for (char& c : value)
            c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
          type = value;
        }
      }
      if (state == nullptr || state->blob_tpl.IsEmpty())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: Blob slice unavailable"));
        return;
      }
      info.GetReturnValue().Set(
          MakeBlobFromBytes(isolate, context, state, part, type));
    }

    void FileConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->file_tpl.IsEmpty())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: File constructor requires new"));
        return;
      }
      std::vector<uint8_t> bytes;
      if (info.Length() >= 1 && !info[0]->IsNullOrUndefined())
      {
        if (info[0]->IsArray())
        {
          v8::Local<v8::Array> parts = info[0].As<v8::Array>();
          for (uint32_t i = 0; i < parts->Length(); i++)
          {
            v8::Local<v8::Value> part;
            if (parts->Get(context, i).ToLocal(&part))
              AppendPartBytes(isolate, context, state, part, bytes);
          }
        }
        else if (info[0]->IsString())
        {
          AppendPartBytes(isolate, context, state, info[0], bytes);
        }
        else
        {
          isolate->ThrowException(ToV8Str(
              isolate, "TypeError: File parts must be iterable"));
          return;
        }
      }
      std::string name = info.Length() >= 2 && !info[1]->IsNullOrUndefined()
          ? ToUtf8(isolate, info[1])
          : std::string("undefined");
      for (char& c : name)
        if (c == '\\' || c == '/') c = ':';
      std::string type;
      double last_modified = 0.0;
      bool has_last_modified = false;
      if (info.Length() >= 3 && info[2]->IsObject())
      {
        v8::Local<v8::Object> options = info[2].As<v8::Object>();
        v8::Local<v8::Value> raw_type;
        if (options->Get(context, ToV8Str(isolate, "type"))
                .ToLocal(&raw_type) &&
            !raw_type->IsNullOrUndefined())
        {
          std::string value = ToUtf8(isolate, raw_type);
          bool token = true;
          for (unsigned char c : value)
            if (c < 0x20 || c > 0x7E) token = false;
          if (token)
          {
            for (char& c : value)
              c = static_cast<char>(
                  std::tolower(static_cast<unsigned char>(c)));
            type = value;
          }
        }
        v8::Local<v8::Value> raw_modified;
        if (options->Get(context, ToV8Str(isolate, "lastModified"))
                .ToLocal(&raw_modified) &&
            !raw_modified->IsUndefined())
        {
          last_modified = raw_modified->IsNumber()
              ? raw_modified->NumberValue(context).FromMaybe(0.0)
              : std::numeric_limits<double>::quiet_NaN();
          has_last_modified = true;
        }
      }
      if (!has_last_modified)
      {
        // route through the installed Date.now so the logical clock
        // semantics stay identical to the reference bootstrap
        v8::Local<v8::Value> date_ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "Date"))
                .ToLocal(&date_ctor) &&
            date_ctor->IsObject())
        {
          v8::Local<v8::Value> now_fn;
          if (date_ctor.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "now"))
                  .ToLocal(&now_fn) &&
              now_fn->IsFunction())
          {
            v8::Local<v8::Value> now;
            if (now_fn.As<v8::Function>()
                    ->Call(context, date_ctor, 0, nullptr)
                    .ToLocal(&now) &&
                now->IsNumber())
              last_modified = now->NumberValue(context).FromMaybe(0.0);
          }
        }
      }
      v8::Local<v8::Object> inst = MakeBlobFromBytes(
          isolate, context, state, bytes, type, true);
      (void)inst->Set(context, ToV8Str(isolate, "name"),
                      ToV8Str(isolate, name))
          .FromMaybe(false);
      (void)inst
          ->Set(context, ToV8Str(isolate, "lastModified"),
                v8::Number::New(isolate, last_modified))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "webkitRelativePath"),
                      ToV8Str(isolate, ""))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void FileListConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->file_list_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: FileList constructor requires new"));
        return;
      }
      v8::Local<v8::Array> items = v8::Array::New(isolate);
      if (info.Length() >= 1 && info[0]->IsArray())
        items = info[0].As<v8::Array>();
      info.GetReturnValue().Set(MakeFileList(isolate, context, items));
    }

    void FileListItem(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> items;
      if (!info.This()->GetPrivate(
                   context,
                   v8::Private::ForApi(isolate,
                                       ToV8Str(isolate, "lv8.filelist.items")))
              .ToLocal(&items) ||
          !items->IsArray())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      double index = info.Length() >= 1 && info[0]->IsNumber()
          ? info[0]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      v8::Local<v8::Array> list = items.As<v8::Array>();
      uint32_t i = static_cast<uint32_t>(index);
      if (index < 0 || index >= static_cast<double>(list->Length()))
      {
        info.GetReturnValue().SetNull();
        return;
      }
      v8::Local<v8::Value> file;
      if (list->Get(context, i).ToLocal(&file) && !file->IsUndefined() &&
          file->BooleanValue(isolate))
        info.GetReturnValue().Set(file);
      else
        info.GetReturnValue().SetNull();
    }

    // JS-side escape hatch: a fresh Uint8Array copy of a Blob's bytes
    // (FileReader / object-URL lookup consume this)
    void BlobBytesHelper(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || info.Length() < 1 ||
          !IsBlobValue(isolate, state, info[0]))
      {
        info.GetReturnValue().SetNull();
        return;
      }
      v8::Local<v8::ArrayBuffer> bytes =
          BlobBytesOf(isolate, context, info[0].As<v8::Object>());
      size_t length = 0;
      const uint8_t* data = ArrayBufferBytes(bytes, &length);
      v8::Local<v8::ArrayBuffer> copy =
          v8::ArrayBuffer::New(isolate, length);
      if (length > 0 && copy->GetBackingStore() &&
          copy->GetBackingStore()->Data())
        std::memcpy(copy->GetBackingStore()->Data(), data, length);
      info.GetReturnValue().Set(
          v8::Uint8Array::New(copy, 0, length));
    }

    // --------------------------------------------------------------- FormData

    v8::Local<v8::Private> FdKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.fd.entries"));
    }

    std::string FormUrlDecode(const std::string& in)
    {
      std::string out;
      auto hex = [](char c) {
        return c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
      };
      for (size_t i = 0; i < in.size(); i++)
      {
        if (in[i] == '+') out += ' ';
        else if (in[i] == '%' && i + 2 < in.size() &&
                 std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(in[i + 2])))
        {
          out += static_cast<char>(hex(in[i + 1]) * 16 + hex(in[i + 2]));
          i += 2;
        }
        else out += in[i];
      }
      return out;
    }

    v8::Local<v8::Array> FdEntries(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> self)
    {
      v8::Local<v8::Value> entries;
      if (self->GetPrivate(context, FdKey(isolate)).ToLocal(&entries) &&
          entries->IsArray())
        return entries.As<v8::Array>();
      return v8::Array::New(isolate);
    }

    void FormDataAppend(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2 || !info[0]->IsString()) return;
      v8::Local<v8::Array> entries =
          FdEntries(isolate, context, info.This());
      v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
      (void)pair->Set(context, 0, info[0]).FromMaybe(false);
      (void)pair->Set(context, 1, info[1]).FromMaybe(false);
      (void)entries->Set(context, entries->Length(), pair).FromMaybe(false);
    }

    void FormDataGet(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      v8::Local<v8::String> wanted = info[0].As<v8::String>();
      v8::Local<v8::Array> entries =
          FdEntries(isolate, context, info.This());
      for (uint32_t i = 0; i < entries->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!entries->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> name;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&name) &&
            name->SameValue(wanted))
        {
          v8::Local<v8::Value> value;
          if (pair.As<v8::Array>()->Get(context, 1).ToLocal(&value))
          {
            info.GetReturnValue().Set(value);
            return;
          }
        }
      }
      info.GetReturnValue().SetNull();
    }

    void FormDataGetAll(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      if (info.Length() >= 1 && info[0]->IsString())
      {
        v8::Local<v8::String> wanted = info[0].As<v8::String>();
        v8::Local<v8::Array> entries =
            FdEntries(isolate, context, info.This());
        uint32_t n = 0;
        for (uint32_t i = 0; i < entries->Length(); i++)
        {
          v8::Local<v8::Value> pair;
          if (!entries->Get(context, i).ToLocal(&pair) || !pair->IsArray())
            continue;
          v8::Local<v8::Value> name;
          if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&name) &&
              name->SameValue(wanted))
          {
            v8::Local<v8::Value> value;
            if (pair.As<v8::Array>()->Get(context, 1).ToLocal(&value))
              (void)out->Set(context, n++, value).FromMaybe(false);
          }
        }
      }
      info.GetReturnValue().Set(out);
    }

    void FormDataHas(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      bool found = false;
      if (info.Length() >= 1 && info[0]->IsString())
      {
        v8::Local<v8::String> wanted = info[0].As<v8::String>();
        v8::Local<v8::Array> entries =
            FdEntries(isolate, context, info.This());
        for (uint32_t i = 0; i < entries->Length() && !found; i++)
        {
          v8::Local<v8::Value> pair;
          if (!entries->Get(context, i).ToLocal(&pair) || !pair->IsArray())
            continue;
          v8::Local<v8::Value> name;
          found = pair.As<v8::Array>()->Get(context, 0).ToLocal(&name) &&
                  name->SameValue(wanted);
        }
      }
      info.GetReturnValue().Set(found);
    }

    void FormDataForEach(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsFunction()) return;
      v8::Local<v8::Function> fn = info[0].As<v8::Function>();
      v8::Local<v8::Value> this_arg =
          info.Length() >= 2 ? info[1] : v8::Local<v8::Value>(
              v8::Undefined(isolate));
      v8::Local<v8::Array> entries =
          FdEntries(isolate, context, info.This());
      for (uint32_t i = 0; i < entries->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!entries->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> name, value;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&name) &&
            pair.As<v8::Array>()->Get(context, 1).ToLocal(&value))
        {
          v8::Local<v8::Value> argv[] = {value, name, info.This()};
          v8::TryCatch try_catch(isolate);
          (void)fn->Call(context, this_arg, 3, argv)
              .FromMaybe(v8::Local<v8::Value>());
          if (try_catch.HasCaught())
            CJavascriptException::ThrowIf(isolate, try_catch);
        }
      }
    }

    // shared FormData instance factory: entries live in a private array of
    // [name, value] pairs so every producer/consumer sees the same shape
    v8::Local<v8::Object> MakeFormDataImpl(v8::Isolate* isolate,
                                           v8::Local<v8::Context> context)
    {
      ContextState* state = GetState(context);
      if (state != nullptr && state->fd_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> tpl =
            v8::FunctionTemplate::New(isolate);
        tpl->SetClassName(ToV8Str(isolate, "FormData"));
        v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M6: zero own symbols on instances (tag on the prototype; page
        // instances also see the JS-installed window.FormData.prototype tag)
        tpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "FormData"),
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
        method("append", FormDataAppend);
        method("get", FormDataGet);
        method("getAll", FormDataGetAll);
        method("has", FormDataHas);
        method("forEach", FormDataForEach);
        state->fd_tpl.Reset(isolate, tpl);
      }
      v8::Local<v8::Object> obj =
          state->fd_tpl.Get(isolate)->InstanceTemplate()->NewInstance(context)
              .ToLocalChecked();
      (void)obj->SetPrivate(context, FdKey(isolate),
                            v8::Array::New(isolate))
          .FromMaybe(false);
      return obj;
    }

    std::string form_attr(lxb_dom_element_t* el, const char* attr)
    {
      const lxb_char_t* v = lxb_dom_element_get_attribute(
          el, (const lxb_char_t*)attr, strlen(attr), nullptr);
      return v == nullptr ? std::string() : std::string((const char*)v);
    }

    // enumerate a form's controls into the FormData entry array; |submitter|
    // (may be null) is the only button admitted, file inputs contribute
    // their JS-assigned File objects rather than the value attribute
    void CollectFormEntries(v8::Isolate* isolate,
                            v8::Local<v8::Context> context,
                            lxb_dom_node_t* form,
                            lxb_dom_node_t* submitter,
                            v8::Local<v8::Array> entries)
    {
      std::vector<lxb_dom_node_t*> stack;
      stack.push_back(form);
      uint32_t index = entries->Length();
      while (!stack.empty())
      {
        lxb_dom_node_t* node = stack.back();
        stack.pop_back();
        for (lxb_dom_node_t* child = node->last_child; child != nullptr;
             child = child->prev)
          stack.push_back(child);
        if (node == form || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
          continue;
        lxb_dom_element_t* el = lxb_dom_interface_element(node);
        const lxb_char_t* tag_c = lxb_dom_element_local_name(el, nullptr);
        std::string tag = tag_c == nullptr ? "" : std::string((const char*)tag_c);
        for (auto& c : tag)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tag != "input" && tag != "textarea" && tag != "select" &&
            tag != "button")
          continue;
        std::string name = form_attr(el, "name");
        if (name.empty()) continue;
        std::string type = form_attr(el, "type");
        for (auto& c : type)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto push = [&](const std::string& value) {
          v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
          (void)pair
              ->Set(context, 0, ToV8Str(isolate, name))
              .FromMaybe(false);
          (void)pair
              ->Set(context, 1, ToV8Str(isolate, value))
              .FromMaybe(false);
          (void)entries
              ->Set(context, index++, pair)
              .FromMaybe(false);
        };
        auto push_value = [&](v8::Local<v8::Value> value) {
          v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
          (void)pair
              ->Set(context, 0, ToV8Str(isolate, name))
              .FromMaybe(false);
          (void)pair->Set(context, 1, value).FromMaybe(false);
          (void)entries
              ->Set(context, index++, pair)
              .FromMaybe(false);
        };
        if (tag == "button")
        {
          // buttons submit nothing unless they are the designated submitter
          if (node != submitter) continue;
          push(form_attr(el, "value"));
          continue;
        }
        if (tag == "input" && type == "file")
        {
          v8::Local<v8::Object> wrap = WrapNode(isolate, context, node);
          v8::Local<v8::Value> files;
          if (!wrap.IsEmpty() &&
              wrap->Get(context, ToV8Str(isolate, "files"))
                  .ToLocal(&files) &&
              files->IsObject())
          {
            v8::Local<v8::Object> list = files.As<v8::Object>();
            v8::Local<v8::Value> len;
            if (list->Get(context, ToV8Str(isolate, "length")).ToLocal(&len) &&
                len->IsNumber() &&
                len.As<v8::Number>()->Value() > 0)
            {
              uint32_t count =
                  static_cast<uint32_t>(len.As<v8::Number>()->Value());
              for (uint32_t file_index = 0; file_index < count; file_index++)
              {
                v8::Local<v8::Value> file;
                if (list->Get(context, file_index).ToLocal(&file) &&
                    file->IsObject())
                  push_value(file);
              }
            }
          }
          continue;
        }
        if (tag == "input" && (type == "checkbox" || type == "radio"))
        {
          if (form_attr(el, "checked").empty() &&
              !lxb_dom_element_has_attribute(
                  el, (const lxb_char_t*)"checked", 7))
            continue;
          std::string value = form_attr(el, "value");
          if (value.empty()) value = "on";
          push(value);
          continue;
        }
        if (tag == "select")
        {
          for (lxb_dom_node_t* opt = node->first_child; opt != nullptr;
               opt = opt->next)
          {
            if (opt->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
            lxb_dom_element_t* opt_el = lxb_dom_interface_element(opt);
            if (!lxb_dom_element_has_attribute(
                    opt_el, (const lxb_char_t*)"selected", 8))
              continue;
            push(form_attr(opt_el, "value"));
          }
          continue;
        }
        if (tag == "textarea")
        {
          size_t len = 0;
          const lxb_char_t* text =
              lxb_dom_node_text_content(node, &len);
          std::string value =
              text == nullptr ? std::string() : std::string((const char*)text, len);
          push(value);
          continue;
        }
        push(form_attr(el, "value"));
      }
    }

    void FormDataConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (!info.IsConstructCall())
      {
        info.GetIsolate()->ThrowException(ToV8Str(
            info.GetIsolate(), "TypeError: FormData constructor requires new"));
        return;
      }
      v8::Local<v8::Object> obj =
          MakeFormDataImpl(info.GetIsolate(),
                           info.GetIsolate()->GetCurrentContext());
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        lxb_dom_node_t* form = NodeOf(info[0].As<v8::Object>());
        lxb_dom_node_t* submitter = nullptr;
        if (info.Length() >= 2 && info[1]->IsObject())
          submitter = NodeOf(info[1].As<v8::Object>());
        v8::Local<v8::Array> entries =
            FdEntries(info.GetIsolate(),
                      info.GetIsolate()->GetCurrentContext(), obj);
        if (form != nullptr)
          CollectFormEntries(info.GetIsolate(),
                             info.GetIsolate()->GetCurrentContext(), form,
                             submitter, entries);
      }
      info.GetReturnValue().Set(obj);
    }

    // formData(): urlencoded bodies parse into the shared FormData shape
    void ResponseFormData(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Value> body;
      std::string text;
      if (info.This()
              ->GetPrivate(context, RespKey(isolate, "lv8.resp.body"))
              .ToLocal(&body) &&
          body->IsString())
        text = ToUtf8(isolate, body);
      v8::Local<v8::Value> headers_obj;
      std::string content_type;
      if (info.This()->Get(context, ToV8Str(isolate, "headers"))
              .ToLocal(&headers_obj) &&
          headers_obj->IsObject())
      {
        v8::Local<v8::Object> headers = headers_obj.As<v8::Object>();
        v8::Local<v8::Value> get_fn;
        if (headers->Get(context, ToV8Str(isolate, "get")).ToLocal(&get_fn) &&
            get_fn->IsFunction())
        {
          v8::Local<v8::Value> argv[] = {ToV8Str(isolate, "content-type")};
          v8::Local<v8::Value> result;
          if (get_fn.As<v8::Function>()
                  ->Call(context, headers, 1, argv)
                  .ToLocal(&result) &&
              result->IsString())
            content_type = ToUtf8(isolate, result);
        }
        else
        {
          v8::Local<v8::Value> direct;
          if (headers->Get(context, ToV8Str(isolate, "content-type"))
                  .ToLocal(&direct) &&
              direct->IsString())
            content_type = ToUtf8(isolate, direct);
        }
      }
      if (content_type.find("application/x-www-form-urlencoded") ==
          std::string::npos)
      {
        (void)resolver
            ->Reject(context,
                     ToV8Str(isolate,
                             "TypeError: response body is not form data"))
            .FromMaybe(false);
        info.GetReturnValue().Set(resolver->GetPromise());
        return;
      }
      v8::Local<v8::Object> form = MakeFormDataImpl(isolate, context);
      v8::Local<v8::Array> entries = FdEntries(isolate, context, form);
      uint32_t n = 0;
      size_t start = 0;
      while (start <= text.size())
      {
        size_t amp = text.find('&', start);
        std::string chunk = text.substr(
            start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!chunk.empty())
        {
          size_t eq = chunk.find('=');
          std::string name = eq == std::string::npos
              ? FormUrlDecode(chunk)
              : FormUrlDecode(chunk.substr(0, eq));
          std::string value = eq == std::string::npos
              ? ""
              : FormUrlDecode(chunk.substr(eq + 1));
          v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
          (void)pair->Set(context, 0, ToV8Str(isolate, name)).FromMaybe(false);
          (void)pair->Set(context, 1, ToV8Str(isolate, value))
              .FromMaybe(false);
          (void)entries->Set(context, n++, pair).FromMaybe(false);
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
      }
      (void)resolver->Resolve(context, form).FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    v8::Local<v8::Object> MakeResponseImpl(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       int status, const HeaderList& headers,
                                       const std::string& body)
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "Response"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: the reference instances carry zero own symbols -
      // the tag lives on the prototype
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Response"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto accessor = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        // M11: prototype-level (instance-template accessors become own
        // properties on every response; reference instances are zero-own)
        tpl->PrototypeTemplate()->SetAccessorProperty(key, fn,
                                                      v8::Local<v8::FunctionTemplate>());
      };
      accessor("ok", ResponseOk);
      accessor("status", ResponseStatus);
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      method("text", ResponseText);
      method("arrayBuffer", ResponseArrayBuffer);
      method("json", ResponseJson);
      method("formData", ResponseFormData);
      v8::Local<v8::Object> obj = inst->NewInstance(context).ToLocalChecked();
      (void)obj
          ->SetPrivate(context, RespKey(isolate, "lv8.resp.status"),
                       v8::Integer::New(isolate, status))
          .FromMaybe(false);
      (void)obj
          ->SetPrivate(context, RespKey(isolate, "lv8.resp.body"),
                       Latin1ToV8(isolate, body))
          .FromMaybe(false);
      // headers is a real own property (JS reads response.headers)
      (void)obj
          ->Set(context, ToV8Str(isolate, "headers"),
                MakeHeaders(isolate, context, headers))
          .FromMaybe(false);
      // body: a ReadableStream over the payload + live bodyUsed flag
      {
        v8::Local<v8::String> src = ToV8Str(
            isolate,
            "(function (RS, text, response) {"
            " const stream = new RS({ start(controller) {"
            " controller.enqueue(new TextEncoder().encode(text));"
            " controller.close(); } });"
            " Object.defineProperty(response, 'body',"
            " { value: stream, configurable: true });"
            " Object.defineProperty(response, 'bodyUsed',"
            " { get() { return stream.locked; }, configurable: true });"
            " })");
        v8::ScriptOrigin origin(ToV8Str(isolate, "<anonymous>"));
        v8::Local<v8::Script> script =
            v8::Script::Compile(context, src, &origin).ToLocalChecked();
        v8::Local<v8::Value> rs;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "ReadableStream"))
                .ToLocal(&rs) &&
            rs->IsFunction())
        {
          v8::Local<v8::Value> args[] = {rs, ToV8Str(isolate, body), obj};
          (void)script->Run(context)
              .ToLocalChecked()
              .As<v8::Function>()
              ->Call(context, context->Global(), 3, args)
              .FromMaybe(v8::Local<v8::Value>());
        }
      }
      return obj;
    }

    // -------------------------------------------------------------- fetch

    const ContextState::ResourceEntry* FindResourceImpl(ContextState* state,
                                      const std::string& url)
    {
      for (const auto& r : state->resources)
      {
        if (r.url == url)
          return &r;
      }
      return nullptr;
    }

    void GlobalFetch(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      if (state == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        (void)resolver
            ->Reject(context, ToV8Str(isolate, "TypeError: fetch requires a url"))
            .FromMaybe(false);
        info.GetReturnValue().Set(promise);
        return;
      }
      std::string url = ResolveUrlString(isolate, context,
                                         ToUtf8(isolate, info[0]));
      if (url.empty())
      {
        (void)resolver
            ->Reject(context, ToV8Str(isolate, "TypeError: invalid url"))
            .FromMaybe(false);
        info.GetReturnValue().Set(promise);
        return;
      }

      // blob: object URLs resolve from the JS registry synchronously
      if (url.rfind("blob:", 0) == 0)
      {
        v8::Local<v8::Value> lookup;
        lookup = InternalBridgeGet(isolate, context, "blobLookup");
        if (lookup->IsFunction())
        {
          v8::Local<v8::Value> argv[] = {ToV8Str(isolate, url)};
          v8::Local<v8::Value> text;
          if (lookup.As<v8::Function>()
                  ->Call(context, context->Global(), 1, argv)
                  .ToLocal(&text) &&
              !text->IsNull() && !text->IsUndefined())
          {
            std::string body = ToUtf8(isolate, text);
            v8::Local<v8::Object> response =
                MakeResponse(isolate, context, 200, {}, body);
            (void)resolver->Resolve(context, response).FromMaybe(false);
            info.GetReturnValue().Set(promise);
            return;
          }
        }
        (void)resolver
            ->Reject(context,
                     ToV8Str(isolate, "TypeError: blob URL not found"))
            .FromMaybe(false);
        info.GetReturnValue().Set(promise);
        return;
      }

      // request headers: profile-derived defaults overridden by
      // caller-supplied names (case-insensitive, stored lowercase)
      HeaderList headers = DefaultRequestHeaders(isolate, context, state);
      HeaderList extra;
      if (info.Length() > 1 && info[1]->IsObject())
      {
        v8::Local<v8::Value> h;
        if (info[1]
                .As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "headers"))
                .ToLocal(&h) &&
            h->IsObject())
        {
          v8::Local<v8::Array> names = h.As<v8::Object>()
                                           ->GetOwnPropertyNames(context)
                                           .ToLocalChecked();
          for (uint32_t i = 0; i < names->Length(); i++)
          {
            v8::Local<v8::Value> key = names->Get(context, i)
                                           .FromMaybe(v8::Local<v8::Value>());
            v8::Local<v8::Value> value;
            if (!key->IsString() ||
                !h.As<v8::Object>()
                     ->Get(context, key)
                     .ToLocal(&value) ||
                !value->IsString())
              continue;
            extra.push_back({ToLower(ToUtf8(isolate, key)),
                             ToUtf8(isolate, value)});
          }
        }
      }
      for (const auto& e : extra)
      {
        bool replaced = false;
        for (auto& h : headers)
        {
          if (h.first == e.first)
          {
            h.second = e.second;
            replaced = true;
            break;
          }
        }
        if (!replaced)
          headers.push_back(e);
      }

      // netLog records every request (cookies included)
      ContextState::NetEntry entry;
      entry.method = "GET";
      entry.url = url;
      entry.headers = headers;
      entry.cookie_header = GetCookieString(isolate, context);
      AppendNetEntryImpl(isolate, context, state, entry);
      state->pending_network_tasks++;

      const ContextState::ResourceEntry* resource =
          FindResourceImpl(state, url);
      int status = 404;
      HeaderList resp_headers;
      std::string body;
      if (resource != nullptr)
      {
        status = resource->status;
        resp_headers = resource->headers;
        body = resource->body;
        // set-cookie headers mutate the shared cookie jar (multiple
        // set-cookie headers are allowed and order-preserving)
        for (const auto& h : resource->headers)
        {
          if (ToLower(h.first) == "set-cookie")
            SetCookieFromDocument(isolate, context, h.second);
        }
        ContextState::PerfResource perf;
        perf.initiator_type = "fetch";
        perf.url = url;
        perf.body_size = body.size();
        state->perf_resources.push_back(perf);
      }
      // responses deliver at the explicit drain, not inside eval
      ContextState::PendingDelivery pending;
      pending.resolver.Reset(isolate, resolver);
      pending.status = status;
      pending.headers = resp_headers;
      pending.body = body;
      if (resource != nullptr)
      {
        pending.status = status;
        pending.headers = resp_headers;
        pending.body = body;
      }
      else if (state->bridge_enabled)
      {
        // park for the Python-side bridge; resolved at drain time
        pending.bridged = true;
        pending.method = "GET";
        pending.url = url;
        pending.request_headers = headers;
        pending.cookie_header = GetCookieString(isolate, context);
        pending.has_request_body = false;
        pending.bridge_initiator = "fetch";
      }
      else
      {
        // reference parity: a missing offline resource rejects the fetch
        // promise instead of resolving with a 404 response
        pending.reject = true;
        pending.body = "lv8 offline resource not found: " + url;
      }
      if (info.Length() > 1 && info[1]->IsObject())
      {
        v8::Local<v8::Value> signal_v;
        if (info[1].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "signal"))
                .ToLocal(&signal_v) && signal_v->IsObject())
          pending.signal.Reset(isolate, signal_v.As<v8::Object>());
      }
      state->pending_responses.push_back(std::move(pending));
      info.GetReturnValue().Set(promise);
    }

    // ------------------------------------------------------------- caches

    void NetReject(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Promise::Resolver> resolver,
                   const std::string& message)
    {
      (void)resolver->Reject(context, ToV8Str(isolate, message))
          .FromMaybe(false);
    }

    void NetReturn(const v8::FunctionCallbackInfo<v8::Value>& info,
                   v8::Local<v8::Value> value)
    {
      info.GetReturnValue().Set(value);
    }

    void CacheStorePut(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       ContextState* state, const std::string& name,
                       const std::string& url, const HeaderList& headers,
                       const std::string& body)
    {
      ContextState::CacheStore* store = nullptr;
      for (auto& s : state->cache_stores)
      {
        if (s.name == name)
        {
          store = &s;
          break;
        }
      }
      if (store == nullptr)
      {
        state->cache_stores.push_back({name, {}});
        store = &state->cache_stores.back();
      }
      for (auto& e : store->entries)
      {
        if (e.url == url)
        {
          e.headers = headers;
          e.body = body;
          return;
        }
      }
      store->entries.push_back({url, headers, body});
    }

    const ContextState::CacheEntry* CacheStoreMatch(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      ContextState* state,
                                      const std::string& name,
                                      const std::string& url)
    {
      for (const auto& s : state->cache_stores)
      {
        if (!name.empty() && s.name != name)
          continue;
        for (const auto& e : s.entries)
        {
          if (e.url == url)
            return &e;
        }
      }
      return nullptr;
    }

    // fetch a resource table entry for cache.add(url)
    bool FetchIntoCache(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        ContextState* state, const std::string& name,
                        const std::string& url, std::string* body_out)
    {
      const ContextState::ResourceEntry* resource =
          FindResourceImpl(state, url);
      if (resource == nullptr)
        return false;
      ContextState::NetEntry entry;
      entry.method = "GET";
      entry.url = url;
      entry.headers = DefaultRequestHeaders(isolate, context, state);
      entry.cookie_header = GetCookieString(isolate, context);
      AppendNetEntryImpl(isolate, context, state, entry);
      for (const auto& h : resource->headers)
      {
        if (ToLower(h.first) == "set-cookie")
          SetCookieFromDocument(isolate, context, h.second);
      }
      CacheStorePut(isolate, context, state, name, url, resource->headers,
                    resource->body);
      *body_out = resource->body;
      return true;
    }

    void CacheStorageOpen(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      if (state == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        (void)resolver
            ->Reject(context,
                     ToV8Str(isolate, "TypeError: caches.open requires a name"))
            .FromMaybe(false);
        info.GetReturnValue().Set(promise);
        return;
      }
      std::string name = ToUtf8(isolate, info[0]);

      // a Cache object bound to |name| through a Private symbol
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "Cache"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Cache"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* mname, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, mname);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      method("add", [](const v8::FunctionCallbackInfo<v8::Value>& cb_info) {
        v8::Isolate* iso = cb_info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        ContextState* st = GetState(ctx);
        v8::Local<v8::Promise::Resolver> res =
            v8::Promise::Resolver::New(ctx).ToLocalChecked();
        v8::Local<v8::Promise> p = res->GetPromise();
        v8::Local<v8::Value> cache_name;
        std::string store;
        if (!cb_info.This()
                         ->GetPrivate(ctx,
                                      v8::Private::ForApi(
                                          iso, ToV8Str(iso, "lv8.cache.name")))
                         .ToLocal(&cache_name) ||
            !cache_name->IsString())
        {
          NetReject(iso, ctx, res, "TypeError: unbound cache");
          NetReturn(cb_info, p);
          return;
        }
        store = ToUtf8(iso, cache_name);
        if (st == nullptr || cb_info.Length() < 1 || !cb_info[0]->IsString())
        {
          NetReject(iso, ctx, res, "TypeError: cache.add requires a url");
          NetReturn(cb_info, p);
          return;
        }
        std::string url =
            ResolveUrlString(iso, ctx, ToUtf8(iso, cb_info[0]));
        std::string body;
        if (url.empty() ||
            !FetchIntoCache(iso, ctx, st, store, url, &body))
        {
          NetReject(iso, ctx, res,
                      "TypeError: request not found in the offline bundle");
          NetReturn(cb_info, p);
          return;
        }
        (void)res->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
        NetReturn(cb_info, p);
      });
      method("match", [](const v8::FunctionCallbackInfo<v8::Value>& cb_info) {
        v8::Isolate* iso = cb_info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        ContextState* st = GetState(ctx);
        v8::Local<v8::Promise::Resolver> res =
            v8::Promise::Resolver::New(ctx).ToLocalChecked();
        v8::Local<v8::Promise> p = res->GetPromise();
        v8::Local<v8::Value> cache_name;
        std::string store;
        if (!cb_info.This()
                         ->GetPrivate(ctx,
                                      v8::Private::ForApi(
                                          iso, ToV8Str(iso, "lv8.cache.name")))
                         .ToLocal(&cache_name) ||
            !cache_name->IsString())
        {
          NetReject(iso, ctx, res, "TypeError: unbound cache");
          NetReturn(cb_info, p);
          return;
        }
        store = ToUtf8(iso, cache_name);
        if (st == nullptr || cb_info.Length() < 1 || !cb_info[0]->IsString())
        {
          (void)res->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
          NetReturn(cb_info, p);
          return;
        }
        std::string url =
            ResolveUrlString(iso, ctx, ToUtf8(iso, cb_info[0]));
        const ContextState::CacheEntry* hit =
            url.empty() ? nullptr
                        : CacheStoreMatch(iso, ctx, st, store, url);
        if (hit == nullptr)
        {
          (void)res->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
        }
        else
        {
          (void)res
              ->Resolve(ctx, MakeResponse(iso, ctx, 200, hit->headers,
                                          hit->body))
              .FromMaybe(false);
        }
        NetReturn(cb_info, p);
      });
      method("keys", [](const v8::FunctionCallbackInfo<v8::Value>& cb_info) {
        v8::Isolate* iso = cb_info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        ContextState* st = GetState(ctx);
        v8::Local<v8::Promise::Resolver> res =
            v8::Promise::Resolver::New(ctx).ToLocalChecked();
        v8::Local<v8::Promise> p = res->GetPromise();
        v8::Local<v8::Array> urls = v8::Array::New(iso);
        uint32_t n = 0;
        if (st != nullptr)
        {
          v8::Local<v8::Value> cache_name;
          std::string store;
          if (cb_info.This()
                  ->GetPrivate(ctx,
                               v8::Private::ForApi(
                                   iso, ToV8Str(iso, "lv8.cache.name")))
                  .ToLocal(&cache_name) &&
              cache_name->IsString())
            store = ToUtf8(iso, cache_name);
          for (const auto& s : st->cache_stores)
          {
            if (s.name != store)
              continue;
            for (const auto& e : s.entries)
              (void)urls->Set(ctx, n++, ToV8Str(iso, e.url)).FromMaybe(false);
          }
        }
        (void)res->Resolve(ctx, urls).FromMaybe(false);
        NetReturn(cb_info, p);
      });

      v8::Local<v8::Object> cache = inst->NewInstance(context).ToLocalChecked();
      (void)cache
          ->SetPrivate(context,
                       v8::Private::ForApi(isolate,
                                           ToV8Str(isolate, "lv8.cache.name")),
                       ToV8Str(isolate, name))
          .FromMaybe(false);
      (void)resolver->Resolve(context, cache).FromMaybe(false);
      info.GetReturnValue().Set(promise);
    }

    void CacheStorageMatch(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      if (state == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        (void)resolver->Resolve(context, v8::Undefined(isolate))
            .FromMaybe(false);
        info.GetReturnValue().Set(promise);
        return;
      }
      std::string url =
          ResolveUrlString(isolate, context, ToUtf8(isolate, info[0]));
      const ContextState::CacheEntry* hit =
          url.empty() ? nullptr : CacheStoreMatch(isolate, context, state, "",
                                                 url);
      if (hit == nullptr)
        (void)resolver->Resolve(context, v8::Undefined(isolate))
            .FromMaybe(false);
      else
        (void)resolver
            ->Resolve(context,
                      MakeResponse(isolate, context, 200, hit->headers,
                                   hit->body))
            .FromMaybe(false);
      info.GetReturnValue().Set(promise);
    }

    void CacheStorageKeys(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Array> names = v8::Array::New(isolate);
      uint32_t n = 0;
      if (state != nullptr)
      {
        for (const auto& s : state->cache_stores)
          (void)names->Set(context, n++, ToV8Str(isolate, s.name))
              .FromMaybe(false);
      }
      (void)resolver->Resolve(context, names).FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void PerformanceGetEntriesByType(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::Array> arr = v8::Array::New(isolate);
      if (state != nullptr && info.Length() >= 1 && info[0]->IsString() &&
          ToUtf8(isolate, info[0]) == "resource")
      {
        uint32_t i = 0;
        for (const auto& e : state->perf_resources)
        {
          v8::Local<v8::Object> entry = v8::Object::New(isolate);
          (void)entry
              ->Set(context, ToV8Str(isolate, "entryType"),
                    ToV8Str(isolate, "resource"))
              .FromMaybe(false);
          (void)entry
              ->Set(context, ToV8Str(isolate, "name"),
                    ToV8Str(isolate, e.url))
              .FromMaybe(false);
          (void)entry
              ->Set(context, ToV8Str(isolate, "initiatorType"),
                    ToV8Str(isolate, e.initiator_type))
              .FromMaybe(false);
          (void)entry
              ->Set(context, ToV8Str(isolate, "encodedBodySize"),
                    v8::Integer::New(isolate,
                                     static_cast<int32_t>(e.body_size)))
              .FromMaybe(false);
          (void)arr->Set(context, i++, entry).FromMaybe(false);
        }
      }
      info.GetReturnValue().Set(arr);
    }

    } // namespace

  void AppendNetEntry(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      ContextState* state,
                      const ContextState::NetEntry& entry)
  {
    AppendNetEntryImpl(isolate, context, state, entry);
  }

  const ContextState::ResourceEntry* FindNetResource(ContextState* state,
                                                     const std::string& url)
  {
    return FindResourceImpl(state, url);
  }

  v8::Local<v8::Object> MakeResponse(
      v8::Isolate* isolate, v8::Local<v8::Context> context, int status,
      const std::vector<std::pair<std::string, std::string>>& headers,
      const std::string& body)
  {
    return MakeResponseImpl(isolate, context, status, headers, body);
  }

  int AddResource(v8::Isolate* isolate, v8::Local<v8::Context> context,
                  const std::string& url, const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& headers,
                  int status)
  {
    ContextState* state = GetState(context);
    if (state == nullptr)
      return -1;
    // replace-or-append by url, mirroring cache.put semantics
    for (auto& r : state->resources)
    {
      if (r.url == url)
      {
        r.body = body;
        r.headers = headers;
        r.status = status;
        return static_cast<int>(state->resources.size());
      }
    }
    state->resources.push_back({url, body, headers, status});
    return static_cast<int>(state->resources.size());
  }

  int PendingNetworkTasks(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    (void)isolate;
    ContextState* state = GetState(context);
    return state == nullptr
               ? 0
               : state->pending_network_tasks +
                     static_cast<int>(state->pending_idb.size());
  }

  void ClearPendingNetworkTasks(v8::Isolate* isolate,
                                v8::Local<v8::Context> context)
  {
    (void)isolate;
    ContextState* state = GetState(context);
    if (state != nullptr)
      state->pending_network_tasks = 0;
  }

  void SetBridgeEnabled(v8::Isolate* isolate,
                        v8::Local<v8::Context> context, bool enabled)
  {
    ContextState* state = GetState(context);
    if (state != nullptr) state->bridge_enabled = enabled;
  }

  std::string JsonEscape(const std::string& raw)
  {
    std::string out;
    for (char c : raw)
    {
      switch (c)
      {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
      }
    }
    return out;
  }

  std::string PendingBridgedRequests(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context)
  {
    ContextState* state = GetState(context);
    if (state == nullptr) return "[]";
    std::string json = "[";
    bool first = true;
    for (const auto& pending : state->pending_responses)
    {
      if (!pending.bridged) continue;
      if (!first) json += ",";
      first = false;
      json += "{\"method\":\"" + JsonEscape(pending.method) + "\"";
      json += ",\"url\":\"" + JsonEscape(pending.url) + "\"";
      json += ",\"headers\":{";
      bool hfirst = true;
      for (const auto& h : pending.request_headers)
      {
        if (!hfirst) json += ",";
        hfirst = false;
        json += "\"" + JsonEscape(h.first) + "\":\"" +
                JsonEscape(h.second) + "\"";
      }
      json += "},\"cookieHeader\":\"" +
              JsonEscape(pending.cookie_header) + "\"";
      if (pending.has_request_body)
        json += ",\"body\":\"" + JsonEscape(pending.request_body) + "\"";
      else
        json += ",\"body\":null";
      json += "}";
    }
    json += "]";
    return json;
  }

  void ResolveBridged(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      const std::string& url, const std::string& outcome,
                      const std::string& body,
                      const std::string& headers_json, int status)
  {
    ContextState* state = GetState(context);
    if (state == nullptr) return;
    for (auto& pending : state->pending_responses)
    {
      if (!pending.bridged || pending.url != url) continue;
      pending.bridged = false;
      if (outcome == "reject")
      {
        pending.reject = true;
        pending.body = body;
        return;
      }
      if (outcome == "bundle")
      {
        const ContextState::ResourceEntry* resource =
            FindResourceImpl(state, url);
        if (resource != nullptr)
        {
          pending.status = resource->status;
          pending.headers = resource->headers;
          pending.body = resource->body;
        }
        else
        {
          pending.reject = true;
          pending.body = "lv8 offline resource not found: " + url;
        }
        return;
      }
      // outcome == "response": body/headers/status carry the answer;
      // headers arrive as a JSON [[k,v],...] array
      pending.status = status;
      pending.body = body;
      {
        ContextState::PerfResource perf;
        perf.initiator_type = pending.bridge_initiator;
        perf.url = pending.url;
        perf.body_size = body.size();
        state->perf_resources.push_back(perf);
      }
      pending.headers.clear();
      if (!headers_json.empty())
      {
        v8::Local<v8::String> json = ToV8Str(isolate, headers_json);
        v8::Local<v8::Value> parsed;
        if (v8::JSON::Parse(context, json).ToLocal(&parsed) &&
            parsed->IsArray())
        {
          v8::Local<v8::Array> pairs = parsed.As<v8::Array>();
          for (uint32_t i = 0; i < pairs->Length(); i++)
          {
            v8::Local<v8::Value> pair;
            if (!pairs->Get(context, i).ToLocal(&pair) ||
                !pair->IsArray())
              continue;
            v8::Local<v8::Array> kv = pair.As<v8::Array>();
            if (kv->Length() < 2) continue;
            v8::Local<v8::Value> k, v;
            if (kv->Get(context, 0).ToLocal(&k) &&
                kv->Get(context, 1).ToLocal(&v) && k->IsString() &&
                v->IsString())
              pending.headers.emplace_back(ToUtf8(isolate, k),
                                           ToUtf8(isolate, v));
          }
        }
      }
      for (const auto& h : pending.headers)
      {
        if (ToLower(h.first) == "set-cookie")
          SetCookieFromDocument(isolate, context, h.second);
      }
      return;
    }
  }

  int DispatchPendingResponses(v8::Isolate* isolate,
                               v8::Local<v8::Context> context)
  {
    ContextState* state = GetState(context);
    if (state == nullptr)
      return 0;
    int delivered = 0;
    DispatchIdbTasks(isolate, context);
    for (auto& pending : state->pending_responses)
    {
      if (pending.bridged)
        continue;  // parked for the Python bridge
      if (!pending.resolver.IsEmpty())
      {
        v8::Local<v8::Promise::Resolver> resolver =
            pending.resolver.Get(isolate);
        if (!pending.signal.IsEmpty())
        {
          v8::Local<v8::Object> signal = pending.signal.Get(isolate);
          v8::Local<v8::Value> aborted;
          if (signal->Get(context, ToV8Str(isolate, "aborted"))
                  .ToLocal(&aborted) &&
              aborted->BooleanValue(isolate))
          {
            v8::Local<v8::Value> reason;
            std::string message = "The operation was aborted.";
            if (signal->Get(context, ToV8Str(isolate, "reason"))
                    .ToLocal(&reason))
            {
              if (reason->IsObject())
              {
                v8::Local<v8::Value> name;
                if (reason.As<v8::Object>()
                        ->Get(context, ToV8Str(isolate, "name"))
                        .ToLocal(&name) && name->IsString())
                {
                  v8::Local<v8::Object> err =
                      v8::Exception::Error(reason.As<v8::String>())
                          .As<v8::Object>();
                  (void)err->Set(context, ToV8Str(isolate, "name"), name)
                      .FromMaybe(false);
                  (void)resolver->Reject(context, err).FromMaybe(false);
                  continue;
                }
              }
              else if (!reason->IsUndefined() && !reason->IsNull())
                message = ToUtf8(isolate, reason);
            }
            v8::Local<v8::Object> err = v8::Exception::Error(
                ToV8Str(isolate, message)).As<v8::Object>();
            (void)err->Set(context, ToV8Str(isolate, "name"),
                           ToV8Str(isolate, "AbortError")).FromMaybe(false);
            (void)resolver->Reject(context, err).FromMaybe(false);
            continue;
          }
        }
        if (pending.reject)
        {
          (void)resolver
              ->Reject(context, v8::Exception::TypeError(
                                    ToV8Str(isolate, pending.body)))
              .FromMaybe(false);
        }
        else
        {
          v8::Local<v8::Object> response =
              MakeResponse(isolate, context, pending.status, pending.headers,
                           pending.body);
          (void)resolver->Resolve(context, response).FromMaybe(false);
        }
      }
      else if (!pending.xhr.IsEmpty())
      {
        v8::Local<v8::Object> xhr = pending.xhr.Get(isolate);
        // store response data first so state-4 callbacks can read it
        (void)xhr
            ->SetPrivate(context, RespKey(isolate, "lv8.xhr.status"),
                         v8::Integer::New(isolate, pending.status))
            .FromMaybe(false);
        (void)xhr
            ->SetPrivate(context, RespKey(isolate, "lv8.xhr.text"),
                         ToV8Str(isolate, pending.body))
            .FromMaybe(false);
        {
          v8::Local<v8::ArrayBuffer> buffer =
              v8::ArrayBuffer::New(isolate, pending.body.size());
          if (pending.body.size() > 0)
          {
            memcpy(buffer->GetBackingStore()->Data(), pending.body.data(),
                   pending.body.size());
          }
          (void)xhr
              ->SetPrivate(context, RespKey(isolate, "lv8.xhr.bytes"), buffer)
              .FromMaybe(false);
        }
        // raw [k, v] pairs: getResponseHeader/getAllResponseHeaders walk
        // this list directly (a Headers object would hide the entries)
        v8::Local<v8::Array> raw = v8::Array::New(isolate);
        for (uint32_t i = 0; i < pending.headers.size(); i++)
        {
          v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
          (void)pair
              ->Set(context, 0,
                    ToV8Str(isolate, pending.headers[i].first))
              .FromMaybe(false);
          (void)pair
              ->Set(context, 1,
                    ToV8Str(isolate, pending.headers[i].second))
              .FromMaybe(false);
          (void)raw->Set(context, i, pair).FromMaybe(false);
        }
        (void)xhr
            ->SetPrivate(context, RespKey(isolate, "lv8.xhr.headers"), raw)
            .FromMaybe(false);
        for (int ready : {1, 2, 3, 4})
        {
          (void)xhr
              ->SetPrivate(context, RespKey(isolate, "lv8.xhr.ready"),
                           v8::Integer::New(isolate, ready))
              .FromMaybe(false);
          v8::Local<v8::Value> handler;
          if (xhr->Get(context, ToV8Str(isolate, "onreadystatechange"))
                  .ToLocal(&handler) &&
              handler->IsFunction())
          {
            v8::TryCatch try_catch(isolate);
            (void)handler.As<v8::Function>()
                ->Call(context, xhr, 0, nullptr)
                .FromMaybe(v8::Local<v8::Value>());
            if (try_catch.HasCaught())
              CJavascriptException::ThrowIf(isolate, try_catch);
          }
        }
        v8::Local<v8::Value> load;
        if (xhr->Get(context, ToV8Str(isolate, "onload")).ToLocal(&load) &&
            load->IsFunction())
        {
          v8::TryCatch try_catch(isolate);
          (void)load.As<v8::Function>()
              ->Call(context, xhr, 0, nullptr)
              .FromMaybe(v8::Local<v8::Value>());
          if (try_catch.HasCaught())
            CJavascriptException::ThrowIf(isolate, try_catch);
        }
      }
      delivered++;
    }
    state->pending_responses.clear();
    state->pending_network_tasks = 0;
    return delivered;
  }

  v8::Local<v8::Object> MakeFileList(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       v8::Local<v8::Array> files)
    {
      ContextState* state = GetState(context);
      v8::Local<v8::Object> inst = state->file_list_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context,
                             v8::Private::ForApi(
                                 isolate,
                                 ToV8Str(isolate, "lv8.filelist.items")),
                             files)
          .FromMaybe(false);
      uint32_t count = files->Length();
      for (uint32_t i = 0; i < count; i++)
      {
        v8::Local<v8::Value> file;
        if (files->Get(context, i).ToLocal(&file))
          (void)inst->Set(context, i, file).FromMaybe(false);
      }
      (void)inst->Set(context, ToV8Str(isolate, "length"),
                      v8::Integer::New(isolate,
                                       static_cast<int32_t>(count)))
          .FromMaybe(false);
      return inst;
    }


    // ---- G4 ledger chunk 7c: deterministic streams ---------------------
    // (ReadableStream/WritableStream/TransformStream + controllers),
    // formerly JS bootstrap classes. Chunks live in private arrays; the
    // reader/writer are per-call native objects closing over the stream.

    v8::Local<v8::Private> RsChunksKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.rs.chunks"));
    }
    v8::Local<v8::Private> RsClosedKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.rs.closed"));
    }
    v8::Local<v8::Private> RsErroredKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.rs.errored"));
    }
    v8::Local<v8::Private> RsHasErroredKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.rs.has_errored"));
    }
    v8::Local<v8::Private> RsControllerKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.rs.controller"));
    }
    v8::Local<v8::Private> RsReaderKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.rs.reader"));
    }
    v8::Local<v8::Private> RscStreamKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.rsc.stream"));
    }
    v8::Local<v8::Private> WsUnderlyingKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.ws.underlying"));
    }

    void RsEnqueue(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> stream, v8::Local<v8::Value> chunk)
    {
      v8::Local<v8::Value> chunks;
      if (stream->GetPrivate(context, RsChunksKey(isolate)).ToLocal(&chunks) &&
          chunks->IsArray())
        (void)chunks.As<v8::Array>()
            ->Set(context, chunks.As<v8::Array>()->Length(), chunk)
            .FromMaybe(false);
    }

    void RsSetClosed(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> stream, bool closed)
    {
      (void)stream
          ->SetPrivate(context, RsClosedKey(isolate),
                       v8::Boolean::New(isolate, closed))
          .FromMaybe(false);
    }

    // resolve a promise with {value, done}
    void ResolveValueDone(v8::Isolate* isolate,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Promise::Resolver> resolver,
                          v8::Local<v8::Value> value, bool done)
    {
      v8::Local<v8::Object> out = v8::Object::New(isolate);
      (void)out->Set(context, ToV8Str(isolate, "value"), value)
          .FromMaybe(false);
      (void)out->Set(context, ToV8Str(isolate, "done"),
                     v8::Boolean::New(isolate, done))
          .FromMaybe(false);
      (void)resolver->Resolve(context, out).FromMaybe(false);
    }

    void RsControllerEnqueue(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1) return;
      v8::Local<v8::Value> stream;
      if (!info.This()
                ->GetPrivate(context, RscStreamKey(isolate))
                .ToLocal(&stream) ||
          !stream->IsObject())
        return;
      RsEnqueue(isolate, context, stream.As<v8::Object>(), info[0]);
    }

    void RsControllerClose(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> stream;
      if (info.This()
              ->GetPrivate(context, RscStreamKey(isolate))
              .ToLocal(&stream) &&
          stream->IsObject())
        RsSetClosed(isolate, context, stream.As<v8::Object>(), true);
    }

    void RsControllerError(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> stream;
      if (!info.This()
                ->GetPrivate(context, RscStreamKey(isolate))
                .ToLocal(&stream) ||
          !stream->IsObject())
        return;
      v8::Local<v8::Value> reason;
      bool have = info.Length() >= 1;
      if (have && info[0]->IsObject())
        reason = info[0];
      if (!have)
      {
        v8::Local<v8::Value> error_ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "Error"))
                .ToLocal(&error_ctor) &&
            error_ctor->IsFunction())
        {
          v8::Local<v8::Value> argv[] = {ToV8Str(isolate, "stream errored")};
          v8::Local<v8::Value> error;
          if (error_ctor.As<v8::Function>()
                  ->NewInstance(context, 1, argv)
                  .ToLocal(&error))
            reason = error;
        }
      }
      (void)stream.As<v8::Object>()
          ->SetPrivate(context, RsErroredKey(isolate),
                       reason.IsEmpty()
                           ? v8::Local<v8::Value>(v8::Null(isolate))
                           : reason)
          .FromMaybe(false);
      (void)stream.As<v8::Object>()
          ->SetPrivate(context, RsHasErroredKey(isolate),
                       v8::Boolean::New(isolate, true))
          .FromMaybe(false);
    }

    void ReadableStreamCancel(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      RsSetClosed(isolate, context, info.This(), true);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      (void)resolver->Resolve(context, v8::Undefined(isolate))
          .FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void ReadableStreamGetReader(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::Object> reader = v8::Object::New(isolate);
      v8::Local<v8::FunctionTemplate> read_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Value> stream_value = args.Data();
            if (stream_value.IsEmpty() || !stream_value->IsObject())
              return;
            v8::Local<v8::Object> stream = stream_value.As<v8::Object>();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            v8::Local<v8::Value> chunks;
            if (stream->GetPrivate(ctx, RsChunksKey(iso)).ToLocal(&chunks) &&
                chunks->IsArray() &&
                chunks.As<v8::Array>()->Length() > 0)
            {
              v8::Local<v8::Array> list = chunks.As<v8::Array>();
              v8::Local<v8::Value> value;
              if (list->Get(ctx, 0).ToLocal(&value))
              {
                // shift: drop the first entry and close the gap
                for (uint32_t k = 0; k + 1 < list->Length(); k++)
                {
                  v8::Local<v8::Value> next;
                  if (list->Get(ctx, k + 1).ToLocal(&next))
                    (void)list->Set(ctx, k, next).FromMaybe(false);
                }
                // delete leaves a hole: shrink length explicitly
                (void)list->Set(ctx, ToV8Str(iso, "length"),
                                v8::Integer::New(iso, static_cast<int32_t>(list->Length() - 1)))
                    .FromMaybe(false);
                ResolveValueDone(iso, ctx, resolver, value, false);
              }
              else
              {
                ResolveValueDone(iso, ctx, resolver,
                                 v8::Undefined(iso), true);
              }
            }
            else
            {
              v8::Local<v8::Value> errored;
              bool has_errored = false;
              if (stream->GetPrivate(ctx, RsHasErroredKey(iso))
                      .ToLocal(&errored) &&
                  errored->IsBoolean())
                has_errored = errored->BooleanValue(iso);
              if (has_errored)
              {
                v8::Local<v8::Value> reason;
                if (stream->GetPrivate(ctx, RsErroredKey(iso)).ToLocal(&reason))
                  (void)resolver->Reject(ctx, reason).FromMaybe(false);
                else
                  (void)resolver->Reject(ctx, v8::Null(iso)).FromMaybe(false);
              }
              else
              {
                ResolveValueDone(iso, ctx, resolver, v8::Undefined(iso), true);
              }
            }
            args.GetReturnValue().Set(resolver->GetPromise());
          },
          info.This());
      read_tpl->SetClassName(ToV8Str(isolate, "read"));
      v8::Local<v8::Function> read_fn =
          read_tpl->GetFunction(context).ToLocalChecked();
      (void)reader->Set(context, ToV8Str(isolate, "read"), read_fn)
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> cancel_tpl =
          v8::FunctionTemplate::New(isolate, ReadableStreamCancel);
      cancel_tpl->SetClassName(ToV8Str(isolate, "cancel"));
      (void)reader->Set(context, ToV8Str(isolate, "cancel"),
                        cancel_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> release_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Value> stream_value = args.Data();
            if (stream_value.IsEmpty() || !stream_value->IsObject())
              return;
            (void)stream_value.As<v8::Object>()
                ->SetPrivate(ctx, RsReaderKey(iso), v8::Null(iso))
                .FromMaybe(false);
          },
          info.This());
      release_tpl->SetClassName(ToV8Str(isolate, "releaseLock"));
      (void)reader->Set(context, ToV8Str(isolate, "releaseLock"),
                        release_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)info.This()->SetPrivate(context, RsReaderKey(isolate), reader)
          .FromMaybe(false);
      info.GetReturnValue().Set(reader);
    }

    void ReadableStreamLocked(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> reader;
      bool locked = info.This()
                        ->GetPrivate(context, RsReaderKey(isolate))
                        .ToLocal(&reader) &&
                    !reader->IsNull() && !reader->IsUndefined();
      info.GetReturnValue().Set(v8::Boolean::New(isolate, locked));
    }

    void ReadableStreamConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->readable_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: ReadableStream constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->readable_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, RsChunksKey(isolate),
                             v8::Array::New(isolate))
          .FromMaybe(false);
      RsSetClosed(isolate, context, inst, false);
      (void)inst->SetPrivate(context, RsErroredKey(isolate), v8::Null(isolate))
          .FromMaybe(false);
      (void)inst->SetPrivate(context, RsHasErroredKey(isolate),
                             v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      v8::Local<v8::Object> underlying =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0].As<v8::Object>()
              : v8::Object::New(isolate);
      v8::Local<v8::Object> controller =
          state->readablectl_tpl.Get(isolate)
              ->InstanceTemplate()
              ->NewInstance(context)
              .ToLocalChecked();
      (void)controller->SetPrivate(context, RscStreamKey(isolate), inst)
          .FromMaybe(false);
      (void)inst->SetPrivate(context, RsControllerKey(isolate), controller)
          .FromMaybe(false);
      v8::Local<v8::Value> start;
      if (underlying->Get(context, ToV8Str(isolate, "start")).ToLocal(&start) &&
          start->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {controller};
        v8::TryCatch try_catch(isolate);
        (void)start.As<v8::Function>()
            ->Call(context, underlying, 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
      info.GetReturnValue().Set(inst);
    }

    void WritableStreamConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->writable_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: WritableStream constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->writable_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      v8::Local<v8::Object> underlying =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0].As<v8::Object>()
              : v8::Object::New(isolate);
      (void)inst->SetPrivate(context, WsUnderlyingKey(isolate), underlying)
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void WritableStreamGetWriter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> writer = v8::Object::New(isolate);
      v8::Local<v8::Value> underlying_value;
      if (!info.This()
                ->GetPrivate(context, WsUnderlyingKey(isolate))
                .ToLocal(&underlying_value))
        underlying_value = v8::Undefined(isolate);
      v8::Local<v8::FunctionTemplate> write_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            v8::Local<v8::Value> underlying = args.Data();
            if (underlying.IsEmpty() || !underlying->IsObject())
            {
              (void)resolver->Resolve(ctx, v8::Undefined(iso))
                  .FromMaybe(false);
              args.GetReturnValue().Set(resolver->GetPromise());
              return;
            }
            v8::Local<v8::Value> sink_fn;
            if (underlying.As<v8::Object>()
                    ->Get(ctx, ToV8Str(iso, "write"))
                    .ToLocal(&sink_fn) &&
                sink_fn->IsFunction())
            {
              v8::Local<v8::Value> sink_argv[] = {
                  args.Length() >= 1
                      ? args[0]
                      : v8::Local<v8::Value>(v8::Undefined(iso))};
              v8::TryCatch try_catch(iso);
              (void)sink_fn.As<v8::Function>()
                  ->Call(ctx, underlying, 1, sink_argv)
                  .FromMaybe(v8::Local<v8::Value>());
              try_catch.Reset();
            }
            (void)resolver->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
            args.GetReturnValue().Set(resolver->GetPromise());
          },
          underlying_value);
      write_tpl->SetClassName(ToV8Str(isolate, "write"));
      v8::Local<v8::FunctionTemplate> close_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            v8::Local<v8::Value> underlying = args.Data();
            if (underlying.IsEmpty() || !underlying->IsObject())
            {
              (void)resolver->Resolve(ctx, v8::Undefined(iso))
                  .FromMaybe(false);
              args.GetReturnValue().Set(resolver->GetPromise());
              return;
            }
            v8::Local<v8::Value> sink_fn;
            if (underlying.As<v8::Object>()
                    ->Get(ctx, ToV8Str(iso, "close"))
                    .ToLocal(&sink_fn) &&
                sink_fn->IsFunction())
            {
              v8::TryCatch try_catch(iso);
              (void)sink_fn.As<v8::Function>()
                  ->Call(ctx, underlying, 0, nullptr)
                  .FromMaybe(v8::Local<v8::Value>());
              try_catch.Reset();
            }
            (void)resolver->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
            args.GetReturnValue().Set(resolver->GetPromise());
          },
          underlying_value);
      close_tpl->SetClassName(ToV8Str(isolate, "close"));
      v8::Local<v8::FunctionTemplate> abort_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(
                    args.GetIsolate()->GetCurrentContext())
                    .ToLocalChecked();
            (void)resolver
                ->Resolve(args.GetIsolate()->GetCurrentContext(),
                          v8::Undefined(args.GetIsolate()))
                .FromMaybe(false);
            args.GetReturnValue().Set(resolver->GetPromise());
          });
      abort_tpl->SetClassName(ToV8Str(isolate, "abort"));
      v8::Local<v8::FunctionTemplate> release_tpl = v8::FunctionTemplate::New(
          isolate, [](const v8::FunctionCallbackInfo<v8::Value>&) {});
      release_tpl->SetClassName(ToV8Str(isolate, "releaseLock"));
      (void)writer->Set(context, ToV8Str(isolate, "write"),
                        write_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)writer->Set(context, ToV8Str(isolate, "close"),
                        close_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)writer->Set(context, ToV8Str(isolate, "abort"),
                        abort_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)writer->Set(context, ToV8Str(isolate, "releaseLock"),
                        release_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      info.GetReturnValue().Set(writer);
    }

    void TransformStreamConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->transform_tpl.IsEmpty() ||
          state->readable_tpl.IsEmpty() ||
          state->writable_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: TransformStream constructor requires new"));
        return;
      }
      v8::Local<v8::Object> transformer =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0].As<v8::Object>()
              : v8::Object::New(isolate);
      v8::Local<v8::Object> inst = state->transform_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      // readable (its controller is captured below for enqueueing)
      v8::Local<v8::Object> readable = state->readable_tpl.Get(isolate)
                                           ->InstanceTemplate()
                                           ->NewInstance(context)
                                           .ToLocalChecked();
      (void)readable->SetPrivate(context, RsChunksKey(isolate),
                                 v8::Array::New(isolate))
          .FromMaybe(false);
      RsSetClosed(isolate, context, readable, false);
      (void)readable->SetPrivate(context, RsErroredKey(isolate),
                                 v8::Null(isolate))
          .FromMaybe(false);
      (void)readable->SetPrivate(context, RsHasErroredKey(isolate),
                                 v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      v8::Local<v8::Object> controller =
          state->readablectl_tpl.Get(isolate)
              ->InstanceTemplate()
              ->NewInstance(context)
              .ToLocalChecked();
      (void)controller->SetPrivate(context, RscStreamKey(isolate), readable)
          .FromMaybe(false);
      (void)readable->SetPrivate(context, RsControllerKey(isolate), controller)
          .FromMaybe(false);
      // writable whose write() runs the transformer and enqueues into the
      // readable controller
      v8::Local<v8::Object> writable = state->writable_tpl.Get(isolate)
                                           ->InstanceTemplate()
                                           ->NewInstance(context)
                                           .ToLocalChecked();
      v8::Local<v8::Object> underlying = v8::Object::New(isolate);
      v8::Local<v8::Array> transform_data = v8::Array::New(isolate, 2);
      (void)transform_data->Set(context, 0, transformer).FromMaybe(false);
      (void)transform_data->Set(context, 1, controller).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> write_fn = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> data = args.Data().As<v8::Array>();
            v8::Local<v8::Value> transformer, controller;
            if (!data->Get(ctx, 0).ToLocal(&transformer) ||
                !transformer->IsObject() ||
                !data->Get(ctx, 1).ToLocal(&controller) ||
                !controller->IsObject())
              return;
            v8::Local<v8::Value> chunk =
                args.Length() >= 1
                    ? args[0]
                    : v8::Local<v8::Value>(v8::Undefined(iso));
            v8::Local<v8::Value> transform_fn;
            if (transformer.As<v8::Object>()
                    ->Get(ctx, ToV8Str(iso, "transform"))
                    .ToLocal(&transform_fn) &&
                transform_fn->IsFunction())
            {
              v8::Local<v8::Array> outgoing = v8::Array::New(iso);
              v8::Local<v8::Object> downstream = v8::Object::New(iso);
              v8::Local<v8::FunctionTemplate> enqueue_fn =
                  v8::FunctionTemplate::New(
                      iso,
                      [](const v8::FunctionCallbackInfo<v8::Value>& eargs) {
                        v8::Isolate* eiso = eargs.GetIsolate();
                        v8::Local<v8::Context> ectx =
                            eiso->GetCurrentContext();
                        v8::Local<v8::Array> out =
                            eargs.Data().As<v8::Array>();
                        v8::Local<v8::Value> value =
                            eargs.Length() >= 1
                                ? eargs[0]
                                : v8::Local<v8::Value>(v8::Undefined(eiso));
                        (void)out->Set(ectx, out->Length(), value)
                            .FromMaybe(false);
                      },
                      outgoing);
              enqueue_fn->SetClassName(ToV8Str(iso, "enqueue"));
              (void)downstream
                  ->Set(ctx, ToV8Str(iso, "enqueue"),
                        enqueue_fn->GetFunction(ctx).ToLocalChecked())
                  .FromMaybe(false);
              v8::Local<v8::FunctionTemplate> close_fn =
                  v8::FunctionTemplate::New(
                      iso, [](const v8::FunctionCallbackInfo<v8::Value>&) {});
              close_fn->SetClassName(ToV8Str(iso, "close"));
              (void)downstream
                  ->Set(ctx, ToV8Str(iso, "close"),
                        close_fn->GetFunction(ctx).ToLocalChecked())
                  .FromMaybe(false);
              v8::Local<v8::FunctionTemplate> error_fn =
                  v8::FunctionTemplate::New(
                      iso, [](const v8::FunctionCallbackInfo<v8::Value>&) {});
              error_fn->SetClassName(ToV8Str(iso, "error"));
              (void)downstream
                  ->Set(ctx, ToV8Str(iso, "error"),
                        error_fn->GetFunction(ctx).ToLocalChecked())
                  .FromMaybe(false);
              v8::Local<v8::Value> argv[] = {chunk, downstream};
              v8::TryCatch try_catch(iso);
              (void)transform_fn.As<v8::Function>()
                  ->Call(ctx, transformer, 2, argv)
                  .FromMaybe(v8::Local<v8::Value>());
              try_catch.Reset();
              for (uint32_t i = 0; i < outgoing->Length(); i++)
              {
                v8::Local<v8::Value> value;
                if (outgoing->Get(ctx, i).ToLocal(&value))
                {
                  v8::Local<v8::Value> enqueue;
                  if (controller.As<v8::Object>()
                          ->Get(ctx, ToV8Str(iso, "enqueue"))
                          .ToLocal(&enqueue) &&
                      enqueue->IsFunction())
                  {
                    v8::Local<v8::Value> eargv[] = {value};
                    v8::TryCatch try_catch2(iso);
                    (void)enqueue.As<v8::Function>()
                        ->Call(ctx, controller, 1, eargv)
                        .FromMaybe(v8::Local<v8::Value>());
                    try_catch2.Reset();
                  }
                }
              }
            }
          },
          transform_data);
      write_fn->SetClassName(ToV8Str(isolate, "write"));
      (void)underlying
          ->Set(context, ToV8Str(isolate, "write"),
                write_fn->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)writable->SetPrivate(context, WsUnderlyingKey(isolate),
                                 underlying)
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "readable"), readable)
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "writable"), writable)
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void InstallStreams(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        ContextState* state)
    {
      v8::Local<v8::Object> global = context->Global();
      auto register_global = [&](const char* name,
                                 v8::Local<v8::FunctionTemplate> ctor) {
        (void)global
            ->DefineOwnProperty(context, ToV8Str(isolate, name),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      };
      auto method = [&](v8::Local<v8::FunctionTemplate> target,
                        const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        target->PrototypeTemplate()->Set(key, fn);
      };
      auto getter = [&](v8::Local<v8::FunctionTemplate> target,
                        const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        target->PrototypeTemplate()->SetAccessorProperty(
            ToV8Str(isolate, name), fn, v8::Local<v8::FunctionTemplate>());
      };

      if (state->readablectl_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, [](const v8::FunctionCallbackInfo<v8::Value>&) {});
        ctor->SetClassName(ToV8Str(isolate, "ReadableStreamDefaultController"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "ReadableStreamDefaultController"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        method(ctor, "enqueue", RsControllerEnqueue);
        method(ctor, "close", RsControllerClose);
        method(ctor, "error", RsControllerError);
        state->readablectl_tpl.Reset(isolate, ctor);
        // the reference does not export the controller as a global
      }

      if (state->readable_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, ReadableStreamConstructor);
        ctor->SetClassName(ToV8Str(isolate, "ReadableStream"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "ReadableStream"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        getter(ctor, "locked", ReadableStreamLocked);
        method(ctor, "getReader", ReadableStreamGetReader);
        method(ctor, "cancel", ReadableStreamCancel);
        state->readable_tpl.Reset(isolate, ctor);
        register_global("ReadableStream", ctor);
      }

      if (state->writable_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              ContextState* st = GetState(ctx);
              if (!info.IsConstructCall() || st == nullptr ||
                  st->writable_tpl.IsEmpty())
              {
                iso->ThrowException(ToV8Str(
                    iso,
                    "TypeError: WritableStream constructor requires new"));
                return;
              }
              v8::Local<v8::Object> inst = st->writable_tpl.Get(iso)
                                               ->InstanceTemplate()
                                               ->NewInstance(ctx)
                                               .ToLocalChecked();
              v8::Local<v8::Object> underlying =
                  info.Length() >= 1 && info[0]->IsObject()
                      ? info[0].As<v8::Object>()
                      : v8::Object::New(iso);
              (void)inst->SetPrivate(ctx, WsUnderlyingKey(iso), underlying)
                  .FromMaybe(false);
              info.GetReturnValue().Set(inst);
            });
        ctor->SetClassName(ToV8Str(isolate, "WritableStream"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "WritableStream"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        v8::Local<v8::FunctionTemplate> get_writer =
            v8::FunctionTemplate::New(isolate, WritableStreamGetWriter);
        get_writer->SetClassName(ToV8Str(isolate, "getWriter"));
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "getWriter"),
                                       get_writer);
        state->writable_tpl.Reset(isolate, ctor);
        register_global("WritableStream", ctor);
      }

      if (state->transform_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, TransformStreamConstructor);
        ctor->SetClassName(ToV8Str(isolate, "TransformStream"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "TransformStream"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        state->transform_tpl.Reset(isolate, ctor);
        register_global("TransformStream", ctor);
      }
    }

    // ---- G4 ledger chunk 8a: ImageBitmap (formerly polyfills4 JS class)

    void ImageBitmapConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->imagebitmap_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: ImageBitmap constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->imagebitmap_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      double width = info.Length() >= 1
          ? info[0]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      double height = info.Length() >= 2
          ? info[1]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      (void)inst->Set(context, ToV8Str(isolate, "width"),
                      v8::Number::New(isolate, width))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "height"),
                      v8::Number::New(isolate, height))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "_closed"),
                      v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void ImageBitmapClose(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      (void)info.This()->Set(context, ToV8Str(isolate, "_closed"),
                             v8::Boolean::New(isolate, true))
          .FromMaybe(false);
    }

    void InstallImageBitmap(v8::Isolate* isolate,
                            v8::Local<v8::Context> context,
                            ContextState* state)
    {
      if (!state->imagebitmap_tpl.IsEmpty()) return;
      v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
          isolate, ImageBitmapConstructor);
      ctor->SetClassName(ToV8Str(isolate, "ImageBitmap"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "ImageBitmap"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      v8::Local<v8::FunctionTemplate> close_fn =
          v8::FunctionTemplate::New(isolate, ImageBitmapClose);
      close_fn->SetClassName(ToV8Str(isolate, "close"));
      ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "close"), close_fn);
      state->imagebitmap_tpl.Reset(isolate, ctor);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "ImageBitmap"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // ---- G4 ledger chunk 8b: Request (formerly polyfills4 JS class) ----

    void RequestMarkUsed(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      (void)info.This()->Set(context, ToV8Str(isolate, "bodyUsed"),
                             v8::Boolean::New(isolate, true))
          .FromMaybe(false);
    }

    void RequestConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->request_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: Request constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->request_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      v8::Local<v8::Object> init =
          info.Length() >= 2 && info[1]->IsObject()
              ? info[1].As<v8::Object>()
              : v8::Object::New(isolate);
      bool original = info.Length() >= 1 && info[0]->IsObject() &&
                      state->request_tpl.Get(isolate)->HasInstance(
                          info[0].As<v8::Object>());
      v8::Local<v8::Object> original_obj =
          original ? info[0].As<v8::Object>() : v8::Local<v8::Object>();
      // url
      std::string url;
      if (original)
      {
        v8::Local<v8::Value> value;
        if (original_obj->Get(context, ToV8Str(isolate, "url")).ToLocal(&value))
          url = ToUtf8(isolate, value);
      }
      else
      {
        std::string input = info.Length() >= 1 && !info[0]->IsNullOrUndefined()
            ? ToUtf8(isolate, info[0])
            : "";
        std::string base;
        v8::Local<v8::Value> location;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "location"))
                .ToLocal(&location) &&
            location->IsObject())
        {
          v8::Local<v8::Value> href;
          if (location.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "href"))
                  .ToLocal(&href) &&
              href->IsString())
            base = ToUtf8(isolate, href);
        }
        url = ResolveUrlPoly(input, base);
      }
      (void)inst->Set(context, ToV8Str(isolate, "url"), ToV8Str(isolate, url))
          .FromMaybe(false);
      // method
      std::string method = "GET";
      v8::Local<v8::Value> init_method;
      if (init->Get(context, ToV8Str(isolate, "method")).ToLocal(&init_method) &&
          !init_method->IsUndefined())
      {
        method = ToUtf8(isolate, init_method);
      }
      else if (original)
      {
        v8::Local<v8::Value> value;
        if (original_obj->Get(context, ToV8Str(isolate, "method"))
                .ToLocal(&value))
          method = ToUtf8(isolate, value);
      }
      for (auto& c : method)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      (void)inst->Set(context, ToV8Str(isolate, "method"),
                      ToV8Str(isolate, method))
          .FromMaybe(false);
      // headers
      {
        v8::Local<v8::Value> init_headers;
        bool have = init->Get(context, ToV8Str(isolate, "headers"))
                        .ToLocal(&init_headers);
        if ((!have || init_headers->IsUndefined()) && original)
          init_headers = original_obj->Get(context, ToV8Str(isolate, "headers"))
                               .FromMaybe(v8::Local<v8::Value>());
        v8::Local<v8::Value> headers_ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "Headers"))
                .ToLocal(&headers_ctor) &&
            headers_ctor->IsFunction())
        {
          v8::Local<v8::Value> hargv[1];
          uint32_t hargc = 0;
          if (!init_headers.IsEmpty() && !init_headers->IsUndefined())
            hargv[hargc++] = init_headers;
          v8::Local<v8::Value> built;
          if (headers_ctor.As<v8::Function>()
                  ->NewInstance(context, hargc, hargv)
                  .ToLocal(&built) &&
              built->IsObject())
            (void)inst->Set(context, ToV8Str(isolate, "headers"), built)
                .FromMaybe(false);
        }
      }
      // string fields
      auto str_field = [&](const char* key, const std::string& fallback) {
        v8::Local<v8::Value> raw;
        std::string value = fallback;
        if (init->Get(context, ToV8Str(isolate, key)).ToLocal(&raw) &&
            !raw->IsUndefined())
          value = ToUtf8(isolate, raw);
        (void)inst->Set(context, ToV8Str(isolate, key), ToV8Str(isolate, value))
            .FromMaybe(false);
      };
      str_field("cache", "default");
      str_field("credentials", "same-origin");
      (void)inst->Set(context, ToV8Str(isolate, "destination"),
                      ToV8Str(isolate, ""))
          .FromMaybe(false);
      str_field("integrity", "");
      {
        v8::Local<v8::Value> raw;
        bool keepalive = false;
        if (init->Get(context, ToV8Str(isolate, "keepalive")).ToLocal(&raw))
          keepalive = raw->BooleanValue(isolate);
        (void)inst->Set(context, ToV8Str(isolate, "keepalive"),
                        v8::Boolean::New(isolate, keepalive))
            .FromMaybe(false);
      }
      (void)inst->Set(context, ToV8Str(isolate, "bodyUsed"),
                      v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      // _body
      v8::Local<v8::Value> init_body;
      bool has_body =
          init->Get(context, ToV8Str(isolate, "body")).ToLocal(&init_body) &&
          !init_body->IsUndefined();
      v8::Local<v8::Value> body_value;
      if (has_body)
        body_value = init_body;
      else if (original)
        body_value = original_obj->Get(context, ToV8Str(isolate, "_body"))
                               .FromMaybe(v8::Local<v8::Value>());
      else
        body_value = v8::Null(isolate);
      (void)inst->Set(context, ToV8Str(isolate, "_body"), body_value)
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void RequestClone(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> request_ctor;
      if (!context->Global()
                ->Get(context, ToV8Str(isolate, "Request"))
                .ToLocal(&request_ctor) ||
          !request_ctor->IsFunction())
        return;
      v8::Local<v8::Value> url, method, headers, body, cache, credentials,
          integrity, keepalive;
      url = info.This()->Get(context, ToV8Str(isolate, "url"))
                .FromMaybe(v8::Local<v8::Value>());
      method = info.This()->Get(context, ToV8Str(isolate, "method"))
                   .FromMaybe(v8::Local<v8::Value>());
      headers = info.This()->Get(context, ToV8Str(isolate, "headers"))
                    .FromMaybe(v8::Local<v8::Value>());
      body = info.This()->Get(context, ToV8Str(isolate, "_body"))
                 .FromMaybe(v8::Local<v8::Value>());
      cache = info.This()->Get(context, ToV8Str(isolate, "cache"))
                  .FromMaybe(v8::Local<v8::Value>());
      credentials = info.This()
                        ->Get(context, ToV8Str(isolate, "credentials"))
                        .FromMaybe(v8::Local<v8::Value>());
      integrity = info.This()->Get(context, ToV8Str(isolate, "integrity"))
                      .FromMaybe(v8::Local<v8::Value>());
      keepalive = info.This()->Get(context, ToV8Str(isolate, "keepalive"))
                      .FromMaybe(v8::Local<v8::Value>());
      v8::Local<v8::Object> init = v8::Object::New(isolate);
      (void)init->Set(context, ToV8Str(isolate, "method"), method)
          .FromMaybe(false);
      (void)init->Set(context, ToV8Str(isolate, "headers"), headers)
          .FromMaybe(false);
      (void)init->Set(context, ToV8Str(isolate, "body"), body)
          .FromMaybe(false);
      (void)init->Set(context, ToV8Str(isolate, "cache"), cache)
          .FromMaybe(false);
      (void)init->Set(context, ToV8Str(isolate, "credentials"), credentials)
          .FromMaybe(false);
      (void)init->Set(context, ToV8Str(isolate, "integrity"), integrity)
          .FromMaybe(false);
      (void)init->Set(context, ToV8Str(isolate, "keepalive"), keepalive)
          .FromMaybe(false);
      v8::Local<v8::Value> argv[] = {url, init};
      v8::Local<v8::Value> copy;
      if (request_ctor.As<v8::Function>()
              ->NewInstance(context, 2, argv)
              .ToLocal(&copy) &&
          !copy.IsEmpty())
        info.GetReturnValue().Set(copy);
    }

    void RequestArrayBuffer(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      RequestMarkUsed(info);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Value> body =
          info.This()->Get(context, ToV8Str(isolate, "_body"))
              .FromMaybe(v8::Local<v8::Value>());
      if (!body.IsEmpty() && body->IsArrayBuffer())
      {
        (void)resolver->Resolve(context, body).FromMaybe(false);
        info.GetReturnValue().Set(resolver->GetPromise());
        return;
      }
      if (!body.IsEmpty() && body->IsArrayBufferView())
      {
        v8::Local<v8::ArrayBufferView> view = body.As<v8::ArrayBufferView>();
        v8::Local<v8::ArrayBuffer> buffer = view->Buffer();
        size_t total = 0;
        const uint8_t* raw = ArrayBufferBytes(buffer, &total);
        size_t offset = static_cast<size_t>(view->ByteOffset());
        size_t length = static_cast<size_t>(view->ByteLength());
        v8::Local<v8::ArrayBuffer> copy =
            v8::ArrayBuffer::New(isolate, length);
        if (raw != nullptr && offset + length <= total && length > 0)
          std::memcpy(copy->GetBackingStore()->Data(), raw + offset, length);
        (void)resolver->Resolve(context, copy).FromMaybe(false);
        info.GetReturnValue().Set(resolver->GetPromise());
        return;
      }
      std::string text = body.IsEmpty() || body->IsNullOrUndefined()
          ? ""
          : ToUtf8(isolate, body);
      v8::Local<v8::ArrayBuffer> buffer =
          v8::ArrayBuffer::New(isolate, text.size());
      if (!text.empty())
        std::memcpy(buffer->GetBackingStore()->Data(), text.data(),
                    text.size());
      (void)resolver->Resolve(context, buffer).FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void RequestText(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      RequestMarkUsed(info);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Value> body =
          info.This()->Get(context, ToV8Str(isolate, "_body"))
              .FromMaybe(v8::Local<v8::Value>());
      std::string text = body.IsEmpty() || body->IsNullOrUndefined()
          ? ""
          : ToUtf8(isolate, body);
      (void)resolver->Resolve(context, ToV8Str(isolate, text))
          .FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void RequestJson(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      RequestMarkUsed(info);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Value> body =
          info.This()->Get(context, ToV8Str(isolate, "_body"))
              .FromMaybe(v8::Local<v8::Value>());
      std::string text = body.IsEmpty() || body->IsNullOrUndefined()
          ? ""
          : ToUtf8(isolate, body);
      v8::Local<v8::Value> parsed;
      if (v8::JSON::Parse(context, ToV8Str(isolate, text)).ToLocal(&parsed))
        (void)resolver->Resolve(context, parsed).FromMaybe(false);
      else
        (void)resolver->Reject(context, v8::Undefined(isolate))
            .FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    // decodeURIComponent with "+" folded to space (urlencoded form parity)
    std::string FormDecode(v8::Isolate* isolate,
                           v8::Local<v8::Context> context,
                           const std::string& raw)
    {
      std::string spaced;
      spaced.reserve(raw.size());
      for (char c : raw) spaced += c == '+' ? ' ' : c;
      v8::Local<v8::Value> dec_fn;
      if (!context->Global()
                   ->Get(context, ToV8Str(isolate, "decodeURIComponent"))
                   .ToLocal(&dec_fn) ||
          !dec_fn->IsFunction())
        return spaced;
      v8::Local<v8::Value> argv[1] = {ToV8Str(isolate, spaced)};
      v8::Local<v8::Value> out;
      if (dec_fn.As<v8::Function>()
              ->Call(context, context->Global(), 1, argv)
              .ToLocal(&out) &&
          out->IsString())
        return ToUtf8(isolate, out);
      return spaced;
    }

    // formData(): parse the urlencoded body into a FormData instance
    void RequestFormData(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      RequestMarkUsed(info);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Value> body =
          info.This()->Get(context, ToV8Str(isolate, "_body"))
              .FromMaybe(v8::Local<v8::Value>());
      // JS parity: this._body === null ? "" : String(this._body)
      std::string text = !body.IsEmpty() && body->IsNull()
          ? ""
          : body.IsEmpty() || body->IsUndefined()
              ? "undefined"
              : ToUtf8(isolate, body);
      v8::Local<v8::Value> fd_ctor;
      if (!context->Global()
                   ->Get(context, ToV8Str(isolate, "FormData"))
                   .ToLocal(&fd_ctor) ||
          !fd_ctor->IsFunction())
      {
        (void)resolver->Resolve(context, v8::Object::New(isolate))
            .FromMaybe(false);
        info.GetReturnValue().Set(resolver->GetPromise());
        return;
      }
      v8::Local<v8::Value> fd;
      if (!fd_ctor.As<v8::Function>()
               ->NewInstance(context, 0, nullptr)
               .ToLocal(&fd))
        return;
      v8::Local<v8::Value> append_fn;
      if (fd->IsObject() &&
          fd.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "append"))
              .ToLocal(&append_fn) &&
          append_fn->IsFunction())
      {
        size_t pos = 0;
        while (pos <= text.size())
        {
          size_t amp = text.find('&', pos);
          size_t end = amp == std::string::npos ? text.size() : amp;
          if (end > pos)
          {
            std::string chunk = text.substr(pos, end - pos);
            size_t eq = chunk.find('=');
            std::string name =
                eq == std::string::npos ? chunk : chunk.substr(0, eq);
            std::string value =
                eq == std::string::npos ? "" : chunk.substr(eq + 1);
            v8::Local<v8::Value> argv[2] = {
                ToV8Str(isolate, FormDecode(isolate, context, name)),
                ToV8Str(isolate, FormDecode(isolate, context, value))};
            v8::TryCatch try_catch(isolate);
            (void)append_fn.As<v8::Function>()
                ->Call(context, fd, 2, argv)
                .FromMaybe(v8::Local<v8::Value>());
            try_catch.Reset();
          }
          if (amp == std::string::npos) break;
          pos = amp + 1;
        }
      }
      (void)resolver->Resolve(context, fd).FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void InstallRequest(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        ContextState* state)
    {
      if (!state->request_tpl.IsEmpty()) return;
      v8::Local<v8::FunctionTemplate> ctor =
          v8::FunctionTemplate::New(isolate, RequestConstructor);
      ctor->SetClassName(ToV8Str(isolate, "Request"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Request"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      method("clone", RequestClone);
      method("arrayBuffer", RequestArrayBuffer);
      method("text", RequestText);
      method("json", RequestJson);
      method("formData", RequestFormData);
      // brand the prototype itself: the oracle's Request.prototype stringifies
      // as "[object Request]" and the WAF SDK classifies interfaces through
      // exactly this String() of the prototype during its init walk.
      {
        v8::Local<v8::Function> ctor_fn =
            ctor->GetFunction(context).ToLocalChecked();
        v8::Local<v8::Value> proto;
        if (ctor_fn->Get(context, ToV8Str(isolate, "prototype")).ToLocal(&proto) &&
            proto->IsObject())
          (void)proto.As<v8::Object>()
              ->Set(context, v8::Symbol::GetToStringTag(isolate),
                    ToV8Str(isolate, "Request"))
              .FromMaybe(false);
      }
      state->request_tpl.Reset(isolate, ctor);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "Request"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

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
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
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
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
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

    // ---- G4.17: performance mark/measure + PerformanceObserver ---------
    v8::Local<v8::Object> PerfEntryObj(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const ContextState::PerfEntry& entry)
    {
      v8::Local<v8::Object> obj = v8::Object::New(isolate);
      (void)obj->Set(context, ToV8Str(isolate, "name"),
                     ToV8Str(isolate, entry.name))
          .FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "entryType"),
                     ToV8Str(isolate, entry.type))
          .FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "startTime"),
                     v8::Number::New(isolate, entry.start))
          .FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "duration"),
                     v8::Number::New(isolate, entry.duration))
          .FromMaybe(false);
      return obj;
    }

    // queue a microtask delivering [entry] to one registered observer
    void PerfNotifyEntry(v8::Isolate* isolate,
                         v8::Local<v8::Context> context,
                         const ContextState::PerfObserver& observer,
                         v8::Local<v8::Object> entry)
    {
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 3);
      (void)payload->Set(context, 0, observer.callback.Get(isolate))
          .FromMaybe(false);
      (void)payload->Set(context, 1, observer.instance.Get(isolate))
          .FromMaybe(false);
      (void)payload->Set(context, 2, entry).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> data = info.Data().As<v8::Array>();
            v8::Local<v8::Value> cb, recv, entry_v;
            if (!data->Get(ctx, 0).ToLocal(&cb) ||
                !data->Get(ctx, 1).ToLocal(&recv) ||
                !data->Get(ctx, 2).ToLocal(&entry_v) ||
                !cb->IsFunction())
              return;
            v8::Local<v8::FunctionTemplate> list_fn = v8::FunctionTemplate::New(
                iso,
                [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                  v8::Isolate* i2 = args.GetIsolate();
                  v8::Local<v8::Context> c2 = i2->GetCurrentContext();
                  v8::Local<v8::Array> out = v8::Array::New(i2, 1);
                  (void)out->Set(c2, 0, args.Data()).FromMaybe(false);
                  args.GetReturnValue().Set(out);
                },
                entry_v);
            list_fn->SetClassName(ToV8Str(iso, "getEntries"));
            v8::Local<v8::Object> list = v8::Object::New(iso);
            (void)list
                ->Set(ctx, ToV8Str(iso, "getEntries"),
                      list_fn->GetFunction(ctx).ToLocalChecked())
                .FromMaybe(false);
            v8::Local<v8::Value> argv[2] = {list, recv};
            v8::TryCatch try_catch(iso);
            (void)cb.As<v8::Function>()
                ->Call(ctx, recv, 2, argv)
                .FromMaybe(v8::Local<v8::Value>());
            try_catch.Reset();
          },
          payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Promise::Resolver> sink =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      MicroResolve(isolate, context, sink, runner);
    }

    void PerfMark(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr) return;
      ContextState::PerfEntry entry;
      entry.name = info.Length() >= 1
          ? ToUtf8(isolate, info[0])
          : std::string();
      entry.type = "mark";
      entry.start = LogicalClock(isolate, context);
      entry.duration = 0.0;
      v8::Local<v8::Object> entry_obj =
          PerfEntryObj(isolate, context, entry);
      state->perf_entries.push_back(entry);
      for (const auto& observer : state->perf_observers)
        PerfNotifyEntry(isolate, context, observer, entry_obj);
      info.GetReturnValue().Set(entry_obj);
    }

    void PerfMeasure(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr) return;
      std::string name = info.Length() >= 1
          ? ToUtf8(isolate, info[0])
          : std::string();
      std::string start_mark = info.Length() >= 2 && !info[1]->IsUndefined()
          ? ToUtf8(isolate, info[1])
          : std::string();
      std::string end_mark = info.Length() >= 3 && !info[2]->IsUndefined()
          ? ToUtf8(isolate, info[2])
          : std::string();
      double start_time = 0.0;
      double end_time = LogicalClock(isolate, context);
      for (auto it = state->perf_entries.rbegin();
           it != state->perf_entries.rend(); ++it)
      {
        if (it->type == "mark" && it->name == start_mark)
        {
          start_time = it->start;
          break;
        }
      }
      if (!end_mark.empty())
      {
        for (auto it = state->perf_entries.rbegin();
             it != state->perf_entries.rend(); ++it)
        {
          if (it->type == "mark" && it->name == end_mark)
          {
            end_time = it->start;
            break;
          }
        }
      }
      ContextState::PerfEntry entry;
      entry.name = name;
      entry.type = "measure";
      entry.start = start_time;
      entry.duration = end_time - start_time;
      v8::Local<v8::Object> entry_obj =
          PerfEntryObj(isolate, context, entry);
      state->perf_entries.push_back(entry);
      for (const auto& observer : state->perf_observers)
        PerfNotifyEntry(isolate, context, observer, entry_obj);
      info.GetReturnValue().Set(entry_obj);
    }

    void PerfGetEntries(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr) return;
      v8::Local<v8::Array> out =
          v8::Array::New(isolate,
                         static_cast<int>(state->perf_entries.size()));
      for (size_t i = 0; i < state->perf_entries.size(); i++)
        (void)out->Set(context, static_cast<uint32_t>(i),
                       PerfEntryObj(isolate, context, state->perf_entries[i]))
            .FromMaybe(false);
      info.GetReturnValue().Set(out);
    }

    void PerfGetEntriesByName(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr) return;
      std::string name = info.Length() >= 1
          ? ToUtf8(isolate, info[0])
          : std::string();
      std::string type = info.Length() >= 2 && !info[1]->IsUndefined()
          ? ToUtf8(isolate, info[1])
          : std::string();
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      for (const auto& entry : state->perf_entries)
      {
        if (entry.name == name &&
            (type.empty() || entry.type == type))
          (void)out->Set(context, n++, PerfEntryObj(isolate, context, entry))
              .FromMaybe(false);
      }
      info.GetReturnValue().Set(out);
    }

    void PerfClear(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr) return;
      const std::string type = ToUtf8(isolate, info.Data());
      std::vector<ContextState::PerfEntry> kept;
      for (const auto& entry : state->perf_entries)
      {
        if (entry.type != type) kept.push_back(entry);
      }
      state->perf_entries.swap(kept);
    }

    void PerfObserverCtor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: PerformanceObserver constructor "
                     "requires new"));
        return;
      }
      if (info.Length() < 1)
      {
        // M12-argreq: reference webidl 0-arg message
        isolate->ThrowException(v8::Exception::TypeError(ToV8Str(
            isolate,
            "Failed to construct 'PerformanceObserver': 1 argument "
            "required, but only 0 present.")));
        return;
      }
      if (!info[0]->IsFunction())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: callback must be a function"));
        return;
      }
      (void)info.This()
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate, ToV8Str(isolate, "lv8.perf.callback")),
                       info[0])
          .FromMaybe(false);
      ContextState::PerfObserver observer;
      observer.instance.Reset(isolate, info.This());
      observer.callback.Reset(isolate, info[0].As<v8::Function>());
      state->perf_observers.push_back(std::move(observer));
    }

    void PerfObserverObserve(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr) return;
      // JS parity: the instance records its observe options
      v8::Local<v8::Value> options =
          info.Length() >= 1 && info[0]->IsObject()
              ? v8::Local<v8::Value>(info[0])
              : v8::Local<v8::Value>(v8::Object::New(isolate));
      (void)info.This()->Set(context, ToV8Str(isolate, "_options"), options)
          .FromMaybe(false);
      for (const auto& observer : state->perf_observers)
      {
        if (observer.instance.Get(isolate)->SameValue(info.This())) return;
      }
      ContextState::PerfObserver observer;
      observer.instance.Reset(isolate, info.This());
      v8::Local<v8::Value> cb;
      if (info.This()
              ->GetPrivate(context,
                           v8::Private::ForApi(
                               isolate, ToV8Str(isolate, "lv8.perf.callback")))
              .ToLocal(&cb) &&
          cb->IsFunction())
        observer.callback.Reset(isolate, cb.As<v8::Function>());
      state->perf_observers.push_back(std::move(observer));
    }

    void PerfObserverDisconnect(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr) return;
      for (size_t i = 0; i < state->perf_observers.size(); i++)
      {
        if (state->perf_observers[i].instance.Get(isolate)->SameValue(info.This()))
        {
          state->perf_observers.erase(state->perf_observers.begin() + i);
          return;
        }
      }
    }

    void PerfObserverTakeRecords(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(
          v8::Array::New(info.GetIsolate(), 0));
    }

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
            (void)held->Set(ctx, key, lock).IsEmpty();
            v8::Local<v8::Value> result;
            bool called = false;
            {
              v8::Local<v8::Value> argv[] = {lock};
              v8::TryCatch try_catch(iso);
              called = handler.As<v8::Function>()->Call(ctx, self, 1, argv).ToLocal(&result);
              v8::Local<v8::Value> reason;
              if (!called && try_catch.HasCaught())
                reason = try_catch.Exception();
              try_catch.Reset();
              if (!called)
              {
                (void)held->Delete(ctx, key).FromMaybe(false);
                if (!reason.IsEmpty())
                  (void)resolver->Reject(ctx, reason).FromMaybe(false);
                else
                  (void)resolver->Reject(ctx, v8::Undefined(iso))
                      .FromMaybe(false);
                return;
              }
            }
            if (result->IsPromise())
            {
              v8::Local<v8::Array> then_data = v8::Array::New(iso, 3);
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
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "LockManager"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
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
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
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
      if (type == "output")
        (void)inst->Set(context, ToV8Str(isolate, "sent"),
                        v8::Array::New(isolate))
            .FromMaybe(false);
      else
        (void)inst->Set(context, ToV8Str(isolate, "onmidimessage"),
                        v8::Null(isolate))
            .FromMaybe(false);
      return inst;
    }

    // queued microtask: set port connection, then resolve with the port
    void MidiPortOpenCloseTask(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> resolver_value, self, connection;
      if (!data->Get(context, 0).ToLocal(&resolver_value) ||
          !data->Get(context, 1).ToLocal(&self) ||
          !data->Get(context, 2).ToLocal(&connection) ||
          !resolver_value->IsObject() || !self->IsObject())
        return;
      v8::Local<v8::Promise::Resolver> resolver =
          resolver_value.As<v8::Promise::Resolver>();
      (void)self.As<v8::Object>()
          ->Set(context, ToV8Str(isolate, "connection"), connection)
          .FromMaybe(false);
      (void)resolver->Resolve(context, self).FromMaybe(false);
    }

    void MidiPortToggle(const v8::FunctionCallbackInfo<v8::Value>& info,
                        const char* connection)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 3);
      (void)payload->Set(context, 0, resolver).FromMaybe(false);
      (void)payload->Set(context, 1, info.This()).FromMaybe(false);
      (void)payload->Set(context, 2, ToV8Str(isolate, connection))
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
          isolate, MidiPortOpenCloseTask, payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      MicroResolve(isolate, context, resolver, runner);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void MidiPortOpen(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MidiPortToggle(info, "open");
    }

    void MidiPortClose(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MidiPortToggle(info, "closed");
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
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MIDIPort"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
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
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      if (state->midiinput_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, [](const v8::FunctionCallbackInfo<v8::Value>& info) {});
        ctor->SetClassName(ToV8Str(isolate, "MIDIInput"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MIDIInput"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        ctor->Inherit(state->midiport_tpl.Get(isolate));
        // G4.380: the legacy `_receive` prototype member was an internal
        // MIDI-dispatch hook nothing ever calls - it leaked an extra own
        // key onto a public prototype the ledger's for-in face flags
        // (MIDIInput own reference=2 / lv8=3).  Removed; the symbol-keyed
        // re-home pattern (G4.364) is the replacement if a delivery path
        // ever needs one.
        state->midiinput_tpl.Reset(isolate, ctor);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "MIDIInput"),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      if (state->midioutput_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, [](const v8::FunctionCallbackInfo<v8::Value>& info) {});
        ctor->SetClassName(ToV8Str(isolate, "MIDIOutput"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MIDIOutput"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        ctor->Inherit(state->midiport_tpl.Get(isolate));
        v8::Local<v8::FunctionTemplate> send_fn =
            v8::FunctionTemplate::New(isolate, MidiOutputSend);
        send_fn->SetClassName(ToV8Str(isolate, "send"));
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "send"), send_fn);
        v8::Local<v8::FunctionTemplate> clear_fn =
            v8::FunctionTemplate::New(isolate, MidiOutputClear);
        clear_fn->SetClassName(ToV8Str(isolate, "clear"));
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "clear"), clear_fn);
        state->midioutput_tpl.Reset(isolate, ctor);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "MIDIOutput"),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      if (state->midiaccess_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, MidiAccessConstructor);
        ctor->SetClassName(ToV8Str(isolate, "MIDIAccess"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MIDIAccess"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
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
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      // navigator.requestMIDIAccess: profile-driven access singleton
      v8::Local<v8::Value> navigator;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "navigator"))
              .ToLocal(&navigator) &&
          navigator->IsObject())
      {
        v8::Local<v8::Value> access_ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "MIDIAccess"))
                .ToLocal(&access_ctor) &&
            access_ctor->IsFunction())
        {
          v8::Local<v8::Object> values = v8::Object::New(isolate);
          if (!state->profile_data.IsEmpty())
          {
            v8::Local<v8::Value> midi_section;
            if (state->profile_data.Get(isolate)
                    ->Get(context, ToV8Str(isolate, "midi"))
                    .ToLocal(&midi_section) &&
                midi_section->IsObject())
              values = midi_section.As<v8::Object>();
          }
          v8::Local<v8::Value> argv[] = {values};
          v8::Local<v8::Value> access;
          if (access_ctor.As<v8::Function>()
                  ->NewInstance(context, 1, argv)
                  .ToLocal(&access))
          {
            v8::Local<v8::FunctionTemplate> get_access =
                v8::FunctionTemplate::New(
                    isolate,
                    [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                      v8::Isolate* iso = info.GetIsolate();
                      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                      v8::Local<v8::Promise::Resolver> resolver =
                          v8::Promise::Resolver::New(ctx).ToLocalChecked();
                      (void)resolver->Resolve(ctx, info.Data())
                          .FromMaybe(false);
                      info.GetReturnValue().Set(resolver->GetPromise());
                    },
                    access);
            get_access->SetClassName(ToV8Str(isolate, "requestMIDIAccess"));
            (void)navigator.As<v8::Object>()
                ->DefineOwnProperty(
                    context, ToV8Str(isolate, "requestMIDIAccess"),
                    get_access->GetFunction(context).ToLocalChecked(),
                    static_cast<v8::PropertyAttribute>(
                        v8::PropertyAttribute::DontEnum))
                .FromMaybe(false);
          }
        }
      }
    }

    // ---- G4 ledger chunk 5: user-facing Headers ------------------------
    // Previously the JS Headers class (polyfills2). Multi-value set-cookie
    // stays split in a private array; every other name joins on ", ".
    // entries() merges and sorts by name (reference semantics).

    v8::Local<v8::Private> HdValuesKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.headers.values"));
    }
    v8::Local<v8::Private> HdCookiesKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.headers.setcookies"));
    }

    void ThrowTypeError(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        const std::string& message)
    {
      v8::Local<v8::Value> ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "TypeError"))
              .ToLocal(&ctor) &&
          ctor->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {ToV8Str(isolate, message)};
        v8::Local<v8::Value> error;
        if (ctor.As<v8::Function>()
                ->NewInstance(context, 1, argv)
                .ToLocal(&error) &&
            !error.IsEmpty())
        {
          isolate->ThrowException(error);
          return;
        }
      }
      isolate->ThrowException(ToV8Str(isolate, "TypeError: " + message));
    }

    bool ValidHeaderToken(const std::string& name)
    {
      if (name.empty()) return false;
      for (char c : name)
      {
        if (std::isalnum(static_cast<unsigned char>(c))) continue;
        if (std::string("!#$%&'*+.^_`|~-").find(c) == std::string::npos)
          return false;
      }
      return true;
    }

    // normalize + validate; false -> URIError-style TypeError already thrown
    bool HeaderNameOf(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Value> raw, std::string& out)
    {
      if (raw.IsEmpty() || !raw->IsString())
      {
        ThrowTypeError(isolate, context, "Invalid HTTP header name");
        return false;
      }
      std::string name = ToUtf8(isolate, raw);
      // trim ASCII whitespace
      auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\f' || c == '\v';
      };
      while (!name.empty() && is_ws(name.front())) name.erase(name.begin());
      while (!name.empty() && is_ws(name.back())) name.pop_back();
      for (char& c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (!ValidHeaderToken(name))
      {
        ThrowTypeError(isolate, context, "Invalid HTTP header name");
        return false;
      }
      out = name;
      return true;
    }

    bool HeaderValueOf(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Value> raw, std::string& out)
    {
      if (raw.IsEmpty()) raw = v8::Undefined(isolate);
      v8::Local<v8::String> text;
      if (!raw->ToString(context).ToLocal(&text))
        return false;
      std::string value = ToUtf8(isolate, text);
      auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\f' || c == '\v';
      };
      while (!value.empty() && is_ws(value.front())) value.erase(value.begin());
      while (!value.empty() && is_ws(value.back())) value.pop_back();
      if (value.find('\r') != std::string::npos ||
          value.find('\n') != std::string::npos)
      {
        ThrowTypeError(isolate, context, "Invalid HTTP header value");
        return false;
      }
      out = value;
      return true;
    }

    // merged + name-sorted [name, value] pairs (set-cookie joined)
    v8::Local<v8::Array> HeaderEntriesSorted(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Object> self)
    {
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      std::vector<std::pair<std::string, std::string>> rows;
      v8::Local<v8::Value> values;
      if (self->GetPrivate(context, HdValuesKey(isolate)).ToLocal(&values) &&
          values->IsMap())
      {
        v8::Local<v8::Array> flat = values.As<v8::Map>()->AsArray();
        for (uint32_t i = 0; i + 1 < flat->Length(); i += 2)
        {
          v8::Local<v8::Value> key, value;
          if (flat->Get(context, i).ToLocal(&key) &&
              flat->Get(context, i + 1).ToLocal(&value))
            rows.emplace_back(ToUtf8(isolate, key), ToUtf8(isolate, value));
        }
      }
      v8::Local<v8::Value> cookies;
      if (self->GetPrivate(context, HdCookiesKey(isolate)).ToLocal(&cookies) &&
          cookies->IsArray())
      {
        v8::Local<v8::Array> list = cookies.As<v8::Array>();
        std::string joined;
        for (uint32_t i = 0; i < list->Length(); i++)
        {
          v8::Local<v8::Value> value;
          if (!list->Get(context, i).ToLocal(&value)) continue;
          if (!joined.empty()) joined += ", ";
          joined += ToUtf8(isolate, value);
        }
        if (!joined.empty()) rows.emplace_back("set-cookie", joined);
      }
      std::stable_sort(rows.begin(), rows.end(),
                       [](const auto& left, const auto& right) {
                         return left.first < right.first;
                       });
      for (const auto& row : rows)
      {
        v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
        (void)pair->Set(context, 0, ToV8Str(isolate, row.first))
            .FromMaybe(false);
        (void)pair->Set(context, 1, ToV8Str(isolate, row.second))
            .FromMaybe(false);
        (void)out->Set(context, n++, pair).FromMaybe(false);
      }
      return out;
    }

    // shared append/set core; |set_mode| replaces instead of joining
    bool HeaderMutate(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> self,
                      const v8::FunctionCallbackInfo<v8::Value>& info,
                      bool set_mode)
    {
      if (info.Length() < 2) return false;
      std::string name;
      std::string value;
      if (!HeaderNameOf(isolate, context, info[0], name)) return false;
      if (!HeaderValueOf(isolate, context, info[1], value)) return false;
      v8::Local<v8::Value> cookies;
      if (!self->GetPrivate(context, HdCookiesKey(isolate)).ToLocal(&cookies) ||
          !cookies->IsArray())
        return false;
      if (name == "set-cookie")
      {
        v8::Local<v8::Array> list = cookies.As<v8::Array>();
        if (set_mode)
        {
          v8::Local<v8::Array> fresh = v8::Array::New(isolate);
          (void)fresh->Set(context, 0, ToV8Str(isolate, value))
              .FromMaybe(false);
          (void)self->SetPrivate(context, HdCookiesKey(isolate), fresh)
              .FromMaybe(false);
        }
        else
        {
          (void)list->Set(context, list->Length(), ToV8Str(isolate, value))
              .FromMaybe(false);
        }
        return true;
      }
      v8::Local<v8::Value> values;
      if (!self->GetPrivate(context, HdValuesKey(isolate)).ToLocal(&values) ||
          !values->IsMap())
        return false;
      v8::Local<v8::Map> map = values.As<v8::Map>();
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      if (!set_mode)
      {
        v8::Local<v8::Value> existing;
        if (map->Get(context, key).ToLocal(&existing) &&
            !existing->IsUndefined())
          value = ToUtf8(isolate, existing) + ", " + value;
      }
      (void)map->Set(context, key, ToV8Str(isolate, value)).IsEmpty();
      return true;
    }

    void HeadersAppend(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      HeaderMutate(isolate, context, info.This(), info, false);
    }

    void HeadersSet(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      HeaderMutate(isolate, context, info.This(), info, true);
    }

    bool HeadersGetCore(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> self,
                        const v8::FunctionCallbackInfo<v8::Value>& info,
                        bool* found)
    {
      *found = false;
      std::string name;
      if (info.Length() < 1) return false;
      if (!HeaderNameOf(isolate, context, info[0], name)) return false;
      if (name == "set-cookie")
      {
        v8::Local<v8::Value> cookies;
        if (!self->GetPrivate(context, HdCookiesKey(isolate)).ToLocal(&cookies) ||
            !cookies->IsArray())
          return false;
        std::string joined;
        v8::Local<v8::Array> list = cookies.As<v8::Array>();
        for (uint32_t i = 0; i < list->Length(); i++)
        {
          v8::Local<v8::Value> value;
          if (!list->Get(context, i).ToLocal(&value)) continue;
          if (!joined.empty()) joined += ", ";
          joined += ToUtf8(isolate, value);
        }
        if (joined.empty())
        {
          info.GetReturnValue().SetNull();
          *found = true;
          return true;
        }
        info.GetReturnValue().Set(ToV8Str(isolate, joined));
        *found = true;
        return true;
      }
      v8::Local<v8::Value> values;
      if (!self->GetPrivate(context, HdValuesKey(isolate)).ToLocal(&values) ||
          !values->IsMap())
        return false;
      v8::Local<v8::Value> existing;
      if (values.As<v8::Map>()
              ->Get(context, ToV8Str(isolate, name))
              .ToLocal(&existing) &&
          !existing->IsUndefined())
      {
        info.GetReturnValue().Set(existing);
        *found = true;
        return true;
      }
      info.GetReturnValue().SetNull();
      *found = true;
      return true;
    }

    void HeadersGet(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      bool found = false;
      if (!HeadersGetCore(isolate, context, info.This(), info, &found) ||
          !found)
        info.GetReturnValue().SetNull();
    }

    void HeadersHas(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string name;
      if (info.Length() < 1 ||
          !HeaderNameOf(isolate, context, info[0], name))
      {
        info.GetReturnValue().Set(v8::Boolean::New(isolate, false));
        return;
      }
      if (name == "set-cookie")
      {
        v8::Local<v8::Value> cookies;
        bool present = false;
        if (info.This()
                ->GetPrivate(context, HdCookiesKey(isolate))
                .ToLocal(&cookies) &&
            cookies->IsArray() &&
            cookies.As<v8::Array>()->Length() > 0)
          present = true;
        info.GetReturnValue().Set(v8::Boolean::New(isolate, present));
        return;
      }
      v8::Local<v8::Value> values;
      bool present = false;
      if (info.This()
              ->GetPrivate(context, HdValuesKey(isolate))
              .ToLocal(&values) &&
          values->IsMap())
      {
        v8::Local<v8::Value> existing;
        present = values.As<v8::Map>()
                      ->Get(context, ToV8Str(isolate, name))
                      .ToLocal(&existing) &&
                  !existing->IsUndefined();
      }
      info.GetReturnValue().Set(v8::Boolean::New(isolate, present));
    }

    void HeadersDelete(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string name;
      if (info.Length() < 1 ||
          !HeaderNameOf(isolate, context, info[0], name))
        return;
      if (name == "set-cookie")
      {
        (void)info.This()
            ->SetPrivate(context, HdCookiesKey(isolate), v8::Array::New(isolate))
            .FromMaybe(false);
        return;
      }
      v8::Local<v8::Value> values;
      if (info.This()
              ->GetPrivate(context, HdValuesKey(isolate))
              .ToLocal(&values) &&
          values->IsMap())
        (void)values.As<v8::Map>()->Delete(context, ToV8Str(isolate, name))
            .FromMaybe(false);
    }

    void HeadersGetSetCookie(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> cookies;
      if (info.This()
              ->GetPrivate(context, HdCookiesKey(isolate))
              .ToLocal(&cookies) &&
          cookies->IsArray())
      {
        info.GetReturnValue().Set(cookies);
        return;
      }
      info.GetReturnValue().Set(v8::Array::New(isolate));
    }

    void HeadersEntries(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> rows =
          HeaderEntriesSorted(isolate, context, info.This());
      v8::Local<v8::Value> array_ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "Array"))
               .ToLocal(&array_ctor) ||
          !array_ctor->IsObject())
        return;
      v8::Local<v8::Value> array_proto;
      if (!array_ctor.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "prototype"))
               .ToLocal(&array_proto) ||
          !array_proto->IsObject())
        return;
      v8::Local<v8::Value> iterator_fn;
      if (!array_proto.As<v8::Object>()
               ->Get(context, v8::Symbol::GetIterator(isolate))
               .ToLocal(&iterator_fn) ||
          !iterator_fn->IsFunction())
        return;
      v8::Local<v8::Value> result =
          iterator_fn.As<v8::Function>()
              ->Call(context, rows, 0, nullptr)
              .FromMaybe(v8::Local<v8::Value>());
      if (!result.IsEmpty())
        info.GetReturnValue().Set(result);
    }

    void HeadersKeys(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> rows =
          HeaderEntriesSorted(isolate, context, info.This());
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      for (uint32_t i = 0; i < rows->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!rows->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> key;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&key))
          (void)out->Set(context, n++, key).FromMaybe(false);
      }
      v8::Local<v8::Value> array_ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "Array"))
               .ToLocal(&array_ctor) ||
          !array_ctor->IsObject())
        return;
      v8::Local<v8::Value> array_proto;
      if (!array_ctor.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "prototype"))
               .ToLocal(&array_proto) ||
          !array_proto->IsObject())
        return;
      v8::Local<v8::Value> iterator_fn;
      if (!array_proto.As<v8::Object>()
               ->Get(context, v8::Symbol::GetIterator(isolate))
               .ToLocal(&iterator_fn) ||
          !iterator_fn->IsFunction())
        return;
      v8::Local<v8::Value> result =
          iterator_fn.As<v8::Function>()
              ->Call(context, out, 0, nullptr)
              .FromMaybe(v8::Local<v8::Value>());
      if (!result.IsEmpty())
        info.GetReturnValue().Set(result);
    }

    void HeadersValues(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> rows =
          HeaderEntriesSorted(isolate, context, info.This());
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      for (uint32_t i = 0; i < rows->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!rows->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> value;
        if (pair.As<v8::Array>()->Get(context, 1).ToLocal(&value))
          (void)out->Set(context, n++, value).FromMaybe(false);
      }
      v8::Local<v8::Value> array_ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "Array"))
               .ToLocal(&array_ctor) ||
          !array_ctor->IsObject())
        return;
      v8::Local<v8::Value> array_proto;
      if (!array_ctor.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "prototype"))
               .ToLocal(&array_proto) ||
          !array_proto->IsObject())
        return;
      v8::Local<v8::Value> iterator_fn;
      if (!array_proto.As<v8::Object>()
               ->Get(context, v8::Symbol::GetIterator(isolate))
               .ToLocal(&iterator_fn) ||
          !iterator_fn->IsFunction())
        return;
      v8::Local<v8::Value> result =
          iterator_fn.As<v8::Function>()
              ->Call(context, out, 0, nullptr)
              .FromMaybe(v8::Local<v8::Value>());
      if (!result.IsEmpty())
        info.GetReturnValue().Set(result);
    }

    void HeadersForEach(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsFunction()) return;
      v8::Local<v8::Function> fn = info[0].As<v8::Function>();
      v8::Local<v8::Value> this_arg =
          info.Length() >= 2
              ? info[1]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
      v8::Local<v8::Array> rows =
          HeaderEntriesSorted(isolate, context, info.This());
      for (uint32_t i = 0; i < rows->Length(); i++)
      {
        v8::Local<v8::Value> pair;
        if (!rows->Get(context, i).ToLocal(&pair) || !pair->IsArray())
          continue;
        v8::Local<v8::Value> key, value;
        if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&key) &&
            pair.As<v8::Array>()->Get(context, 1).ToLocal(&value))
        {
          v8::Local<v8::Value> argv[] = {value, key, info.This()};
          v8::TryCatch try_catch(isolate);
          (void)fn->Call(context, this_arg, 3, argv)
              .FromMaybe(v8::Local<v8::Value>());
          if (try_catch.HasCaught())
            CJavascriptException::ThrowIf(isolate, try_catch);
        }
      }
    }

    void HeadersConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->headers_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: Headers constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->headers_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, HdValuesKey(isolate),
                             v8::Map::New(isolate))
          .FromMaybe(false);
      (void)inst->SetPrivate(context, HdCookiesKey(isolate),
                             v8::Array::New(isolate))
          .FromMaybe(false);
      if (info.Length() >= 1 && !info[0]->IsNullOrUndefined())
      {
        v8::Local<v8::Value> init = info[0];
        v8::Local<v8::Value> append_fn;
        if (inst->Get(context, ToV8Str(isolate, "append")).ToLocal(&append_fn))
        {
          if (init->IsObject() && !init->IsArray() &&
              state->headers_tpl.Get(isolate)->HasInstance(init.As<v8::Object>()))
          {
            // Headers instance: iterate its merged sorted entries
            v8::Local<v8::Array> rows =
                HeaderEntriesSorted(isolate, context, init.As<v8::Object>());
            for (uint32_t i = 0; i < rows->Length(); i++)
            {
              v8::Local<v8::Value> pair;
              if (!rows->Get(context, i).ToLocal(&pair) || !pair->IsArray())
                continue;
              v8::Local<v8::Value> key, value;
              if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&key) &&
                  pair.As<v8::Array>()->Get(context, 1).ToLocal(&value))
              {
                v8::Local<v8::Value> argv[] = {key, value};
                v8::TryCatch try_catch(isolate);
                (void)append_fn.As<v8::Function>()
                    ->Call(context, inst, 2, argv)
                    .FromMaybe(v8::Local<v8::Value>());
                if (try_catch.HasCaught())
                  CJavascriptException::ThrowIf(isolate, try_catch);
              }
            }
          }
          else if (init->IsArray())
          {
            v8::Local<v8::Array> items = init.As<v8::Array>();
            for (uint32_t i = 0; i < items->Length(); i++)
            {
              v8::Local<v8::Value> pair;
              if (!items->Get(context, i).ToLocal(&pair) || !pair->IsArray())
                continue;
              v8::Local<v8::Value> key, value;
              if (pair.As<v8::Array>()->Get(context, 0).ToLocal(&key) &&
                  pair.As<v8::Array>()->Get(context, 1).ToLocal(&value))
              {
                v8::Local<v8::Value> argv[] = {key, value};
                v8::TryCatch try_catch(isolate);
                (void)append_fn.As<v8::Function>()
                    ->Call(context, inst, 2, argv)
                    .FromMaybe(v8::Local<v8::Value>());
                if (try_catch.HasCaught())
                  CJavascriptException::ThrowIf(isolate, try_catch);
              }
            }
          }
          else if (init->IsObject())
          {
            v8::Local<v8::Array> keys =
                init.As<v8::Object>()
                    ->GetOwnPropertyNames(context)
                    .ToLocalChecked();
            for (uint32_t i = 0; i < keys->Length(); i++)
            {
              v8::Local<v8::Value> key = keys->Get(context, i).ToLocalChecked();
              v8::Local<v8::Value> value;
              if (init.As<v8::Object>()->Get(context, key).ToLocal(&value))
              {
                v8::Local<v8::Value> argv[] = {key, value};
                v8::TryCatch try_catch(isolate);
                (void)append_fn.As<v8::Function>()
                    ->Call(context, inst, 2, argv)
                    .FromMaybe(v8::Local<v8::Value>());
                if (try_catch.HasCaught())
                  CJavascriptException::ThrowIf(isolate, try_catch);
              }
            }
          }
        }
      }
      info.GetReturnValue().Set(inst);
    }

    // ---- G4 ledger chunk 6: native FileReader --------------------------
    // Previously the JS FileReader class (polyfills.py). The event
    // pipeline (loadstart/progress/load/error/abort/loadend + on* handlers
    // + addEventListener) is native; reads go through __lv8_blob_bytes
    // semantics (private blob bytes, fresh copies) and schedule the actual
    // read through the global queueMicrotask (logical microtask ordering).

    v8::Local<v8::Private> FrListenersKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.frl.listeners"));
    }

    v8::Local<v8::Object> MakeSimpleEvent(v8::Isolate* isolate,
                                          v8::Local<v8::Context> context,
                                          const std::string& type)
    {
      v8::Local<v8::Object> ev = v8::Object::New(isolate);
      (void)ev->Set(context, ToV8Str(isolate, "type"),
                    ToV8Str(isolate, type))
          .FromMaybe(false);
      (void)ev->Set(context, ToV8Str(isolate, "target"), v8::Null(isolate))
          .FromMaybe(false);
      (void)ev->Set(context, ToV8Str(isolate, "currentTarget"),
                    v8::Null(isolate))
          .FromMaybe(false);
      (void)ev->Set(context, ToV8Str(isolate, "eventPhase"),
                    v8::Integer::New(isolate, 0))
          .FromMaybe(false);
      (void)ev->Set(context, ToV8Str(isolate, "bubbles"),
                    v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      (void)ev->Set(context, ToV8Str(isolate, "cancelBubble"),
                    v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      (void)ev->Set(context, ToV8Str(isolate, "cancelImmediate"),
                    v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      (void)ev->Set(context, ToV8Str(isolate, "defaultPrevented"),
                    v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      struct FlagMethod
      {
        const char* name;
        const char* flag;
        bool also;
      };
      const FlagMethod methods[] = {
          {"preventDefault", "defaultPrevented", false},
          {"stopPropagation", "cancelBubble", false},
          {"stopImmediatePropagation", "cancelImmediate", true},
      };
      for (const FlagMethod& m : methods)
      {
        v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& args) {
              v8::Isolate* iso = args.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              v8::Local<v8::String> flag = args.Data().As<v8::String>();
              v8::Local<v8::Value> current;
              if (args.This()->Get(ctx, flag).ToLocal(&current) &&
                  current->IsBoolean() && current->BooleanValue(iso))
                return;
              (void)args.This()
                  ->Set(ctx, flag, v8::Boolean::New(iso, true))
                  .FromMaybe(false);
            },
            ToV8Str(isolate, m.flag));
        fn->SetClassName(ToV8Str(isolate, m.name));
        (void)ev->Set(context, ToV8Str(isolate, m.name),
                      fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      }
      return ev;
    }

    void FrSwallowCall(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Function> fn,
                       v8::Local<v8::Object> receiver,
                       v8::Local<v8::Value> event)
    {
      v8::Local<v8::Value> argv[] = {event};
      v8::TryCatch try_catch(isolate);
      (void)fn->Call(context, receiver, 1, argv)
          .FromMaybe(v8::Local<v8::Value>());
      // reference swallows listener/handler exceptions (try {} catch (_))
      try_catch.Reset();
    }

    // reference installSimpleTarget.dispatchEvent semantics over the
    // private listener map, plus the on<type> handler
    void FrDispatchSimpleEvent(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Object> reader,
                               const std::string& type)
    {
      v8::Local<v8::Object> ev =
          MakeSimpleEvent(isolate, context, type);
      v8::Local<v8::Value> current_target;
      if (reader->Get(context, ToV8Str(isolate, "target")).ToLocal(&current_target) &&
          current_target->IsNull())
        (void)ev->Set(context, ToV8Str(isolate, "target"), reader)
            .FromMaybe(false);
      else
        (void)ev->Set(context, ToV8Str(isolate, "target"), reader)
            .FromMaybe(false);

      v8::Local<v8::Value> listeners;
      if (!reader
               ->GetPrivate(context, FrListenersKey(isolate))
               .ToLocal(&listeners) ||
          !listeners->IsObject())
        return;
      v8::Local<v8::Object> map = listeners.As<v8::Object>();
      v8::Local<v8::Value> bucket;
      if (!map->Get(context, ToV8Str(isolate, type)).ToLocal(&bucket) ||
          !bucket->IsArray())
        bucket = v8::Array::New(isolate);
      v8::Local<v8::Array> callbacks = bucket.As<v8::Array>();
      for (uint32_t i = 0; i < callbacks->Length(); i++)
      {
        v8::Local<v8::Value> entry;
        if (!callbacks->Get(context, i).ToLocal(&entry) || !entry->IsObject())
          continue;
        v8::Local<v8::Object> entry_obj = entry.As<v8::Object>();
        v8::Local<v8::Value> callback;
        if (!entry_obj->Get(context, ToV8Str(isolate, "callback"))
                 .ToLocal(&callback) ||
            !callback->IsFunction())
          continue;
        (void)ev->Set(context, ToV8Str(isolate, "currentTarget"), reader)
            .FromMaybe(false);
        FrSwallowCall(isolate, context, callback.As<v8::Function>(), reader,
                      ev);
        v8::Local<v8::Value> once;
        if (entry_obj->Get(context, ToV8Str(isolate, "once")).ToLocal(&once) &&
            once->BooleanValue(isolate))
        {
          // removeEventListener(type, callback, {capture})
          v8::Local<v8::Value> capture;
          if (entry_obj->Get(context, ToV8Str(isolate, "capture"))
                  .ToLocal(&capture))
          {
            v8::Local<v8::Value> remove_fn;
            if (reader->Get(context, ToV8Str(isolate, "removeEventListener"))
                    .ToLocal(&remove_fn) &&
                remove_fn->IsFunction())
            {
              v8::Local<v8::Value> rargs[] = {
                  ToV8Str(isolate, type), callback, capture};
              (void)remove_fn.As<v8::Function>()
                  ->Call(context, reader, 3, rargs)
                  .FromMaybe(v8::Local<v8::Value>());
            }
          }
        }
        v8::Local<v8::Value> stop;
        if (ev->Get(context, ToV8Str(isolate, "cancelImmediate"))
                .ToLocal(&stop) &&
            stop->BooleanValue(isolate))
          break;
      }
      v8::Local<v8::Value> stop;
      if (ev->Get(context, ToV8Str(isolate, "cancelImmediate")).ToLocal(&stop) &&
          stop->BooleanValue(isolate))
        return;
      std::string handler_name = "on" + type;
      v8::Local<v8::Value> handler;
      if (reader->Get(context, ToV8Str(isolate, handler_name))
              .ToLocal(&handler) &&
          handler->IsFunction())
        FrSwallowCall(isolate, context, handler.As<v8::Function>(), reader,
                      ev);
    }

    void FrAddEventListener(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2 || !info[1]->IsFunction()) return;
      std::string type = info.Length() >= 1 ? ToUtf8(isolate, info[0]) : "";
      bool capture = info.Length() >= 3 && info[2]->IsObject() &&
                     info[2].As<v8::Object>()
                         ->Get(context, ToV8Str(isolate, "capture"))
                         .FromMaybe(v8::Local<v8::Value>())
                         ->BooleanValue(isolate);
      bool once = info.Length() >= 3 && info[2]->IsObject() &&
                  info[2].As<v8::Object>()
                      ->Get(context, ToV8Str(isolate, "once"))
                      .FromMaybe(v8::Local<v8::Value>())
                      ->BooleanValue(isolate);
      v8::Local<v8::Value> listeners;
      if (!info.This()
               ->GetPrivate(context, FrListenersKey(isolate))
               .ToLocal(&listeners) ||
          !listeners->IsObject())
        return;
      v8::Local<v8::Object> map = listeners.As<v8::Object>();
      v8::Local<v8::String> key = ToV8Str(isolate, type);
      v8::Local<v8::Value> bucket;
      if (!map->Get(context, key).ToLocal(&bucket) || !bucket->IsArray())
      {
        bucket = v8::Array::New(isolate);
        (void)map->Set(context, key, bucket).FromMaybe(false);
      }
      v8::Local<v8::Array> callbacks = bucket.As<v8::Array>();
      for (uint32_t i = 0; i < callbacks->Length(); i++)
      {
        v8::Local<v8::Value> entry;
        if (!callbacks->Get(context, i).ToLocal(&entry) || !entry->IsObject())
          continue;
        v8::Local<v8::Value> callback, cap;
        if (entry.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "callback"))
                .ToLocal(&callback) &&
            callback->SameValue(info[1]) &&
            entry.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "capture"))
                .ToLocal(&cap) &&
            cap->IsBoolean() &&
            cap->BooleanValue(isolate) == capture)
          return;
      }
      v8::Local<v8::Object> entry_obj = v8::Object::New(isolate);
      (void)entry_obj->Set(context, ToV8Str(isolate, "callback"), info[1])
          .FromMaybe(false);
      (void)entry_obj->Set(context, ToV8Str(isolate, "capture"),
                           v8::Boolean::New(isolate, capture))
          .FromMaybe(false);
      (void)entry_obj->Set(context, ToV8Str(isolate, "once"),
                           v8::Boolean::New(isolate, once))
          .FromMaybe(false);
      (void)callbacks->Set(context, callbacks->Length(), entry_obj)
          .FromMaybe(false);
    }

    void FrRemoveEventListener(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2 || !info[1]->IsFunction()) return;
      std::string type = ToUtf8(isolate, info[0]);
      bool capture = info.Length() >= 3 && info[2]->IsObject() &&
                     info[2].As<v8::Object>()
                         ->Get(context, ToV8Str(isolate, "capture"))
                         .FromMaybe(v8::Local<v8::Value>())
                         ->BooleanValue(isolate);
      v8::Local<v8::Value> listeners;
      if (!info.This()
               ->GetPrivate(context, FrListenersKey(isolate))
               .ToLocal(&listeners) ||
          !listeners->IsObject())
        return;
      v8::Local<v8::Object> map = listeners.As<v8::Object>();
      v8::Local<v8::Value> bucket;
      if (!map->Get(context, ToV8Str(isolate, type)).ToLocal(&bucket) ||
          !bucket->IsArray())
        return;
      v8::Local<v8::Array> callbacks = bucket.As<v8::Array>();
      for (uint32_t i = 0; i < callbacks->Length(); i++)
      {
        v8::Local<v8::Value> entry;
        if (!callbacks->Get(context, i).ToLocal(&entry) || !entry->IsObject())
          continue;
        v8::Local<v8::Value> callback, cap;
        if (entry.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "callback"))
                .ToLocal(&callback) &&
            callback->SameValue(info[1]) &&
            entry.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "capture"))
                .ToLocal(&cap) &&
            cap->IsBoolean() &&
            cap->BooleanValue(isolate) == capture)
        {
          (void)callbacks->Delete(context, i).FromMaybe(false);
          return;
        }
      }
    }

    void FrDispatchEvent(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsObject()) return;
      v8::Local<v8::Value> type;
      if (!info[0].As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "type"))
               .ToLocal(&type) ||
          !type->IsString())
        return;
      std::string event_type = ToUtf8(isolate, type);
      FrDispatchSimpleEvent(isolate, context, info.This(), event_type);
      info.GetReturnValue().Set(v8::Boolean::New(isolate, true));
    }

    std::string FrBase64(const std::vector<uint8_t>& bytes)
    {
      static const char* alphabet =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string out;
      size_t i = 0;
      while (i < bytes.size())
      {
        uint32_t first = bytes[i];
        uint32_t second = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        uint32_t third = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        out += alphabet[first >> 2];
        out += alphabet[((first & 3) << 4) | (second >> 4)];
        out += i + 1 < bytes.size()
            ? alphabet[((second & 15) << 2) | (third >> 6)]
            : '=';
        out += i + 2 < bytes.size() ? alphabet[third & 63] : '=';
        i += 3;
      }
      return out;
    }

    // schedule the read through the global queueMicrotask; runner converts
    // and dispatches progress/load/error/loadend
    void FrScheduleRead(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        ContextState* state, v8::Local<v8::Object> reader,
                        const std::string& kind, v8::Local<v8::Value> blob)
    {
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 3);
      (void)payload->Set(context, 0, reader).FromMaybe(false);
      (void)payload->Set(context, 1, ToV8Str(isolate, kind))
          .FromMaybe(false);
      (void)payload->Set(context, 2, blob).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                v8::Isolate* iso = args.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Array> data = args.Data().As<v8::Array>();
                v8::Local<v8::Value> reader_value, kind_value, blob_value;
                if (!data->Get(ctx, 0).ToLocal(&reader_value) ||
                    !reader_value->IsObject() ||
                    !data->Get(ctx, 1).ToLocal(&kind_value) ||
                    !data->Get(ctx, 2).ToLocal(&blob_value))
                  return;
                v8::Local<v8::Object> reader = reader_value.As<v8::Object>();
                std::string kind = ToUtf8(iso, kind_value);
                // only run while LOADING (JS: readyState !== LOADING return)
                v8::Local<v8::Value> state_value;
                if (!reader->Get(ctx, ToV8Str(iso, "readyState"))
                         .ToLocal(&state_value) ||
                    !state_value->IsInt32() ||
                    state_value->Int32Value(ctx).FromMaybe(0) != 1)
                  return;
                v8::Local<v8::Value> bytes;
                {
                  v8::Local<v8::Value> helper;
                  if (ctx->Global()
                          ->Get(ctx, ToV8Str(iso, "__lv8_blob_bytes"))
                          .ToLocal(&helper) &&
                      helper->IsFunction())
                  {
                    v8::Local<v8::Value> argv[] = {blob_value};
                    (void)helper.As<v8::Function>()
                        ->Call(ctx, ctx->Global(), 1, argv)
                        .ToLocal(&bytes);
                  }
                }
                // compute the result per kind
                v8::Local<v8::Value> result;
                if (!bytes.IsEmpty() && bytes->IsUint8Array())
                {
                  v8::Local<v8::Uint8Array> view = bytes.As<v8::Uint8Array>();
                  v8::Local<v8::ArrayBuffer> buffer = view->Buffer();
                  size_t total = 0;
                  const uint8_t* raw =
                      ArrayBufferBytes(buffer, &total);
                  size_t offset = static_cast<size_t>(view->ByteOffset());
                  size_t length = static_cast<size_t>(view->ByteLength());
                  if (raw != nullptr && offset + length <= total)
                  {
                    if (kind == "arraybuffer")
                    {
                      v8::Local<v8::ArrayBuffer> copy =
                          v8::ArrayBuffer::New(iso, length);
                      std::memcpy(copy->GetBackingStore()->Data(),
                                  raw + offset, length);
                      result = copy;
                    }
                    else if (kind == "text")
                    {
                      std::vector<uint8_t> slice(raw + offset,
                                                 raw + offset + length);
                      result = ToV8Str(iso, DecodeUtf8Replacement(slice));
                    }
                    else if (kind == "binarystring")
                    {
                      result = v8::String::NewFromOneByte(
                          iso, raw + offset,
                          v8::NewStringType::kNormal,
                          static_cast<int>(length))
                          .ToLocalChecked();
                    }
                    else if (kind == "dataurl")
                    {
                      v8::Local<v8::Value> type;
                      std::string mime = "application/octet-stream";
                      if (blob_value->IsObject() &&
                          blob_value.As<v8::Object>()
                              ->Get(ctx, ToV8Str(iso, "type"))
                              .ToLocal(&type) &&
                          type->IsString() &&
                          ToUtf8(iso, type) != "")
                        mime = ToUtf8(iso, type);
                      std::string out =
                          "data:" + mime + ";base64," +
                          FrBase64(std::vector<uint8_t>(
                              raw + offset, raw + offset + length));
                      result = ToV8Str(iso, out);
                    }
                  }
                }
                (void)reader->Set(ctx, ToV8Str(iso, "readyState"),
                                  v8::Integer::New(iso, 2))
                    .FromMaybe(false);
                if (!result.IsEmpty())
                {
                  (void)reader->Set(ctx, ToV8Str(iso, "result"), result)
                      .FromMaybe(false);
                  FrDispatchSimpleEvent(iso, ctx, reader, "progress");
                  FrDispatchSimpleEvent(iso, ctx, reader, "load");
                }
                else
                {
                  (void)reader->Set(ctx, ToV8Str(iso, "error"),
                                    ToV8Str(iso, "load failed"))
                      .FromMaybe(false);
                  FrDispatchSimpleEvent(iso, ctx, reader, "error");
                }
                FrDispatchSimpleEvent(iso, ctx, reader, "loadend");
              },
              payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Function> runner_fn =
          runner->GetFunction(context).ToLocalChecked();
      v8::Local<v8::Value> queue;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "queueMicrotask"))
              .ToLocal(&queue) &&
          queue->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {runner_fn};
        v8::TryCatch try_catch(isolate);
        (void)queue.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        if (try_catch.HasCaught())
          CJavascriptException::ThrowIf(isolate, try_catch);
      }
    }

    void FrRead(const v8::FunctionCallbackInfo<v8::Value>& info,
                const std::string& kind)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (info.Length() < 1 || state == nullptr ||
          !IsBlobValue(isolate, state, info[0]))
      {
        ThrowTypeError(isolate, context,
                       "FileReader input must be a Blob");
        return;
      }
      v8::Local<v8::Value> ready;
      if (info.This()
              ->Get(context, ToV8Str(isolate, "readyState"))
              .ToLocal(&ready) &&
          ready->IsInt32() &&
          ready->Int32Value(context).FromMaybe(0) == 1)
      {
        isolate->ThrowException(
            ToV8Str(isolate, "Error: InvalidStateError"));
        return;
      }
      (void)info.This()->Set(context, ToV8Str(isolate, "readyState"),
                             v8::Integer::New(isolate, 1))
          .FromMaybe(false);
      (void)info.This()->Set(context, ToV8Str(isolate, "result"),
                             v8::Null(isolate))
          .FromMaybe(false);
      (void)info.This()->Set(context, ToV8Str(isolate, "error"),
                             v8::Null(isolate))
          .FromMaybe(false);
      FrDispatchSimpleEvent(isolate, context, info.This(), "loadstart");
      FrScheduleRead(isolate, context, state, info.This(), kind, info[0]);
    }

    void FrReadAsArrayBuffer(const v8::FunctionCallbackInfo<v8::Value>& info)
    { FrRead(info, "arraybuffer"); }
    void FrReadAsText(const v8::FunctionCallbackInfo<v8::Value>& info)
    { FrRead(info, "text"); }
    void FrReadAsBinaryString(const v8::FunctionCallbackInfo<v8::Value>& info)
    { FrRead(info, "binarystring"); }
    void FrReadAsDataURL(const v8::FunctionCallbackInfo<v8::Value>& info)
    { FrRead(info, "dataurl"); }

    void FrAbort(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> ready;
      if (!info.This()
               ->Get(context, ToV8Str(isolate, "readyState"))
               .ToLocal(&ready) ||
          !ready->IsInt32() ||
          ready->Int32Value(context).FromMaybe(0) != 1)
        return;
      (void)info.This()->Set(context, ToV8Str(isolate, "readyState"),
                             v8::Integer::New(isolate, 2))
          .FromMaybe(false);
      (void)info.This()->Set(context, ToV8Str(isolate, "result"),
                             v8::Null(isolate))
          .FromMaybe(false);
      FrDispatchSimpleEvent(isolate, context, info.This(), "abort");
      FrDispatchSimpleEvent(isolate, context, info.This(), "loadend");
    }

    void InstallFileReader(v8::Isolate* isolate,
                           v8::Local<v8::Context> context,
                           ContextState* state)
    {
      if (!state->filereader_tpl.IsEmpty()) return;
      v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            ContextState* st = GetState(ctx);
            if (!info.IsConstructCall() || st == nullptr ||
                st->filereader_tpl.IsEmpty())
            {
              iso->ThrowException(ToV8Str(
                  iso, "TypeError: FileReader constructor requires new"));
              return;
            }
            v8::Local<v8::Object> inst = st->filereader_tpl.Get(iso)
                                             ->InstanceTemplate()
                                             ->NewInstance(ctx)
                                             .ToLocalChecked();
            (void)inst->SetPrivate(
                        ctx,
                        v8::Private::ForApi(
                            iso, ToV8Str(iso, "lv8.frl.listeners")),
                        v8::Object::New(iso))
                .FromMaybe(false);
            (void)inst->Set(ctx, ToV8Str(iso, "readyState"),
                            v8::Integer::New(iso, 0))
                .FromMaybe(false);
            (void)inst->Set(ctx, ToV8Str(iso, "result"), v8::Null(iso))
                .FromMaybe(false);
            (void)inst->Set(ctx, ToV8Str(iso, "error"), v8::Null(iso))
                .FromMaybe(false);
            const char* handlers[] = {"onloadstart", "onprogress", "onload",
                                      "onerror", "onabort", "onloadend"};
            for (const char* handler : handlers)
              (void)inst->Set(ctx, ToV8Str(iso, handler), v8::Null(iso))
                  .FromMaybe(false);
            info.GetReturnValue().Set(inst);
          });
      ctor->SetClassName(ToV8Str(isolate, "FileReader"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: the reference instances carry zero own symbols -
      // the tag lives on the prototype
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "FileReader"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      method("addEventListener", FrAddEventListener);
      method("removeEventListener", FrRemoveEventListener);
      method("dispatchEvent", FrDispatchEvent);
      method("readAsArrayBuffer", FrReadAsArrayBuffer);
      method("readAsText", FrReadAsText);
      method("readAsBinaryString", FrReadAsBinaryString);
      method("readAsDataURL", FrReadAsDataURL);
      method("abort", FrAbort);
      // EMPTY/LOADING/DONE on both the constructor and the prototype
      ctor->Set(isolate, "EMPTY", v8::Integer::New(isolate, 0),
                v8::PropertyAttribute::ReadOnly);
      ctor->Set(isolate, "LOADING", v8::Integer::New(isolate, 1),
                v8::PropertyAttribute::ReadOnly);
      ctor->Set(isolate, "DONE", v8::Integer::New(isolate, 2),
                v8::PropertyAttribute::ReadOnly);
      ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "EMPTY"),
                                     v8::Integer::New(isolate, 0));
      ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "LOADING"),
                                     v8::Integer::New(isolate, 1));
      ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "DONE"),
                                     v8::Integer::New(isolate, 2));
      state->filereader_tpl.Reset(isolate, ctor);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "FileReader"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    void InstallHeadersGlobal(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              ContextState* state)
    {
      if (!state->headers_tpl.IsEmpty()) return;
      v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
          isolate, HeadersConstructor);
      ctor->SetClassName(ToV8Str(isolate, "Headers"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M6: no instance tag; prototype tag at template level so page
      // scripts (before the JS face tables) also see it
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Headers"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      method("append", HeadersAppend);
      method("set", HeadersSet);
      method("get", HeadersGet);
      method("has", HeadersHas);
      method("delete", HeadersDelete);
      method("getSetCookie", HeadersGetSetCookie);
      method("entries", HeadersEntries);
      method("keys", HeadersKeys);
      method("values", HeadersValues);
      method("forEach", HeadersForEach);
      ctor->PrototypeTemplate()->Set(v8::Symbol::GetIterator(isolate),
                                     v8::FunctionTemplate::New(
                                         isolate, HeadersEntries));
      state->headers_tpl.Reset(isolate, ctor);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "Headers"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // ---- G4 ledger chunk 7b: offline workers ----------------------------
    // (Worker/SharedWorker, formerly JS bootstrap classes). Deterministic:
    // configured messages from __LV8_CONFIG__.workerMessages /
    // sharedWorkerMessages; posts record into the __lv8__.netLog surface.

    v8::Local<v8::Private> WrListenersKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.wrk.listeners"));
    }
    v8::Local<v8::Private> WrSharedInstKey(v8::Isolate* isolate)
    {
      // M5: brand slot - the SharedWorker.prototype accessors only answer
      // to constructed instances (reference throws Illegal invocation
      // otherwise)
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.wrk.swinst"));
    }
    v8::Local<v8::Private> WrSharedOnErrorKey(v8::Isolate* isolate)
    {
      // M5: onerror is stored internally, never as an own property
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.wrk.swonerror"));
    }
    v8::Local<v8::Private> WrSharedKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.sharedworkers"));
    }

    double WrNumOr(v8::Local<v8::Context> context,
                   v8::Local<v8::Value> value, double fallback)
    {
      if (value.IsEmpty()) return fallback;
      return value->NumberValue(context).FromMaybe(fallback);
    }

    // installWorkerTarget semantics: dedup by callback, no capture, no
    // target/currentTarget mutation, exceptions swallowed, on* handlers
    void WrDispatchByType(v8::Isolate* isolate,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Object> receiver,
                          const std::string& type,
                          v8::Local<v8::Value> event)
    {
      v8::Local<v8::Value> listeners;
      if (!receiver
                ->GetPrivate(context, WrListenersKey(isolate))
                .ToLocal(&listeners) ||
          !listeners->IsObject())
        return;
      v8::Local<v8::Object> map = listeners.As<v8::Object>();
      v8::Local<v8::Value> bucket;
      if (!map->Get(context, ToV8Str(isolate, type)).ToLocal(&bucket) ||
          !bucket->IsArray())
        bucket = v8::Array::New(isolate);
      v8::Local<v8::Array> callbacks = bucket.As<v8::Array>();
      for (uint32_t i = 0; i < callbacks->Length(); i++)
      {
        v8::Local<v8::Value> callback;
        if (!callbacks->Get(context, i).ToLocal(&callback) ||
            !callback->IsFunction())
          continue;
        v8::Local<v8::Value> argv[] = {event};
        v8::TryCatch try_catch(isolate);
        (void)callback.As<v8::Function>()
            ->Call(context, receiver, 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
      std::string handler_name = "on" + type;
      v8::Local<v8::Value> handler;
      if (receiver->Get(context, ToV8Str(isolate, handler_name))
              .ToLocal(&handler) &&
          handler->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {event};
        v8::TryCatch try_catch(isolate);
        (void)handler.As<v8::Function>()
            ->Call(context, receiver, 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    void WrQueueTask(v8::Isolate* isolate, v8::Local<v8::Context> context,
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

    // reference structuredish: JSON clone, undefined/functions drop
    v8::Local<v8::Value> WrStructuredish(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context,
                                         v8::Local<v8::Value> value)
    {
      if (value.IsEmpty() || value->IsUndefined() || value->IsFunction())
        return v8::Undefined(isolate);
      v8::Local<v8::String> text;
      if (v8::JSON::Stringify(context, value).ToLocal(&text))
      {
        v8::Local<v8::Value> parsed;
        if (v8::JSON::Parse(context, text).ToLocal(&parsed))
          return parsed;
      }
      return value;
    }

    void WrNetLogPush(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      const char* method, const std::string& url,
                      bool with_body, const std::string& body)
    {
      v8::Local<v8::Value> control;
      if (!context->Global()
                ->Get(context, ToV8Str(isolate, "__lv8__"))
                .ToLocal(&control) ||
          !control->IsObject())
        return;
      v8::Local<v8::Value> netlog;
      if (!control.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "netLog"))
                .ToLocal(&netlog) ||
          !netlog->IsObject())
        return;
      v8::Local<v8::Value> entries;
      if (!netlog.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "entries"))
                .ToLocal(&entries) ||
          !entries->IsArray())
        return;
      v8::Local<v8::Object> entry = v8::Object::New(isolate);
      (void)entry->Set(context, ToV8Str(isolate, "method"),
                       ToV8Str(isolate, method))
          .FromMaybe(false);
      (void)entry->Set(context, ToV8Str(isolate, "url"),
                       ToV8Str(isolate, url))
          .FromMaybe(false);
      (void)entry->Set(context, ToV8Str(isolate, "headers"),
                       v8::Object::New(isolate))
          .FromMaybe(false);
      (void)entry->Set(context, ToV8Str(isolate, "cookieHeader"),
                       ToV8Str(isolate, ""))
          .FromMaybe(false);
      if (with_body)
        (void)entry->Set(context, ToV8Str(isolate, "body"),
                         ToV8Str(isolate, body))
            .FromMaybe(false);
      v8::Local<v8::Array> list = entries.As<v8::Array>();
      (void)list->Set(context, list->Length(), entry).FromMaybe(false);
    }

    // resolve via the native URL constructor (new URL(url, location.href))
    std::string WrResolveUrl(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             const std::string& url)
    {
      v8::Local<v8::Value> location;
      std::string base;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "location"))
              .ToLocal(&location) &&
          location->IsObject())
      {
        v8::Local<v8::Value> href;
        if (location.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "href"))
                .ToLocal(&href) &&
            href->IsString())
          base = ToUtf8(isolate, href);
      }
      v8::Local<v8::Value> url_ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "URL"))
               .ToLocal(&url_ctor) ||
          !url_ctor->IsFunction())
        return url;
      v8::Local<v8::Value> argv[2];
      uint32_t argc = 1;
      argv[0] = ToV8Str(isolate, url);
      if (!base.empty())
        argv[argc++] = ToV8Str(isolate, base);
      v8::Local<v8::Value> instance;
      if (url_ctor.As<v8::Function>()
              ->NewInstance(context, argc, argv)
              .ToLocal(&instance) &&
          instance->IsObject())
      {
        v8::Local<v8::Value> href;
        if (instance.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "href"))
                .ToLocal(&href))
          return ToUtf8(isolate, href);
      }
      return url;
    }

    v8::Local<v8::Value> WrConfiguredMessages(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const char* config_key, const std::string& url)
    {
      v8::Local<v8::Value> config;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "__LV8_CONFIG__"))
              .ToLocal(&config) &&
          config->IsObject())
      {
        v8::Local<v8::Value> table;
        if (config.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, config_key))
                .ToLocal(&table) &&
            table->IsObject())
        {
          v8::Local<v8::Value> list;
          if (table.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, url))
                  .ToLocal(&list) &&
              list->IsArray())
            return list;
        }
      }
      return v8::Array::New(isolate);
    }

    void WrDispatchMessage(v8::Isolate* isolate,
                           v8::Local<v8::Context> context,
                           v8::Local<v8::Object> receiver,
                           v8::Local<v8::Value> data)
    {
      v8::Local<v8::Value> message_event;
      if (!context->Global()
                ->Get(context, ToV8Str(isolate, "MessageEvent"))
                .ToLocal(&message_event) ||
          !message_event->IsFunction())
        return;
      v8::Local<v8::Object> options = v8::Object::New(isolate);
      (void)options->Set(context, ToV8Str(isolate, "data"), data)
          .FromMaybe(false);
      v8::Local<v8::Value> argv[] = {ToV8Str(isolate, "message"), options};
      v8::Local<v8::Value> event;
      if (!message_event.As<v8::Function>()
               ->NewInstance(context, 2, argv)
               .ToLocal(&event) ||
          event.IsEmpty())
        return;
      WrDispatchByType(isolate, context, receiver, "message", event);
    }

    void WrDispatchNamed(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> receiver,
                         const char* ctor_name, const char* type)
    {
      v8::Local<v8::Value> ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, ctor_name))
               .ToLocal(&ctor) ||
          !ctor->IsFunction())
        return;
      v8::Local<v8::Value> argv[] = {ToV8Str(isolate, type)};
      v8::Local<v8::Value> event;
      if (ctor.As<v8::Function>()->NewInstance(context, 1, argv).ToLocal(&event) &&
          !event.IsEmpty())
        WrDispatchByType(isolate, context, receiver, type, event);
    }

    // M4 forward declarations (definitions sit with the worker runtime
    // block below; the constructor and the listeners they wire to are
    // compiled earlier in this translation unit)
    v8::Local<v8::Private> WrRuntimeKey(v8::Isolate* isolate);
    std::string WrWorkerScriptBody(ContextState* state,
                                   const std::string& url);
    void WrWorkerQueue(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       ContextState::WorkerRuntime* rt, int kind,
                       int timer_index);
    void WrWorkerInstallGlobal(v8::Isolate* isolate,
                               v8::Local<v8::Context> main_context,
                               v8::Local<v8::Context> wctx,
                               ContextState::WorkerRuntime* rt);

    void WorkerConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->worker_tpl.IsEmpty())
      {
        // G4.287: the reference throws a real TypeError (an Error instance),
        // not a bare string - exception kinds are part of the observable face.
        // M5: message text aligned to the reference verbatim.
        isolate->ThrowException(v8::Exception::TypeError(ToV8Str(
            isolate, "Failed to construct 'Worker': Constructor Worker "
                     "requires 'new'")));
        return;
      }
      // M5: the reference validates the argument count before any side
      // effect (netLog/worker runtime); undefined still counts as present.
      if (info.Length() < 1)
      {
        isolate->ThrowException(v8::Exception::TypeError(ToV8Str(
            isolate, "Failed to construct 'Worker': 1 argument required, "
                     "but only 0 present.")));
        return;
      }
      v8::Local<v8::Object> inst = state->worker_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, WrListenersKey(isolate),
                             v8::Object::New(isolate))
          .FromMaybe(false);
      std::string url = info.Length() >= 1
          ? ToUtf8(isolate, info[0])
          : "";
      url = WrResolveUrl(isolate, context, url);
      // M5: the reference instance carries zero own properties (no
      // url/onmessage/onerror - w.url reads undefined there) and zero own
      // symbols (the tag lives on Worker.prototype).
      v8::Local<v8::Value> configured = WrConfiguredMessages(
          isolate, context, "workerMessages", url);
      WrNetLogPush(isolate, context, "WORKER", url, false, "");
      // M4 runtime: independent context (profile-snapshot global, own
      // message queues); the start task runs before the load runner so
      // script-time posts reach the page in order
      auto runtime = std::make_unique<ContextState::WorkerRuntime>();
      runtime->url = url;
      runtime->worker.Reset(isolate, inst);
      runtime->main_context.Reset(isolate, context);
      runtime->script_body = WrWorkerScriptBody(state, url);
      ContextState::WorkerRuntime* rt = runtime.get();
      {
        v8::Local<v8::Context> wctx = v8::Context::New(isolate);
        {
          v8::Context::Scope scope(wctx);
          WrWorkerInstallGlobal(isolate, context, wctx, rt);
        }
        runtime->context.Reset(isolate, wctx);
      }
      state->worker_runtimes.push_back(std::move(runtime));
      (void)inst->SetPrivate(context, WrRuntimeKey(isolate),
                             v8::External::New(isolate, rt))
          .FromMaybe(false);
      WrWorkerQueue(isolate, context, rt, 0, -1);
      // microtask: dispatch "load", then the configured messages
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 3);
      (void)payload->Set(context, 0, inst).FromMaybe(false);
      (void)payload->Set(context, 1, ToV8Str(isolate, "load"))
          .FromMaybe(false);
      (void)payload->Set(context, 2, configured).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> data = args.Data().As<v8::Array>();
            v8::Local<v8::Value> receiver, load_type, configured;
            if (!data->Get(ctx, 0).ToLocal(&receiver) ||
                !receiver->IsObject() ||
                !data->Get(ctx, 1).ToLocal(&load_type) ||
                !data->Get(ctx, 2).ToLocal(&configured) ||
                !configured->IsArray())
              return;
            WrDispatchNamed(iso, ctx, receiver.As<v8::Object>(), "Event",
                            ToUtf8(iso, load_type).c_str());
            v8::Local<v8::Array> messages = configured.As<v8::Array>();
            for (uint32_t i = 0; i < messages->Length(); i++)
            {
              v8::Local<v8::Value> message;
              if (messages->Get(ctx, i).ToLocal(&message))
                WrDispatchMessage(iso, ctx, receiver.As<v8::Object>(),
                                  message);
            }
          },
          payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      WrQueueTask(isolate, context, runner);
      info.GetReturnValue().Set(inst);
    }

    void WorkerPostMessage(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> url;
      std::string url_text;
      if (info.This()->Get(context, ToV8Str(isolate, "url")).ToLocal(&url))
        url_text = ToUtf8(isolate, url);
      std::string body = info.Length() >= 1 ? ToUtf8(isolate, info[0]) : "";
      WrNetLogPush(isolate, context, "WORKER_POST", url_text, true, body);
      // M4: deliver through the structuredish boundary into the worker's
      // own queue (functions/undefined drop, matching the contract test)
      v8::Local<v8::Value> runtime;
      if (info.This()
              ->GetPrivate(context, WrRuntimeKey(isolate))
              .ToLocal(&runtime) &&
          runtime->IsExternal())
      {
        auto* rt = static_cast<ContextState::WorkerRuntime*>(
            runtime.As<v8::External>()->Value());
        if (rt->terminated) return;
        v8::Local<v8::Value> cloned =
            WrStructuredish(isolate, context,
                            info.Length() >= 1 ? info[0]
                                               : v8::Local<v8::Value>());
        if (cloned.IsEmpty() || cloned->IsUndefined()) return;
        v8::Local<v8::String> text;
        if (!v8::JSON::Stringify(context, cloned).ToLocal(&text)) return;
        rt->to_worker.push_back(ToUtf8(isolate, text));
        WrWorkerQueue(isolate, context, rt, 1, -1);
      }
    }

    void WorkerTerminate(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> runtime;
      if (info.This()
              ->GetPrivate(context, WrRuntimeKey(isolate))
              .ToLocal(&runtime) &&
          runtime->IsExternal())
      {
        auto* rt = static_cast<ContextState::WorkerRuntime*>(
            runtime.As<v8::External>()->Value());
        rt->terminated = true;
        rt->to_worker.clear();
        rt->to_main.clear();
      }
    }

    // ---- M4 (G4.309): dedicated worker runtimes -------------------------
    // Real (offline, deterministic) execution for dedicated workers: an
    // independent context per Worker whose global carries the resolved
    // profile snapshot and no page state, plus two message queues.  The
    // page's microtask queue pumps everything -- construction queues the
    // script eval, each postMessage queues one delivery -- so delivery
    // stays deterministic with ctx.drain().  Message data crosses the
    // boundary through the reference-style structuredish clone.

    // (listeners used by the worker global are declared later in this
    // translation unit)
    void WorkerAddEventListener(const v8::FunctionCallbackInfo<v8::Value>& info);
    void WorkerRemoveEventListener(
        const v8::FunctionCallbackInfo<v8::Value>& info);
    void WorkerDispatchEvent(const v8::FunctionCallbackInfo<v8::Value>& info);
    void WrWorkerRunner(const v8::FunctionCallbackInfo<v8::Value>& info);

    v8::Local<v8::Private> WrRuntimeKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.wrk.runtime"));
    }

    std::string WrWorkerScriptBody(ContextState* state, const std::string& url)
    {
      for (const auto& r : state->resources)
        if (r.url == url) return r.body;
      return std::string();
    }

    // absolute importScripts URL: keep schemed, root-relative resolves
    // against the page, everything else against the worker script dir
    std::string WrWorkerImportUrl(v8::Isolate* isolate,
                                  v8::Local<v8::Context> main_context,
                                  ContextState::WorkerRuntime* rt,
                                  const std::string& url)
    {
      if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
        return url;
      if (!url.empty() && url[0] == '/')
        return WrResolveUrl(isolate, main_context, url);
      std::string base = rt->url;
      size_t slash = base.rfind('/');
      std::string dir = slash == std::string::npos ? std::string()
                                                   : base.substr(0, slash + 1);
      return dir + url;
    }

    void WrWorkerQueue(v8::Isolate* isolate,
                       v8::Local<v8::Context> context,
                       ContextState::WorkerRuntime* rt, int kind,
                       int timer_index)
    {
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 3);
      (void)payload->Set(context, 0, v8::External::New(isolate, rt))
          .FromMaybe(false);
      (void)payload->Set(context, 1, v8::Int32::New(isolate, kind))
          .FromMaybe(false);
      (void)payload->Set(context, 2, v8::Int32::New(isolate, timer_index))
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> task =
          v8::FunctionTemplate::New(isolate, WrWorkerRunner, payload);
      WrQueueTask(isolate, context, task);
    }

    void WrWorkerDeliverPending(v8::Isolate* isolate,
                                ContextState::WorkerRuntime* rt)
    {
      if (rt->terminated || !rt->started || rt->to_worker.empty()) return;
      v8::Local<v8::Context> wctx = rt->context.Get(isolate);
      v8::Context::Scope scope(wctx);
      while (!rt->terminated && !rt->to_worker.empty())
      {
        std::string text = rt->to_worker.front();
        rt->to_worker.pop_front();
        v8::Local<v8::Value> parsed;
        if (!v8::JSON::Parse(wctx, ToV8Str(isolate, text)).ToLocal(&parsed))
          continue;
        WrDispatchMessage(isolate, wctx, wctx->Global(), parsed);
      }
    }

    void WrWorkerRunner(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> runtime, kind_value, index_value;
      if (!data->Get(context, 0).ToLocal(&runtime) ||
          !runtime->IsExternal() ||
          !data->Get(context, 1).ToLocal(&kind_value))
        return;
      auto* rt = static_cast<ContextState::WorkerRuntime*>(
          runtime.As<v8::External>()->Value());
      int kind = kind_value->Int32Value(context).FromMaybe(-1);
      if (rt->terminated) return;
      if (kind == 0)
      {
        // start: evaluate the resource-backed script body once
        if (rt->started) return;
        rt->started = true;
        if (!rt->script_body.empty())
        {
          v8::Local<v8::Context> wctx = rt->context.Get(isolate);
          v8::Context::Scope scope(wctx);
          v8::ScriptOrigin origin(ToV8Str(isolate, rt->url));
          v8::Local<v8::Script> script;
          v8::TryCatch try_catch(isolate);
          if (v8::Script::Compile(wctx, ToV8Str(isolate, rt->script_body),
                                  &origin)
                  .ToLocal(&script))
          {
            (void)script->Run(wctx);
          }
          if (try_catch.HasCaught())
          {
            v8::Local<v8::Context> main = rt->main_context.Get(isolate);
            v8::Context::Scope main_scope(main);
            std::string message;
            v8::Local<v8::Message> msg = try_catch.Message();
            if (!msg.IsEmpty())
              message = ToUtf8(isolate, msg->Get());
            WrNetLogPush(isolate, main, "WORKER_ERR", rt->url, true, message);
          }
        }
        // any messages posted before the script ran are deliverable now
        WrWorkerDeliverPending(isolate, rt);
      }
      else if (kind == 1)
      {
        WrWorkerDeliverPending(isolate, rt);
      }
      else if (kind == 2)
      {
        // one worker -> page message onto the main-side Worker object
        if (rt->to_main.empty()) return;
        std::string text = rt->to_main.front();
        rt->to_main.pop_front();
        v8::Local<v8::Context> main = rt->main_context.Get(isolate);
        v8::Context::Scope scope(main);
        // G4.338 (reference ground truth, _m20_worker_reflow.json): the
        // reference NEVER reflows dedicated-Worker messages to the page -
        // no onmessage, no listener dispatch (M20 A/B/C all empty under
        // reference).  MessageChannel/SharedWorker PORT reflow exists there and
        // is kept; the dedicated-Worker dispatch is removed.
      }
      else if (kind == 3)
      {
        // deterministic timer: run once at the next pump
        if (!data->Get(context, 2).ToLocal(&index_value)) return;
        int idx = index_value->Int32Value(context).FromMaybe(-1);
        if (idx < 0 || idx >= static_cast<int>(rt->timers.size())) return;
        v8::Local<v8::Function> fn = rt->timers[idx]->Get(isolate);
        if (fn.IsEmpty()) return;
        v8::Local<v8::Context> wctx = rt->context.Get(isolate);
        v8::Context::Scope scope(wctx);
        v8::TryCatch try_catch(isolate);
        (void)fn->Call(wctx, wctx->Global(), 0, nullptr)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    void WrWorkerSelfPostMessage(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> wctx = isolate->GetCurrentContext();
      auto* rt = static_cast<ContextState::WorkerRuntime*>(
          info.Data().As<v8::External>()->Value());
      if (rt->terminated) return;
      v8::Local<v8::Value> cloned =
          WrStructuredish(isolate, wctx,
                          info.Length() >= 1 ? info[0]
                                             : v8::Local<v8::Value>());
      if (cloned.IsEmpty() || cloned->IsUndefined()) return;
      v8::Local<v8::String> text;
      if (!v8::JSON::Stringify(wctx, cloned).ToLocal(&text)) return;
      rt->to_main.push_back(ToUtf8(isolate, text));
      v8::Local<v8::Context> main = rt->main_context.Get(isolate);
      WrWorkerQueue(isolate, main, rt, 2, -1);
    }

    void WrWorkerImportScripts(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> wctx = isolate->GetCurrentContext();
      auto* rt = static_cast<ContextState::WorkerRuntime*>(
          info.Data().As<v8::External>()->Value());
      v8::Local<v8::Context> main = rt->main_context.Get(isolate);
      ContextState* mstate = GetState(main);
      for (int i = 0; i < info.Length(); i++)
      {
        std::string abs = WrWorkerImportUrl(isolate, main, rt,
                                            ToUtf8(isolate, info[i]));
        std::string body =
            mstate ? WrWorkerScriptBody(mstate, abs) : std::string();
        if (body.empty()) continue;
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Script> script;
        if (v8::Script::Compile(wctx, ToV8Str(isolate, body)).ToLocal(&script))
          (void)script->Run(wctx);
        try_catch.Reset();
      }
      info.GetReturnValue().SetUndefined();
    }

    void WrWorkerSetTimeout(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      auto* rt = static_cast<ContextState::WorkerRuntime*>(
          info.Data().As<v8::External>()->Value());
      if (rt->terminated || info.Length() < 1 || !info[0]->IsFunction())
      {
        info.GetReturnValue().Set(0);
        return;
      }
      auto handle = std::make_unique<v8::Global<v8::Function>>(
          isolate, info[0].As<v8::Function>());
      int idx = static_cast<int>(rt->timers.size());
      rt->timers.push_back(std::move(handle));
      v8::Local<v8::Context> main = rt->main_context.Get(isolate);
      WrWorkerQueue(isolate, main, rt, 3, idx);
      info.GetReturnValue().Set(idx + 1);
    }

    void WrWorkerNoOp(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetUndefined();
    }

    void WrWorkerIllegalCtor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      isolate->ThrowException(
          v8::Exception::TypeError(ToV8Str(isolate, "Illegal constructor")));
    }

    void WrWorkerEventCtor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(v8::Exception::TypeError(
            ToV8Str(isolate, "Constructor Event requires 'new'")));
        return;
      }
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> ev = info.This();
      v8::Local<v8::Value> type_value =
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(ToV8Str(isolate, ""));
      (void)ev->Set(context, ToV8Str(isolate, "type"), type_value)
          .FromMaybe(false);
      (void)ev->Set(context, ToV8Str(isolate, "defaultPrevented"),
                    v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      v8::Local<v8::Function> prevent;
      if (v8::Function::New(
              context,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                return;  // no default actions inside workers
              })
              .ToLocal(&prevent))
        (void)ev->Set(context, ToV8Str(isolate, "preventDefault"), prevent)
            .FromMaybe(false);
      info.GetReturnValue().Set(ev);
    }

    void WrWorkerMessageEventCtor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(v8::Exception::TypeError(ToV8Str(
            isolate, "Constructor MessageEvent requires 'new'")));
        return;
      }
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> ev = info.This();
      v8::Local<v8::Value> type_value =
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(ToV8Str(isolate, "message"));
      (void)ev->Set(context, ToV8Str(isolate, "type"), type_value)
          .FromMaybe(false);
      if (info.Length() >= 2 && info[1]->IsObject())
      {
        v8::Local<v8::Value> data;
        if (info[1].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "data"))
                .ToLocal(&data))
          (void)ev->Set(context, ToV8Str(isolate, "data"), data)
              .FromMaybe(false);
      }
      info.GetReturnValue().Set(ev);
    }

    void WrWorkerInstallGlobal(v8::Isolate* isolate,
                               v8::Local<v8::Context> main_context,
                               v8::Local<v8::Context> wctx,
                               ContextState::WorkerRuntime* rt)
    {
      // Install on the INNER global (Global()'s prototype), never on the
      // global proxy: the proxy's prototype link to the inner global is
      // what routes script var declarations and property stores, and
      // replacing it orphans them (observed: self.x = 1 never sticks).
      // 'self' aliases the proxy so self === globalThis stays true.
      v8::Local<v8::Object> g = wctx->Global();
      v8::Local<v8::Object> inner = g->GetPrototype().As<v8::Object>();
      v8::Local<v8::Value> object_proto = inner->GetPrototype();
      v8::Local<v8::External> rt_external = v8::External::New(isolate, rt);
      // the listener map lives on the receiver: both addEventListener and
      // the dispatcher read it off info.This()/receiver (the global proxy,
      // == self) and silently no-op when it is missing
      (void)g->SetPrivate(wctx, WrListenersKey(isolate),
                          v8::Object::New(isolate))
          .FromMaybe(false);
      auto define = [&](const char* name, v8::Local<v8::Value> value) {
        (void)inner->DefineOwnProperty(wctx, ToV8Str(isolate, name), value,
                                       static_cast<v8::PropertyAttribute>(
                                           v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      };
      auto define_fn = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::FunctionTemplate> tpl =
            v8::FunctionTemplate::New(isolate, cb, rt_external);
        tpl->SetClassName(ToV8Str(isolate, name));
        define(name, tpl->GetFunction(wctx).ToLocalChecked());
      };

      // self aliases the worker global; no window/document ever installs
      define("self", g);
      define("name", ToV8Str(isolate, ""));
      v8::Local<v8::Object> location = v8::Object::New(isolate);
      (void)location
          ->DefineOwnProperty(wctx, ToV8Str(isolate, "href"),
                              ToV8Str(isolate, rt->url),
                              v8::PropertyAttribute::ReadOnly)
          .FromMaybe(false);
      define("location", location);

      // resolved profile snapshot: marshal the main navigator's fields
      // through JSON so no main-context object identity crosses over
      {
        const char* kNavFields[] = {
            "userAgent", "appVersion", "platform", "vendor", "product",
            "language", "languages", "hardwareConcurrency", "deviceMemory",
            "cookieEnabled", "onLine", "maxTouchPoints"};
        v8::Local<v8::Object> pick = v8::Object::New(isolate);
        v8::Local<v8::Value> nav;
        if (main_context->Global()
                ->Get(main_context, ToV8Str(isolate, "navigator"))
                .ToLocal(&nav) &&
            nav->IsObject())
        {
          for (const char* f : kNavFields)
          {
            v8::Local<v8::Value> v;
            if (nav.As<v8::Object>()
                    ->Get(main_context, ToV8Str(isolate, f))
                    .ToLocal(&v) &&
                !v->IsUndefined())
              (void)pick->Set(main_context, ToV8Str(isolate, f), v)
                  .FromMaybe(false);
          }
        }
        v8::Local<v8::String> text;
        if (!v8::JSON::Stringify(main_context, pick).ToLocal(&text))
          text = ToV8Str(isolate, "{}");
        v8::Local<v8::Value> parsed;
        if (v8::JSON::Parse(wctx, text).ToLocal(&parsed) && parsed->IsObject())
          define("navigator", parsed);
      }

      // messaging surface
      define_fn("postMessage", WrWorkerSelfPostMessage);
      define_fn("importScripts", WrWorkerImportScripts);
      define_fn("setTimeout", WrWorkerSetTimeout);
      define_fn("setInterval", WrWorkerSetTimeout);
      define_fn("clearTimeout", WrWorkerNoOp);
      define_fn("clearInterval", WrWorkerNoOp);
      define_fn("addEventListener", WorkerAddEventListener);
      define_fn("removeEventListener", WorkerRemoveEventListener);
      define_fn("dispatchEvent", WorkerDispatchEvent);

      // worker-side Event / MessageEvent constructors (dispatch helpers
      // look them up on the receiving global)
      {
        v8::Local<v8::FunctionTemplate> event_tpl =
            v8::FunctionTemplate::New(isolate, WrWorkerEventCtor);
        event_tpl->SetClassName(ToV8Str(isolate, "Event"));
        define("Event", event_tpl->GetFunction(wctx).ToLocalChecked());
        v8::Local<v8::FunctionTemplate> msg_tpl =
            v8::FunctionTemplate::New(isolate, WrWorkerMessageEventCtor);
        msg_tpl->SetClassName(ToV8Str(isolate, "MessageEvent"));
        define("MessageEvent", msg_tpl->GetFunction(wctx).ToLocalChecked());
      }

      // WorkerGlobalScope chain via real FunctionTemplate prototypes
      // (Inherit chains them; the automatic .prototype property of a
      // template function is non-configurable, so hand-defining it does
      // not take -- instanceof must see the template's own chain).
      {
        auto scope_ctor = [&](const char* name,
                              v8::Local<v8::FunctionTemplate> parent) {
          v8::Local<v8::FunctionTemplate> tpl =
              v8::FunctionTemplate::New(isolate, WrWorkerIllegalCtor);
          tpl->SetClassName(ToV8Str(isolate, name));
          if (!parent.IsEmpty()) tpl->Inherit(parent);
          tpl->PrototypeTemplate()->Set(
              v8::Symbol::GetToStringTag(isolate), ToV8Str(isolate, name),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));
          define(name, tpl->GetFunction(wctx).ToLocalChecked());
          return tpl;
        };
        v8::Local<v8::FunctionTemplate> et =
            scope_ctor("EventTarget", v8::Local<v8::FunctionTemplate>());
        v8::Local<v8::FunctionTemplate> wg = scope_ctor("WorkerGlobalScope", et);
        v8::Local<v8::FunctionTemplate> dw =
            scope_ctor("DedicatedWorkerGlobalScope", wg);
        v8::Local<v8::Object> dwgp =
            dw->GetFunction(wctx).ToLocalChecked()
                ->Get(wctx, ToV8Str(isolate, "prototype"))
                .ToLocalChecked()
                .As<v8::Object>();
        (void)inner->SetPrototype(wctx, dwgp).FromMaybe(false);
      }

      // silent console: workers run offline and deterministic
      v8::Local<v8::String> console_src = ToV8Str(
          isolate,
          "var console={log:function(){},warn:function(){},error:function(){"
          ",info:function(){},debug:function(){},trace:function(){}};");
      v8::Local<v8::Script> console_script;
      if (v8::Script::Compile(wctx, console_src).ToLocal(&console_script))
        (void)console_script->Run(wctx);
    }


    // fan-out a cloned message to every sibling port of the entry
    void WrFanoutPorts(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> ports, sender, cloned;
      if (!data->Get(context, 0).ToLocal(&ports) ||
          !ports->IsArray() ||
          !data->Get(context, 1).ToLocal(&sender) ||
          !data->Get(context, 2).ToLocal(&cloned))
        return;
      v8::Local<v8::Array> plist = ports.As<v8::Array>();
      for (uint32_t p = 0; p < plist->Length(); p++)
      {
        v8::Local<v8::Value> peer;
        if (!plist->Get(context, p).ToLocal(&peer) ||
            !peer->IsObject() ||
            peer->SameValue(sender))
          continue;
        v8::Local<v8::Value> dispatch;
        if (peer.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "dispatchEvent"))
                .ToLocal(&dispatch) &&
            dispatch->IsFunction())
        {
          v8::Local<v8::Value> message_event;
          if (context->Global()
                   ->Get(context, ToV8Str(isolate, "MessageEvent"))
                   .ToLocal(&message_event) &&
              message_event->IsFunction())
          {
            v8::Local<v8::Object> options = v8::Object::New(isolate);
            (void)options->Set(context, ToV8Str(isolate, "data"), cloned)
                .FromMaybe(false);
            v8::Local<v8::Value> argv[] = {ToV8Str(isolate, "message"),
                                           options};
            v8::Local<v8::Value> event;
            if (message_event.As<v8::Function>()
                    ->NewInstance(context, 2, argv)
                    .ToLocal(&event) &&
                event->IsObject())
            {
              v8::Local<v8::Value> dispatch_argv[] = {event};
              v8::TryCatch try_catch(isolate);
              (void)dispatch.As<v8::Function>()
                  ->Call(context, peer.As<v8::Object>(), 1, dispatch_argv)
                  .FromMaybe(v8::Local<v8::Value>());
              try_catch.Reset();
            }
          }
        }
      }
    }

    void WrQueueFunction(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Function> fn)
    {
      v8::Local<v8::Value> queue;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "queueMicrotask"))
              .ToLocal(&queue) &&
          queue->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {fn};
        v8::TryCatch try_catch(isolate);
        (void)queue.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    void SharedWorkerConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->sharedworker_tpl.IsEmpty())
      {
        // M5: real TypeError instance; message text verbatim from the
        // reference (double prefix is the reference's own quirk).
        isolate->ThrowException(v8::Exception::TypeError(ToV8Str(
            isolate, "Failed to construct 'SharedWorker': Failed to "
                     "construct 'SharedWorker': Please use the 'new' "
                     "operator.")));
        return;
      }
      // M5: argument-count validation before any side effect.
      if (info.Length() < 1)
      {
        isolate->ThrowException(v8::Exception::TypeError(ToV8Str(
            isolate, "Failed to construct 'SharedWorker': 1 argument "
                     "required, but only 0 present.")));
        return;
      }
      v8::Local<v8::Object> inst = state->sharedworker_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, WrSharedInstKey(isolate),
                             v8::True(isolate))
          .FromMaybe(false);
      std::string url = info.Length() >= 1
          ? ToUtf8(isolate, info[0])
          : std::string();
      std::string name;
      if (info.Length() >= 2 && info[1]->IsObject())
      {
        v8::Local<v8::Value> raw;
        if (info[1].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "name"))
                .ToLocal(&raw) &&
            !raw->IsUndefined())
          name = ToUtf8(isolate, raw);
      }
      std::string key = url + "#" + name;
      v8::Local<v8::Private> shared_key = WrSharedKey(isolate);
      v8::Local<v8::Value> registry_value;
      v8::Local<v8::Map> registry;
      if (context->Global()
              ->GetPrivate(context, shared_key)
              .ToLocal(&registry_value) &&
          registry_value->IsMap())
      {
        registry = registry_value.As<v8::Map>();
      }
      else
      {
        registry = v8::Map::New(isolate);
        (void)context->Global()->SetPrivate(context, shared_key, registry)
            .FromMaybe(false);
      }
      v8::Local<v8::Value> key_value = ToV8Str(isolate, key);
      v8::Local<v8::Value> entry;
      if (!registry->Get(context, key_value).ToLocal(&entry) ||
          entry->IsUndefined())
      {
        entry = v8::Object::New(isolate);
        (void)entry.As<v8::Object>()
            ->Set(context, ToV8Str(isolate, "ports"), v8::Array::New(isolate))
            .FromMaybe(false);
        (void)entry.As<v8::Object>()
            ->Set(context, ToV8Str(isolate, "configured"),
                  WrConfiguredMessages(isolate, context,
                                       "sharedWorkerMessages", url))
            .FromMaybe(false);
        (void)registry->Set(context, key_value, entry).IsEmpty();
        // configured messages fan out to every port joined at run time
        v8::Local<v8::Array> runner_data = v8::Array::New(isolate, 1);
        (void)runner_data->Set(context, 0, entry).FromMaybe(false);
        v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& args) {
              v8::Isolate* iso = args.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              v8::Local<v8::Value> entry_value, configured;
              if (!args.Data().As<v8::Array>()
                        ->Get(ctx, 0).ToLocal(&entry_value) ||
                  !entry_value->IsObject() ||
                  !entry_value.As<v8::Object>()
                       ->Get(ctx, ToV8Str(iso, "configured"))
                       .ToLocal(&configured) ||
                  !configured->IsArray())
                return;
              v8::Local<v8::Value> ports;
              if (!entry_value.As<v8::Object>()
                       ->Get(ctx, ToV8Str(iso, "ports"))
                       .ToLocal(&ports) ||
                  !ports->IsArray())
                return;
              v8::Local<v8::Array> messages = configured.As<v8::Array>();
              for (uint32_t i = 0; i < messages->Length(); i++)
              {
                v8::Local<v8::Value> message;
                if (!messages->Get(ctx, i).ToLocal(&message)) continue;
                v8::Local<v8::Array> port_list = ports.As<v8::Array>();
                for (uint32_t p = 0; p < port_list->Length(); p++)
                {
                  v8::Local<v8::Value> port;
                  if (!port_list->Get(ctx, p).ToLocal(&port) ||
                      !port->IsObject())
                    continue;
                  // MessagePort keeps its listeners on the JS side; call
                  // its own dispatchEvent with a native MessageEvent
                  v8::Local<v8::Value> message_event;
                  if (ctx->Global()
                           ->Get(ctx, ToV8Str(iso, "MessageEvent"))
                           .ToLocal(&message_event) &&
                      message_event->IsFunction())
                  {
                    v8::Local<v8::Object> options = v8::Object::New(iso);
                    (void)options->Set(ctx, ToV8Str(iso, "data"), message)
                        .FromMaybe(false);
                    v8::Local<v8::Value> eargv[] = {
                        ToV8Str(iso, "message"), options};
                    v8::Local<v8::Value> event;
                    if (message_event.As<v8::Function>()
                            ->NewInstance(ctx, 2, eargv)
                            .ToLocal(&event) &&
                        event->IsObject())
                    {
                      v8::Local<v8::Value> dispatch;
                      if (port.As<v8::Object>()
                              ->Get(ctx, ToV8Str(iso, "dispatchEvent"))
                              .ToLocal(&dispatch) &&
                          dispatch->IsFunction())
                      {
                        v8::Local<v8::Value> dispatch_argv[] = {event};
                        v8::TryCatch try_catch(iso);
                        (void)dispatch.As<v8::Function>()
                            ->Call(ctx, port, 1, dispatch_argv)
                            .FromMaybe(v8::Local<v8::Value>());
                        try_catch.Reset();
                      }
                    }
                  }
                }
              }
            },
            runner_data);
        runner->SetClassName(ToV8Str(isolate, ""));
        WrQueueTask(isolate, context, runner);
      }
      // join: create a MessagePort and register it with the entry
      v8::Local<v8::Object> port;
      {
        // M16: the MessagePort interface ctor is Illegal (reference parity) -
        // internal factories go through MessagePort.__lv8Create, which
        // page-level `new MessagePort()` must NOT be able to reach
        v8::Local<v8::Value> port_ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "MessagePort"))
                .ToLocal(&port_ctor) &&
            port_ctor->IsFunction())
        {
          v8::Local<v8::Value> factory;
          if (port_ctor.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "__lv8Create"))
                  .ToLocal(&factory) &&
              factory->IsFunction())
          {
            v8::Local<v8::Value> port_value;
            if (factory.As<v8::Function>()
                    ->Call(context, port_ctor, 0, nullptr)
                    .ToLocal(&port_value) &&
                port_value->IsObject())
              port = port_value.As<v8::Object>();
          }
        }
      }
      if (port.IsEmpty())
      {
        info.GetReturnValue().Set(inst);
        return;
      }
      v8::Local<v8::Value> entry_ports;
      if (entry.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "ports"))
              .ToLocal(&entry_ports) &&
          entry_ports->IsArray())
        (void)entry_ports.As<v8::Array>()
            ->Set(context, entry_ports.As<v8::Array>()->Length(), port)
            .FromMaybe(false);
      // posting loops the (JSON-cloned) message to sibling ports
      v8::Local<v8::Array> post_data = v8::Array::New(isolate, 2);
      (void)post_data->Set(context, 0, entry).FromMaybe(false);
      (void)post_data->Set(context, 1, port).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> post_impl =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                v8::Isolate* iso = args.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Array> data = args.Data().As<v8::Array>();
                v8::Local<v8::Value> entry_value, self_port;
                if (!data->Get(ctx, 0).ToLocal(&entry_value) ||
                    !entry_value->IsObject() ||
                    !data->Get(ctx, 1).ToLocal(&self_port) ||
                    !self_port->IsObject())
                  return;
                v8::Local<v8::Value> message =
                    args.Length() >= 1
                        ? args[0]
                        : v8::Local<v8::Value>(v8::Undefined(iso));
                v8::Local<v8::Value> cloned =
                    WrStructuredish(iso, ctx, message);
                v8::Local<v8::Value> ports;
                if (!entry_value.As<v8::Object>()
                         ->Get(ctx, ToV8Str(iso, "ports"))
                         .ToLocal(&ports))
                  return;
                v8::Local<v8::Array> fanout_data = v8::Array::New(iso, 3);
                (void)fanout_data->Set(ctx, 0, ports).FromMaybe(false);
                v8::Local<v8::Value> sender;
                if (data->Get(ctx, 1).ToLocal(&sender))
                  (void)fanout_data->Set(ctx, 1, sender).FromMaybe(false);
                (void)fanout_data->Set(ctx, 2, cloned).FromMaybe(false);
                v8::Local<v8::Function> fanout =
                    v8::FunctionTemplate::New(iso, WrFanoutPorts,
                                              fanout_data)
                        ->GetFunction(ctx)
                        .ToLocalChecked();
                WrQueueFunction(iso, ctx, fanout);
              },
              post_data);
      post_impl->SetClassName(ToV8Str(isolate, "postMessage"));
      (void)port->Set(context, ToV8Str(isolate, "postMessage"),
                      post_impl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      // M5: the reference instance carries zero own properties - the port
      // stays reachable only through the shared entry (the prototype's
      // port accessor reads back null), and name/url are not exposed.
      info.GetReturnValue().Set(inst);
    }

    // M5: SharedWorker.prototype accessors - brand-checked like the
    // reference webidl bindings (Illegal invocation on foreign receivers),
    // reading back null / the internally-stored handler.
    bool SwBrandOk(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Value> receiver)
    {
      // GetPrivate yields undefined (not an empty MaybeLocal) when the
      // slot is absent - the constructor stores v8::True, so require it.
      v8::Local<v8::Value> brand;
      return !receiver.IsEmpty() && receiver->IsObject() &&
             receiver.As<v8::Object>()
                 ->GetPrivate(context, WrSharedInstKey(isolate))
                 .ToLocal(&brand) &&
             brand->IsTrue();
    }

    void SwPortGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!SwBrandOk(isolate, context, info.This()))
      {
        isolate->ThrowException(v8::Exception::TypeError(
            ToV8Str(isolate, "Illegal invocation")));
        return;
      }
      info.GetReturnValue().Set(v8::Null(isolate));
    }

    void SwOnErrorGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!SwBrandOk(isolate, context, info.This()))
      {
        isolate->ThrowException(v8::Exception::TypeError(
            ToV8Str(isolate, "Illegal invocation")));
        return;
      }
      v8::Local<v8::Value> handler;
      if (!info.This()->GetPrivate(context, WrSharedOnErrorKey(isolate))
               .ToLocal(&handler) ||
          handler.IsEmpty() || handler->IsUndefined())
        handler = v8::Null(isolate);
      info.GetReturnValue().Set(handler);
    }

    void SwOnErrorSetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!SwBrandOk(isolate, context, info.This()))
      {
        isolate->ThrowException(v8::Exception::TypeError(
            ToV8Str(isolate, "Illegal invocation")));
        return;
      }
      v8::Local<v8::Value> handler = info.Length() >= 1
          ? info[0]
          : v8::Local<v8::Value>(v8::Null(isolate));
      (void)info.This()
          ->SetPrivate(context, WrSharedOnErrorKey(isolate), handler)
          .FromMaybe(false);
    }

    void WorkerAddEventListener(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2 || !info[1]->IsFunction()) return;
      std::string type = ToUtf8(isolate, info[0]);
      v8::Local<v8::Value> listeners;
      if (!info.This()
               ->GetPrivate(context, WrListenersKey(isolate))
               .ToLocal(&listeners) ||
          !listeners->IsObject())
        return;
      v8::Local<v8::Object> map = listeners.As<v8::Object>();
      v8::Local<v8::String> key = ToV8Str(isolate, type);
      v8::Local<v8::Value> bucket;
      if (!map->Get(context, key).ToLocal(&bucket) || !bucket->IsArray())
      {
        bucket = v8::Array::New(isolate);
        (void)map->Set(context, key, bucket).FromMaybe(false);
      }
      v8::Local<v8::Array> callbacks = bucket.As<v8::Array>();
      for (uint32_t i = 0; i < callbacks->Length(); i++)
      {
        v8::Local<v8::Value> callback;
        if (callbacks->Get(context, i).ToLocal(&callback) &&
            callback->SameValue(info[1]))
          return;
      }
      (void)callbacks->Set(context, callbacks->Length(), info[1])
          .FromMaybe(false);
    }

    void WorkerRemoveEventListener(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2 || !info[1]->IsFunction()) return;
      std::string type = ToUtf8(isolate, info[0]);
      v8::Local<v8::Value> listeners;
      if (!info.This()
               ->GetPrivate(context, WrListenersKey(isolate))
               .ToLocal(&listeners) ||
          !listeners->IsObject())
        return;
      v8::Local<v8::Object> map = listeners.As<v8::Object>();
      v8::Local<v8::Value> bucket;
      if (!map->Get(context, ToV8Str(isolate, type)).ToLocal(&bucket) ||
          !bucket->IsArray())
        return;
      v8::Local<v8::Array> callbacks = bucket.As<v8::Array>();
      for (uint32_t i = 0; i < callbacks->Length(); i++)
      {
        v8::Local<v8::Value> callback;
        if (callbacks->Get(context, i).ToLocal(&callback) &&
            callback->SameValue(info[1]))
        {
          (void)callbacks->Delete(context, i).FromMaybe(false);
          return;
        }
      }
    }

    void WorkerDispatchEvent(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsObject()) return;
      v8::Local<v8::Value> type;
      if (!info[0].As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "type"))
               .ToLocal(&type) ||
          !type->IsString())
        return;
      WrDispatchByType(isolate, context, info.This(), ToUtf8(isolate, type),
                       info[0]);
      info.GetReturnValue().Set(v8::Boolean::New(isolate, true));
    }

    void MessagePortShimConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->portshim_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: MessagePortShim constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->portshim_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, WrListenersKey(isolate),
                             v8::Object::New(isolate))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "_onmessage"),
                      v8::Null(isolate))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void MessagePortShimStart(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
    }

    void MessagePortShimClose(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
    }

    void MessagePortShimPostMessage(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> message =
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
      v8::Local<v8::Value> cloned = WrStructuredish(isolate, context, message);
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 2);
      (void)payload->Set(context, 0, info.This()).FromMaybe(false);
      (void)payload->Set(context, 1, cloned).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> data = args.Data().As<v8::Array>();
            v8::Local<v8::Value> receiver, cloned_value;
            if (!data->Get(ctx, 0).ToLocal(&receiver) ||
                !receiver->IsObject() ||
                !data->Get(ctx, 1).ToLocal(&cloned_value))
              return;
            WrDispatchMessage(iso, ctx, receiver.As<v8::Object>(),
                              cloned_value);
          },
          payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      WrQueueTask(isolate, context, runner);
    }

    void InstallWorkers(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        ContextState* state)
    {
      if (state->worker_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, WorkerConstructor);
        ctor->SetClassName(ToV8Str(isolate, "Worker"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M5: no instance-template tag - the reference carries zero own
        // symbols on instances; Worker.prototype owns the tag.
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        // M5: install order is observable (gOPN order) - the reference
        // lists terminate before postMessage; the event methods are
        // moved to EventTarget.prototype by the element-hierarchy pass.
        method("terminate", WorkerTerminate);
        method("postMessage", WorkerPostMessage);
        method("addEventListener", WorkerAddEventListener);
        method("removeEventListener", WorkerRemoveEventListener);
        method("dispatchEvent", WorkerDispatchEvent);
        state->worker_tpl.Reset(isolate, ctor);
        v8::Local<v8::Function> worker_fn =
            ctor->GetFunction(context).ToLocalChecked();
        // M5: reference arity is 1 (page scripts read Worker.length).
        (void)worker_fn
            ->DefineOwnProperty(context, ToV8Str(isolate, "length"),
                                v8::Integer::New(isolate, 1),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "Worker"),
                                worker_fn,
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      if (state->sharedworker_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, SharedWorkerConstructor);
        ctor->SetClassName(ToV8Str(isolate, "SharedWorker"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M5: no instance tag (zero own symbols, like the reference) and
        // the port/onerror accessors live on the prototype - brand-checked
        // (receiver must be a constructed SharedWorker, else the reference
        // throws TypeError 'Illegal invocation'); port reads back null.
        auto sw_getter = [&](const char* name, v8::FunctionCallback cb,
                             bool with_set, v8::FunctionCallback set) {
          v8::Local<v8::FunctionTemplate> get_fn =
              v8::FunctionTemplate::New(isolate, cb);
          get_fn->SetClassName(ToV8Str(
              isolate, std::string("get ") + name));
          v8::Local<v8::FunctionTemplate> set_fn;
          if (with_set)
          {
            set_fn = v8::FunctionTemplate::New(isolate, set);
            set_fn->SetClassName(ToV8Str(
                isolate, std::string("set ") + name));
          }
          ctor->PrototypeTemplate()->SetAccessorProperty(
              ToV8Str(isolate, name), get_fn, set_fn);
        };
        sw_getter("port", SwPortGetter, false, nullptr);
        sw_getter("onerror", SwOnErrorGetter, true, SwOnErrorSetter);
        ctor->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate), ToV8Str(isolate, "SharedWorker"),
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::ReadOnly |
                v8::PropertyAttribute::DontEnum));
        state->sharedworker_tpl.Reset(isolate, ctor);
        v8::Local<v8::Function> sw_fn =
            ctor->GetFunction(context).ToLocalChecked();
        (void)sw_fn
            ->DefineOwnProperty(context, ToV8Str(isolate, "length"),
                                v8::Integer::New(isolate, 1),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, "SharedWorker"),
                                sw_fn,
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
      if (state->portshim_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, MessagePortShimConstructor);
        ctor->SetClassName(ToV8Str(isolate, "MessagePortShim"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "MessagePortShim"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        method("start", MessagePortShimStart);
        method("close", MessagePortShimClose);
        method("postMessage", MessagePortShimPostMessage);
        state->portshim_tpl.Reset(isolate, ctor);
        // MessagePortShim was never a global in the reference either
      }
    }

  // Fetch-spec forbidden header names (reference-wheel parity, probed on
  // reference: user-agent/content-type/custom names pass; cookie, referer,
  // origin, dnt, te/trailer/upgrade/via/... and the sec-/proxy- prefixes
  // are silently dropped - a real browser never lets page JS set them).
  // The page SDK sets sec-ch-ua* on every XHR; replaying those headers
  // fails the WAF request fingerprint (G4.221 login-chain ess 400).
  static bool IsForbiddenHeaderName(const std::string &name)
  {
    static const char *const kForbidden[] = {
        "accept-charset", "accept-encoding",
        "access-control-request-headers", "access-control-request-method",
        "connection", "content-length", "cookie", "cookie2", "date", "dnt",
        "expect", "host", "keep-alive", "origin", "referer", "set-cookie",
        "te", "trailer", "transfer-encoding", "upgrade", "via"};
    for (const char *k : kForbidden)
      if (name == k)
        return true;
    return name.rfind("proxy-", 0) == 0 || name.rfind("sec-", 0) == 0;
  }

  void InstallNet(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    ContextState* state = EnsureState(isolate, context);
    state->net_log.clear();
    state->pending_network_tasks = 0;

    // shared Blob / File / FileList constructor templates (G4: the
    // reference bootstrap's JS classes moved into native constructors;
    // instances are created from these templates so instanceof holds)
    if (state->blob_tpl.IsEmpty())
    {
      v8::Local<v8::FunctionTemplate> ctor =
          v8::FunctionTemplate::New(isolate, BlobConstructor);
      ctor->SetClassName(ToV8Str(isolate, "Blob"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Blob"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      v8::Local<v8::FunctionTemplate> size_get =
          v8::FunctionTemplate::New(isolate, BlobGetSize);
      size_get->SetClassName(ToV8Str(isolate, "get size"));
      ctor->PrototypeTemplate()->SetAccessorProperty(
          ToV8Str(isolate, "size"), size_get,
          v8::Local<v8::FunctionTemplate>());
      auto blob_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      blob_method("arrayBuffer", BlobArrayBuffer);
      blob_method("text", BlobText);
      blob_method("slice", BlobSlice);
      state->blob_tpl.Reset(isolate, ctor);
    }
    if (state->file_tpl.IsEmpty())
    {
      v8::Local<v8::FunctionTemplate> ctor =
          v8::FunctionTemplate::New(isolate, FileConstructor);
      ctor->SetClassName(ToV8Str(isolate, "File"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "File"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      // File instances must satisfy `instanceof Blob`
      ctor->Inherit(state->blob_tpl.Get(isolate));
      state->file_tpl.Reset(isolate, ctor);
    }
    if (state->file_list_tpl.IsEmpty())
    {
      v8::Local<v8::FunctionTemplate> ctor =
          v8::FunctionTemplate::New(isolate, FileListConstructor);
      ctor->SetClassName(ToV8Str(isolate, "FileList"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "FileList"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      v8::Local<v8::FunctionTemplate> item_fn =
          v8::FunctionTemplate::New(isolate, FileListItem);
      item_fn->SetClassName(ToV8Str(isolate, "item"));
      ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "item"), item_fn);
      state->file_list_tpl.Reset(isolate, ctor);
    }

    InstallHeadersGlobal(isolate, context, state);
    InstallWorkers(isolate, context, state);
    InstallStreams(isolate, context, state);

    // G4 ledger chunk 4: URL / URLSearchParams / URLPattern globals
    auto register_global4 = [&](const char* name,
                                v8::Local<v8::FunctionTemplate> ctor) {
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, name),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    };
    if (state->usp_tpl.IsEmpty())
    {
      v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
          isolate, UspConstructor);
      ctor->SetClassName(ToV8Str(isolate, "URLSearchParams"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "URLSearchParams"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      method("append", UspAppend);
      method("delete", UspDelete);
      method("get", UspGet);
      method("getAll", UspGetAll);
      method("has", UspHas);
      method("set", UspSet);
      method("sort", UspSort);
      method("entries", UspEntries);
      method("keys", UspKeys);
      method("values", UspValues);
      method("forEach", UspForEach);
      method("toString", UspToStringFn);
      ctor->PrototypeTemplate()->Set(v8::Symbol::GetIterator(isolate),
                                     UspIteratorTemplate(isolate));
      state->usp_tpl.Reset(isolate, ctor);
      register_global4("URLSearchParams", ctor);
    }

    if (state->url_tpl.IsEmpty())
    {
      v8::Local<v8::FunctionTemplate> ctor =
          v8::FunctionTemplate::New(isolate, UrlConstructor);
      ctor->SetClassName(ToV8Str(isolate, "URL"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M6: no instance tag; the prototype tag is installed at template
      // level so page scripts (before the JS face tables) also see it -
      // the later JS install re-sets the same descriptor
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "URL"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto access = [&](const char* name, v8::FunctionCallback get,
                        v8::FunctionCallback set) {
        v8::Local<v8::FunctionTemplate> get_fn =
            v8::FunctionTemplate::New(isolate, get);
        get_fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        v8::Local<v8::FunctionTemplate> set_fn =
            v8::FunctionTemplate::New(isolate, set);
        set_fn->SetClassName(ToV8Str(isolate, std::string("set ") + name));
        ctor->PrototypeTemplate()->SetAccessorProperty(
            ToV8Str(isolate, name), get_fn, set_fn);
      };
      auto getter = [&](const char* name, v8::FunctionCallback get) {
        v8::Local<v8::FunctionTemplate> get_fn =
            v8::FunctionTemplate::New(isolate, get);
        get_fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        ctor->PrototypeTemplate()->SetAccessorProperty(
            ToV8Str(isolate, name), get_fn, v8::Local<v8::FunctionTemplate>());
      };
      access("href", UrlGetHref, UrlSetHrefFn);
      access("protocol", UrlGetProtocol, UrlSetProtocol);
      access("host", UrlGetHost, UrlSetHost);
      access("hostname", UrlGetHostname, UrlSetHostname);
      access("port", UrlGetPort, UrlSetPort);
      access("pathname", UrlGetPathname, UrlSetPathname);
      access("search", UrlGetSearch, UrlSetSearch);
      access("hash", UrlGetHash, UrlSetHash);
      getter("origin", UrlGetOrigin);
      getter("searchParams", UrlGetSearchParams);
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      method("toString", UrlToStringFn);
      method("toJSON", UrlToStringFn);
      ctor->Set(isolate, "canParse",
                v8::FunctionTemplate::New(isolate, UrlCanParse),
                v8::PropertyAttribute::DontEnum);
      ctor->Set(isolate, "parse",
                v8::FunctionTemplate::New(isolate, UrlParseStatic),
                v8::PropertyAttribute::DontEnum);
      state->url_tpl.Reset(isolate, ctor);
      register_global4("URL", ctor);
    }

    if (state->urlpattern_tpl.IsEmpty())
    {
      v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
          isolate, UrlPatternConstructor);
      ctor->SetClassName(ToV8Str(isolate, "URLPattern"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "URLPattern"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      method("exec", UrlPatternExec);
      method("test", UrlPatternTest);
      state->urlpattern_tpl.Reset(isolate, ctor);
      register_global4("URLPattern", ctor);
    }
    // G4 ledger chunk 6: native FileReader
    InstallFileReader(isolate, context, state);
    InstallLockManager(isolate, context, state);
    InstallWebMidiFamily(isolate, context, state);
    InstallImageBitmap(isolate, context, state);
    InstallRequest(isolate, context, state);

    // global Blob / File / FileList constructors + the JS-side byte
    // escape hatch used by FileReader / object-URL lookup
    {
      v8::Local<v8::Value> ctor =
          state->blob_tpl.Get(isolate)->GetFunction(context).ToLocalChecked();
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "Blob"), ctor,
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    {
      v8::Local<v8::Value> ctor =
          state->file_tpl.Get(isolate)->GetFunction(context).ToLocalChecked();
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "File"), ctor,
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    {
      v8::Local<v8::Value> ctor = state->file_list_tpl.Get(isolate)
                                      ->GetFunction(context)
                                      .ToLocalChecked();
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "FileList"), ctor,
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    {
      v8::Local<v8::FunctionTemplate> helper =
          v8::FunctionTemplate::New(isolate, BlobBytesHelper);
      helper->SetClassName(ToV8Str(isolate, "__lv8_blob_bytes"));
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "__lv8_blob_bytes"),
                              helper->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // global FormData constructor (shared template with Response.formData)
    {
      (void)MakeFormDataImpl(isolate, context);
      v8::Local<v8::FunctionTemplate> tpl =
          v8::FunctionTemplate::New(isolate, FormDataConstructor);
      tpl->SetClassName(ToV8Str(isolate, "FormData"));
      tpl->Inherit(state->fd_tpl.Get(isolate));
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "FormData"),
                              tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // global fetch
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
          isolate, GlobalFetch);
      tpl->SetClassName(ToV8Str(isolate, "fetch"));
      // G4.393l: native-method form - the reference exposes fetch without a
      // .prototype property.
      tpl->RemovePrototype();
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "fetch"),
                              tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // global caches
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "CacheStorage"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "CacheStorage"),
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
      method("open", CacheStorageOpen);
      method("match", CacheStorageMatch);
      method("keys", CacheStorageKeys);
      v8::Local<v8::Object> caches =
          inst->NewInstance(context).ToLocalChecked();
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "caches"), caches,
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // global performance (resource timings only)
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "Performance"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Performance"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      {
        v8::Local<v8::String> key =
            ToV8Str(isolate, "getEntriesByType");
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, PerformanceGetEntriesByType);
        fn->SetClassName(key);
        fn->SetLength(1);        // G4.251: reference arity
        tpl->PrototypeTemplate()->Set(key, fn);
      }
      {
        auto perf_method = [&](const char* name, v8::FunctionCallback cb,
                               int length) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          fn->SetLength(length);   // G4.251: reference arity
          tpl->PrototypeTemplate()->Set(key, fn);
        };
        perf_method("mark", PerfMark, 1);
        perf_method("measure", PerfMeasure, 1);
        perf_method("getEntries", PerfGetEntries, 0);
        perf_method("getEntriesByName", PerfGetEntriesByName, 1);
        {
          v8::Local<v8::String> key = ToV8Str(isolate, "clearMarks");
          v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
              isolate, PerfClear, ToV8Str(isolate, "mark"));
          fn->SetClassName(key);
          tpl->PrototypeTemplate()->Set(key, fn);
        }
        {
          v8::Local<v8::String> key = ToV8Str(isolate, "clearMeasures");
          v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
              isolate, PerfClear, ToV8Str(isolate, "measure"));
          fn->SetClassName(key);
          tpl->PrototypeTemplate()->Set(key, fn);
        }
      }
      // now()/timeOrigin ride as own accessor properties over the
      // performance object (logical clock driven)
      {
        v8::Local<v8::FunctionTemplate> now_tpl = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              ContextState* st = GetState(ctx);
              if (st != nullptr && (st->system_time || st->perf_real))
              {
                // reference wheel: performance.now advances at microsecond
                // scale inside synchronous loops (QPC-backed); an
                // ms-truncated clock freezes instead and the challenge's
                // encoded timing values diverge
                double now_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    st->system_origin_steady)
                                    .count();
                // G4.375: reference's autoAdvanceStep (wheel knob, default
                // 0.001ms - measured 50000 perf reads = +51ms in
                // _g375_clock_probe.json) makes the page's own polling
                // drive the shared timer clock; without it the delayed
                // DOM cleanup never becomes due in system mode (rc 6 vs
                // 7, headChildren 92 vs 91).  The returned value stays
                // real steady time, like the wheel's.
                if (st->system_time && st->perf_advance_step > 0.0)
                  st->clock_ms += st->perf_advance_step;
                info.GetReturnValue().Set(v8::Number::New(iso, now_ms));
                return;
              }
              if (st != nullptr && st->perf_advance_step > 0.0)
                st->clock_ms += st->perf_advance_step;
              info.GetReturnValue().Set(
                  v8::Number::New(iso, LogicalClock(iso, ctx)));
            });
        now_tpl->SetClassName(ToV8Str(isolate, "now"));
        tpl->PrototypeTemplate()->Set(ToV8Str(isolate, "now"), now_tpl);
      }
      {
        v8::Local<v8::FunctionTemplate> origin_tpl =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  ContextState* st = GetState(ctx);
                  double origin = st != nullptr
                          ? st->system_origin_ms_precise
                          : 0.0;
                  info.GetReturnValue().Set(v8::Number::New(iso, origin));
                });
        origin_tpl->SetClassName(ToV8Str(isolate, "get timeOrigin"));
        inst->SetAccessorProperty(ToV8Str(isolate, "timeOrigin"), origin_tpl,
                                  v8::Local<v8::FunctionTemplate>());
      }
      v8::Local<v8::Object> performance =
          inst->NewInstance(context).ToLocalChecked();
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "performance"),
                              performance,
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // global PerformanceObserver: microtask delivery per recorded entry
    {
      v8::Local<v8::FunctionTemplate> ctor =
          v8::FunctionTemplate::New(isolate, PerfObserverCtor);
      ctor->SetClassName(ToV8Str(isolate, "PerformanceObserver"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "PerformanceObserver"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto po_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      po_method("observe", PerfObserverObserve);
      po_method("disconnect", PerfObserverDisconnect);
      po_method("takeRecords", PerfObserverTakeRecords);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "PerformanceObserver"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // global Response constructor: new Response(body) stores the text
    // payload so text()/json() read it back (buckets/caches construct
    // cached responses this way)
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            std::string body = info.Length() >= 1 && !info[0]->IsUndefined()
                                   ? ToUtf8(iso, info[0])
                                   : std::string();
            (void)info.This()
                ->SetPrivate(ctx, RespKey(iso, "lv8.resp.body"),
                             ToV8Str(iso, body))
                .FromMaybe(false);
            int status = 200;
            v8::Local<v8::Value> seed_headers = v8::Undefined(iso);
            if (info.Length() >= 2 && info[1]->IsObject())
            {
              v8::Local<v8::Object> init = info[1].As<v8::Object>();
              v8::Local<v8::Value> status_v;
              if (init->Get(ctx, ToV8Str(iso, "status"))
                      .ToLocal(&status_v) &&
                  status_v->IsNumber())
                status = static_cast<int>(
                    status_v->NumberValue(ctx).FromMaybe(200));
              v8::Local<v8::Value> init_headers;
              if (init->Get(ctx, ToV8Str(iso, "headers"))
                      .ToLocal(&init_headers))
                seed_headers = init_headers;
            }
            (void)info.This()
                ->SetPrivate(ctx, RespKey(iso, "lv8.resp.status"),
                             v8::Integer::New(iso, status))
                .FromMaybe(false);
            // headers always surface as a Headers instance so factories
            // like Response.redirect can set location on them
            {
              v8::Local<v8::Value> headers_ctor;
              if (ctx->Global()
                      ->Get(ctx, ToV8Str(iso, "Headers"))
                      .ToLocal(&headers_ctor) &&
                  headers_ctor->IsFunction())
              {
                v8::Local<v8::Value> hargs[1] = {seed_headers};
                v8::Local<v8::Value> headers_obj;
                if (headers_ctor.As<v8::Function>()
                        ->NewInstance(ctx, 1, hargs)
                        .ToLocal(&headers_obj))
                  (void)info.This()
                      ->Set(ctx, ToV8Str(iso, "headers"), headers_obj)
                      .FromMaybe(false);
              }
            }
          });
      tpl->SetClassName(ToV8Str(isolate, "Response"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Response"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      // M11: ok/status are prototype accessors (an instance-template
      // accessor becomes an own property on every Response; reference
      // instances are zero-own - same rule as the XHR face)
      tpl->PrototypeTemplate()->SetAccessorProperty(
          ToV8Str(isolate, "ok"),
          v8::FunctionTemplate::New(isolate, ResponseOk),
          v8::Local<v8::FunctionTemplate>());
      tpl->PrototypeTemplate()->SetAccessorProperty(
          ToV8Str(isolate, "status"),
          v8::FunctionTemplate::New(isolate, ResponseStatus),
          v8::Local<v8::FunctionTemplate>());
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      method("text", ResponseText);
      method("arrayBuffer", ResponseArrayBuffer);
      method("json", ResponseJson);
      method("formData", ResponseFormData);
      auto static_fn = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        tpl->Set(key, fn);
      };
      static_fn("json", ResponseStaticJson);
      static_fn("error", ResponseStaticError);
      static_fn("redirect", ResponseStaticRedirect);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "Response"),
                              tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // global XMLHttpRequest: state machine over the offline bridge
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "XMLHttpRequest"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "XMLHttpRequest"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      // reference parity: XHR accessors live on the prototype (an instance-
      // template accessor becomes an own property on every instance,
      // polluting the own-face the WAF enumerates)
      auto accessor = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        tpl->PrototypeTemplate()->SetAccessorProperty(
            key, fn, v8::Local<v8::FunctionTemplate>());
      };
      accessor("readyState", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Value> v;
        if (info.This()
                ->GetPrivate(iso->GetCurrentContext(),
                             RespKey(iso, "lv8.xhr.ready"))
                .ToLocal(&v) &&
            v->IsInt32())
          info.GetReturnValue().Set(v);
        else
          info.GetReturnValue().Set(0);
      });
      // reference's fresh XHR already answers onprogress with an object
      // (surface-audit xhr probe); keep it off the prototype (reference carries no
      // on* there) by materializing it per instance through a private slot
      accessor("onprogress",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 v8::Local<v8::Private> key =
                     RespKey(iso, "lv8.xhr.onprogress");
                 v8::Local<v8::Value> v;
                 if (info.This()->GetPrivate(ctx, key).ToLocal(&v) &&
                     v->IsObject())
                 {
                   info.GetReturnValue().Set(v);
                   return;
                 }
                 v8::Local<v8::Object> handler = v8::Object::New(iso);
                 (void)info.This()
                     ->SetPrivate(ctx, key, handler)
                     .FromMaybe(false);
                 info.GetReturnValue().Set(handler);
               });
      accessor("status", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Value> v;
        if (info.This()
                ->GetPrivate(iso->GetCurrentContext(),
                             RespKey(iso, "lv8.xhr.status"))
                .ToLocal(&v) &&
            v->IsInt32())
          info.GetReturnValue().Set(v);
        else
          info.GetReturnValue().Set(0);
      });
      {
        v8::Local<v8::String> rt_key = ToV8Str(isolate, "responseType");
        v8::Local<v8::FunctionTemplate> rt_get = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              v8::Local<v8::Value> v;
              if (info.This()
                      ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.rtype"))
                      .ToLocal(&v) &&
                  v->IsString())
                info.GetReturnValue().Set(v);
              else
                info.GetReturnValue().Set(ToV8Str(iso, ""));
            });
        rt_get->SetClassName(ToV8Str(isolate, "get responseType"));
        v8::Local<v8::FunctionTemplate> rt_set = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              if (info.Length() < 1 || !info[0]->IsString()) return;
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              std::string value = ToUtf8(iso, info[0]);
              const bool supported = value.empty() || value == "text" ||
                  value == "json" || value == "arraybuffer" ||
                  value == "blob";
              if (!supported)
              {
                iso->ThrowException(
                    ToV8Str(iso, "Error: unsupported responseType: " + value));
                return;
              }
              (void)info.This()
                  ->SetPrivate(ctx, RespKey(iso, "lv8.xhr.rtype"), info[0])
                  .FromMaybe(false);
            });
        rt_set->SetClassName(ToV8Str(isolate, "set responseType"));
        // reference parity: prototype accessor (own-face clean)
        tpl->PrototypeTemplate()->SetAccessorProperty(rt_key, rt_get, rt_set);
      }
      accessor("responseText",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 v8::Local<v8::Value> type;
                 if (info.This()
                         ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.rtype"))
                         .ToLocal(&type) &&
                     type->IsString())
                 {
                   std::string t = ToUtf8(iso, type);
                   if (t == "json" || t == "arraybuffer" || t == "blob")
                   {
                     v8::Local<v8::Object> err = v8::Exception::TypeError(
                         ToV8Str(iso,
                                 "responseText is only available if "
                                 "responseType is '' or 'text'"))
                         .As<v8::Object>();
                     (void)err->Set(ctx, ToV8Str(iso, "name"),
                                    ToV8Str(iso, "InvalidStateError"))
                         .FromMaybe(false);
                     iso->ThrowException(err);
                     return;
                   }
                 }
                 v8::Local<v8::Value> v;
                 if (info.This()
                         ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.text"))
                         .ToLocal(&v) &&
                     v->IsString())
                   info.GetReturnValue().Set(v);
                 else
                   info.GetReturnValue().SetEmptyString();
               });
      accessor("response",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 std::string type;
                 v8::Local<v8::Value> type_v;
                 if (info.This()
                         ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.rtype"))
                         .ToLocal(&type_v) &&
                     type_v->IsString())
                   type = ToUtf8(iso, type_v);
                 if (type == "arraybuffer" || type == "blob")
                 {
                   v8::Local<v8::Value> bytes;
                   if (info.This()
                           ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.bytes"))
                           .ToLocal(&bytes) &&
                       bytes->IsArrayBuffer())
                     info.GetReturnValue().Set(bytes);
                   else
                     info.GetReturnValue().SetNull();
                   return;
                 }
                 if (type == "json")
                 {
                   v8::Local<v8::Value> text;
                   if (info.This()
                           ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.text"))
                           .ToLocal(&text) &&
                       text->IsString())
                   {
                     v8::Local<v8::Value> parsed;
                     if (v8::JSON::Parse(ctx, text.As<v8::String>())
                             .ToLocal(&parsed))
                       info.GetReturnValue().Set(parsed);
                     else
                       info.GetReturnValue().SetNull();
                   }
                   else
                   {
                     info.GetReturnValue().SetNull();
                   }
                   return;
                 }
                 v8::Local<v8::Value> v;
                 if (info.This()
                         ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.text"))
                         .ToLocal(&v) &&
                     v->IsString())
                   info.GetReturnValue().Set(v);
                 else
                   info.GetReturnValue().SetEmptyString();
               });
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      // open(method, url): remember the request, readyState -> 1
      method("open", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString())
          return;
        std::string method_name = ToUtf8(iso, info[0]);
        for (auto& c : method_name)
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::string url = ResolveUrlString(iso, ctx, ToUtf8(iso, info[1]));
        if (url.empty())
          url = ToUtf8(iso, info[1]);
        (void)info.This()
            ->SetPrivate(ctx, RespKey(iso, "lv8.xhr.method"),
                         ToV8Str(iso, method_name))
            .FromMaybe(false);
        (void)info.This()
            ->SetPrivate(ctx, RespKey(iso, "lv8.xhr.url"), ToV8Str(iso, url))
            .FromMaybe(false);
        (void)info.This()
            ->SetPrivate(ctx, RespKey(iso, "lv8.xhr.ready"),
                         v8::Integer::New(iso, 1))
            .FromMaybe(false);
      });
      // setRequestHeader(name, value): lowercase, accumulate
      method("setRequestHeader",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               if (info.Length() < 2 || !info[0]->IsString() ||
                   !info[1]->IsString())
                 return;
               std::string name = ToLower(ToUtf8(iso, info[0]));
               std::string value = ToUtf8(iso, info[1]);
               if (IsForbiddenHeaderName(name))
                 return;
               v8::Local<v8::Value> raw;
               v8::Local<v8::Array> list;
               if (info.This()
                       ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.reqheaders"))
                       .ToLocal(&raw) &&
                   raw->IsArray())
                 list = raw.As<v8::Array>();
               else
                 list = v8::Array::New(iso);
               v8::Local<v8::Array> pair = v8::Array::New(iso, 2);
               (void)pair->Set(ctx, 0, ToV8Str(iso, name)).FromMaybe(false);
               (void)pair->Set(ctx, 1, ToV8Str(iso, value)).FromMaybe(false);
               (void)list->Set(ctx, list->Length(), pair).FromMaybe(false);
               (void)info.This()
                   ->SetPrivate(ctx, RespKey(iso, "lv8.xhr.reqheaders"), list)
                   .FromMaybe(false);
             });
      // send(body): netLog + queue the state machine delivery
      method("send", [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* iso = info.GetIsolate();
        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
        ContextState* st = GetState(ctx);
        v8::Local<v8::Value> method_v;
        v8::Local<v8::Value> url_v;
        if (st == nullptr ||
            !info.This()
                 ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.method"))
                 .ToLocal(&method_v) ||
            !method_v->IsString())
          return;
        std::string method_name = ToUtf8(iso, method_v);
        std::string url;
        if (info.This()
                ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.url"))
                .ToLocal(&url_v) &&
            url_v->IsString())
          url = ToUtf8(iso, url_v);
        // request headers: script-set names only (reference-wheel parity -
        // reference's XHR netLog entries carry exactly the headers the page set;
        // the profile-derived sec-ch-* defaults stay on the navigation/fetch
        // paths, where a real browser's network stack does add them).  The
        // login-chain replay must not send headers the SDK never signed.
        HeaderList headers;
        v8::Local<v8::Value> raw;
        if (info.This()
                ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.reqheaders"))
                .ToLocal(&raw) &&
            raw->IsArray())
        {
          v8::Local<v8::Array> list = raw.As<v8::Array>();
          for (uint32_t i = 0; i < list->Length(); i++)
          {
            v8::Local<v8::Value> pair =
                list->Get(ctx, i).FromMaybe(v8::Local<v8::Value>());
            if (!pair->IsArray())
              continue;
            v8::Local<v8::Value> k =
                pair.As<v8::Array>()->Get(ctx, 0).FromMaybe(
                    v8::Local<v8::Value>());
            v8::Local<v8::Value> v =
                pair.As<v8::Array>()->Get(ctx, 1).FromMaybe(
                    v8::Local<v8::Value>());
            if (!k->IsString() || !v->IsString())
              continue;
            std::string name = ToLower(ToUtf8(iso, k));
            bool replaced = false;
            for (auto& h : headers)
            {
              if (h.first == name)
              {
                h.second = ToUtf8(iso, v);
                replaced = true;
                break;
              }
            }
            if (!replaced)
              headers.push_back({name, ToUtf8(iso, v)});
          }
        }
        ContextState::NetEntry entry;
        entry.method = method_name;
        entry.url = url;
        entry.headers = headers;
        entry.cookie_header = GetCookieString(iso, ctx);
        entry.has_body = info.Length() >= 1 && !info[0]->IsUndefined();
        entry.body = entry.has_body ? ToUtf8(iso, info[0]) : "";
        AppendNetEntry(iso, ctx, st, entry);
        st->pending_network_tasks++;

        const ContextState::ResourceEntry* resource = FindResourceImpl(st, url);
        ContextState::PendingDelivery pending;
        pending.xhr.Reset(iso, info.This());
        if (resource == nullptr && st->bridge_enabled)
        {
          pending.bridged = true;
          pending.method = method_name;
          pending.url = url;
          pending.request_headers = headers;
          pending.cookie_header = entry.cookie_header;
          pending.has_request_body = entry.has_body;
          pending.request_body = entry.body;
          pending.bridge_initiator = "xhr";
        }
        else
        {
          pending.status = resource != nullptr ? resource->status : 404;
          pending.headers =
              resource != nullptr ? resource->headers : HeaderList{};
          pending.body = resource != nullptr ? resource->body : "";
        }
        st->pending_responses.push_back(std::move(pending));
      });
      // getResponseHeader(name): case-insensitive response header lookup
      method("getResponseHeader",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               if (info.Length() < 1 || !info[0]->IsString())
               {
                 info.GetReturnValue().SetNull();
                 return;
               }
               std::string wanted = ToLower(ToUtf8(iso, info[0]));
               v8::Local<v8::Value> raw;
               if (info.This()
                       ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.headers"))
                       .ToLocal(&raw) &&
                   raw->IsArray())
               {
                 v8::Local<v8::Array> list = raw.As<v8::Array>();
                 for (uint32_t i = 0; i < list->Length(); i++)
                 {
                   v8::Local<v8::Value> pair =
                       list->Get(ctx, i).FromMaybe(v8::Local<v8::Value>());
                   if (!pair->IsArray())
                     continue;
                   v8::Local<v8::Value> k = pair.As<v8::Array>()
                                                ->Get(ctx, 0)
                                                .FromMaybe(
                                                    v8::Local<v8::Value>());
                   if (k->IsString() && ToLower(ToUtf8(iso, k)) == wanted)
                   {
                     info.GetReturnValue().Set(
                         pair.As<v8::Array>()
                             ->Get(ctx, 1)
                             .FromMaybe(v8::Local<v8::Value>()));
                     return;
                   }
                 }
               }
               info.GetReturnValue().SetNull();
             });
      method("getAllResponseHeaders",
             [](const v8::FunctionCallbackInfo<v8::Value>& info) {
               v8::Isolate* iso = info.GetIsolate();
               v8::Local<v8::Context> ctx = iso->GetCurrentContext();
               std::string out;
               v8::Local<v8::Value> raw;
               if (info.This()
                       ->GetPrivate(ctx, RespKey(iso, "lv8.xhr.headers"))
                       .ToLocal(&raw) &&
                   raw->IsObject())
               {
                 v8::Local<v8::Array> names = raw.As<v8::Object>()
                                                  ->GetOwnPropertyNames(ctx)
                                                  .ToLocalChecked();
                 for (uint32_t i = 0; i < names->Length(); i++)
                 {
                   v8::Local<v8::Value> k = names->Get(ctx, i).FromMaybe(
                       v8::Local<v8::Value>());
                   v8::Local<v8::Value> v;
                   if (!k->IsString())
                     continue;
                   (void)raw.As<v8::Object>()->Get(ctx, k).ToLocal(&v);
                   if (!out.empty())
                     out += "\r\n";
                   out += ToUtf8(iso, k) + ": " + ToUtf8(iso, v);
                 }
               }
               info.GetReturnValue().Set(ToV8Str(iso, out));
             });
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "XMLHttpRequest"),
                              tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // navigator.sendBeacon: offline POST that only records the request
    {
      v8::Local<v8::Value> navigator;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "navigator"))
              .ToLocal(&navigator) &&
          navigator->IsObject())
      {
        v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              ContextState* st = GetState(ctx);
              if (st == nullptr || info.Length() < 1 ||
                  !info[0]->IsString())
              {
                info.GetReturnValue().Set(false);
                return;
              }
              std::string url =
                  ResolveUrlString(iso, ctx, ToUtf8(iso, info[0]));
              if (url.empty())
              {
                info.GetReturnValue().Set(false);
                return;
              }
              ContextState::NetEntry entry;
              entry.method = "POST";
              entry.url = url;
              entry.headers = DefaultRequestHeaders(iso, ctx, st);
              entry.cookie_header = GetCookieString(iso, ctx);
              AppendNetEntry(iso, ctx, st, entry);
              info.GetReturnValue().Set(true);
            });
        tpl->SetClassName(ToV8Str(isolate, "sendBeacon"));
        (void)navigator.As<v8::Object>()
            ->DefineOwnProperty(context, ToV8Str(isolate, "sendBeacon"),
                                tpl->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
    }

    // __lv8__.netLog: the control surface may already exist (monitor);
    // create it when absent so standalone installs still expose the log
    v8::Local<v8::Object> control;
    v8::Local<v8::Value> existing;
    if (!context->Global()
                      ->Get(context, ToV8Str(isolate, "__lv8__"))
                      .ToLocal(&existing) ||
        !existing->IsObject())
    {
      control = v8::Object::New(isolate);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "__lv8__"), control,
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    else
    {
      control = existing.As<v8::Object>();
    }
    v8::Local<v8::Object> netlog = v8::Object::New(isolate);
    state->netlog_object.Reset(isolate, netlog);
    RefreshNetLog(isolate, context, state);
    (void)control
        ->DefineOwnProperty(context, ToV8Str(isolate, "netLog"), netlog,
                            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
        .FromMaybe(false);
  }
  // ------------------------------------ offline declarations + clipboard

  bool LastEvalWasPromise(v8::Isolate* isolate,
                          v8::Local<v8::Context> context)
  {
    ContextState* state = GetState(context);
    return state != nullptr && state->last_eval_was_promise;
  }

  void SetLastEvalWasPromise(v8::Isolate* isolate,
                             v8::Local<v8::Context> context, bool value)
  {
    ContextState* state = GetState(context);
    if (state != nullptr)
      state->last_eval_was_promise = value;
  }

  void InstallBrowserChrome(v8::Isolate* isolate,
                            v8::Local<v8::Context> context)
  {
    ContextState* state = EnsureState(isolate, context);
    v8::Local<v8::Object> profile = state->profile_data.IsEmpty()
                                        ? v8::Object::New(isolate)
                                        : state->profile_data.Get(isolate);

    // window.chrome: app/csi/loadTimes with profile-overridable values
    {
      v8::Local<v8::Object> chrome = v8::Object::New(isolate);

      // chrome.app
      v8::Local<v8::Object> app = v8::Object::New(isolate);
      (void)app
          ->Set(context, ToV8Str(isolate, "isInstalled"),
                v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      auto app_method = [&](const char* name, const char* value) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::Value> payload =
            v8::External::New(isolate, const_cast<char*>(value));
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  info.GetReturnValue().Set(ToV8Str(
                      info.GetIsolate(),
                      static_cast<const char*>(
                          info.Data()
                              .As<v8::External>()
                              ->Value())));
                }, payload);
        fn->SetClassName(key);
        (void)app
            ->Set(context, key,
                  fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      };
      app_method("installState", "not_installed");
      app_method("runningState", "cannot_run");
      (void)chrome->Set(context, ToV8Str(isolate, "app"), app)
          .FromMaybe(false);

      // chrome.csi(): {tran: 15, startE, onloadT, pageT}
      {
        v8::Local<v8::FunctionTemplate> csi_tpl =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  v8::Local<v8::Object> csi = v8::Object::New(iso);
                  (void)csi->Set(ctx, ToV8Str(iso, "tran"),
                                 v8::Integer::New(iso, 15))
                      .FromMaybe(false);
                  (void)csi->Set(ctx, ToV8Str(iso, "startE"),
                                 v8::Number::New(iso, 1.7e12))
                      .FromMaybe(false);
                  (void)csi->Set(ctx, ToV8Str(iso, "onloadT"),
                                 v8::Number::New(iso, 1.7e12))
                      .FromMaybe(false);
                  (void)csi->Set(ctx, ToV8Str(iso, "pageT"),
                                 v8::Number::New(iso, 0))
                      .FromMaybe(false);
                  info.GetReturnValue().Set(csi);
                });
        csi_tpl->SetClassName(ToV8Str(isolate, "csi"));
        (void)chrome
            ->Set(context, ToV8Str(isolate, "csi"),
                  csi_tpl->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      }

      // chrome.loadTimes(): defaults from the profile chrome section
      {
        v8::Local<v8::Value> profile_data = profile;
        v8::Local<v8::FunctionTemplate> lt_tpl =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  v8::Local<v8::Object> times = v8::Object::New(iso);
                  auto set_str = [&](const char* key, const char* value) {
                    (void)times
                        ->Set(ctx, ToV8Str(iso, key), ToV8Str(iso, value))
                        .FromMaybe(false);
                  };
                  auto set_num = [&](const char* key, double value) {
                    (void)times
                        ->Set(ctx, ToV8Str(iso, key),
                              v8::Number::New(iso, value))
                        .FromMaybe(false);
                  };
                  auto set_bool = [&](const char* key, bool value) {
                    (void)times
                        ->Set(ctx, ToV8Str(iso, key),
                              v8::Boolean::New(iso, value))
                        .FromMaybe(false);
                  };
                  set_str("navigationType", "Other");
                  set_str("connectionInfo", "h2");
                  set_str("npnNegotiatedProtocol", "h2");
                  set_bool("wasAlternateProtocolAvailable", false);
                  set_bool("wasFetchedViaSpdy", false);
                  set_bool("wasNpnNegotiated", true);
                  set_num("requestTime", 100.5);
                  set_num("finishLoadTime", 200.25);
                  set_num("firstPaintTime", 150.5);
                  set_num("startLoadTime", 100.5);
                  set_num("commitLoadTime", 120.25);
                  // profile chrome.loadTimes.* overrides (connectionInfo,
                  // navigationType, ...)
                  v8::Local<v8::Value> profile_v = info.Data();
                  v8::Local<v8::Object> profile =
                      profile_v->IsObject()
                          ? profile_v.As<v8::Object>()
                          : v8::Local<v8::Object>();
                  if (!profile.IsEmpty())
                  {
                    v8::Local<v8::Value> chrome_v;
                    if (profile
                            ->Get(ctx, ToV8Str(iso, "chrome"))
                            .ToLocal(&chrome_v) &&
                        chrome_v->IsObject())
                    {
                      v8::Local<v8::Value> lt;
                      if (chrome_v.As<v8::Object>()
                              ->Get(ctx, ToV8Str(iso, "loadTimes"))
                              .ToLocal(&lt) &&
                          lt->IsObject())
                      {
                        v8::Local<v8::Array> names = lt.As<v8::Object>()
                                                         ->GetOwnPropertyNames(
                                                             ctx)
                                                         .ToLocalChecked();
                        for (uint32_t i = 0; i < names->Length(); i++)
                        {
                          v8::Local<v8::Value> k = names->Get(ctx, i)
                                                       .FromMaybe(
                                                           v8::Local<
                                                               v8::Value>());
                          v8::Local<v8::Value> v;
                          if (k->IsString() &&
                              lt.As<v8::Object>()
                                  ->Get(ctx, k)
                                  .ToLocal(&v))
                            (void)times->Set(ctx, k, v).FromMaybe(false);
                        }
                      }
                    }
                  }
                  info.GetReturnValue().Set(times);
                }, profile_data);
        lt_tpl->SetClassName(ToV8Str(isolate, "loadTimes"));
        (void)chrome
            ->Set(context, ToV8Str(isolate, "loadTimes"),
                  lt_tpl->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      }

      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "chrome"), chrome,
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // document.hasFocus()
    {
      v8::Local<v8::Value> document;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "document"))
              .ToLocal(&document) &&
          document->IsObject())
      {
        v8::Local<v8::String> key = ToV8Str(isolate, "hasFocus");
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  info.GetReturnValue().Set(true);
                });
        fn->SetClassName(key);
        (void)document.As<v8::Object>()
            ->DefineOwnProperty(context, key,
                                fn->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
    }

    // navigator.getGamepads() -> [] (frozen oracle contract; Chrome ships
    // [null x4] but the 125-item oracle pins the empty list)
    {
      v8::Local<v8::String> key = ToV8Str(isolate, "getGamepads");
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                info.GetReturnValue().Set(
                    v8::Array::New(info.GetIsolate()));
              });
      fn->SetClassName(key);
      v8::Local<v8::Value> navigator;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "navigator"))
              .ToLocal(&navigator) &&
          navigator->IsObject())
      {
        (void)navigator.As<v8::Object>()
            ->DefineOwnProperty(context, key,
                                fn->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      }
    }

    // speechSynthesis: two deterministic voices, idle state, speak()
    // fires utterance.onstart/onend synchronously
    {
      v8::Local<v8::FunctionTemplate> utter_tpl =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                if (info.Length() >= 1 && !info[0]->IsUndefined())
                  (void)info.This()
                      ->Set(ctx, ToV8Str(iso, "text"), info[0])
                      .FromMaybe(false);
              });
      utter_tpl->SetClassName(ToV8Str(isolate, "SpeechSynthesisUtterance"));
      v8::Local<v8::ObjectTemplate> utter_inst =
          utter_tpl->InstanceTemplate();
      utter_inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      utter_tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "SpeechSynthesisUtterance"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      (void)context
          ->Global()
          ->DefineOwnProperty(
              context, ToV8Str(isolate, "SpeechSynthesisUtterance"),
              utter_tpl->GetFunction(context).ToLocalChecked(),
              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);

      v8::Local<v8::FunctionTemplate> synth_tpl =
          v8::FunctionTemplate::New(isolate);
      synth_tpl->SetClassName(ToV8Str(isolate, "SpeechSynthesis"));
      v8::Local<v8::ObjectTemplate> synth_inst =
          synth_tpl->InstanceTemplate();
      synth_inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      synth_tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "SpeechSynthesis"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto synth_accessor = [&](const char* name, bool value) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::Value> payload =
            v8::External::New(isolate,
                              reinterpret_cast<void*>(
                                  static_cast<intptr_t>(value ? 1 : 0)));
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  bool value =
                      info.Data()->IsExternal() &&
                      reinterpret_cast<intptr_t>(
                          info.Data().As<v8::External>()->Value()) == 1;
                  info.GetReturnValue().Set(
                      v8::Boolean::New(info.GetIsolate(), value));
                }, payload);
        fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        synth_inst->SetAccessorProperty(key, fn,
                                        v8::Local<v8::FunctionTemplate>());
      };
      synth_accessor("pending", false);
      synth_accessor("speaking", false);
      synth_accessor("paused", false);
      {
        v8::Local<v8::String> voices_key = ToV8Str(isolate, "voices");
        v8::Local<v8::FunctionTemplate> voices_fn =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  v8::Local<v8::Array> voices = v8::Array::New(iso);
                  auto add_voice = [&](uint32_t index, const char* name,
                                       bool default_voice) {
                    v8::Local<v8::Object> voice = v8::Object::New(iso);
                    (void)voice
                        ->Set(ctx, ToV8Str(iso, "default"),
                              v8::Boolean::New(iso, default_voice))
                        .FromMaybe(false);
                    (void)voice
                        ->Set(ctx, ToV8Str(iso, "name"), ToV8Str(iso, name))
                        .FromMaybe(false);
                    (void)voice
                        ->Set(ctx, ToV8Str(iso, "lang"),
                              ToV8Str(iso, "en-US"))
                        .FromMaybe(false);
                    (void)voices->Set(ctx, index, voice).FromMaybe(false);
                  };
                  add_voice(0,
                            "Microsoft David - English (United States)",
                            true);
                  add_voice(1,
                            "Microsoft Zira - English (United States)",
                            false);
                  info.GetReturnValue().Set(voices);
                });
        voices_fn->SetClassName(ToV8Str(isolate, "get voices"));
        synth_inst->SetAccessorProperty(voices_key, voices_fn,
                                        v8::Local<v8::FunctionTemplate>());
      }
      auto synth_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        synth_tpl->PrototypeTemplate()->Set(key, fn);
      };
      synth_method("getVoices",
                   [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                     v8::Isolate* iso = info.GetIsolate();
                     v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                     v8::Local<v8::Array> voices = v8::Array::New(iso);
                     auto add_voice = [&](uint32_t index, const char* name,
                                          bool default_voice) {
                       v8::Local<v8::Object> voice = v8::Object::New(iso);
                       (void)voice
                           ->Set(ctx, ToV8Str(iso, "default"),
                                 v8::Boolean::New(iso, default_voice))
                           .FromMaybe(false);
                       (void)voice
                           ->Set(ctx, ToV8Str(iso, "name"),
                                 ToV8Str(iso, name))
                           .FromMaybe(false);
                       (void)voice
                           ->Set(ctx, ToV8Str(iso, "lang"),
                                 ToV8Str(iso, "en-US"))
                           .FromMaybe(false);
                       (void)voices->Set(ctx, index, voice)
                           .FromMaybe(false);
                     };
                     add_voice(0,
                               "Microsoft David - English (United States)",
                               true);
                     add_voice(1,
                               "Microsoft Zira - English (United States)",
                               false);
                     info.GetReturnValue().Set(voices);
                   });
      synth_method(
          "speak",
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            if (info.Length() >= 1 && info[0]->IsObject())
            {
              v8::Local<v8::Object> utterance = info[0].As<v8::Object>();
              for (const char* handler : {"onstart", "onend"})
              {
                v8::Local<v8::Value> fn;
                if (utterance
                        ->Get(ctx, ToV8Str(iso, handler))
                        .ToLocal(&fn) &&
                    fn->IsFunction())
                {
                  v8::TryCatch try_catch(iso);
                  (void)fn.As<v8::Function>()
                      ->Call(ctx, utterance, 0, nullptr)
                      .FromMaybe(v8::Local<v8::Value>());
                  if (try_catch.HasCaught())
                    CJavascriptException::ThrowIf(iso, try_catch);
                }
              }
            }
          });
      v8::Local<v8::Object> synthesis =
          synth_inst->NewInstance(context).ToLocalChecked();
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "speechSynthesis"),
                              synthesis,
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }
  }

  void InstallOfflineDeclarations(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context)
  {
    ContextState* state = EnsureState(isolate, context);
    {
      v8::Local<v8::Value> seed;
      if (!state->profile_data.IsEmpty() &&
          state->profile_data
              .Get(isolate)
              ->Get(context, ToV8Str(isolate, "$clipboard_text"))
              .ToLocal(&seed) &&
          seed->IsString())
        state->clipboard_text = ToUtf8(isolate, seed);
    }
    v8::Local<v8::Value> navigator;
    if (!context->Global()
             ->Get(context, ToV8Str(isolate, "navigator"))
             .ToLocal(&navigator) ||
        !navigator->IsObject())
      return;
    v8::Local<v8::Object> nav = navigator.As<v8::Object>();
    auto nav_property = [&](const char* name, v8::Local<v8::Value> value) {
      (void)nav
          ->DefineOwnProperty(context, ToV8Str(isolate, name), value,
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    };

    // navigator.serviceWorker.register(url) -> Promise<registration>
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "ServiceWorkerContainer"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "ServiceWorkerContainer"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      {
        v8::Local<v8::String> key = ToV8Str(isolate, "register");
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  v8::Local<v8::Promise::Resolver> resolver =
                      v8::Promise::Resolver::New(ctx).ToLocalChecked();
                  v8::Local<v8::Promise> promise = resolver->GetPromise();
                  ContextState* st = GetState(ctx);
                  std::string script =
                      info.Length() >= 1 && info[0]->IsString()
                          ? ResolveUrlString(iso, ctx, ToUtf8(iso, info[0]))
                          : std::string();
                  if (script.empty())
                  {
                    (void)resolver
                        ->Reject(ctx,
                                 ToV8Str(iso,
                                         "TypeError: invalid script url"))
                        .FromMaybe(false);
                    info.GetReturnValue().Set(promise);
                    return;
                  }
                  // scope defaults to the page directory (reference
                  // semantics confirmed by the oracle), not the script's
                  std::string scope = script;
                  if (st != nullptr && !st->entries.empty())
                  {
                    scope = st->entries[st->index].url;
                    size_t q = scope.find('?');
                    if (q != std::string::npos)
                      scope = scope.substr(0, q);
                    size_t slash = scope.rfind('/');
                    if (slash != std::string::npos)
                      scope = scope.substr(0, slash + 1);
                  }
                  v8::Local<v8::Object> registration = v8::Object::New(iso);
                  (void)registration
                      ->Set(ctx, ToV8Str(iso, "scope"), ToV8Str(iso, scope))
                      .FromMaybe(false);
                  v8::Local<v8::Object> active = v8::Object::New(iso);
                  (void)active
                      ->Set(ctx, ToV8Str(iso, "scriptURL"),
                            ToV8Str(iso, script))
                      .FromMaybe(false);
                  (void)registration
                      ->Set(ctx, ToV8Str(iso, "active"), active)
                      .FromMaybe(false);
                  v8::Local<v8::FunctionTemplate> unreg =
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
                            (void)res
                                ->Resolve(ctx2,
                                          v8::Boolean::New(iso2, true))
                                .FromMaybe(false);
                            cb_info.GetReturnValue().Set(res->GetPromise());
                          });
                  unreg->SetClassName(ToV8Str(iso, "unregister"));
                  (void)registration
                      ->Set(ctx, ToV8Str(iso, "unregister"),
                            unreg->GetFunction(ctx).ToLocalChecked())
                      .FromMaybe(false);
                  (void)resolver->Resolve(ctx, registration).FromMaybe(false);
                  info.GetReturnValue().Set(promise);
                });
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      }
      v8::Local<v8::Object> container =
          inst->NewInstance(context).ToLocalChecked();
      nav_property("serviceWorker", container);
    }

    // navigator.mediaDevices.getUserMedia(opts) -> Promise<stream>
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "MediaDevices"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "MediaDevices"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      {
        v8::Local<v8::String> key = ToV8Str(isolate, "getUserMedia");
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  v8::Local<v8::Promise::Resolver> resolver =
                      v8::Promise::Resolver::New(ctx).ToLocalChecked();
                  v8::Local<v8::Promise> promise = resolver->GetPromise();
                  std::vector<std::string> kinds;
                  if (info.Length() >= 1 && info[0]->IsObject())
                  {
                    v8::Local<v8::Value> flag;
                    if (info[0]
                            .As<v8::Object>()
                            ->Get(ctx, ToV8Str(iso, "audio"))
                            .ToLocal(&flag) &&
                        flag->BooleanValue(iso))
                      kinds.push_back("audio");
                    if (info[0]
                            .As<v8::Object>()
                            ->Get(ctx, ToV8Str(iso, "video"))
                            .ToLocal(&flag) &&
                        flag->BooleanValue(iso))
                      kinds.push_back("video");
                  }
                  v8::Local<v8::Object> stream = v8::Object::New(iso);
                  v8::Local<v8::Array> tracks = v8::Array::New(iso);
                  for (size_t i = 0; i < kinds.size(); i++)
                  {
                    v8::Local<v8::Object> track = v8::Object::New(iso);
                    (void)track
                        ->Set(ctx, ToV8Str(iso, "kind"),
                              ToV8Str(iso, kinds[i]))
                        .FromMaybe(false);
                    (void)tracks
                        ->Set(ctx, static_cast<uint32_t>(i), track)
                        .FromMaybe(false);
                  }
                  (void)stream
                      ->SetPrivate(ctx,
                                   v8::Private::ForApi(
                                       iso,
                                       ToV8Str(iso, "lv8.stream.tracks")),
                                   tracks)
                      .FromMaybe(false);
                  (void)stream
                      ->Set(ctx, ToV8Str(iso, "getTracks"),
                            v8::Function::New(
                                ctx,
                                [](const v8::FunctionCallbackInfo<
                                    v8::Value>& cb_info) {
                                  v8::Isolate* iso2 =
                                      cb_info.GetIsolate();
                                  v8::Local<v8::Context> ctx2 =
                                      iso2->GetCurrentContext();
                                  v8::Local<v8::Value> tracks;
                                  if (cb_info.This()
                                          ->GetPrivate(
                                              ctx2,
                                              v8::Private::ForApi(
                                                  iso2,
                                                  ToV8Str(
                                                      iso2,
                                                      "lv8.stream.tracks")))
                                          .ToLocal(&tracks) &&
                                      tracks->IsArray())
                                    cb_info.GetReturnValue().Set(tracks);
                                })
                                .ToLocalChecked())
                      .FromMaybe(false);
                  (void)resolver->Resolve(ctx, stream).FromMaybe(false);
                  info.GetReturnValue().Set(promise);
                });
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      }
      v8::Local<v8::Object> devices =
          inst->NewInstance(context).ToLocalChecked();
      nav_property("mediaDevices", devices);
    }

    // navigator.clipboard: memory clipboard (readText/writeText)
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "Clipboard"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Clipboard"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto clipboard_method = [&](const char* name, bool write) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::Value> payload =
            v8::External::New(isolate, reinterpret_cast<void*>(
                                           write ? 1 : 0));
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  bool write = info.Data()->IsExternal() &&
                               reinterpret_cast<intptr_t>(
                                   info.Data()
                                       .As<v8::External>()
                                       ->Value()) == 1;
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  ContextState* st = GetState(ctx);
                  v8::Local<v8::Promise::Resolver> resolver =
                      v8::Promise::Resolver::New(ctx).ToLocalChecked();
                  if (st != nullptr)
                  {
                    if (write && info.Length() >= 1 && info[0]->IsString())
                      st->clipboard_text = ToUtf8(iso, info[0]);
                    (void)resolver
                        ->Resolve(ctx, ToV8Str(iso, st->clipboard_text))
                        .FromMaybe(false);
                  }
                  else
                  {
                    (void)resolver->Resolve(ctx, ToV8Str(iso, ""))
                        .FromMaybe(false);
                  }
                  info.GetReturnValue().Set(resolver->GetPromise());
                },
                payload);
        fn->SetClassName(key);
        tpl->PrototypeTemplate()->Set(key, fn);
      };
      clipboard_method("readText", false);
      clipboard_method("writeText", true);
      v8::Local<v8::Object> clipboard =
          inst->NewInstance(context).ToLocalChecked();
      nav_property("clipboard", clipboard);
    }

    // global RTCPeerConnection: deterministic offer state machine
    v8::Local<v8::FunctionTemplate> rtc_tpl = v8::FunctionTemplate::New(
        isolate,
        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
          v8::Isolate* iso = info.GetIsolate();
          v8::Local<v8::Context> ctx = iso->GetCurrentContext();
          (void)info.This()
              ->SetPrivate(ctx,
                           v8::Private::ForApi(
                               iso, ToV8Str(iso, "lv8.rtc.signaling")),
                           ToV8Str(iso, "stable"))
              .FromMaybe(false);
        });
    rtc_tpl->SetClassName(ToV8Str(isolate, "RTCPeerConnection"));
    v8::Local<v8::ObjectTemplate> rtc_inst = rtc_tpl->InstanceTemplate();
    rtc_inst->SetInternalFieldCount(0);
    // M10: zero own symbols on instances (tag on prototype)
    rtc_tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "RTCPeerConnection"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));
    {
      // M11: these accessors live on the PROTOTYPE - an instance-template
      // accessor becomes an own property on every RTCPeerConnection
      // (reference instances are zero-own; same rule as the XHR face below)
      rtc_tpl->PrototypeTemplate()->SetAccessorProperty(
          ToV8Str(isolate, "localDescription"),
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Value> signaling;
                if (info.This()
                        ->GetPrivate(ctx,
                                     v8::Private::ForApi(
                                         iso,
                                         ToV8Str(iso, "lv8.rtc.signaling")))
                        .ToLocal(&signaling) &&
                    signaling->IsString() &&
                    ToUtf8(iso, signaling) == "have-local-offer")
                {
                  v8::Local<v8::Object> desc = v8::Object::New(iso);
                  (void)desc->Set(ctx, ToV8Str(iso, "type"),
                                  ToV8Str(iso, "offer"))
                      .FromMaybe(false);
                  info.GetReturnValue().Set(desc);
                }
                else
                {
                  info.GetReturnValue().SetNull();
                }
              }),
          v8::Local<v8::FunctionTemplate>());
      rtc_tpl->PrototypeTemplate()->SetAccessorProperty(
          ToV8Str(isolate, "signalingState"),
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Value> signaling;
                if (info.This()
                        ->GetPrivate(ctx,
                                     v8::Private::ForApi(
                                         iso,
                                         ToV8Str(iso, "lv8.rtc.signaling")))
                        .ToLocal(&signaling) &&
                    signaling->IsString())
                  info.GetReturnValue().Set(signaling);
                else
                  info.GetReturnValue().Set(ToV8Str(iso, "stable"));
              }),
          v8::Local<v8::FunctionTemplate>());
      auto rtc_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        rtc_tpl->PrototypeTemplate()->Set(key, fn);
      };
      rtc_method("createDataChannel",
                 [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                   v8::Isolate* iso = info.GetIsolate();
                   v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                   v8::Local<v8::Object> channel = v8::Object::New(iso);
                   (void)channel
                       ->Set(ctx, ToV8Str(iso, "readyState"),
                             ToV8Str(iso, "open"))
                       .FromMaybe(false);
                   (void)channel
                       ->Set(ctx, ToV8Str(iso, "send"),
                             v8::Function::New(
                                 ctx,
                                 [](const v8::FunctionCallbackInfo<
                                     v8::Value>& cb_info) {
                                   cb_info.GetReturnValue().Set(
                                       v8::Undefined(
                                           cb_info.GetIsolate()));
                                 })
                                 .ToLocalChecked())
                       .FromMaybe(false);
                   info.GetReturnValue().Set(channel);
                 });
      rtc_method("createOffer",
                 [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                   v8::Isolate* iso = info.GetIsolate();
                   v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                   v8::Local<v8::Promise::Resolver> resolver =
                       v8::Promise::Resolver::New(ctx).ToLocalChecked();
                   v8::Local<v8::Object> offer = v8::Object::New(iso);
                   (void)offer->Set(ctx, ToV8Str(iso, "type"),
                                    ToV8Str(iso, "offer"))
                       .FromMaybe(false);
                   (void)resolver->Resolve(ctx, offer).FromMaybe(false);
                   info.GetReturnValue().Set(resolver->GetPromise());
                 });
      rtc_method("setLocalDescription",
                 [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                   v8::Isolate* iso = info.GetIsolate();
                   v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                   v8::Local<v8::Promise::Resolver> resolver =
                       v8::Promise::Resolver::New(ctx).ToLocalChecked();
                   (void)info.This()
                       ->SetPrivate(ctx,
                                    v8::Private::ForApi(
                                        iso,
                                        ToV8Str(iso, "lv8.rtc.signaling")),
                                    ToV8Str(iso, "have-local-offer"))
                       .FromMaybe(false);
                   (void)resolver->Resolve(ctx, v8::Undefined(iso))
                       .FromMaybe(false);
                   info.GetReturnValue().Set(resolver->GetPromise());
                 });
    }
    (void)context
        ->Global()
        ->DefineOwnProperty(context,
                            ToV8Str(isolate, "RTCPeerConnection"),
                            rtc_tpl->GetFunction(context).ToLocalChecked(),
                            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
        .FromMaybe(false);
  }

} // namespace LV8
