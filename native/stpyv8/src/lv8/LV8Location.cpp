// lv8 G1 navigation: a small URL model plus native location and history
// objects driven by a session-history state machine. Semantics (validated
// against the 125-test oracle subset):
//   - pushState/replaceState build/replace entries, no events fire;
//   - back/forward/go fire popstate first, then hashchange when only the
//     fragment changed, with oldURL/newURL as absolute URL strings;
//   - location.hash = x pushes a new entry and fires hashchange;
//   - location.assign/href/component setters push a new entry (synchronous
//     state update; no document navigation exists yet).

#include "LV8.h"

#include <cctype>

#include <string>
#include <vector>

namespace LV8
  {
  namespace
    {
    // ------------------------------------------------------------------ URL

    struct Url
    {
      std::string scheme;  // "https" (no colon)
      std::string host;    // "example.test" (no port)
      std::string port;    // "" when default/absent
      std::string path;    // "/first" (starts with / for authority URLs)
      std::string query;   // "tab=b" (no question mark)
      std::string hash;    // "end" (no hash mark)
      bool opaque = false; // about:blank-style, no authority

      std::string HostPort() const
      {
        return port.empty() ? host : host + ":" + port;
      }

      std::string Serialize() const
      {
        if (opaque)
          return scheme + ":" + path;
        std::string out = scheme + "://" + HostPort() +
                          (path.empty() ? "/" : path);
        if (!query.empty()) out += "?" + query;
        if (!hash.empty()) out += "#" + hash;
        return out;
      }

      std::string Origin() const
      {
        if (opaque) return "null";
        return scheme + "://" + HostPort();
      }

      static std::string DefaultPort(const std::string& scheme)
      {
        if (scheme == "http" || scheme == "ws") return "80";
        if (scheme == "https" || scheme == "wss") return "443";
        if (scheme == "ftp") return "21";
        return "";
      }
    };

    bool ParseUrl(const std::string& input, Url& out)
    {
      // scheme
      size_t colon = input.find(':');
      if (colon == std::string::npos)
        return false;
      out.scheme.assign(input, 0, colon);
      for (size_t i = 0; i < out.scheme.size(); i++)
      {
        char c = out.scheme[i];
        if (i == 0
                ? !std::isalpha(static_cast<unsigned char>(c))
                : !(std::isalnum(static_cast<unsigned char>(c)) || c == '+' ||
                    c == '-' || c == '.'))
          return false;
      }
      for (auto& c : out.scheme)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      std::string rest(input, colon + 1);

      // opaque URL (about:blank, mailto:, ...)
      if (rest.size() < 2 || rest[0] != '/' || rest[1] != '/')
      {
        out.opaque = true;
        out.host.clear();
        out.port.clear();
        out.path = rest;
        out.query.clear();
        out.hash.clear();
        return true;
      }

      rest.assign(input, colon + 3, std::string::npos);

      // authority runs until /, ? or #
      size_t auth_end = rest.find_first_of("/?#");
      std::string authority = rest.substr(0, auth_end);
      std::string tail =
          auth_end == std::string::npos ? "" : rest.substr(auth_end);

      // hash
      size_t hash_at = tail.find('#');
      if (hash_at != std::string::npos)
      {
        out.hash.assign(tail, hash_at + 1, std::string::npos);
        tail.resize(hash_at);
      }
      // query
      size_t query_at = tail.find('?');
      if (query_at != std::string::npos)
      {
        out.query.assign(tail, query_at + 1, std::string::npos);
        tail.resize(query_at);
      }
      out.path = tail.empty() ? "/" : tail;

      // userinfo is accepted but ignored (never observed in lv8 profiles)
      size_t at = authority.rfind('@');
      if (at != std::string::npos)
        authority.erase(0, at + 1);

      // IPv6 literal [..]
      if (!authority.empty() && authority[0] == '[')
      {
        size_t close = authority.find(']');
        if (close == std::string::npos)
          return false;
        out.host = authority.substr(0, close + 1);
        out.port.assign(authority, close + 1,
                        authority[close + 1] == ':'
                            ? std::string::npos
                            : 0);
      }
      else
      {
        size_t port_at = authority.rfind(':');
        if (port_at != std::string::npos)
        {
          out.host.assign(authority, 0, port_at);
          out.port.assign(authority, port_at + 1, std::string::npos);
        }
        else
        {
          out.host = authority;
          out.port.clear();
        }
      }

      for (auto& c : out.host)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      // strip default port so https://x:443/ serializes like Chrome does
      if (!out.port.empty() && out.port == Url::DefaultPort(out.scheme))
        out.port.clear();
      return !out.host.empty();
    }

    // Resolve |relative| against |base| (RFC 3986 style for the common cases
    // that session-history emulation needs).
    bool ResolveUrl(const Url& base, const std::string& relative, Url& out)
    {
      if (!relative.empty())
      {
        size_t colon = relative.find(':');
        size_t slash = relative.find('/');
        size_t query_at = relative.find('?');
        size_t hash_at = relative.find('#');
        auto found = [&](size_t pos) { return pos != std::string::npos; };
        bool maybe_scheme =
            found(colon) &&
            (!found(slash) || colon < slash) &&
            (!found(query_at) || colon < query_at) &&
            (!found(hash_at) || colon < hash_at);
        if (maybe_scheme)
        {
          // absolute (or scheme-relative handled after parse failure)
          Url parsed;
          if (ParseUrl(relative, parsed))
          {
            out = parsed;
            return true;
          }
          if (colon == 0)
            return false;
          // falls through to treat as relative when it cannot parse
        }
      }

      if (base.opaque)
        return false;  // cannot resolve against about:blank

      std::string path = base.path;
      std::string query = base.query;
      std::string hash = base.hash;
      std::string rest = relative;

      if (!rest.empty() && rest[0] == '#')
      {
        hash.assign(rest, 1, std::string::npos);
      }
      else
      {
        size_t hash_at = rest.find('#');
        if (hash_at != std::string::npos)
        {
          hash.assign(rest, hash_at + 1, std::string::npos);
          rest.resize(hash_at);
        }
        size_t query_at = rest.find('?');
        if (query_at != std::string::npos)
        {
          query.assign(rest, query_at + 1, std::string::npos);
          rest.resize(query_at);
        }
        if (!rest.empty())
        {
          if (rest[0] == '/')
          {
            path = rest;
          }
          else if (rest[0] == '.' && rest.size() > 1 && rest[1] == '/')
          {
            // ./x replaces the last segment
            std::string dir = path.substr(0, path.rfind('/') + 1);
            path = dir + rest.substr(2);
          }
          else
          {
            std::string dir = path.substr(0, path.rfind('/') + 1);
            path = dir + rest;
          }
          // normalize ./ and ../ segments
          std::vector<std::string> segments;
          size_t start = 0;
          while (start <= path.size())
          {
            size_t slash = path.find('/', start);
            std::string seg = path.substr(
                start, slash == std::string::npos ? std::string::npos
                                                  : slash - start);
            if (seg == "..")
            {
              if (!segments.empty()) segments.pop_back();
            }
            else if (!seg.empty() && seg != ".")
            {
              segments.push_back(seg);
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
          }
          path = "/";
          for (size_t i = 0; i < segments.size(); i++)
          {
            if (i) path += "/";
            path += segments[i];
          }
        }
      }

      out = base;
      out.path = path.empty() ? "/" : path;
      out.query = query;
      out.hash = hash;
      return true;
    }

    Url CurrentUrl(v8::Local<v8::Context> context)
    {
      Url url;
      ContextState* state = GetState(context);
      if (state != nullptr && !state->entries.empty())
        ParseUrl(state->entries[state->index].url, url);
      return url;
    }

    // ------------------------------------------------------------- helpers

    ContextState* RequireState(v8::Isolate* isolate)
    {
      return GetState(isolate->GetCurrentContext());
    }

    void PushEntry(v8::Isolate* isolate, ContextState* state,
                   const Url& url, v8::Local<v8::Value> state_value)
    {
      HistoryEntry entry;
      entry.url = url.Serialize();
      entry.key = "h" + std::to_string(state->next_history_key++);
      if (!state_value.IsEmpty())
        entry.state.Reset(isolate, state_value);
      // truncate the forward stack, then append (browser pushState)
      state->entries.resize(static_cast<size_t>(state->index) + 1);
      entry.key = "h" + std::to_string(state->next_history_key++);
      state->entries.push_back(std::move(entry));
      state->index = static_cast<int>(state->entries.size()) - 1;
    }

    void ReplaceEntry(v8::Isolate* isolate, ContextState* state,
                      const Url& url, v8::Local<v8::Value> state_value)
    {
      HistoryEntry& entry = state->entries[state->index];
      entry.url = url.Serialize();
      if (!state_value.IsEmpty())
        entry.state.Reset(isolate, state_value);
    }

    // Navigate: replace=true mimics location.replace/replaceState semantics.
    void Navigate(v8::Isolate* isolate, ContextState* state,
                  const std::string& resolved, bool replace)
    {
      Url url;
      if (!ParseUrl(resolved, url))
        return;
      if (replace)
      {
        HistoryEntry& entry = state->entries[state->index];
        entry.url = url.Serialize();
      }
      else
      {
        HistoryEntry entry;
        entry.url = url.Serialize();
        state->entries.resize(static_cast<size_t>(state->index) + 1);
        entry.key = "h" + std::to_string(state->next_history_key++);
      state->entries.push_back(std::move(entry));
        state->index = static_cast<int>(state->entries.size()) - 1;
      }
    }

    void GoTo(v8::Isolate* isolate, v8::Local<v8::Context> context, int delta)
    {
      ContextState* state = RequireState(isolate);
      if (state == nullptr || state->entries.empty())
        return;

      int target = state->index + delta;
      if (target < 0) target = 0;
      if (target >= static_cast<int>(state->entries.size()))
        target = static_cast<int>(state->entries.size()) - 1;
      if (target == state->index)
        return;

      std::string old_url = state->entries[state->index].url;
      std::string new_url = state->entries[target].url;
      state->index = target;

      // popstate first, hashchange after (session-history step order)
      // Entries without a pushState payload carry state === null; an empty
      // Global would hand V8 an empty Local (access violation), so normalize.
      v8::Local<v8::Value> state_value =
          v8::Local<v8::Value>::New(isolate, state->entries[target].state);
      if (state_value.IsEmpty())
        state_value = v8::Null(isolate);
      DispatchEvent(isolate, context, "PopStateEvent",
                    {{"type", ToV8Str(isolate, "popstate")},
                     {"state", state_value}});

      Url old_parsed;
      Url new_parsed;
      if (ParseUrl(old_url, old_parsed) && ParseUrl(new_url, new_parsed) &&
          old_parsed.hash != new_parsed.hash)
      {
        DispatchEvent(isolate, context, "HashChangeEvent",
                      {{"type", ToV8Str(isolate, "hashchange")},
                       {"oldURL", ToV8Str(isolate, old_url)},
                       {"newURL", ToV8Str(isolate, new_url)}});
      }
    }

    // ------------------------------------------------------- location impl

#define LV8_LOC_GETTER(name, expr)                                           \
    void Location##name(const v8::FunctionCallbackInfo<v8::Value>& info)     \
    {                                                                        \
      Url url = CurrentUrl(info.GetIsolate()->GetCurrentContext());          \
      std::string value = expr;                                              \
      info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), value));          \
    }

    LV8_LOC_GETTER(Href, url.Serialize())
    LV8_LOC_GETTER(Protocol, url.scheme + ":")
    LV8_LOC_GETTER(Host, url.HostPort())
    LV8_LOC_GETTER(Hostname, url.host)
    LV8_LOC_GETTER(Port, url.port)
    LV8_LOC_GETTER(Pathname, url.path)
    LV8_LOC_GETTER(Search, url.query.empty() ? std::string() : "?" + url.query)
    LV8_LOC_GETTER(Hash, url.hash.empty() ? std::string() : "#" + url.hash)
    LV8_LOC_GETTER(Origin, url.Origin())
