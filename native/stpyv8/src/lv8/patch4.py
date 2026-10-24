import io

p = 'LV8.cpp'
s = io.open(p, encoding='utf-8').read()

# 1. attach per-interface async surface regardless of the object's own $async
old = '''    if (has_async)
    {
      AttachAsyncMethods(isolate, std::string(class_name), data, proto_tmpl);
    }
'''
new = '''    AttachAsyncMethods(isolate, std::string(class_name), data, proto_tmpl);
'''
assert old in s, 'gate'
s = s.replace(old, new, 1)

# 2. namedItem() over the private array
old = '''    v8::Local<v8::Object> ArrayHostFromProfile(v8::Isolate* isolate,
                                               v8::Local<v8::Context> context,
                                               const char* class_name,
                                               v8::Local<v8::Array> arr)
    {'''
new = '''    // namedItem() over the private array; |field| selects the item key
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
                                               v8::Local<v8::Array> arr)
    {'''
assert old in s, 'nameditem-fn'
s = s.replace(old, new, 1)

# 3. wire namedItem into the prototype
old = '''      v8::Local<v8::String> item_key = ToV8Str(isolate, "item");
      v8::Local<v8::FunctionTemplate> item_fn =
          v8::FunctionTemplate::New(isolate, ArrayItemMethod);
      item_fn->SetClassName(item_key);
      tpl->PrototypeTemplate()->Set(item_key, item_fn);
'''
new = '''      v8::Local<v8::String> item_key = ToV8Str(isolate, "item");
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
'''
assert old in s, 'nameditem-wire'
s = s.replace(old, new, 1)

# 4. named own props after the index props
old = '''      for (uint32_t i = 0; i < arr->Length(); i++)
      {
        (void)instance
            ->Set(context, i,
                  arr->Get(context, i)
                      .FromMaybe(v8::Local<v8::Value>(v8::Undefined(isolate))))
            .FromMaybe(false);
      }
      return instance;
    }
    } // namespace'''
new = '''      for (uint32_t i = 0; i < arr->Length(); i++)
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
    } // namespace'''
assert old in s, 'named-props'
s = s.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('patch4 ok')
