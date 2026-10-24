// lv8 G3: dataset interceptors and NamedNodeMap for element wrappers.
// Dataset uses NamedPropertyHandlerConfiguration (7-param, Intercepted V2)
// mapping camelCase keys to data-* attributes. NamedNodeMap provides
// item/getNamedItem/length over the element's attributes.

#include "LV8.h"

#include "../Exception.h"

#include <lexbor/html/html.h>
#include <string>

namespace LV8
  {
  namespace
    {
    v8::Local<v8::Private> DsEl(v8::Isolate* iso)
    {
      return v8::Private::ForApi(iso, ToV8Str(iso, "lv8.ds.el"));
    }

    v8::Local<v8::Private> NnmEl(v8::Isolate* iso)
    {
      return v8::Private::ForApi(iso, ToV8Str(iso, "lv8.nnm.el"));
    }

    std::string KeyToDataAttr(const std::string& key)
    {
      std::string out = "data-";
      for (char c : key)
      {
        if (std::isupper(static_cast<unsigned char>(c)))
        {
          out += '-';
          out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        else
          out += c;
      }
      return out;
    }

    lxb_dom_element_t* GetDsElement(v8::Local<v8::Value> data)
    {
      if (!data->IsObject()) return nullptr;
      lxb_dom_node_t* n = NodeOf(data.As<v8::Object>());
      if (n == nullptr || n->type != LXB_DOM_NODE_TYPE_ELEMENT) return nullptr;
      return lxb_dom_interface_element(n);
    }
  } // anonymous namespace

  // ---- dataset interceptors (called from WrapNode's template handler) ----

  v8::Intercepted DatasetGetter(
      v8::Local<v8::Name> name,
      const v8::PropertyCallbackInfo<v8::Value>& info)
  {
    if (!name->IsString()) return v8::Intercepted::kNo;
    v8::Isolate* iso = info.GetIsolate();
    std::string attr = KeyToDataAttr(ToUtf8(iso, name));
    lxb_dom_element_t* el = GetDsElement(info.Data());
    if (el == nullptr) return v8::Intercepted::kNo;
    size_t len = 0;
    const lxb_char_t* v = lxb_dom_element_get_attribute(
        el, (const lxb_char_t*)attr.c_str(), attr.size(), &len);
    if (v == nullptr) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(
        ToV8Str(iso, std::string((const char*)v, len)));
    return v8::Intercepted::kYes;
  }

  v8::Intercepted DatasetSetter(
      v8::Local<v8::Name> name, v8::Local<v8::Value> value,
      const v8::PropertyCallbackInfo<void>& info)
  {
    if (!name->IsString()) return v8::Intercepted::kNo;
    v8::Isolate* iso = info.GetIsolate();
    std::string attr = KeyToDataAttr(ToUtf8(iso, name));
    lxb_dom_element_t* el = GetDsElement(info.Data());
    if (el == nullptr) return v8::Intercepted::kNo;
    v8::String::Utf8Value val(iso, value);
    lxb_dom_element_set_attribute(
        el, (const lxb_char_t*)attr.c_str(), attr.size(),
        (const lxb_char_t*)*val, val.length());
    return v8::Intercepted::kYes;
  }

  v8::Intercepted DatasetQuery(
      v8::Local<v8::Name> name,
      const v8::PropertyCallbackInfo<v8::Integer>& info)
  {
    if (!name->IsString()) return v8::Intercepted::kNo;
    v8::Isolate* iso = info.GetIsolate();
    std::string attr = KeyToDataAttr(ToUtf8(iso, name));
    lxb_dom_element_t* el = GetDsElement(info.Data());
    if (el == nullptr) return v8::Intercepted::kNo;
    if (lxb_dom_element_has_attribute(
            el, (const lxb_char_t*)attr.c_str(), attr.size()))
    {
      info.GetReturnValue().Set(
          static_cast<int32_t>(v8::PropertyAttribute::None));
      return v8::Intercepted::kYes;
    }
    return v8::Intercepted::kNo;
  }

  v8::Intercepted DatasetDeleter(
      v8::Local<v8::Name> name,
      const v8::PropertyCallbackInfo<v8::Boolean>& info)
  {
    if (!name->IsString()) return v8::Intercepted::kNo;
    v8::Isolate* iso = info.GetIsolate();
    std::string attr = KeyToDataAttr(ToUtf8(iso, name));
    lxb_dom_element_t* el = GetDsElement(info.Data());
    if (el == nullptr) return v8::Intercepted::kNo;
    lxb_dom_element_remove_attribute(
        el, (const lxb_char_t*)attr.c_str(), attr.size());
    info.GetReturnValue().Set(true);
    return v8::Intercepted::kYes;
  }

  void DatasetEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Object> el = info.Data().As<v8::Object>();
    lxb_dom_node_t* node = NodeOf(el);
    v8::Local<v8::Array> names = v8::Array::New(iso);
    uint32_t n = 0;
    if (node != nullptr && node->type == LXB_DOM_NODE_TYPE_ELEMENT)
    {
      lxb_dom_element_t* elem = lxb_dom_interface_element(node);
      for (lxb_dom_attr_t* a = elem->first_attr; a != nullptr; a = a->next)
      {
        size_t alen = 0;
        const lxb_char_t* an = lxb_dom_attr_qualified_name(a, &alen);
        std::string s((const char*)an, alen);
        if (s.rfind("data-", 0) == 0)
        {
          std::string key;
          bool upper = false;
          for (size_t ci = 5; ci < s.size(); ci++)
          {
            char c = s[ci];
            if (c == '-') { upper = true; continue; }
            key += upper
                ? static_cast<char>(std::toupper(
                    static_cast<unsigned char>(c)))
                : c;
            upper = false;
          }
          (void)names->Set(ctx, n++, ToV8Str(iso, key)).FromMaybe(false);
        }
      }
    }
    info.GetReturnValue().Set(names);
  }

  // ---- NamedNodeMap methods ----

  void NNM_Item(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> el;
    if (!info.This()
             ->GetPrivate(ctx, NnmEl(iso))
             .ToLocal(&el) || !el->IsObject())
    { info.GetReturnValue().SetNull(); return; }
    lxb_dom_node_t* node = NodeOf(el.As<v8::Object>());
    if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    { info.GetReturnValue().SetNull(); return; }
    int32_t idx = info.Length() >= 1 && info[0]->IsNumber()
        ? info[0]->Int32Value(ctx).FromMaybe(0) : -1;
    lxb_dom_element_t* elem = lxb_dom_interface_element(node);
    int32_t i = 0;
    for (lxb_dom_attr_t* a = elem->first_attr; a != nullptr; a = a->next, i++)
    {
      if (i == idx)
      {
        size_t nl = 0, vl = 0;
        const lxb_char_t* n = lxb_dom_attr_qualified_name(a, &nl);
        const lxb_char_t* v = lxb_dom_attr_value(a, &vl);
        v8::Local<v8::Object> ao = v8::Object::New(iso);
        (void)ao->Set(ctx, ToV8Str(iso, "name"),
                      ToV8Str(iso, std::string((const char*)n, nl)))
            .FromMaybe(false);
        (void)ao->Set(ctx, ToV8Str(iso, "value"),
                      ToV8Str(iso, std::string((const char*)v, vl)))
            .FromMaybe(false);
        info.GetReturnValue().Set(ao);
        return;
      }
    }
    info.GetReturnValue().SetNull();
  }

  void NNM_GetNamedItem(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> el;
    if (!info.This()
             ->GetPrivate(ctx, NnmEl(iso))
             .ToLocal(&el) || !el->IsObject())
    { info.GetReturnValue().SetNull(); return; }
    lxb_dom_node_t* node = NodeOf(el.As<v8::Object>());
    if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
        info.Length() < 1 || !info[0]->IsString())
    { info.GetReturnValue().SetNull(); return; }
    std::string name = ToUtf8(iso, info[0]);
    lxb_dom_element_t* elem = lxb_dom_interface_element(node);
    size_t vlen = 0;
    const lxb_char_t* v = lxb_dom_element_get_attribute(
        elem, (const lxb_char_t*)name.c_str(), name.size(), &vlen);
    if (v == nullptr)
    { info.GetReturnValue().SetNull(); return; }
    v8::Local<v8::Object> ao = v8::Object::New(iso);
    (void)ao->Set(ctx, ToV8Str(iso, "name"), ToV8Str(iso, name))
        .FromMaybe(false);
    (void)ao->Set(ctx, ToV8Str(iso, "value"),
                  ToV8Str(iso, std::string((const char*)v, vlen)))
        .FromMaybe(false);
    info.GetReturnValue().Set(ao);
  }

  // Indexed access (attrs[j]): the challenge SDK iterates attributes with
  // integer indices; without this interceptor reads returned undefined.
  v8::Intercepted NNM_IndexedGetter(uint32_t index,
                                    const v8::PropertyCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> el;
    if (!info.This()
             ->GetPrivate(ctx, NnmEl(iso))
             .ToLocal(&el) || !el->IsObject())
    { return v8::Intercepted::kNo; }
    lxb_dom_node_t* node = NodeOf(el.As<v8::Object>());
    if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    { return v8::Intercepted::kNo; }
    int32_t idx = static_cast<int32_t>(index);
    lxb_dom_element_t* elem = lxb_dom_interface_element(node);
    int32_t i = 0;
    for (lxb_dom_attr_t* a = elem->first_attr; a != nullptr; a = a->next, i++)
    {
      if (i == idx)
      {
        size_t nl = 0, vl = 0;
        const lxb_char_t* n = lxb_dom_attr_qualified_name(a, &nl);
        const lxb_char_t* v = lxb_dom_attr_value(a, &vl);
        v8::Local<v8::Object> ao = v8::Object::New(iso);
        (void)ao->Set(ctx, ToV8Str(iso, "name"),
                      ToV8Str(iso, std::string((const char*)n, nl)))
            .FromMaybe(false);
        (void)ao->Set(ctx, ToV8Str(iso, "value"),
                      ToV8Str(iso, std::string((const char*)v, vl)))
            .FromMaybe(false);
        info.GetReturnValue().Set(ao);
        return v8::Intercepted::kYes;
      }
    }
    return v8::Intercepted::kNo;
  }

  void NNM_Length(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* iso = info.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> el;
    int32_t count = 0;
    if (info.This()
            ->GetPrivate(ctx, NnmEl(iso))
            .ToLocal(&el) && el->IsObject())
    {
      lxb_dom_node_t* node = NodeOf(el.As<v8::Object>());
      if (node != nullptr && node->type == LXB_DOM_NODE_TYPE_ELEMENT)
      {
        lxb_dom_element_t* elem = lxb_dom_interface_element(node);
        for (lxb_dom_attr_t* a = elem->first_attr; a != nullptr;
             a = a->next)
          count++;
      }
    }
    info.GetReturnValue().Set(count);
  }
} // namespace LV8