#undef LV8_LOC_GETTER

    // Shared setter machinery: all component setters navigate like
    // location.assign, except hash which pushes + fires hashchange.
    void LocationAssignImpl(v8::Isolate* isolate,
                            const v8::FunctionCallbackInfo<v8::Value>& info,
                            bool replace)
    {
      ContextState* state = RequireState(isolate);
      if (state == nullptr || state->entries.empty() || info.Length() < 1)
        return;

      Url base = CurrentUrl(isolate->GetCurrentContext());
      Url resolved;
      std::string relative = ToUtf8(isolate, info[0]);
      // a bare component value (no scheme) resolves against the current URL
      if (!ResolveUrl(base, relative, resolved))
        return;
      Navigate(isolate, state, resolved.Serialize(), replace);
    }

    void LocationAssign(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MonitorCall(info.GetIsolate(),
                  info.GetIsolate()->GetCurrentContext(), info.This(),
                  "assign");
      LocationAssignImpl(info.GetIsolate(), info, false);
    }

    void LocationReplace(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MonitorCall(info.GetIsolate(),
                  info.GetIsolate()->GetCurrentContext(), info.This(),
                  "replace");
      LocationAssignImpl(info.GetIsolate(), info, true);
    }

    void LocationReload(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MonitorCall(info.GetIsolate(),
                  info.GetIsolate()->GetCurrentContext(), info.This(),
                  "reload");
      // No document model yet: reload keeps the entry (and drops no state).
    }

    void LocationHashSetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = RequireState(isolate);
      if (state == nullptr || state->entries.empty() || info.Length() < 1)
        return;

      std::string value = ToUtf8(isolate, info[0]);
      if (!value.empty() && value[0] == '#')
        value.erase(0, 1);

      Url old_url;
      ParseUrl(state->entries[state->index].url, old_url);
      if (value == old_url.hash)
        return;  // same fragment: browsers do not push an entry

      Url base = CurrentUrl(context);
      std::string relative = value.empty() ? "#" : "#" + value;
      Url resolved;
      if (!ResolveUrl(base, relative, resolved))
        return;

      std::string old_serialized = base.Serialize();
      Navigate(isolate, state, resolved.Serialize(), false);
      DispatchEvent(isolate, context, "HashChangeEvent",
                    {{"type", ToV8Str(isolate, "hashchange")},
                     {"oldURL", ToV8Str(isolate, old_serialized)},
                     {"newURL", ToV8Str(isolate, resolved.Serialize())}});
    }

    // ------------------------------------------------------- history impl

    void HistoryLength(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      ContextState* state = RequireState(info.GetIsolate());
      if (state == nullptr)
      {
        info.GetReturnValue().Set(1);
        return;
      }
      info.GetReturnValue().Set(
          static_cast<int32_t>(state->entries.size()));
    }

    void HistoryState(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      ContextState* state = RequireState(isolate);
      if (state == nullptr || state->entries.empty())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      v8::Local<v8::Value> state_value = v8::Local<v8::Value>::New(
          isolate, state->entries[state->index].state);
      info.GetReturnValue().Set(
          state_value.IsEmpty() ? v8::Local<v8::Value>(v8::Null(isolate))
                                : state_value);
    }

    void HistoryScrollRestoration(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(
          ToV8Str(info.GetIsolate(), "auto"));
    }

    void HistoryPushState(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      MonitorCall(isolate, isolate->GetCurrentContext(), info.This(),
                  "pushState");
      ContextState* state = RequireState(isolate);
      if (state == nullptr || state->entries.empty())
        return;

      Url resolved = CurrentUrl(isolate->GetCurrentContext());
      if (info.Length() > 2 && info[2]->IsString())
      {
        Url base = CurrentUrl(isolate->GetCurrentContext());
        if (!ResolveUrl(base, ToUtf8(isolate, info[2]), resolved))
          return;
        Url current;
        ParseUrl(state->entries[state->index].url, current);
        if (resolved.Origin() != current.Origin())
        {
          isolate->ThrowException(ToV8Str(
              isolate,
              "SecurityError: A history state object with URL '" +
                  resolved.Serialize() +
                  "' cannot be created in a document with origin '" +
                  current.Origin() + "'"));
          return;
        }
      }
      PushEntry(isolate, state, resolved,
                info.Length() > 0 ? info[0]
                                  : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    void HistoryReplaceState(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      MonitorCall(isolate, isolate->GetCurrentContext(), info.This(),
                  "replaceState");
      ContextState* state = RequireState(isolate);
      if (state == nullptr || state->entries.empty())
        return;

      Url resolved = CurrentUrl(isolate->GetCurrentContext());
      if (info.Length() > 2 && info[2]->IsString())
      {
        Url base = CurrentUrl(isolate->GetCurrentContext());
        if (!ResolveUrl(base, ToUtf8(isolate, info[2]), resolved))
          return;
        Url current;
        ParseUrl(state->entries[state->index].url, current);
        if (resolved.Origin() != current.Origin())
        {
          isolate->ThrowException(ToV8Str(
              isolate,
              "SecurityError: A history state object with URL '" +
                  resolved.Serialize() +
                  "' cannot be created in a document with origin '" +
                  current.Origin() + "'"));
          return;
        }
      }
      ReplaceEntry(isolate, state, resolved,
                   info.Length() > 0 ? info[0]
                                     : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    void HistoryBack(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MonitorCall(info.GetIsolate(),
                  info.GetIsolate()->GetCurrentContext(), info.This(),
                  "back");
      GoTo(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), -1);
    }

    void HistoryForward(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MonitorCall(info.GetIsolate(),
                  info.GetIsolate()->GetCurrentContext(), info.This(),
                  "forward");
      GoTo(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), 1);
    }

    void HistoryGo(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      MonitorCall(info.GetIsolate(),
                  info.GetIsolate()->GetCurrentContext(), info.This(),
                  "go");
      int delta = 0;
      if (info.Length() > 0 && info[0]->IsNumber())
        delta = info[0]->Int32Value(
            info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
      GoTo(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), delta);
    }

    // Install |object| as a non-configurable, non-enumerable global and
    // define its accessor/method surface on the object's own template.
    void InstallAccessor(v8::Isolate* isolate,
                         v8::Local<v8::ObjectTemplate> tmpl,
                         const char* name, v8::FunctionCallback getter,
                         v8::FunctionCallback setter = nullptr)
    {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> getter_fn =
          v8::FunctionTemplate::New(isolate, getter);
      getter_fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
      v8::Local<v8::FunctionTemplate> setter_fn;
      if (setter != nullptr)
      {
        setter_fn = v8::FunctionTemplate::New(isolate, setter);
        setter_fn->SetClassName(ToV8Str(isolate, std::string("set ") + name));
      }
      tmpl->SetAccessorProperty(key, getter_fn, setter_fn);
    }

    void InstallMethod(v8::Isolate* isolate,
                       v8::Local<v8::ObjectTemplate> tmpl,
                       const char* name, v8::FunctionCallback callback,
                       int length = 0)
    {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, callback);
      fn->SetClassName(key);
      // G4.251: reference arity (fingerprintable) - e.g. pushState 2
      fn->SetLength(length);
      tmpl->Set(key, fn);
    }
    } // namespace

  // navigation API: live views over the shared session history
  void MakeEntryWrapper(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        ContextState* state, int index,
                        v8::Local<v8::Object>* out)
  {
    v8::Local<v8::Object> wrapper = v8::Object::New(isolate);
    HistoryEntry& entry = state->entries[index];
    (void)wrapper
        ->Set(context, ToV8Str(isolate, "url"), ToV8Str(isolate, entry.url))
        .FromMaybe(false);
    (void)wrapper
        ->Set(context, ToV8Str(isolate, "key"), ToV8Str(isolate, entry.key))
        .FromMaybe(false);
    (void)wrapper
        ->Set(context, ToV8Str(isolate, "index"),
              v8::Integer::New(isolate, index))
        .FromMaybe(false);
    (void)wrapper
        ->SetPrivate(context,
                     v8::Private::ForApi(isolate,
                                         ToV8Str(isolate, "lv8.hist.key")),
                     ToV8Str(isolate, entry.key))
        .FromMaybe(false);
    v8::Local<v8::FunctionTemplate> get_state = v8::FunctionTemplate::New(
        isolate,
        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
          v8::Isolate* iso = info.GetIsolate();
          v8::Local<v8::Context> ctx = iso->GetCurrentContext();
          ContextState* st = GetState(ctx);
          v8::Local<v8::Value> key;
          if (st == nullptr ||
              !info.This()
                   ->GetPrivate(ctx,
                                v8::Private::ForApi(
                                    iso, ToV8Str(iso, "lv8.hist.key")))
                   .ToLocal(&key) ||
              !key->IsString())
          {
            info.GetReturnValue().SetNull();
            return;
          }
          std::string wanted = ToUtf8(iso, key);
          for (auto& e : st->entries)
          {
            if (e.key == wanted)
            {
              if (e.state.IsEmpty())
                info.GetReturnValue().SetNull();
              else
                info.GetReturnValue().Set(e.state.Get(iso));
              return;
            }
          }
          info.GetReturnValue().SetNull();
        });
    get_state->SetClassName(ToV8Str(isolate, "getState"));
    (void)wrapper
        ->Set(context, ToV8Str(isolate, "getState"),
              get_state->GetFunction(context).ToLocalChecked())
        .FromMaybe(false);
    *out = wrapper;
  }

  void InstallNavigationApi(v8::Isolate* isolate,
                            v8::Local<v8::Context> context)
  {
    ContextState* state = GetState(context);
    v8::Local<v8::Object> nav = v8::Object::New(isolate);
    auto add_method = [&](const char* name, v8::FunctionCallback cb,
                          v8::Local<v8::Value> data) {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, cb, data);
      fn->SetClassName(key);
      (void)nav->Set(context, key, fn->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
    };
    auto add_getter = [&](const char* name, v8::FunctionCallback cb) {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> get =
          v8::FunctionTemplate::New(isolate, cb);
      get->SetClassName(ToV8Str(isolate, std::string("get ") + name));
      nav->SetAccessorProperty(
          key, get->GetFunction(context).ToLocalChecked(),
          v8::Local<v8::Function>());
    };
    add_method("entries",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 ContextState* st = GetState(ctx);
                 v8::Local<v8::Array> arr = v8::Array::New(iso);
                 if (st != nullptr)
                 {
                   for (size_t i = 0; i < st->entries.size(); i++)
                   {
                     v8::Local<v8::Object> w = v8::Object::New(iso);
                     MakeEntryWrapper(iso, ctx, st, static_cast<int>(i), &w);
                     (void)arr->Set(ctx, static_cast<uint32_t>(i), w)
                         .FromMaybe(false);
                   }
                 }
                 info.GetReturnValue().Set(arr);
               },
               v8::Local<v8::Value>());
    add_getter("currentEntry",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 ContextState* st = GetState(ctx);
                 if (st == nullptr || st->entries.empty())
                 {
                   info.GetReturnValue().SetNull();
                   return;
                 }
                 v8::Local<v8::Object> w = v8::Object::New(iso);
                 MakeEntryWrapper(iso, ctx, st, st->index, &w);
                 info.GetReturnValue().Set(w);
               });
    add_getter("canGoBack",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 ContextState* st = GetState(ctx);
                 info.GetReturnValue().Set(
                     v8::Boolean::New(iso, st != nullptr && st->index > 0));
               });
    add_getter("canGoForward",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 ContextState* st = GetState(ctx);
                 info.GetReturnValue().Set(v8::Boolean::New(
                     iso, st != nullptr && st->index + 1 <
                              static_cast<int>(st->entries.size())));
               });
    add_method("navigate",
               [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                 v8::Isolate* iso = info.GetIsolate();
                 v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                 ContextState* st = GetState(ctx);
                 v8::Local<v8::Promise::Resolver> resolver =
                     v8::Promise::Resolver::New(ctx).ToLocalChecked();
                 v8::Local<v8::Promise> finished = resolver->GetPromise();
                 v8::Local<v8::Object> result = v8::Object::New(iso);
                 (void)result
                     ->Set(ctx, ToV8Str(iso, "finished"), finished)
                     .FromMaybe(false);
                 if (st == nullptr || info.Length() < 1 ||
                     !info[0]->IsString())
                 {
                   info.GetReturnValue().Set(result);
                   return;
                 }
                 Url base = CurrentUrl(ctx);
                 Url resolved;
                 if (!ResolveUrl(base, ToUtf8(iso, info[0]), resolved))
                 {
                   info.GetReturnValue().Set(result);
                   return;
                 }
                 v8::Local<v8::Value> nav_state = v8::Undefined(iso);
                 if (info.Length() > 1 && info[1]->IsObject())
                 {
                   v8::Local<v8::Value> state_v;
                   if (info[1].As<v8::Object>()
                           ->Get(ctx, ToV8Str(iso, "state"))
                           .ToLocal(&state_v))
                     nav_state = state_v;
                 }
                 PushEntry(iso, st, resolved, nav_state);
                 v8::Local<v8::Object> wrapper = v8::Object::New(iso);
                 MakeEntryWrapper(iso, ctx, st, st->index, &wrapper);
                 (void)resolver->Resolve(ctx, wrapper).FromMaybe(false);
                 info.GetReturnValue().Set(result);
               },
               v8::Local<v8::Value>());
    auto traverse = [&](int delta) {
      v8::Local<v8::String> key =
          ToV8Str(isolate, delta < 0 ? "back" : "forward");
      v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            ContextState* st = GetState(ctx);
            const int delta = info.Data().As<v8::Int32>()->Value();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            v8::Local<v8::Promise> finished = resolver->GetPromise();
            if (st != nullptr)
            {
              int target = st->index + delta;
              if (target >= 0 &&
                  target < static_cast<int>(st->entries.size()))
              {
                st->index = target;
                v8::Local<v8::Object> wrapper = v8::Object::New(iso);
                MakeEntryWrapper(iso, ctx, st, st->index, &wrapper);
                (void)resolver->Resolve(ctx, wrapper).FromMaybe(false);
              }
            }
            info.GetReturnValue().Set(finished);
          },
          v8::Integer::New(isolate, delta));
      fn->SetClassName(key);
      (void)nav->Set(context, key, fn->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
    };
    traverse(-1);
    traverse(1);

    (void)context->Global()->DefineOwnProperty(
        context, ToV8Str(isolate, "navigation"), nav,
        static_cast<v8::PropertyAttribute>(0));
    {
      v8::Local<v8::String> src = ToV8Str(
          isolate,
          "(function (nav) {"
          " class Navigation { constructor() { throw new TypeError("
          " 'Illegal constructor'); } }"
          " Object.setPrototypeOf(nav, Navigation.prototype);"
          " Object.defineProperty(globalThis, 'Navigation', {"
          " value: Navigation, writable: true, enumerable: false,"
          " configurable: true }); })");
      v8::ScriptOrigin origin(ToV8Str(isolate, "<anonymous>"));
      v8::Local<v8::Script> script =
          v8::Script::Compile(context, src, &origin).ToLocalChecked();
      v8::Local<v8::Value> args[] = {nav};
      (void)script->Run(context)
          .ToLocalChecked()
          .As<v8::Function>()
          ->Call(context, context->Global(), 1, args)
          .FromMaybe(v8::Local<v8::Value>());
    }
  }

  void InstallNavigation(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         const std::string& initial_url)
  {
    ContextState* state = EnsureState(isolate, context);

    Url url;
    if (!ParseUrl(initial_url, url))
      ParseUrl("about:blank", url);

    HistoryEntry entry;
    entry.url = url.Serialize();
    state->entries.clear();
    entry.key = "h" + std::to_string(state->next_history_key++);
      state->entries.push_back(std::move(entry));
    state->index = 0;
    InstallNavigationApi(isolate, context);

    // ---- location -----------------------------------------------------
    v8::Local<v8::FunctionTemplate> location_tpl =
        v8::FunctionTemplate::New(isolate);
    location_tpl->SetClassName(ToV8Str(isolate, "Location"));
    v8::Local<v8::ObjectTemplate> location_tmpl =
        location_tpl->InstanceTemplate();
    location_tmpl->SetInternalFieldCount(0);
    // Reference-engine parity: String(location) returns the href URL.
    // The toString method sits on the instance template (location is a
    // singleton per context; ObjectTemplate has no prototype template).
    {
      v8::Local<v8::FunctionTemplate> to_str =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Local<v8::Value> href;
                if (info.This()
                        ->Get(info.GetIsolate()->GetCurrentContext(),
                              ToV8Str(info.GetIsolate(), "href"))
                        .ToLocal(&href))
                  info.GetReturnValue().Set(href);
              });
      to_str->SetClassName(ToV8Str(isolate, "toString"));
      location_tmpl->Set(ToV8Str(isolate, "toString"), to_str);
    }
    // M10: zero own symbols on instances (tag on prototype)
    location_tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "Location"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));

    // location is [LegacyUnforgeable]: accessors sit directly on the
    // instance and the properties are non-configurable.
    InstallAccessor(isolate, location_tmpl, "href", LocationHref,
                    LocationAssign);
    InstallAccessor(isolate, location_tmpl, "protocol", LocationProtocol);
    InstallAccessor(isolate, location_tmpl, "host", LocationHost);
    InstallAccessor(isolate, location_tmpl, "hostname", LocationHostname);
    InstallAccessor(isolate, location_tmpl, "port", LocationPort);
    InstallAccessor(isolate, location_tmpl, "pathname", LocationPathname,
                    LocationAssign);
    InstallAccessor(isolate, location_tmpl, "search", LocationSearch,
                    LocationAssign);
    InstallAccessor(isolate, location_tmpl, "hash", LocationHash,
                    LocationHashSetter);
    InstallAccessor(isolate, location_tmpl, "origin", LocationOrigin);
    InstallMethod(isolate, location_tmpl, "assign", LocationAssign);
    InstallMethod(isolate, location_tmpl, "replace", LocationReplace);
    InstallMethod(isolate, location_tmpl, "reload", LocationReload);

    v8::Local<v8::Object> location =
        location_tmpl->NewInstance(context).ToLocalChecked();
    // tag the instance path for the API monitor
    (void)location
        ->SetPrivate(context,
                     v8::Private::ForApi(
                         isolate, ToV8Str(isolate, "lv8:path")),
                     ToV8Str(isolate, "location"))
        .FromMaybe(false);

    // ---- history --------------------------------------------------------
    v8::Local<v8::FunctionTemplate> history_tpl =
        v8::FunctionTemplate::New(isolate);
    history_tpl->SetClassName(ToV8Str(isolate, "History"));
    v8::Local<v8::ObjectTemplate> history_tmpl =
        history_tpl->InstanceTemplate();
    history_tmpl->SetInternalFieldCount(0);
    // M10: zero own symbols on instances (tag on prototype)
    history_tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "History"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));

    InstallAccessor(isolate, history_tmpl, "length", HistoryLength);
    InstallAccessor(isolate, history_tmpl, "state", HistoryState);
    InstallAccessor(isolate, history_tmpl, "scrollRestoration",
                    HistoryScrollRestoration);
    InstallMethod(isolate, history_tmpl, "back", HistoryBack);
    InstallMethod(isolate, history_tmpl, "forward", HistoryForward);
    InstallMethod(isolate, history_tmpl, "go", HistoryGo);
    InstallMethod(isolate, history_tmpl, "pushState", HistoryPushState, 2);
    InstallMethod(isolate, history_tmpl, "replaceState", HistoryReplaceState, 2);

    v8::Local<v8::Object> history =
        history_tmpl->NewInstance(context).ToLocalChecked();
    (void)history
        ->SetPrivate(context,
                     v8::Private::ForApi(
                         isolate, ToV8Str(isolate, "lv8:path")),
                     ToV8Str(isolate, "history"))
        .FromMaybe(false);

    // Both are [LegacyUnforgeable] window properties: own, non-configurable,
    // enumerable (matches Chrome's Object.getOwnPropertyDescriptor).
    (void)context->Global()->DefineOwnProperty(
        context, ToV8Str(isolate, "location"), location,
        static_cast<v8::PropertyAttribute>(0));
    (void)context->Global()->DefineOwnProperty(
        context, ToV8Str(isolate, "history"), history,
        static_cast<v8::PropertyAttribute>(0));
  }

  std::string ResolveUrlString(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               const std::string& relative)
  {
    ContextState* state = GetState(context);
    if (state == nullptr || state->entries.empty())
      return "";
    Url base;
    if (!ParseUrl(state->entries[state->index].url, base))
      return "";
    Url out;
    if (!ResolveUrl(base, relative, out))
      return "";
    return out.Serialize();
  }
} // namespace LV8
