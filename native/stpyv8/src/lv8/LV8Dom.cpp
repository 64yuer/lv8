// lv8 G2: document/Element host objects over the lexbor HTML parser.
//
// The parsed lexbor document lives in the per-context state; every DOM node
// exposed to JS is a FunctionTemplate instance with the lxb pointer in
// internal field 0 plus a wrap cache so node identity holds across reads.
// Page.load (via CContext::LoadDocument) parses, runs inline scripts in
// document order, fires DOMContentLoaded, then flips readyState to complete.

#include "LV8.h"

#include "../Exception.h"

#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace LV8
  {
  // document-level query methods used by MakeDocumentObject (defined in the
  // public section below); declared at LV8 scope so the anonymous namespace
  // does not shadow them
  void GetElementById(const v8::FunctionCallbackInfo<v8::Value>& info);
  void GetElementsByTagName(const v8::FunctionCallbackInfo<v8::Value>& info);
  void GetElementsByClassName(const v8::FunctionCallbackInfo<v8::Value>& info);
  void QuerySelector(const v8::FunctionCallbackInfo<v8::Value>& info);
  void QuerySelectorAll(const v8::FunctionCallbackInfo<v8::Value>& info);
  void ElementGetElementsByTagName(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  void ElementGetElementsByClassName(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  void ElementQuerySelector(const v8::FunctionCallbackInfo<v8::Value>& info);
  void ElementQuerySelectorAll(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  void ConfigureElementInterfaceTemplate(v8::Isolate* isolate,
                                         const std::string& iface,
                                         v8::Local<v8::FunctionTemplate> tpl);

  namespace
  {
    // window.document accessor plumbing: the current wrapper lives in a
    // global private slot (realms have no ContextState to hold it), and a
    // private flag marks the once-only accessor install.
    v8::Local<v8::Private> DocumentSlotKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8:page-document"));
    }

    // G4.231: set once the accessor property exists.  It is created
    // CONFIGURABLE so the window-surface polyfill tranche can relocate it to
    // the reference insertion point (between navigation and history) and then
    // re-define it non-configurable.
    v8::Local<v8::Private> DocumentAccessorFlagKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8:document-accessor"));
    }

    void DocumentSlotGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      // receiver first (cross-realm contentWindow.document reads answer the
      // target realm's document), current context's global as fallback
      for (v8::Local<v8::Object> target : {info.This(), context->Global()})
      {
        v8::Local<v8::Value> slot;
        if (target->GetPrivate(context, DocumentSlotKey(isolate))
                .ToLocal(&slot) &&
            slot->IsObject())
        {
          info.GetReturnValue().Set(slot);
          return;
        }
      }
    }
  } // namespace

  void InstallDocumentAccessor(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Object> document_obj,
                               bool configurable)
  {
    v8::Local<v8::Object> global = context->Global();
    (void)global->SetPrivate(context, DocumentSlotKey(isolate), document_obj)
        .FromMaybe(false);
    v8::Local<v8::Value> installed;
    if (global->GetPrivate(context, DocumentAccessorFlagKey(isolate))
            .ToLocal(&installed) &&
        installed->IsTrue())
      return;
    // reference shape (work/reference-traces/window-surface.json): getter-only
    // accessor, getter named "get document", enumerable, non-configurable
    v8::Local<v8::FunctionTemplate> tpl =
        v8::FunctionTemplate::New(isolate, DocumentSlotGetter);
    tpl->SetClassName(ToV8Str(isolate, "get document"));
    v8::Local<v8::Function> getter =
        tpl->GetFunction(context).ToLocalChecked();
    (void)global->SetAccessorProperty(
        ToV8Str(isolate, "document"), getter, v8::Local<v8::Function>(),
        // G4.231: the boot context (configurable=true) leaves the accessor
        // movable so the window-surface tranche can place it at the reference
        // insertion point (between navigation and history) and then re-define
        // it non-configurable.  Barren iframe realms keep the final shape
        // directly.
        configurable
            ? static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::None)
            : static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::DontDelete));
    (void)global->SetPrivate(context, DocumentAccessorFlagKey(isolate),
                             v8::True(isolate))
        .FromMaybe(false);
  }

  // Parsed-document state (LV8.h forward-declares this name).
  struct DomState
  {
    lxb_html_document_t* doc = nullptr;
    lxb_css_parser_t* css_parser = nullptr;
    lxb_selectors_t* selectors = nullptr;
    std::string ready_state = "loading";
    // JS wrapper identity cache: lexbor node -> JS object
    std::map<const void*, v8::Global<v8::Object>> wraps;
    std::map<std::string, v8::Global<v8::FunctionTemplate>> tag_ctors;
    // Nodes detached from the document (innerHTML replacement, removeChild)
    // or standalone clones stay alive and readable until the context dies;
    // they are freed with their document's mraw allocator, never explicitly.
    std::vector<lxb_dom_node_t*> detached;
    // Documents parsed by DOMParser live as long as the context.
    std::vector<lxb_html_document_t*> parsed_docs;
    // Inline script currently executing (document.write insertion point).
    lxb_dom_node_t* current_script = nullptr;
    // Last node document.write inserted; writes chain after it so multiple
    // writes keep document order.
    lxb_dom_node_t* write_point = nullptr;
    // custom elements: tag name -> user constructor
    std::map<std::string, v8::Global<v8::Function>> custom_definitions;
    // open shadow roots: host node -> shadow-root fragment node
    std::map<const void*, lxb_dom_node_t*> shadow_roots;
    // page URL for inline script origins (resolved baseURL)
    std::string page_url;
    // createElementNS namespaces (node ptr -> namespaceURI)
    std::map<const void*, std::string> svg_namespaces;
    v8::Global<v8::Function> svg_point_factory;
    // B1: per-iframe barren realms (node ptr -> its own v8::Context).  Each
    // realm gets fresh V8 intrinsics (own Array/Object/eval), so realm
    // identity checks (`iframe.contentWindow.Array !== window.Array`) hold
    // the way real browsers answer.  Realms die with the DomState.
    std::map<const void*, v8::UniquePersistent<v8::Context>> iframe_realms;
    // B1: undetectable document.all (cached; reads the live tree per access)
    v8::Global<v8::Object> all_collection;
    // shared prototype for every element wrapper (HTMLElement.prototype)
    v8::Global<v8::Object> element_proto;
    v8::Global<v8::Function> html_element_ctor;
    v8::Global<v8::FunctionTemplate> html_element_tpl;
    // inline script nodes whose execution has finished; the all enumerator
    // skips them (matching reference engine all.length)
    std::vector<lxb_dom_node_t*> executed_scripts;
    // previous script node, detached when next script starts
    lxb_dom_node_t* pending_detach = nullptr;
    // SDK script tree node: created per run, lives in head while executing
    lxb_dom_node_t* sdk_script_node = nullptr;
    // Shared collection constructors (NodeList / HTMLCollection) so that
    // instanceof works across every collection a page ever sees.
    v8::Global<v8::FunctionTemplate> nl_tpl;
    v8::Global<v8::FunctionTemplate> hc_tpl;
    v8::Global<v8::FunctionTemplate> range_tpl;
    // G3.13 shared interface constructors (instanceof across wrappers)
    v8::Global<v8::FunctionTemplate> nnm_tpl;    // NamedNodeMap
    v8::Global<v8::FunctionTemplate> tl_tpl;     // DOMTokenList
    v8::Global<v8::FunctionTemplate> attr_tpl;   // Attr
    v8::Global<v8::Object> element_iface_proto;  // Element.prototype
    v8::Global<v8::Function> element_iface_ctor; // Element
    v8::Global<v8::Function> node_iface_ctor;    // Node
    v8::Global<v8::Object> node_iface_proto;     // Node.prototype
    // per-target element event listeners (capture/target/bubble dispatch);
    // std::list keeps entry references stable while listeners mutate tables
    struct ElListener
    {
      std::string type;
      v8::Global<v8::Function> fn;
      bool capture;
      bool once = false;
    };
    struct ElTarget
    {
      v8::Global<v8::Object> target;
      std::vector<ElListener> listeners;
    };
    std::list<ElTarget> el_targets;

    ~DomState()
    {
      // detached nodes belong to their document's allocator and die with it
      // (lxb_dom_document_destroy tears down the whole mraw) -- destroying
      // them here would be a double free
      wraps.clear();
      detached.clear();
      if (selectors != nullptr)
        (void)lxb_selectors_destroy(selectors, true);
      if (css_parser != nullptr)
        (void)lxb_css_parser_destroy(css_parser, true);
      for (lxb_html_document_t* parsed : parsed_docs)
        lxb_html_document_destroy(parsed);
      parsed_docs.clear();
      if (doc != nullptr)
        lxb_html_document_destroy(doc);
    }
  };

  namespace
    {
    // ------------------------------------------------------- DOM state

    DomState* GetDomState(v8::Local<v8::Context> context)
    {
      ContextState* state = GetState(context);
      return state == nullptr ? nullptr : state->dom_state.get();
    }

    // The lexbor node pointer travels in a Private symbol, NOT an internal
    // field: stpyv8 treats any object with one internal field as a Python
    // wrapper (CPythonObject::IsWrapped) and dereferences field 0, so an
    // internal field here crashes on conversion back to Python.

    // The document node backing a document host object (|self|), falling
    // back to the main page document. Lets DOMParser documents reuse the
    // whole document surface.
    lxb_dom_node_t* DocNodeOf(v8::Local<v8::Object> self,
                              v8::Local<v8::Context> context)
    {
      lxb_dom_node_t* node = NodeOf(self);
      if (node != nullptr && node->type == LXB_DOM_NODE_TYPE_DOCUMENT)
        return node;
      DomState* dom = GetDomState(context);
      if (dom == nullptr || dom->doc == nullptr)
        return nullptr;
      return lxb_dom_interface_node(&dom->doc->dom_document);
    }

    // ------------------------------------------------- interface naming

    // Per-tag interface names (the per-tag sweep grows with G2 coverage).
    std::string InterfaceNameFor(lxb_dom_node_t* node)
    {
      if (node->type == LXB_DOM_NODE_TYPE_TEXT)
        return "Text";
      if (node->type == LXB_DOM_NODE_TYPE_CDATA_SECTION)
        return "CDATASection";
      if (node->type == LXB_DOM_NODE_TYPE_COMMENT)
        return "Comment";
      if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_TYPE)
        return "DocumentType";
      if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
        return "DocumentFragment";
      const lxb_char_t* name;
      size_t len;
      name = lxb_dom_node_name(node, &len);
      std::string tag((const char*)name, len);
      for (auto& c : tag)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      // reference: createElement('svg') in the HTML namespace yields a plain
      // Element wrapper; only SVG-namespace nodes are SVGSVGElement
      if (tag == "svg")
        return node->ns == LXB_NS_SVG ? "SVGSVGElement" : "Element";
      if (tag == "circle") return "SVGCircleElement";
      if (tag == "path") return "SVGPathElement";
      if (tag == "g") return "SVGGElement";
      if (tag == "ellipse") return "SVGEllipseElement";
      if (tag == "line") return "SVGLineElement";
      if (tag == "rect") return "SVGRectElement";
      if (tag == "polygon") return "SVGPolygonElement";
      if (tag == "polyline") return "SVGPolylineElement";
      if (tag == "text") return "SVGTextElement";
      // reference: embed/object have no dedicated interface and are not even
      // HTMLElement - they are plain Elements there (window-surface.json:
      // embedTag "[object Element]", chain Element->Node->EventTarget).
      // Window named access and form.elements keep their tag-name logic,
      // which does not consult the interface.
      if (tag == "embed" || tag == "object") return "Element";
      if (tag == "html") return "HTMLHtmlElement";
      if (tag == "head") return "HTMLHeadElement";
      if (tag == "body") return "HTMLBodyElement";
      if (tag == "title") return "HTMLTitleElement";
      if (tag == "script") return "HTMLScriptElement";
      if (tag == "div") return "HTMLDivElement";
      if (tag == "iframe") return "HTMLIFrameElement";
      if (tag == "span") return "HTMLSpanElement";
      if (tag == "p") return "HTMLParagraphElement";
      if (tag == "a") return "HTMLAnchorElement";
      if (tag == "img") return "HTMLImageElement";
      if (tag == "input") return "HTMLInputElement";
      if (tag == "button") return "HTMLButtonElement";
      if (tag == "option") return "HTMLOptionElement";
      if (tag == "form") return "HTMLFormElement";
      if (tag == "style") return "HTMLStyleElement";
      if (tag == "link") return "HTMLLinkElement";
      if (tag == "template") return "HTMLTemplateElement";
      if (tag == "canvas") return "HTMLCanvasElement";
      if (tag == "meta") return "HTMLMetaElement";
      if (tag == "dialog") return "HTMLDialogElement";
      if (tag == "audio" || tag == "video") return tag == "audio" ? "HTMLAudioElement" : "HTMLVideoElement";
      if (tag == "template") return "HTMLTemplateElement";
      if (tag == "button") return "HTMLButtonElement";
      if (tag == "form") return "HTMLFormElement";
      if (tag == "h1" || tag == "h2" || tag == "h3" ||
          tag == "h4" || tag == "h5" || tag == "h6")
        return "HTMLHeadingElement";
      if (tag == "ul") return "HTMLUListElement";
      if (tag == "ol") return "HTMLOListElement";
      if (tag == "table") return "HTMLTableElement";
      if (tag == "tr") return "HTMLTableRowElement";
      if (tag == "td" || tag == "th") return "HTMLTableCellElement";
      // reference carries no dedicated interfaces for the semantic sections -
      // they are plain HTMLElement wrappers there
      if (tag == "label") return "HTMLLabelElement";
      if (tag == "select") return "HTMLSelectElement";
      if (tag == "textarea") return "HTMLTextAreaElement";
      if (tag == "slot") return "HTMLSlotElement";
      return "HTMLElement";
    }

    std::string SlotNameOf(lxb_dom_node_t* node)
    {
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return "";
      lxb_dom_element_t* el = lxb_dom_interface_element(node);
      const lxb_char_t* value = lxb_dom_element_get_attribute(
          el, (const lxb_char_t*)"name", 4, nullptr);
      return value == nullptr ? std::string() : std::string((const char*)value);
    }

    lxb_dom_node_t* SlotRootOf(lxb_dom_node_t* node)
    {
      while (node != nullptr && node->parent != nullptr)
        node = node->parent;
      return node;
    }

    lxb_dom_node_t* ShadowHostOfRoot(DomState* dom,
                                     lxb_dom_node_t* root)
    {
      for (const auto& pair : dom->shadow_roots)
      {
        if (pair.second == root)
          return static_cast<lxb_dom_node_t*>(
              const_cast<void*>(pair.first));
      }
      return nullptr;
    }

    void SlotAssignedNodes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* slot = NodeOf(info.This());
      DomState* dom = GetDomState(context);
      bool flatten = false;
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        v8::Local<v8::Value> flatten_v;
        if (info[0].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "flatten"))
                .ToLocal(&flatten_v))
          flatten = flatten_v->BooleanValue(isolate);
      }
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t index = 0;
      if (slot != nullptr && dom != nullptr)
      {
        std::string name = SlotNameOf(slot);
        lxb_dom_node_t* root = SlotRootOf(slot);
        lxb_dom_node_t* host = ShadowHostOfRoot(dom, root);
        std::vector<lxb_dom_node_t*> assigned;
        if (host != nullptr)
        {
          for (lxb_dom_node_t* child = host->first_child; child != nullptr;
               child = child->next)
          {
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
            {
              lxb_dom_element_t* el = lxb_dom_interface_element(child);
              const lxb_char_t* slot_attr = lxb_dom_element_get_attribute(
                  el, (const lxb_char_t*)"slot", 4, nullptr);
              std::string child_slot =
                  slot_attr == nullptr
                      ? std::string()
                      : std::string((const char*)slot_attr);
              if (child_slot == name)
                assigned.push_back(child);
            }
            else if (child->type == LXB_DOM_NODE_TYPE_TEXT && name.empty())
            {
              assigned.push_back(child);
            }
          }
        }
        if (flatten && assigned.empty())
        {
          for (lxb_dom_node_t* child = slot->first_child; child != nullptr;
               child = child->next)
            assigned.push_back(child);
        }
        for (lxb_dom_node_t* node : assigned)
        {
          (void)out
              ->Set(context, index++, WrapNode(isolate, context, node))
              .FromMaybe(false);
        }
      }
      info.GetReturnValue().Set(out);
    }

    void SlotAssignedElements(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* slot = NodeOf(info.This());
      DomState* dom = GetDomState(context);
      bool flatten = false;
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        v8::Local<v8::Value> flatten_v;
        if (info[0].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "flatten"))
                .ToLocal(&flatten_v))
          flatten = flatten_v->BooleanValue(isolate);
      }
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t index = 0;
      if (slot != nullptr && dom != nullptr)
      {
        std::string name = SlotNameOf(slot);
        lxb_dom_node_t* root = SlotRootOf(slot);
        lxb_dom_node_t* host = ShadowHostOfRoot(dom, root);
        std::vector<lxb_dom_node_t*> assigned;
        if (host != nullptr)
        {
          for (lxb_dom_node_t* child = host->first_child; child != nullptr;
               child = child->next)
          {
            if (child->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
            lxb_dom_element_t* el = lxb_dom_interface_element(child);
            const lxb_char_t* slot_attr = lxb_dom_element_get_attribute(
                el, (const lxb_char_t*)"slot", 4, nullptr);
            std::string child_slot =
                slot_attr == nullptr
                    ? std::string()
                    : std::string((const char*)slot_attr);
            if (child_slot == name)
              assigned.push_back(child);
          }
        }
        if (flatten && assigned.empty())
        {
          for (lxb_dom_node_t* child = slot->first_child; child != nullptr;
               child = child->next)
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
              assigned.push_back(child);
        }
        for (lxb_dom_node_t* node : assigned)
        {
          (void)out
              ->Set(context, index++, WrapNode(isolate, context, node))
              .FromMaybe(false);
        }
      }
      info.GetReturnValue().Set(out);
    }

    void SlotGetName(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(
          ToV8Str(info.GetIsolate(), SlotNameOf(NodeOf(info.This()))));
    }

    void SlotSetName(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1)
        return;
      std::string value = ToUtf8(info.GetIsolate(), info[0]);
      lxb_dom_element_set_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)"name", 4,
          (const lxb_char_t*)value.c_str(), value.size());
    }

    lxb_dom_document_fragment_t* TemplateContentOf(lxb_dom_node_t* node)
    {
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return nullptr;
      const lxb_char_t* name;
      size_t len;
      name = lxb_dom_node_name(node, &len);
      std::string tag((const char*)name, len);
      for (auto& c : tag)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (tag != "template") return nullptr;
      lxb_html_template_element_t* tpl =
          reinterpret_cast<lxb_html_template_element_t*>(node);
      return tpl->content;
    }

    // --------------------------------------------------- serialization

    std::string serialize_buffer;
    lxb_status_t SerializeCb(const lxb_char_t* data, size_t len, void* ctx)
    {
      (void)ctx;
      serialize_buffer.append((const char*)data, len);
      return LXB_STATUS_OK;
    }

    std::string NodeText(lxb_dom_node_t* node)
    {
      size_t len = 0;
      const lxb_char_t* text = lxb_dom_node_text_content(node, &len);
      if (text == nullptr)
        return "";
      return std::string((const char*)text, len);
    }

    std::string NodeInnerHTML(lxb_dom_node_t* node)
    {
      lxb_dom_document_fragment_t* content = TemplateContentOf(node);
      if (content != nullptr) node = &content->node;
      serialize_buffer.clear();
      lxb_html_serialize_deep_cb(node, SerializeCb, nullptr);
      return serialize_buffer;
    }

    std::string NodeOuterHTML(lxb_dom_node_t* node)
    {
      serialize_buffer.clear();
      lxb_html_serialize_tree_cb(node, SerializeCb, nullptr);
      return serialize_buffer;
    }

    // ------------------------------------------------------ query engine

    struct FoundNodes
    {
      std::vector<lxb_dom_node_t*> nodes;
    };

    lxb_status_t FoundCb(lxb_dom_node_t* node,
                         lxb_css_selector_specificity_t spec, void* ctx)
    {
      (void)spec;
      auto& nodes = static_cast<FoundNodes*>(ctx)->nodes;
      // lexbor's general-sibling combinator can report the same node
      // twice; dedupe to keep browser-equal results
      if (std::find(nodes.begin(), nodes.end(), node) == nodes.end())
        nodes.push_back(node);
      return LXB_STATUS_OK;
    }

    // Run a CSS selector against |root|; returns false on selector errors.
    bool SelectNodes(DomState* dom, lxb_dom_node_t* root,
                     const std::string& raw_selector,
                     std::vector<lxb_dom_node_t*>& out)
    {
      // lexbor's pseudo-class table misses form-state pseudos; the offline
      // DOM treats attribute presence as the state, so rewrite them to the
      // equivalent attribute selectors before parsing
      std::string selector = raw_selector;
      for (const char* pseudo : {":checked", ":disabled"})
      {
        size_t pos = 0;
        while ((pos = selector.find(pseudo, pos)) != std::string::npos)
        {
          std::string attr = std::string("[") + (pseudo + 1) + "]";
          selector.replace(pos, strlen(pseudo), attr);
          pos += attr.size();
        }
      }
      if (dom->css_parser == nullptr)
      {
        dom->css_parser = lxb_css_parser_create();
        if (lxb_css_parser_init(dom->css_parser, nullptr) != LXB_STATUS_OK)
          return false;
        dom->selectors = lxb_selectors_create();
        if (lxb_selectors_init(dom->selectors) != LXB_STATUS_OK)
          return false;
      }
      lxb_css_selector_list_t* list = lxb_css_selectors_parse(
          dom->css_parser, (const lxb_char_t*)selector.c_str(),
          selector.size());
      if (dom->css_parser->status != LXB_STATUS_OK || list == nullptr)
        return false;
      FoundNodes found;
      if (lxb_selectors_find(dom->selectors, root, list, FoundCb, &found) !=
          LXB_STATUS_OK)
        return false;
      lxb_css_selector_list_destroy_memory(list);
      out = std::move(found.nodes);
      return true;
    }

    // --------------------------------------------------- element surface

    std::string ElementAttribute(lxb_dom_element_t* el, const std::string& name)
    {
      size_t len = 0;
      const lxb_char_t* value = lxb_dom_element_get_attribute(
          el, (const lxb_char_t*)name.c_str(), name.size(), &len);
      if (value == nullptr)
        return "";
      return std::string((const char*)value, len);
    }

    void GetId(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      info.GetReturnValue().Set(ToV8Str(
          info.GetIsolate(),
          ElementAttribute(lxb_dom_interface_element(node), "id")));
    }

    void SetId(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1)
        return;
      std::string value = ToUtf8(isolate, info[0]);
      lxb_dom_element_set_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)"id", 2,
          (const lxb_char_t*)value.c_str(), value.size());
    }

    void SetSrcAttr(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1)
        return;
      std::string value = ToUtf8(isolate, info[0]);
      lxb_dom_element_set_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)"src", 3,
          (const lxb_char_t*)value.c_str(), value.size());
    }

    // Attribute-backed IDL setters whose attribute name is supplied through
    // the callback data.  Keeping this generic lets specialized interfaces
    // expose their members on one shared prototype rather than materializing
    // data properties on each wrapper instance.
    void SetNamedAttr(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1)
        return;
      v8::String::Utf8Value name_v(isolate, info.Data());
      std::string name(*name_v, name_v.length());
      std::string value = ToUtf8(isolate, info[0]);
      lxb_dom_element_set_attribute(
          lxb_dom_interface_element(node),
          reinterpret_cast<const lxb_char_t*>(name.c_str()), name.size(),
          reinterpret_cast<const lxb_char_t*>(value.c_str()), value.size());
    }

    void GetNameAttr(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      info.GetReturnValue().Set(ToV8Str(
          info.GetIsolate(),
          ElementAttribute(lxb_dom_interface_element(node), "name")));
    }

    void SetNameAttr(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1)
        return;
      std::string value = ToUtf8(isolate, info[0]);
      lxb_dom_element_set_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)"name", 4,
          (const lxb_char_t*)value.c_str(), value.size());
    }

    void GetCheckedAttr(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> self = info.This();
      // dynamic override wins (click toggling never mutates the tree);
      // stored as a Private symbol because the template accessor itself is
      // an own property -- an own data property read via Get() would
      // re-enter this getter and overflow the stack
      v8::Local<v8::Private> override_key = v8::Private::ForApi(
          isolate, ToV8Str(isolate, "checked"));
      v8::Local<v8::Value> flag;
      if (self->GetPrivate(context, override_key).ToLocal(&flag) &&
          flag->IsBoolean())
      {
        info.GetReturnValue().Set(flag);
        return;
      }
      lxb_dom_node_t* node = NodeOf(info.This());
      bool checked = false;
      if (node != nullptr && node->type == LXB_DOM_NODE_TYPE_ELEMENT)
      {
        checked = lxb_dom_element_has_attribute(
            lxb_dom_interface_element(node), (const lxb_char_t*)"checked",
            7);
      }
      info.GetReturnValue().Set(
          v8::Boolean::New(isolate, checked));
    }

    void SetCheckedAttr(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> self = info.This();
      // keep the override out of the public property set (IDL semantics);
      // a Public own property would shadow the template accessor in the
      // getter and re-enter it
      if (info.Length() < 1)
        return;
      (void)self
          ->SetPrivate(context,
                       v8::Private::ForApi(isolate,
                                           ToV8Str(isolate, "checked")),
                       v8::Boolean::New(isolate,
                                        info[0]->BooleanValue(isolate)))
          .FromMaybe(false);
    }

    void SetClassName(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1)
        return;
      std::string value = ToUtf8(isolate, info[0]);
      lxb_dom_element_set_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)"class", 5,
          (const lxb_char_t*)value.c_str(), value.size());
    }

    void GetClassName(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      info.GetReturnValue().Set(ToV8Str(
          info.GetIsolate(),
          ElementAttribute(lxb_dom_interface_element(node), "class")));
    }

    void GetTagName(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr)
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      const lxb_char_t* name;
      size_t len = 0;
      name = lxb_dom_node_name(node, &len);
      std::string tag((const char*)name, len);
      // HTML tagName is uppercase
      for (auto& c : tag)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), tag));
    }

    void GetNodeName(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr)
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      // DOM standard nodeName: elements carry the uppercase tag name,
      // text/comment/document nodes the #name form. The challenge SDK
      // walks head.childNodes filtering on nodeName to snapshot inline
      // script sources (reference gold reports 'SCRIPT' here).
      switch (node->type)
      {
      case LXB_DOM_NODE_TYPE_TEXT:
        info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), "#text"));
        return;
      case LXB_DOM_NODE_TYPE_COMMENT:
        info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), "#comment"));
        return;
      case LXB_DOM_NODE_TYPE_DOCUMENT:
        info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), "#document"));
        return;
      case LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT:
        info.GetReturnValue().Set(
            ToV8Str(info.GetIsolate(), "#document-fragment"));
        return;
      default:
        break;
      }
      const lxb_char_t* name;
      size_t len = 0;
      name = lxb_dom_node_name(node, &len);
      std::string tag((const char*)name, len);
      for (auto& c : tag)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), tag));
    }

    void GetTextContent(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr)
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), NodeText(node)));
    }

    void SetTextContent(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || dom == nullptr || dom->doc == nullptr ||
          info.Length() < 1)
        return;
      std::string value = ToUtf8(isolate, info[0]);
      lxb_dom_node_text_content_set(node, (const lxb_char_t*)value.c_str(),
                                    value.size());
    }

    // G4.260: reference shape - Node.nodeValue and Element.outerHTML are
    // read/write.  nodeValue writes the leaf text (elements ignore it);
    // outerHTML parses the markup and splices it in place of this node.
    void SetNodeValue(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      DomState* dom = GetDomState(isolate->GetCurrentContext());
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || dom == nullptr || dom->doc == nullptr ||
          info.Length() < 1)
        return;
      switch (node->type)
      {
      case LXB_DOM_NODE_TYPE_TEXT:
      case LXB_DOM_NODE_TYPE_COMMENT:
      case LXB_DOM_NODE_TYPE_CDATA_SECTION:
        break;
      default:
        return;  // elements/documents keep a null nodeValue
      }
      std::string value =
          info[0]->IsNull() || info[0]->IsUndefined()
              ? std::string()
              : ToUtf8(isolate, info[0]);
      lxb_dom_node_text_content_set(node, (const lxb_char_t*)value.c_str(),
                                    value.size());
    }

    void SetOuterHTML(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      DomState* dom = GetDomState(isolate->GetCurrentContext());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          dom == nullptr || dom->doc == nullptr || node->parent == nullptr ||
          info.Length() < 1)
        return;
      if (!info[0]->IsString() && !info[0]->IsStringObject())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: outerHTML value must be a string"));
        return;
      }
      std::string html = ToUtf8(isolate, info[0]);
      lxb_dom_node_t* context = node->parent;
      lxb_dom_element_t* ctx_el =
          context->type == LXB_DOM_NODE_TYPE_ELEMENT
              ? lxb_dom_interface_element(context)
              : nullptr;
      lxb_dom_node_t* frag = lxb_html_document_parse_fragment(
          dom->doc, ctx_el, (const lxb_char_t*)html.c_str(), html.size());
      if (frag == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "SyntaxError: invalid HTML fragment"));
        return;
      }
      while (frag->first_child != nullptr)
      {
        lxb_dom_node_t* child = frag->first_child;
        lxb_dom_node_remove(child);
        lxb_dom_node_insert_before(node, child);
      }
      lxb_dom_node_destroy(frag);
      lxb_dom_node_t* parent = node->parent;
      lxb_dom_node_remove(node);
      dom->detached.push_back(node);
      (void)parent;
    }

    void GetTemplateContent(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      lxb_dom_document_fragment_t* content = TemplateContentOf(node);
      if (content == nullptr)
      {
        info.GetReturnValue().SetUndefined();
        return;
      }
      info.GetReturnValue().Set(
          WrapNode(isolate, context, &content->node));
    }

    void GetInnerHTML(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr)
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      info.GetReturnValue().Set(
          ToV8Str(info.GetIsolate(), NodeInnerHTML(node)));
    }

    void GetOuterHTML(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr)
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      info.GetReturnValue().Set(
          ToV8Str(info.GetIsolate(), NodeOuterHTML(node)));
    }

    void GetAttribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      std::string name = ToUtf8(isolate, info[0]);
      size_t len = 0;
      const lxb_char_t* value = lxb_dom_element_get_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)name.c_str(),
          name.size(), &len);
      if (value == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(
          ToV8Str(isolate, std::string((const char*)value, len)));
    }

    void SetAttribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 2)
        return;
      std::string name = ToUtf8(isolate, info[0]);
      std::string value = ToUtf8(isolate, info[1]);
      lxb_dom_element_set_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)name.c_str(),
          name.size(), (const lxb_char_t*)value.c_str(), value.size());
    }

    void HasAttribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().Set(false);
        return;
      }
      std::string name = ToUtf8(isolate, info[0]);
      info.GetReturnValue().Set(lxb_dom_element_has_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)name.c_str(),
          name.size()));
    }

    void RemoveAttribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1 || !info[0]->IsString())
        return;
      std::string name = ToUtf8(isolate, info[0]);
      lxb_dom_element_remove_attribute(
          lxb_dom_interface_element(node), (const lxb_char_t*)name.c_str(),
          name.size());
    }

    void SetInnerHTML(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      DomState* dom = GetDomState(isolate->GetCurrentContext());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          dom == nullptr || dom->doc == nullptr || info.Length() < 1)
        return;
      // accept primitive strings and String-boxed trusted values alike
      if (!info[0]->IsString() && !info[0]->IsStringObject())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: innerHTML value must be a string"));
        return;
      }
      std::string html = ToUtf8(isolate, info[0]);
      lxb_dom_node_t* frag = lxb_html_document_parse_fragment(
          dom->doc, lxb_dom_interface_element(node),
          (const lxb_char_t*)html.c_str(), html.size());
      if (frag == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "SyntaxError: invalid HTML fragment"));
        return;
      }
      // <template> parses into its content fragment, not the element
      lxb_dom_document_fragment_t* content = TemplateContentOf(node);
      lxb_dom_node_t* target =
          content != nullptr ? &content->node : node;
      // detach the old children into the graveyard (still readable)
      while (target->first_child != nullptr)
      {
        lxb_dom_node_t* child = target->first_child;
        lxb_dom_node_remove(child);
        dom->detached.push_back(child);
      }
      // move the parsed fragment children into the target
      while (frag->first_child != nullptr)
      {
        lxb_dom_node_t* child = frag->first_child;
        lxb_dom_node_remove(child);
        lxb_dom_node_insert_child(target, child);
      }
      lxb_dom_node_destroy(frag);
    }

    // defined with the custom elements machinery below
    void FireConnectedIfCustom(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               lxb_dom_node_t* node);

    // fragments splice their children at the insertion point (DOM
    // semantics); returns the list of nodes to insert
    std::vector<lxb_dom_node_t*> NodesToInsert(lxb_dom_node_t* node)
    {
      std::vector<lxb_dom_node_t*> nodes;
      if (node != nullptr &&
          node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
      {
        for (lxb_dom_node_t* c = node->first_child; c != nullptr;
             c = c->next)
          nodes.push_back(c);
      }
      else if (node != nullptr)
      {
        nodes.push_back(node);
      }
      return nodes;
    }

    void AppendChild(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || info.Length() < 1 || !info[0]->IsObject() ||
          NodeOf(info[0].As<v8::Object>()) == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: appendChild requires a DOM node"));
        return;
      }
      for (lxb_dom_node_t* child :
           NodesToInsert(NodeOf(info[0].As<v8::Object>())))
      {
        // appending an attached node moves it (browser semantics)
        if (child->parent != nullptr)
          lxb_dom_node_remove(child);
        lxb_dom_node_insert_child(node, child);
        // custom element lifecycle on the inserted subtree
        FireConnectedIfCustom(isolate, context, child);
      }
      info.GetReturnValue().Set(info[0]);
    }

    void InsertBefore(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || info.Length() < 1 || !info[0]->IsObject() ||
          NodeOf(info[0].As<v8::Object>()) == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: insertBefore requires a DOM node"));
        return;
      }
      lxb_dom_node_t* ref = nullptr;
      if (info.Length() >= 2 && info[1]->IsObject() &&
          NodeOf(info[1].As<v8::Object>()) != nullptr)
        ref = NodeOf(info[1].As<v8::Object>());
      for (lxb_dom_node_t* child :
           NodesToInsert(NodeOf(info[0].As<v8::Object>())))
      {
        if (child == ref) continue;
        if (child->parent != nullptr)
          lxb_dom_node_remove(child);
        if (ref != nullptr)
          lxb_dom_node_insert_before(ref, child);
        else
          lxb_dom_node_insert_child(node, child);
        FireConnectedIfCustom(isolate, context, child);
      }
      info.GetReturnValue().Set(info[0]);
    }

    void ReplaceChild(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || info.Length() < 2 || !info[0]->IsObject() ||
          !info[1]->IsObject() ||
          NodeOf(info[1].As<v8::Object>()) == nullptr ||
          NodeOf(info[0].As<v8::Object>()) == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: replaceChild requires DOM nodes"));
        return;
      }
      lxb_dom_node_t* old_child = NodeOf(info[1].As<v8::Object>());
      if (old_child->parent != node)
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "DOMException: NotFoundError: child is not a child of this node"));
        return;
      }
      for (lxb_dom_node_t* child :
           NodesToInsert(NodeOf(info[0].As<v8::Object>())))
      {
        if (child == old_child) continue;
        if (child->parent != nullptr)
          lxb_dom_node_remove(child);
        lxb_dom_node_insert_before(old_child, child);
        FireConnectedIfCustom(isolate, context, child);
      }
      lxb_dom_node_remove(old_child);
      DomState* dom = GetDomState(context);
      if (dom != nullptr)
        dom->detached.push_back(old_child);
      info.GetReturnValue().Set(info[1]);
    }

    void GetNodeType(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      int type = 0;
      if (node != nullptr)
      {
        switch (node->type)
        {
          case LXB_DOM_NODE_TYPE_ELEMENT: type = 1; break;
          case LXB_DOM_NODE_TYPE_TEXT: type = 3; break;
          case LXB_DOM_NODE_TYPE_CDATA_SECTION: type = 4; break;
          case LXB_DOM_NODE_TYPE_PROCESSING_INSTRUCTION: type = 7; break;
          case LXB_DOM_NODE_TYPE_COMMENT: type = 8; break;
          case LXB_DOM_NODE_TYPE_DOCUMENT: type = 9; break;
          case LXB_DOM_NODE_TYPE_DOCUMENT_TYPE: type = 10; break;
          case LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT: type = 11; break;
          default: type = 0; break;
        }
      }
      info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), type));
    }

    void DocumentCreateDocumentFragment(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      DomState* dom = GetDomState(context);
      if (doc_node == nullptr || dom == nullptr || dom->doc == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: no document is loaded"));
        return;
      }
      lxb_dom_document_fragment_t* frag =
          lxb_dom_document_create_document_fragment(
              lxb_dom_interface_document(doc_node));
      if (frag == nullptr)
      {
        isolate->ThrowException(
            ToV8Str(isolate, "Error: fragment allocation failed"));
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, &frag->node));
    }

    void DocumentCreateElementNS(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      DomState* dom = GetDomState(context);
      if (doc_node == nullptr || dom == nullptr || dom->doc == nullptr ||
          info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: createElementNS requires strings"));
        return;
      }
      std::string ns = ToUtf8(isolate, info[0]);
      std::string qname = ToUtf8(isolate, info[1]);
      std::string local = qname;
      size_t colon = local.rfind(':');
      if (colon != std::string::npos) local = local.substr(colon + 1);
      for (auto& c : local)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      lxb_dom_element_t* el = lxb_dom_document_create_element(
          lxb_dom_interface_document(doc_node),
          (const lxb_char_t*)local.c_str(), local.size(), nullptr);
      if (el == nullptr)
      {
        isolate->ThrowException(
            ToV8Str(isolate, "Error: element allocation failed"));
        return;
      }
      lxb_dom_node_t* node = lxb_dom_interface_node(el);
      dom->svg_namespaces[node] = ns;
      info.GetReturnValue().Set(WrapNode(isolate, context, node));
    }

    void RemoveChild(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || info.Length() < 1 || !info[0]->IsObject() ||
          NodeOf(info[0].As<v8::Object>()) == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: removeChild requires a DOM node"));
        return;
      }
      lxb_dom_node_t* child = NodeOf(info[0].As<v8::Object>());
      if (child->parent != node)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "DOMException: NotFoundError: child is not a child of this node"));
        return;
      }
      lxb_dom_node_remove(child);
      DomState* dom = GetDomState(context);
      if (dom != nullptr)
        dom->detached.push_back(child);
      info.GetReturnValue().Set(info[0]);
    }

    void ElementInsertAdjacentHTML(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      DomState* dom = GetDomState(context);
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          dom == nullptr || dom->doc == nullptr || info.Length() < 2 ||
          !info[0]->IsString() || !info[1]->IsString())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: insertAdjacentHTML(position, html) requires strings"));
        return;
      }
      std::string position = ToUtf8(isolate, info[0]);
      if (position != "beforebegin" && position != "afterbegin" &&
          position != "beforeend" && position != "afterend")
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "SyntaxError: invalid insert position: " + position));
        return;
      }
      // beforebegin/afterend need a parent; without one they are no-ops
      if ((position == "beforebegin" || position == "afterend") &&
          node->parent == nullptr)
        return;
      std::string html = ToUtf8(isolate, info[1]);
      lxb_dom_node_t* frag = lxb_html_document_parse_fragment(
          dom->doc, lxb_dom_interface_element(node),
          (const lxb_char_t*)html.c_str(), html.size());
      if (frag == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "SyntaxError: invalid HTML fragment"));
        return;
      }
      while (frag->first_child != nullptr)
      {
        lxb_dom_node_t* child = frag->first_child;
        lxb_dom_node_remove(child);
        if (position == "beforebegin")
          lxb_dom_node_insert_before(node, child);
        else if (position == "afterbegin")
        {
          if (node->first_child != nullptr)
            lxb_dom_node_insert_before(node->first_child, child);
          else
            lxb_dom_node_insert_child(node, child);
        }
        else if (position == "afterend")
        {
          if (node->next != nullptr)
            lxb_dom_node_insert_before(node->next, child);
          else
            lxb_dom_node_insert_child(node->parent, child);
        }
        else // beforeend
          lxb_dom_node_insert_child(node, child);
      }
      lxb_dom_node_destroy(frag);
    }

    void ElementCloneNode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      bool deep = info.Length() >= 1 && info[0]->BooleanValue(isolate);
      lxb_dom_node_t* clone = lxb_dom_node_clone(node, deep);
      if (clone == nullptr)
      {
        isolate->ThrowException(
            ToV8Str(isolate, "Error: node clone failed"));
        return;
      }
      // <template> clones must deep-copy the content fragment children;
      // lxb_dom_node_clone only walks the light DOM
      if (deep)
      {
        lxb_dom_document_fragment_t* src_content =
            TemplateContentOf(node);
        lxb_dom_document_fragment_t* dst_content =
            TemplateContentOf(clone);
        if (src_content != nullptr && dst_content != nullptr)
        {
          for (lxb_dom_node_t* child =
                   (&src_content->node)->first_child;
               child != nullptr;)
          {
            lxb_dom_node_t* next = child->next;
            lxb_dom_node_t* child_clone =
                lxb_dom_node_clone(child, true);
            if (child_clone != nullptr)
              lxb_dom_node_insert_child(&dst_content->node,
                                        child_clone);
            child = next;
          }
        }
      }
      // a standalone clone has no parent; it stays alive with the document
      DomState* dom = GetDomState(context);
      if (dom != nullptr)
        dom->detached.push_back(clone);
      info.GetReturnValue().Set(WrapNode(isolate, context, clone));
    }

    // ------------------------------------------------------ write surface

    void GetLastChild(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node != nullptr && node->last_child != nullptr)
      {
        info.GetReturnValue().Set(
            WrapNode(info.GetIsolate(),
                     info.GetIsolate()->GetCurrentContext(),
                     node->last_child));
        return;
      }
      info.GetReturnValue().SetNull();
    }

    void GetLastElementChild(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node != nullptr)
      {
        for (lxb_dom_node_t* child = node->last_child; child != nullptr;
             child = child->prev)
        {
          if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
          {
            info.GetReturnValue().Set(
                WrapNode(info.GetIsolate(),
                         info.GetIsolate()->GetCurrentContext(), child));
            return;
          }
        }
      }
      info.GetReturnValue().SetNull();
    }

    void GetNextSibling(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node != nullptr && node->next != nullptr)
      {
        info.GetReturnValue().Set(
            WrapNode(info.GetIsolate(),
                     info.GetIsolate()->GetCurrentContext(), node->next));
        return;
      }
      info.GetReturnValue().SetNull();
    }

    void GetPreviousSibling(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node != nullptr && node->prev != nullptr)
      {
        info.GetReturnValue().Set(
            WrapNode(info.GetIsolate(),
                     info.GetIsolate()->GetCurrentContext(), node->prev));
        return;
      }
      info.GetReturnValue().SetNull();
    }

    void GetParentNode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node != nullptr && node->parent != nullptr)
      {
        info.GetReturnValue().Set(
            WrapNode(info.GetIsolate(),
                     info.GetIsolate()->GetCurrentContext(),
                     node->parent));
        return;
      }
      info.GetReturnValue().SetNull();
    }

    void GetFirstChild(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node != nullptr && node->first_child != nullptr)
      {
        info.GetReturnValue().Set(
            WrapNode(isolate, context, node->first_child));
        return;
      }
      info.GetReturnValue().SetNull();
    }

    void GetFirstElementChild(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node != nullptr)
      {
        for (lxb_dom_node_t* child = node->first_child; child != nullptr;
             child = child->next)
        {
          if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
          {
            info.GetReturnValue().Set(WrapNode(isolate, context, child));
            return;
          }
        }
      }
      info.GetReturnValue().SetNull();
    }

    void GetParentElement(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->parent == nullptr ||
          node->parent->type != LXB_DOM_NODE_TYPE_ELEMENT)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(
          WrapNode(info.GetIsolate(), context, node->parent));
    }

    // ------------------------------------------------- G4.240 Node level
    // The reference wheel's Node.prototype carries these members; the
    // measured behaviour lives in tools/_nodeface_ab.py (work/reference-traces/
    // _nodeface_ref.json).  The payload walks the document/element faces, so
    // the level has to answer exactly like reference.
    v8::Local<v8::Object> DocumentObjectOf(v8::Local<v8::Context> context)
    {
      v8::Isolate* isolate = context->GetIsolate();
      v8::Local<v8::Value> value;
      if (context->Global()
              ->GetPrivate(context, DocumentSlotKey(isolate))
              .ToLocal(&value) &&
          value->IsObject())
        return value.As<v8::Object>();
      return v8::Local<v8::Object>();
    }

    void GetBaseURI(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state != nullptr && !state->entries.empty())
      {
        info.GetReturnValue().Set(
            ToV8Str(isolate, state->entries[state->index].url));
        return;
      }
      info.GetReturnValue().Set(ToV8Str(isolate, "about:blank"));
    }

    void GetOwnerDocument(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      // reference answers the document object itself for the document node
      if (node != nullptr && node->type == LXB_DOM_NODE_TYPE_DOCUMENT)
      {
        info.GetReturnValue().Set(info.This());
        return;
      }
      // A realm may contain the page document and parsed documents.  Resolve
      // from Lexbor's owning document first so ownerDocument preserves that
      // per-tree identity instead of always falling through to window.document.
      DomState* dom = GetDomState(context);
      if (node != nullptr && node->owner_document != nullptr &&
          dom != nullptr)
      {
        lxb_dom_node_t* owner =
            lxb_dom_interface_node(node->owner_document);
        auto owner_it = dom->wraps.find(owner);
        if (owner_it != dom->wraps.end())
        {
          info.GetReturnValue().Set(owner_it->second.Get(isolate));
          return;
        }
      }
      v8::Local<v8::Object> doc = DocumentObjectOf(context);
      if (!doc.IsEmpty())
      {
        info.GetReturnValue().Set(doc);
        return;
      }
      if (dom != nullptr && dom->doc != nullptr)
      {
        info.GetReturnValue().Set(WrapNode(
            isolate, context, lxb_dom_interface_node(&dom->doc->dom_document)));
        return;
      }
      info.GetReturnValue().SetNull();
    }

    void GetNodeValue(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      switch (node->type)
      {
      case LXB_DOM_NODE_TYPE_TEXT:
      case LXB_DOM_NODE_TYPE_COMMENT:
      case LXB_DOM_NODE_TYPE_CDATA_SECTION:
        info.GetReturnValue().Set(
            ToV8Str(info.GetIsolate(), NodeText(node)));
        return;
      default:
        break;
      }
      info.GetReturnValue().SetNull();
    }

    void GetRootNode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      lxb_dom_node_t* root = node;
      while (root->parent != nullptr)
        root = root->parent;
      if (root == node)
      {
        info.GetReturnValue().Set(info.This());
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, root));
    }

    void HasChildNodes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      info.GetReturnValue().Set(
          v8::Boolean::New(info.GetIsolate(),
                           node != nullptr && node->first_child != nullptr));
    }

    void IsSameNode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      bool same = false;
      if (node != nullptr && info.Length() >= 1 && info[0]->IsObject())
        same = NodeOf(info[0].As<v8::Object>()) == node;
      info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), same));
    }

    void IsEqualNode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      bool equal = false;
      if (node != nullptr && info.Length() >= 1 && info[0]->IsObject())
      {
        lxb_dom_node_t* other = NodeOf(info[0].As<v8::Object>());
        if (other == node)
          equal = true;
        else if (other != nullptr)
          equal = other->type == node->type && NodeText(node) == NodeText(other);
      }
      info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), equal));
    }

    void IsDefaultNamespace(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      // measured: elements report false, every other node type true
      bool hit = node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT;
      info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), hit));
    }

    void LookupPrefix(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    void LookupNamespaceURI(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    void Normalize(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetUndefined();
    }

    // ----------------------------------------------------- classList surface

    lxb_dom_node_t* ElementFromDataOrThis(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (info.Data()->IsObject())
        return NodeOf(info.Data().As<v8::Object>());
      return NodeOf(info.This());
    }

    void ClassListContains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = ElementFromDataOrThis(info);
      if (node == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().Set(false);
        return;
      }
      std::string wanted = ToUtf8(isolate, info[0]);
      std::string classes = ElementAttribute(
          lxb_dom_interface_element(node), "class");
      std::stringstream ss(classes);
      std::string token;
      while (std::getline(ss, token, ' '))
      {
        if (!token.empty() && token == wanted)
        {
          info.GetReturnValue().Set(true);
          return;
        }
      }
      info.GetReturnValue().Set(false);
    }

    void ClassListAdd(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = ElementFromDataOrThis(info);
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1 || !info[0]->IsString())
        return;
      lxb_dom_element_t* el = lxb_dom_interface_element(node);
      std::string classes = ElementAttribute(el, "class");
      if (!classes.empty())
        classes += " ";
      classes += ToUtf8(isolate, info[0]);
      lxb_dom_element_set_attribute(el, (const lxb_char_t*)"class", 5,
                                    (const lxb_char_t*)classes.c_str(),
                                    classes.size());
    }

    void ClassListRemove(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = ElementFromDataOrThis(info);
      if (node == nullptr || info.Length() < 1 || !info[0]->IsString())
        return;
      std::string wanted = ToUtf8(isolate, info[0]);
      std::string classes = ElementAttribute(
          lxb_dom_interface_element(node), "class");
      std::string kept;
      std::stringstream ss(classes);
      std::string token;
      while (std::getline(ss, token, ' '))
      {
        if (token.empty() || token == wanted)
          continue;
        if (!kept.empty())
          kept += " ";
        kept += token;
      }
      lxb_dom_element_set_attribute(lxb_dom_interface_element(node),
                                    (const lxb_char_t*)"class", 5,
                                    (const lxb_char_t*)kept.c_str(),
                                    kept.size());
    }

    // -------------------------------------------------- DOMTokenList surface
    // Live view over the element's class attribute; every mutation writes
    // the attribute back so attributes/serialization stay in sync.

    lxb_dom_element_t* TokenListElement(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Value> self)
    {
      v8::Local<v8::Value> el;
      if (!self.As<v8::Object>()
                  ->GetPrivate(
                      context,
                      v8::Private::ForApi(isolate,
                                          ToV8Str(isolate, "lv8.tl.el")))
                  .ToLocal(&el) ||
          !el->IsObject())
        return nullptr;
      lxb_dom_node_t* node = NodeOf(el.As<v8::Object>());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return nullptr;
      return lxb_dom_interface_element(node);
    }

    std::vector<std::string> ClassTokens(lxb_dom_element_t* el)
    {
      std::vector<std::string> tokens;
      std::stringstream ss(ElementAttribute(el, "class"));
      std::string token;
      while (ss >> token) tokens.push_back(token);
      return tokens;
    }

    void WriteClassTokens(lxb_dom_element_t* el,
                          const std::vector<std::string>& tokens)
    {
      std::string joined;
      for (const std::string& t : tokens)
      {
        if (!joined.empty()) joined += " ";
        joined += t;
      }
      lxb_dom_element_set_attribute(el, (const lxb_char_t*)"class", 5,
                                    (const lxb_char_t*)joined.c_str(),
                                    joined.size());
    }

    void TL_Contains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_element_t* el = TokenListElement(isolate, context, info.This());
      if (el == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().Set(false);
        return;
      }
      std::string wanted = ToUtf8(isolate, info[0]);
      for (const std::string& t : ClassTokens(el))
      {
        if (t == wanted)
        {
          info.GetReturnValue().Set(true);
          return;
        }
      }
      info.GetReturnValue().Set(false);
    }

    void TL_Add(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_element_t* el = TokenListElement(isolate, context, info.This());
      if (el == nullptr) return;
      std::vector<std::string> tokens = ClassTokens(el);
      for (int i = 0; i < info.Length(); i++)
      {
        if (!info[i]->IsString()) continue;
        std::string wanted = ToUtf8(isolate, info[i]);
        bool present = false;
        for (const std::string& t : tokens) present |= (t == wanted);
        if (!present) tokens.push_back(wanted);
      }
      WriteClassTokens(el, tokens);
    }

    void TL_Remove(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_element_t* el = TokenListElement(isolate, context, info.This());
      if (el == nullptr) return;
      std::vector<std::string> tokens = ClassTokens(el);
      for (int i = 0; i < info.Length(); i++)
      {
        if (!info[i]->IsString()) continue;
        std::string wanted = ToUtf8(isolate, info[i]);
        for (auto it = tokens.begin(); it != tokens.end();)
        {
          if (*it == wanted) it = tokens.erase(it);
          else ++it;
        }
      }
      WriteClassTokens(el, tokens);
    }

    void TL_Toggle(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_element_t* el = TokenListElement(isolate, context, info.This());
      if (el == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().Set(false);
        return;
      }
      std::string wanted = ToUtf8(isolate, info[0]);
      // optional force argument: undefined means plain toggle
      bool has_force = info.Length() >= 2 && !info[1]->IsUndefined();
      bool force = has_force && info[1]->BooleanValue(isolate);
      std::vector<std::string> tokens = ClassTokens(el);
      bool present = false;
      for (const std::string& t : tokens) present |= (t == wanted);
      if (present && (!has_force || !force))
      {
        for (auto it = tokens.begin(); it != tokens.end();)
        {
          if (*it == wanted) it = tokens.erase(it);
          else ++it;
        }
        WriteClassTokens(el, tokens);
        info.GetReturnValue().Set(false);
        return;
      }
      if (!present && (!has_force || force))
      {
        tokens.push_back(wanted);
        WriteClassTokens(el, tokens);
        info.GetReturnValue().Set(true);
        return;
      }
      // forced to the state it already had
      info.GetReturnValue().Set(present);
    }

    void TL_Replace(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_element_t* el = TokenListElement(isolate, context, info.This());
      if (el == nullptr || info.Length() < 2 || !info[0]->IsString() ||
          !info[1]->IsString())
      {
        info.GetReturnValue().Set(false);
        return;
      }
      std::string old_token = ToUtf8(isolate, info[0]);
      std::string new_token = ToUtf8(isolate, info[1]);
      std::vector<std::string> tokens = ClassTokens(el);
      bool replaced = false;
      for (std::string& t : tokens)
      {
        if (t == old_token)
        {
          t = new_token;
          replaced = true;
        }
      }
      if (replaced) WriteClassTokens(el, tokens);
      info.GetReturnValue().Set(replaced);
    }

    void TL_ValueGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_element_t* el = TokenListElement(isolate, context, info.This());
      if (el == nullptr)
      {
        info.GetReturnValue().Set(ToV8Str(isolate, ""));
        return;
      }
      info.GetReturnValue().Set(
          ToV8Str(isolate, ElementAttribute(el, "class")));
    }

    void TL_ValueSetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_element_t* el = TokenListElement(isolate, context, info.This());
      if (el == nullptr || info.Length() < 1) return;
      std::string value = ToUtf8(isolate, info[0]);
      lxb_dom_element_set_attribute(el, (const lxb_char_t*)"class", 5,
                                    (const lxb_char_t*)value.c_str(),
                                    value.size());
    }

    void TL_LengthGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_element_t* el = TokenListElement(isolate, context, info.This());
      info.GetReturnValue().Set(
          static_cast<int32_t>(el == nullptr ? 0 : ClassTokens(el).size()));
    }

    // --------------------------------------------------- Node tree position

    bool NodeContainsTree(lxb_dom_node_t* ancestor, lxb_dom_node_t* node)
    {
      for (lxb_dom_node_t* n = node; n != nullptr; n = n->parent)
      {
        if (n == ancestor) return true;
      }
      return false;
    }

    // true when |node| still hangs below its document root
    bool NodeIsConnected(lxb_dom_node_t* node)
    {
      for (lxb_dom_node_t* n = node; n != nullptr; n = n->parent)
      {
        if (n->type == LXB_DOM_NODE_TYPE_DOCUMENT) return true;
      }
      return false;
    }

    void ElementContains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || info.Length() < 1 || !info[0]->IsObject())
      {
        info.GetReturnValue().Set(false);
        return;
      }
      lxb_dom_node_t* other = NodeOf(info[0].As<v8::Object>());
      info.GetReturnValue().Set(other != nullptr &&
                                NodeContainsTree(node, other));
    }

    // Document#contains: browsers report a document as containing itself
    // (document.contains(document) === true); the element-level tree walk
    // rejects that because the document node is not an element root.
    void DocumentContains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (info.Length() >= 1 && info[0]->IsObject() &&
          info.This() == info[0].As<v8::Object>())
      {
        info.GetReturnValue().Set(true);
        return;
      }
      ElementContains(info);
    }

    void ElementCompareDocumentPosition(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      const int kDisconnected = 1;
      const int kPreceding = 2;
      const int kFollowing = 4;
      const int kContains = 8;
      const int kContainedBy = 16;
      const int kSpecific = 32;

      lxb_dom_node_t* a = NodeOf(info.This());
      if (a == nullptr || info.Length() < 1 || !info[0]->IsObject())
      {
        info.GetReturnValue().Set(0);
        return;
      }
      lxb_dom_node_t* b = NodeOf(info[0].As<v8::Object>());
      if (b == nullptr)
      {
        info.GetReturnValue().Set(0);
        return;
      }
      if (a == b)
      {
        info.GetReturnValue().Set(0);
        return;
      }
      if (NodeContainsTree(a, b))
      {
        info.GetReturnValue().Set(kFollowing | kContainedBy);
        return;
      }
      if (NodeContainsTree(b, a))
      {
        info.GetReturnValue().Set(kPreceding | kContains);
        return;
      }
      // locate the common ancestor, then order the two branches
      std::vector<lxb_dom_node_t*> a_chain;
      for (lxb_dom_node_t* n = a; n != nullptr; n = n->parent)
        a_chain.push_back(n);
      lxb_dom_node_t* common = nullptr;
      std::vector<lxb_dom_node_t*> b_path;
      for (lxb_dom_node_t* n = b; n != nullptr; n = n->parent)
      {
        b_path.push_back(n);
        for (lxb_dom_node_t* m : a_chain)
        {
          if (m == n) { common = n; break; }
        }
        if (common != nullptr) break;
      }
      if (common == nullptr)
      {
        // disconnected subtrees: spec says DISCONNECTED | IMPLEMENTATION_SPECIFIC
        info.GetReturnValue().Set(kDisconnected | kSpecific);
        return;
      }
      // walk both nodes up to the common ancestor recording the child slot
      auto branch_root = [](lxb_dom_node_t* n, lxb_dom_node_t* stop)
      {
        while (n != nullptr && n->parent != stop) n = n->parent;
        return n;
      };
      lxb_dom_node_t* a_branch = branch_root(a, common);
      lxb_dom_node_t* b_branch = branch_root(b, common);
      if (a_branch == nullptr || b_branch == nullptr)
      {
        info.GetReturnValue().Set(kDisconnected | kSpecific);
        return;
      }
      int result = 0;
      if (a_branch == b_branch) result = 0;
      else
      {
        // sibling order: whichever branch the common ancestor reaches
        // first precedes; hitting a_branch means b follows a
        for (lxb_dom_node_t* n = common->first_child; n != nullptr;
             n = n->next)
        {
          if (n == a_branch) { result = kFollowing; break; }
          if (n == b_branch) { result = kPreceding; break; }
        }
      }
      info.GetReturnValue().Set(result);
    }

    void ElementIsConnected(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      lxb_dom_node_t* node = NodeOf(info.This());
      info.GetReturnValue().Set(node != nullptr && NodeIsConnected(node));
    }

    void ElementToggleAttribute(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().Set(false);
        return;
      }
      std::string name = ToUtf8(isolate, info[0]);
      lxb_dom_element_t* el = lxb_dom_interface_element(node);
      bool present = lxb_dom_element_has_attribute(
          el, (const lxb_char_t*)name.c_str(), name.size());
      bool has_force = info.Length() >= 2 && !info[1]->IsUndefined();
      bool force = has_force && info[1]->BooleanValue(isolate);
      if (present && (!has_force || !force))
      {
        lxb_dom_element_remove_attribute(el, (const lxb_char_t*)name.c_str(),
                                         name.size());
        info.GetReturnValue().Set(false);
        return;
      }
      if (!present && (!has_force || force))
      {
        lxb_dom_element_set_attribute(el, (const lxb_char_t*)name.c_str(),
                                      name.size(), (const lxb_char_t*)"", 0);
        info.GetReturnValue().Set(true);
        return;
      }
      info.GetReturnValue().Set(present);
    }

    // ------------------------------------------------------------- Attr / NamedNodeMap

    lxb_dom_element_t* NnmElement(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> self)
    {
      v8::Local<v8::Value> el;
      if (!self->GetPrivate(context,
                            v8::Private::ForApi(isolate,
                                                ToV8Str(isolate, "lv8.nnm.el")))
               .ToLocal(&el) || !el->IsObject())
        return nullptr;
      lxb_dom_node_t* node = NodeOf(el.As<v8::Object>());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return nullptr;
      return lxb_dom_interface_element(node);
    }

    void AttrConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: Attr constructor requires new"));
        return;
      }
      v8::Local<v8::Object> self = info.This();
      v8::Local<v8::Value> name_value =
          info.Length() >= 1 ? info[0]
                             : v8::Local<v8::Value>(ToV8Str(isolate, ""));
      v8::Local<v8::Value> attr_value =
          info.Length() >= 2 ? info[1]
                             : v8::Local<v8::Value>(ToV8Str(isolate, ""));
      (void)self->Set(context, ToV8Str(isolate, "name"), name_value)
          .FromMaybe(false);
      (void)self->Set(context, ToV8Str(isolate, "value"), attr_value)
          .FromMaybe(false);
      (void)self
          ->Set(context, ToV8Str(isolate, "ownerElement"), v8::Null(isolate))
          .FromMaybe(false);
    }

    // Shared interface templates (NamedNodeMap / DOMTokenList / Attr) so
    // every per-element instance and the exposed global constructor agree.
    void EnsureInterfaceTemplates(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context,
                                  DomState* dom)
    {
      if (dom->nnm_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> tpl =
            v8::FunctionTemplate::New(isolate);
        tpl->SetClassName(ToV8Str(isolate, "NamedNodeMap"));
        v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        tpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "NamedNodeMap"),
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
        method("item", NNM_Item);
        method("getNamedItem", NNM_GetNamedItem);
        // indexed access: attrs[j] must resolve to the j-th attribute
        inst->SetHandler(v8::IndexedPropertyHandlerConfiguration(
            NNM_IndexedGetter));
        {
          v8::Local<v8::String> len_key = ToV8Str(isolate, "length");
          v8::Local<v8::FunctionTemplate> len_fn =
              v8::FunctionTemplate::New(isolate, NNM_Length);
          len_fn->SetClassName(ToV8Str(isolate, "get length"));
          inst->SetAccessorProperty(len_key, len_fn,
                                    v8::Local<v8::FunctionTemplate>());
        }
        dom->nnm_tpl.Reset(isolate, tpl);
      }
      if (dom->tl_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> tpl =
            v8::FunctionTemplate::New(isolate);
        tpl->SetClassName(ToV8Str(isolate, "DOMTokenList"));
        v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        tpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DOMTokenList"),
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
        method("contains", TL_Contains);
        method("add", TL_Add);
        method("remove", TL_Remove);
        method("toggle", TL_Toggle);
        method("replace", TL_Replace);
        {
          v8::Local<v8::String> value_key = ToV8Str(isolate, "value");
          v8::Local<v8::FunctionTemplate> get_fn =
              v8::FunctionTemplate::New(isolate, TL_ValueGetter);
          get_fn->SetClassName(ToV8Str(isolate, "get value"));
          v8::Local<v8::FunctionTemplate> set_fn =
              v8::FunctionTemplate::New(isolate, TL_ValueSetter);
          set_fn->SetClassName(ToV8Str(isolate, "set value"));
          tpl->PrototypeTemplate()->SetAccessorProperty(value_key, get_fn,
                                                        set_fn);
          v8::Local<v8::String> len_key = ToV8Str(isolate, "length");
          v8::Local<v8::FunctionTemplate> len_fn =
              v8::FunctionTemplate::New(isolate, TL_LengthGetter);
          len_fn->SetClassName(ToV8Str(isolate, "get length"));
          tpl->PrototypeTemplate()->SetAccessorProperty(
              len_key, len_fn, v8::Local<v8::FunctionTemplate>());
        }
        dom->tl_tpl.Reset(isolate, tpl);
      }
      if (dom->attr_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> tpl =
            v8::FunctionTemplate::New(isolate, AttrConstructor);
        tpl->SetClassName(ToV8Str(isolate, "Attr"));
        v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        tpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "Attr"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        dom->attr_tpl.Reset(isolate, tpl);
      }
    }

    // build a fresh Attr wrapper carrying |owner| (may be an empty handle)
    v8::Local<v8::Object> MakeAttrNode(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       DomState* dom,
                                       const std::string& name,
                                       const std::string& value,
                                       v8::Local<v8::Object> owner)
    {
      EnsureInterfaceTemplates(isolate, context, dom);
      v8::Local<v8::Object> attr =
          dom->attr_tpl.Get(isolate)->InstanceTemplate()->NewInstance(
              context).ToLocalChecked();
      (void)attr->Set(context, ToV8Str(isolate, "name"), ToV8Str(isolate, name))
          .FromMaybe(false);
      (void)attr->Set(context, ToV8Str(isolate, "value"),
                      ToV8Str(isolate, value))
          .FromMaybe(false);
      (void)attr
          ->Set(context, ToV8Str(isolate, "ownerElement"),
                owner.IsEmpty() ? v8::Local<v8::Value>(v8::Null(isolate))
                                : v8::Local<v8::Value>(owner))
          .FromMaybe(false);
      return attr;
    }

    void ElementGetAttributeNode(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      DomState* dom = GetDomState(context);
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          dom == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      std::string name = ToUtf8(isolate, info[0]);
      lxb_dom_element_t* el = lxb_dom_interface_element(node);
      size_t len = 0;
      const lxb_char_t* value = lxb_dom_element_get_attribute(
          el, (const lxb_char_t*)name.c_str(), name.size(), &len);
      if (value == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(MakeAttrNode(
          isolate, context, dom, name,
          std::string((const char*)value, len), info.This().As<v8::Object>()));
    }

    void ElementSetAttributeNode(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
          info.Length() < 1 || !info[0]->IsObject())
        return;
      v8::Local<v8::Object> attr_obj = info[0].As<v8::Object>();
      v8::Local<v8::Value> name_v, value_v;
      if (!attr_obj->Get(context, ToV8Str(isolate, "name")).ToLocal(&name_v) ||
          !name_v->IsString())
        return;
      std::string name = ToUtf8(isolate, name_v);
      std::string value;
      if (attr_obj->Get(context, ToV8Str(isolate, "value")).ToLocal(&value_v) &&
          !value_v->IsUndefined() && !value_v->IsNull())
        value = ToUtf8(isolate, value_v);
      lxb_dom_element_t* el = lxb_dom_interface_element(node);
      lxb_dom_element_set_attribute(el, (const lxb_char_t*)name.c_str(),
                                    name.size(), (const lxb_char_t*)value.c_str(),
                                    value.size());
      (void)attr_obj
          ->Set(context, ToV8Str(isolate, "ownerElement"), info.This())
          .FromMaybe(false);
      info.GetReturnValue().Set(info[0]);
    }

    // ------------------------------------------------ element event surface

    DomState::ElTarget* FindElTarget(DomState* dom, v8::Isolate* isolate,
                                     v8::Local<v8::Object> target)
    {
      if (target.IsEmpty()) return nullptr;
      for (DomState::ElTarget& entry : dom->el_targets)
      {
        v8::Local<v8::Object> stored = entry.target.Get(isolate);
        if (!stored.IsEmpty() && stored->SameValue(target)) return &entry;
      }
      return nullptr;
    }

    void ElementAddEventListener(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr || info.Length() < 2 || !info[0]->IsString() ||
          !info[1]->IsFunction())
        return;  // browsers silently ignore malformed registrations
      if (NodeOf(info.This()) == nullptr)
      {
        // non-node receiver (window resolved through EventTarget.prototype)
        WindowEventAdd(info);
        return;
      }
      std::string type = ToUtf8(isolate, info[0]);
      bool capture = info.Length() >= 3 && info[2]->BooleanValue(isolate);
      bool once = false;
      if (info.Length() >= 3 && info[2]->IsObject())
      {
        v8::Local<v8::Value> once_v;
        if (info[2].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "once"))
                .ToLocal(&once_v))
          once = once_v->BooleanValue(isolate);
      }
      DomState::ElTarget* entry =
          FindElTarget(dom, isolate, info.This().As<v8::Object>());
      if (entry == nullptr)
      {
        DomState::ElTarget fresh;
        fresh.target.Reset(isolate, info.This().As<v8::Object>());
        dom->el_targets.push_back(std::move(fresh));
        entry = &dom->el_targets.back();
      }
      v8::Global<v8::Function> fn(isolate, info[1].As<v8::Function>());
      for (const DomState::ElListener& l : entry->listeners)
      {
        if (l.type == type && l.capture == capture && l.fn == fn) return;
      }
      (void)once;
      // reference parity: named listeners carry "<Interface>.<name>" so
      // stack frames inside handlers name the dispatch origin
      {
        v8::Local<v8::Value> lname_v;
        std::string lname;
        if (info[1].As<v8::Function>()
                ->Get(context, ToV8Str(isolate, "name"))
                .ToLocal(&lname_v) &&
            lname_v->IsString())
          lname = ToUtf8(isolate, lname_v);
        if (!lname.empty())
        {
          std::string frame =
              InterfaceNameFor(NodeOf(info.This())) + "." + lname;
          (void)info[1]
              .As<v8::Function>()
              ->DefineOwnProperty(
                  context, ToV8Str(isolate, "name"), ToV8Str(isolate, frame),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::None))
              .FromMaybe(false);
        }
      }
      entry->listeners.push_back(
          {std::move(type), std::move(fn), capture, once});
    }

    void ElementRemoveEventListener(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr || info.Length() < 2 || !info[0]->IsString() ||
          !info[1]->IsFunction())
        return;
      if (NodeOf(info.This()) == nullptr)
      {
        WindowEventRemove(info);
        return;
      }
      DomState::ElTarget* entry =
          FindElTarget(dom, isolate, info.This().As<v8::Object>());
      if (entry == nullptr) return;
      std::string type = ToUtf8(isolate, info[0]);
      bool capture = info.Length() >= 3 && info[2]->BooleanValue(isolate);
      v8::Global<v8::Function> fn(isolate, info[1].As<v8::Function>());
      for (auto it = entry->listeners.begin(); it != entry->listeners.end();
           ++it)
      {
        if (it->type == type && it->capture == capture && it->fn == fn)
        {
          entry->listeners.erase(it);
          return;
        }
      }
    }

    // invoke one listener group: refresh target fields, honor stop flags,
    // keep a fresh TryCatch per listener (browser uncaught semantics)
    void InvokeElListeners(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           DomState::ElTarget& entry, const std::string& type,
                           v8::Local<v8::Object> event, int phase,
                           v8::Local<v8::Object> current_target, bool capture)
    {
      v8::Local<v8::Value> ct = current_target;
      SetEventField(isolate, context, event, "currentTarget", ct);
      SetEventField(isolate, context, event, "eventPhase",
                    v8::Integer::New(isolate, phase));
      v8::Local<v8::Value> event_value = event;
      // snapshot: listeners may register more listeners while we dispatch
      struct SnapEntry
      {
        v8::Global<v8::Function> fn;
        bool once;
      };
      std::vector<SnapEntry> snapshot;
      for (DomState::ElListener& l : entry.listeners)
      {
        if (l.type == type && l.capture == capture)
          snapshot.push_back({v8::Global<v8::Function>(isolate, l.fn),
                              l.once});
      }
      for (SnapEntry& snap : snapshot)
      {
        if (EventStopRequested(isolate, context, event)) return;
        v8::Local<v8::Function> fn =
            v8::Local<v8::Function>::New(isolate, snap.fn);
        v8::TryCatch try_catch(isolate);
        (void)fn->Call(context, current_target, 1, &event_value)
            .FromMaybe(v8::Local<v8::Value>());
        if (try_catch.HasCaught())
          CJavascriptException::ThrowIf(isolate, try_catch);
        if (snap.once)
        {
          auto& ls = entry.listeners;
          ls.erase(std::remove_if(
                       ls.begin(), ls.end(),
                       [&snap, &type, &capture](const DomState::ElListener& l) {
                         return l.type == type && l.capture == capture &&
                                l.fn == snap.fn;
                       }),
                   ls.end());
        }
      }
    }

    void ElementDispatchEvent(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr || info.Length() < 1 || !info[0]->IsObject())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: dispatchEvent requires an Event"));
        return;
      }
      if (NodeOf(info.This()) == nullptr)
      {
        WindowEventDispatch(info);
        return;
      }
      v8::Local<v8::Object> event = info[0].As<v8::Object>();
      std::string type = EventTypeOf(isolate, context, event);
      if (type.empty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: Event type missing on dispatchEvent"));
        return;
      }
      v8::Local<v8::Object> target = info.This().As<v8::Object>();
      SetEventField(isolate, context, event, "target", target);
      {
        // trusted input dispatch pre-sets isTrusted=true; only default it
        v8::Local<v8::Value> trusted;
        if (!event
                 ->HasOwnProperty(context,
                                  ToV8Str(isolate, "isTrusted"))
                 .FromMaybe(false) ||
            !event->Get(context, ToV8Str(isolate, "isTrusted"))
                 .ToLocal(&trusted) ||
            !trusted->BooleanValue(isolate))
        {
          SetEventField(isolate, context, event, "isTrusted",
                        v8::Boolean::New(isolate, false));
        }
      }

      // reference's DOM host intentionally has a target-only dispatcher: its
      // JS-visible eventPhase is AT_TARGET for target listeners, but it does
      // not walk ancestor capture/bubble listeners.  This is measured by
      // tools/event_oracle.py against the wheel, not inferred from browsers.
      // Target listeners still retain registration order (capture first if
      // registered first), and only stopImmediatePropagation interrupts that
      // same-target sequence.
      {
        DomState::ElTarget* entry = FindElTarget(dom, isolate, target);
        if (entry != nullptr)
        {
          v8::Local<v8::Value> ct = target;
          SetEventField(isolate, context, event, "currentTarget", ct);
          SetEventField(isolate, context, event, "eventPhase",
                        v8::Integer::New(isolate, 2));
          struct SnapEntry
          {
            v8::Global<v8::Function> fn;
            bool once;
          };
          std::vector<SnapEntry> snapshot;
          for (DomState::ElListener& l : entry->listeners)
          {
            if (l.type == type)
              snapshot.push_back({v8::Global<v8::Function>(isolate, l.fn),
                                  l.once});
          }
          for (SnapEntry& snap : snapshot)
          {
            if (EventImmediateStopRequested(isolate, context, event)) break;
            v8::Local<v8::Function> fn =
                v8::Local<v8::Function>::New(isolate, snap.fn);
            v8::Local<v8::Value> event_value = event;
            v8::TryCatch try_catch(isolate);
            (void)fn->Call(context, target, 1, &event_value)
                .FromMaybe(v8::Local<v8::Value>());
            if (try_catch.HasCaught())
              CJavascriptException::ThrowIf(isolate, try_catch);
            if (snap.once)
            {
              auto& ls = entry->listeners;
              ls.erase(std::remove_if(
                           ls.begin(), ls.end(),
                           [&snap, &type](const DomState::ElListener& l) {
                             return l.type == type && l.fn == snap.fn;
                           }),
                       ls.end());
            }
          }
          // on<type> property handlers fire in the target phase too.
          v8::Local<v8::Value> handler;
          if (target
                  ->Get(context, ToV8Str(isolate, "on" + type))
                  .ToLocal(&handler) &&
              handler->IsFunction())
          {
            v8::Local<v8::Value> event_value = event;
            v8::TryCatch try_catch(isolate);
            (void)handler.As<v8::Function>()
                ->Call(context, target, 1, &event_value)
                .FromMaybe(v8::Local<v8::Value>());
            if (try_catch.HasCaught())
              CJavascriptException::ThrowIf(isolate, try_catch);
          }
        }
      }
      SetEventField(isolate, context, event, "currentTarget", v8::Null(isolate));
      SetEventField(isolate, context, event, "eventPhase",
                    v8::Integer::New(isolate, 0));
      info.GetReturnValue().Set(true);
    }

    // ------------------------------------------------ specialized URL props

    // img[src]/a[href]/script[src]/link[href] resolve against the location
    void ResolvedAttrGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      v8::String::Utf8Value name_v(isolate, info.Data());
      std::string attr_name(*name_v, name_v.length());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
      {
        info.GetReturnValue().Set(ToV8Str(isolate, ""));
        return;
      }
      std::string raw = ElementAttribute(
          lxb_dom_interface_element(node), attr_name);
      // The reference wheel exposes HTMLScriptElement.src as the literal attribute
      // string, including relative SDK paths, while other URL attributes
      // use the resolved form.
      if (attr_name == "src")
      {
        size_t tag_len = 0;
        const lxb_char_t* tag_name = lxb_dom_node_name(node, &tag_len);
        if (tag_name != nullptr && tag_len == 6 &&
            std::equal(tag_name, tag_name + tag_len, "script",
                       [](lxb_char_t a, char b) {
                         return std::tolower(static_cast<unsigned char>(a))
                                == b;
                       }))
        {
          info.GetReturnValue().Set(ToV8Str(isolate, raw));
          return;
        }
      }
      // An absent src is the empty URL on HTMLScriptElement and other
      // resource elements.  Resolving it against the page URL makes inline
      // scripts appear to have an external source to page code.
      if (raw.empty() && attr_name == "src")
      {
        info.GetReturnValue().Set(ToV8Str(isolate, ""));
        return;
      }
      std::string resolved = ResolveUrlString(isolate, context, raw);
      info.GetReturnValue().Set(ToV8Str(isolate, resolved));
    }

    void NumberAttrGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      v8::String::Utf8Value name_v(isolate, info.Data());
      std::string attr_name(*name_v, name_v.length());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
      {
        info.GetReturnValue().Set(0);
        return;
      }
      std::string raw = ElementAttribute(
          lxb_dom_interface_element(node), attr_name);
      info.GetReturnValue().Set(std::atoi(raw.c_str()));
    }

    // HTMLCanvasElement dimensions have browser defaults even when the DOM
    // attribute is absent.  reference exposes these as inherited accessors and the
    // WAF reads both before it starts its canvas fingerprint.
    void CanvasDimensionGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      v8::String::Utf8Value name_v(isolate, info.Data());
      std::string name(*name_v, name_v.length());
      const int fallback = name == "width" ? 300 : 150;
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
      {
        info.GetReturnValue().Set(fallback);
        return;
      }
      std::string raw = ElementAttribute(lxb_dom_interface_element(node), name);
      info.GetReturnValue().Set(raw.empty() ? fallback : std::atoi(raw.c_str()));
    }

    void CanvasCaptureStream(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      // The reference's offline canvas shell returns an otherwise empty
      // object.  Keep the capability shape without inventing a live media
      // pipeline that the deterministic host does not provide.
      info.GetReturnValue().Set(v8::Object::New(info.GetIsolate()));
    }

    void PlainAttrGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      v8::String::Utf8Value name_v(isolate, info.Data());
      std::string attr_name(*name_v, name_v.length());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
      {
        info.GetReturnValue().Set(ToV8Str(isolate, ""));
        return;
      }
      info.GetReturnValue().Set(ToV8Str(
          isolate,
          ElementAttribute(lxb_dom_interface_element(node), attr_name)));
    }

    // ---------------------------------------------------------- Image() ctor

    void ImageConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr || dom->doc == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: Image requires a document"));
        return;
      }
      lxb_dom_element_t* el = lxb_dom_document_create_element(
          &dom->doc->dom_document, (const lxb_char_t*)"img", 3, nullptr);
      if (el == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: element allocation failed"));
        return;
      }
      if (info.Length() >= 1 && info[0]->IsNumber())
      {
        std::string w = ToUtf8(isolate, info[0]);
        lxb_dom_element_set_attribute(el, (const lxb_char_t*)"width", 5,
                                      (const lxb_char_t*)w.c_str(), w.size());
      }
      if (info.Length() >= 2 && info[1]->IsNumber())
      {
        std::string h = ToUtf8(isolate, info[1]);
        lxb_dom_element_set_attribute(el, (const lxb_char_t*)"height", 6,
                                      (const lxb_char_t*)h.c_str(), h.size());
      }
      info.GetReturnValue().Set(
          WrapNode(isolate, context, lxb_dom_interface_node(el)));
    }

    // ------------------------------------------------ shadow DOM surface

    // A shadow root is a real lexbor document fragment wrapped like any
    // node; host/mode/delegatesFocus ride as own properties. Content lives
    // outside the light DOM, so textContent/queries on ancestors ignore it.

    void ElementAttachShadow(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* host = NodeOf(info.This());
      DomState* dom = GetDomState(context);
      if (host == nullptr || dom == nullptr || dom->doc == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: attachShadow requires an element"));
        return;
      }
      auto existing = dom->shadow_roots.find(host);
      if (existing != dom->shadow_roots.end())
      {
        info.GetReturnValue().Set(
            WrapNode(isolate, context, existing->second));
        return;
      }
      lxb_dom_document_fragment_t* frag =
          lxb_dom_document_create_document_fragment(&dom->doc->dom_document);
      if (frag == nullptr)
        return;
      lxb_dom_node_t* root = lxb_dom_interface_node(frag);
      dom->shadow_roots[host] = root;
      v8::Local<v8::Object> root_obj = WrapNode(isolate, context, root);
      (void)root_obj->Set(context, ToV8Str(isolate, "host"), info.This())
          .FromMaybe(false);
      v8::Local<v8::Value> mode =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0].As<v8::Object>()
                    ->Get(context, ToV8Str(isolate, "mode"))
                    .FromMaybe(v8::Local<v8::Value>())
              : v8::Local<v8::Value>();
      (void)root_obj
          ->Set(context, ToV8Str(isolate, "mode"),
                mode->IsString()
                    ? mode
                    : v8::Local<v8::Value>(ToV8Str(isolate, "open")))
          .FromMaybe(false);
      bool delegates_focus =
          info.Length() >= 1 && info[0]->IsObject() &&
          info[0].As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "delegatesFocus"))
              .FromMaybe(v8::Local<v8::Value>())
              ->BooleanValue(isolate);
      (void)root_obj
          ->Set(context, ToV8Str(isolate, "delegatesFocus"),
                v8::Boolean::New(isolate, delegates_focus))
          .FromMaybe(false);
      info.GetReturnValue().Set(root_obj);
    }

    void GetShadowRoot(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* host = NodeOf(info.This());
      DomState* dom = GetDomState(context);
      if (host == nullptr || dom == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      auto it = dom->shadow_roots.find(host);
      if (it == dom->shadow_roots.end())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, it->second));
    }

    // ------------------------------------------------- collection surface

    // Collections hide their items in a Private symbol array. Indexed reads
    // go through item(); `length` is a read-only accessor, matching the
    // browser shape (no writable index properties).

    v8::Local<v8::Array> CollectionNodes(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> self)
    {
      v8::Local<v8::Value> nodes_val;
      if (!self
              ->GetPrivate(context,
                           v8::Private::ForApi(
                               isolate,
                               ToV8Str(isolate, "lv8.dom.collection.nodes")))
              .ToLocal(&nodes_val) ||
          !nodes_val->IsArray())
        return v8::Local<v8::Array>();
      return nodes_val.As<v8::Array>();
    }

    // Indexed reads resolve through the interceptor so collection[i] works
    // while staying read-only (browsers expose live collections this way).
    v8::Intercepted CollectionIndexedGet(
        uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> nodes =
          CollectionNodes(isolate, context, info.This());
      if (nodes.IsEmpty() || index >= nodes->Length())
        return v8::Intercepted::kNo;
      info.GetReturnValue().Set(
          nodes->Get(context, index)
              .FromMaybe(v8::Local<v8::Value>(v8::Undefined(isolate))));
      return v8::Intercepted::kYes;
    }

    void CollectionLength(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> nodes =
          CollectionNodes(isolate, context, info.This());
      if (nodes.IsEmpty())
      {
        info.GetReturnValue().Set(0);
        return;
      }
      info.GetReturnValue().Set(nodes->Length());
    }

    void NodeList_Item(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> nodes =
          CollectionNodes(isolate, context, info.This());
      if (nodes.IsEmpty() || info.Length() < 1 || !info[0]->IsNumber())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      int32_t index = info[0]->Int32Value(context).FromMaybe(0);
      if (index < 0 || static_cast<uint32_t>(index) >= nodes->Length())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(
          nodes->Get(context, static_cast<uint32_t>(index))
              .FromMaybe(v8::Local<v8::Value>(v8::Undefined(isolate))));
    }

    void NodeList_ForEach(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> nodes =
          CollectionNodes(isolate, context, info.This());
      if (nodes.IsEmpty() || info.Length() < 1 || !info[0]->IsFunction())
        return;
      v8::Local<v8::Function> cb = info[0].As<v8::Function>();
      v8::Local<v8::Value> this_arg =
          info.Length() > 1
              ? info[1]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
      for (uint32_t i = 0; i < nodes->Length(); i++)
      {
        v8::Local<v8::Value> argv[3] = {
            nodes->Get(context, i).FromMaybe(
                v8::Local<v8::Value>(v8::Undefined(isolate))),
            v8::Integer::New(isolate, i), info.This()};
        v8::TryCatch try_catch(isolate);
        (void)cb->Call(context, this_arg, 3, argv)
            .FromMaybe(v8::Local<v8::Value>());
        if (try_catch.HasCaught())
          CJavascriptException::ThrowIf(isolate, try_catch);
      }
    }

    void HTMLCollection_Item(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      NodeList_Item(info);
    }

    // NodeList is iterable (for-of, spread); the hidden item array's own
    // array iterator, bound to that array, provides the semantics.
    // HTMLCollection stays non-iterable, like real browsers.
    void NodeListIterator(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> nodes =
          CollectionNodes(isolate, context, info.This());
      if (nodes.IsEmpty())
        return;
      v8::Local<v8::Value> it_fn;
      if (!nodes
               ->Get(context, v8::Symbol::GetIterator(isolate))
               .ToLocal(&it_fn) ||
          !it_fn->IsFunction())
        return;
      v8::Local<v8::Value> it = it_fn.As<v8::Function>()
                                    ->Call(context, nodes, 0, nullptr)
                                    .FromMaybe(v8::Local<v8::Value>());
      info.GetReturnValue().Set(it);
    }

    void HTMLCollection_NamedItem(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> nodes =
          CollectionNodes(isolate, context, info.This());
      if (nodes.IsEmpty() || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      std::string wanted = ToUtf8(isolate, info[0]);
      for (uint32_t i = 0; i < nodes->Length(); i++)
      {
        v8::Local<v8::Value> item =
            nodes->Get(context, i).FromMaybe(v8::Local<v8::Value>());
        if (!item->IsObject())
          continue;
        lxb_dom_node_t* node = NodeOf(item.As<v8::Object>());
        if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
          continue;
        lxb_dom_element_t* el = lxb_dom_interface_element(node);
        for (const char* attr : {"id", "name"})
        {
          size_t len = 0;
          const lxb_char_t* value = lxb_dom_element_get_attribute(
              el, (const lxb_char_t*)attr, std::strlen(attr), &len);
          if (value != nullptr &&
              wanted == std::string((const char*)value, len))
          {
            info.GetReturnValue().Set(item);
            return;
          }
        }
      }
      info.GetReturnValue().SetNull();
    }

    // Shared per-context collection templates (one NodeList / one
    // HTMLCollection constructor each, so instanceof holds across reads).

    void EnsureCollectionTemplates(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomState* dom)
    {
      (void)context;
      auto ensure = [&](v8::Global<v8::FunctionTemplate>& slot,
                        const char* class_name, bool html) {
        if (!slot.IsEmpty())
          return;
        v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
        tpl->SetClassName(ToV8Str(isolate, class_name));
        v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, class_name),
                  v8::PropertyAttribute::ReadOnly);
        inst->SetHandler(
            v8::IndexedPropertyHandlerConfiguration(CollectionIndexedGet));
        {
          v8::Local<v8::String> key = ToV8Str(isolate, "length");
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, CollectionLength);
          fn->SetClassName(ToV8Str(isolate, "get length"));
          inst->SetAccessorProperty(key, fn,
                                    v8::Local<v8::FunctionTemplate>());
        }
        auto m = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          tpl->PrototypeTemplate()->Set(key, fn);
        };
        m("item", html ? HTMLCollection_Item : NodeList_Item);
        if (html)
          m("namedItem", HTMLCollection_NamedItem);
        else
        {
          m("forEach", NodeList_ForEach);
          tpl->PrototypeTemplate()->Set(
              v8::Symbol::GetIterator(isolate),
              v8::FunctionTemplate::New(isolate, NodeListIterator));
        }
        slot.Reset(isolate, tpl);
      };
      ensure(dom->nl_tpl, "NodeList", false);
      ensure(dom->hc_tpl, "HTMLCollection", true);
    }

    v8::Local<v8::Object> MakeCollectionInstance(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Global<v8::FunctionTemplate>& slot,
        const std::vector<v8::Local<v8::Value>>& items)
    {
      v8::Local<v8::Object> obj = slot.Get(isolate)
                                      ->InstanceTemplate()
                                      ->NewInstance(context)
                                      .ToLocalChecked();
      v8::Local<v8::Array> nodes =
          v8::Array::New(isolate, static_cast<int>(items.size()));
      for (size_t i = 0; i < items.size(); i++)
      {
        (void)nodes->Set(context, static_cast<uint32_t>(i), items[i])
            .FromMaybe(false);
      }
      (void)obj
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate,
                           ToV8Str(isolate, "lv8.dom.collection.nodes")),
                       nodes)
          .FromMaybe(false);
      return obj;
    }

    // ------------------------------------------------- child/element lists

    void GetChildNodes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      DomState* dom = GetDomState(context);
      if (node == nullptr || dom == nullptr)
      {
        info.GetReturnValue().Set(v8::Array::New(isolate));
        return;
      }
      std::vector<v8::Local<v8::Value>> items;
      for (lxb_dom_node_t* child = node->first_child; child != nullptr;
           child = child->next)
        items.push_back(WrapNode(isolate, context, child));
      EnsureCollectionTemplates(isolate, context, dom);
      info.GetReturnValue().Set(
          MakeCollectionInstance(isolate, context, dom->nl_tpl, items));
    }

    void GetChildren(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node = NodeOf(info.This());
      DomState* dom = GetDomState(context);
      if (node == nullptr || dom == nullptr)
      {
        info.GetReturnValue().Set(v8::Array::New(isolate));
        return;
      }
      // reference quirk (G4.222): document.children aliases the document
      // element's children - [HEAD, BODY] - instead of [HTML]
      if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT)
      {
        for (lxb_dom_node_t* child = node->first_child; child != nullptr;
             child = child->next)
        {
          if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
          {
            node = child;
            break;
          }
        }
      }
      std::vector<lxb_dom_node_t*> elements;
      for (lxb_dom_node_t* child = node->first_child; child != nullptr;
           child = child->next)
      {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
          elements.push_back(child);
      }
      EnsureCollectionTemplates(isolate, context, dom);
      std::vector<v8::Local<v8::Value>> items;
      for (lxb_dom_node_t* el : elements)
        items.push_back(WrapNode(isolate, context, el));
      info.GetReturnValue().Set(
          MakeCollectionInstance(isolate, context, dom->hc_tpl, items));
    }

    // G4.154: reference is the sole behavioral reference (the behavioral oracle
    // and the Python reference implementation were retired).  reference answers
    // empty document.images/links/scripts/forms with null - the quirk the
    // challenge fingerprints - while non-empty matches stay collections.
    void GetDocSelectorCollection(
        const v8::FunctionCallbackInfo<v8::Value>& info, const char* selector,
        bool null_when_empty = false)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (dom == nullptr || doc_node == nullptr)
      {
        EnsureCollectionTemplates(isolate, context, dom);
        info.GetReturnValue().Set(
            MakeCollectionInstance(isolate, context, dom->hc_tpl, {}));
        return;
      }
      lxb_dom_node_t* root =
          lxb_dom_document_root(lxb_dom_interface_document(doc_node));
      std::vector<lxb_dom_node_t*> found;
      if (root != nullptr) (void)SelectNodes(dom, root, selector, found);
      EnsureCollectionTemplates(isolate, context, dom);
      std::vector<v8::Local<v8::Value>> items;
      for (lxb_dom_node_t* el : found)
        items.push_back(WrapNode(isolate, context, el));
      if (null_when_empty && items.empty())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(
          MakeCollectionInstance(isolate, context, dom->hc_tpl, items));
    }

    void GetImages(const v8::FunctionCallbackInfo<v8::Value>& info)
    { GetDocSelectorCollection(info, "img", true); }
    void GetLinks(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      // links = <a href> / <area href>; lexbor selectors support [href]
      GetDocSelectorCollection(info, "a[href]", true);
    }
    void GetAnchors(const v8::FunctionCallbackInfo<v8::Value>& info)
    { GetDocSelectorCollection(info, "a[name]", true); }
    void GetScripts(const v8::FunctionCallbackInfo<v8::Value>& info)
    { GetDocSelectorCollection(info, "script", true); }

    void GetForms(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      // G4.331 (reference ground truth, symaudit + g2-scene probes): the
      // reference's document.forms is ALWAYS null - even on pages with
      // <form> elements (work/reference-traces/_m19* / _m3_symaudit evidence).
      // The HTMLCollection return here was an lv8 invention and carried an
      // own Symbol.toStringTag the reference does not have.
      (void)info;
      info.GetReturnValue().SetNull();
    }

    // ------------------------------------------------- custom elements

    // The shared element method surface. It lives on HTMLElement.prototype
    // (element_proto) so custom element wrappers keep working after their
    // prototype is swapped to the user class prototype, which extends
    // HTMLElement per the JS `class X extends HTMLElement` clause.
    struct ElementMethod
    {
      const char* name;
      v8::FunctionCallback cb;
      // G4.240: FunctionTemplate arity (FunctionTemplate::SetLength);
      // the Node-level members carry reference's measured lengths
      int len;
    };


    // polyfill-registered element surface (JS-implemented DOM APIs);
    // __lv8ElementPatches: name -> function (prototype methods),
    // __lv8ElementAccessors: name -> getter function (accessor properties)
    void InstallPolyfillPatches(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                v8::Local<v8::ObjectTemplate> tmpl)
    {
      v8::Local<v8::Value> patches;
      patches = InternalBridgeGet(isolate, context, "elementPatches");
      if (patches->IsObject())
      {
        v8::Local<v8::Array> keys = patches.As<v8::Object>()
            ->GetOwnPropertyNames(context).ToLocalChecked();
        for (uint32_t i = 0; i < keys->Length(); i++)
        {
          v8::Local<v8::Value> key = keys->Get(context, i).ToLocalChecked();
          v8::Local<v8::Value> fn;
          if (key->IsString() &&
              patches.As<v8::Object>()->Get(context, key).ToLocal(&fn) &&
              fn->IsFunction())
          {
            v8::Local<v8::FunctionTemplate> method_tpl =
                v8::FunctionTemplate::New(
                    isolate,
                    [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                      v8::Isolate* iso = info.GetIsolate();
                      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                      v8::Local<v8::Function> patch =
                          info.Data().As<v8::Function>();
                      std::vector<v8::Local<v8::Value>> args;
                      for (int i = 0; i < info.Length(); i++)
                        args.push_back(info[i]);
                      v8::Local<v8::Value> result;
                      if (patch->Call(ctx, info.This(),
                                      info.Length(),
                                      args.empty() ? nullptr : args.data())
                              .ToLocal(&result))
                        info.GetReturnValue().Set(result);
                    },
                    fn);
            method_tpl->SetClassName(key.As<v8::String>());
            tmpl->Set(key.As<v8::String>(), method_tpl);
          }
        }
      }
      v8::Local<v8::Value> accessors;
      accessors = InternalBridgeGet(isolate, context, "elementAccessors");
      if (accessors->IsObject())
      {
        v8::Local<v8::Array> keys = accessors.As<v8::Object>()
            ->GetOwnPropertyNames(context).ToLocalChecked();
        for (uint32_t i = 0; i < keys->Length(); i++)
        {
          v8::Local<v8::Value> key = keys->Get(context, i).ToLocalChecked();
          v8::Local<v8::Value> fn;
          if (key->IsString() &&
              accessors.As<v8::Object>()->Get(context, key).ToLocal(&fn) &&
              fn->IsFunction())
          {
            v8::Local<v8::FunctionTemplate> get_tpl =
                v8::FunctionTemplate::New(
                    isolate,
                    [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                      v8::Isolate* iso = info.GetIsolate();
                      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                      v8::Local<v8::Function> get_fn =
                          info.Data().As<v8::Function>();
                      v8::Local<v8::Value> result;
                      if (get_fn->Call(ctx, info.This(), 0, nullptr)
                              .ToLocal(&result))
                        info.GetReturnValue().Set(result);
                    },
                    fn);
            get_tpl->SetClassName(ToV8Str(
                isolate, std::string("get ") + *v8::String::Utf8Value(
                             isolate, key)));
            // optional setter: __lv8ElementAccessorsSetters[name]
            v8::Local<v8::FunctionTemplate> set_tpl =
                v8::Local<v8::FunctionTemplate>();
            v8::Local<v8::Value> setters;
            setters = InternalBridgeGet(isolate, context,
                                         "elementAccessorsSetters");
            if (setters->IsObject())
            {
              v8::Local<v8::Value> set_fn;
              if (setters.As<v8::Object>()
                      ->Get(context, key)
                      .ToLocal(&set_fn) &&
                  set_fn->IsFunction())
              {
                set_tpl = v8::FunctionTemplate::New(
                    isolate,
                    [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                      if (info.Length() < 1) return;
                      v8::Isolate* iso = info.GetIsolate();
                      v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                      v8::Local<v8::Function> set_callback =
                          info.Data().As<v8::Function>();
                      v8::Local<v8::Value> argv[] = {info[0]};
                      (void)set_callback->Call(ctx, info.This(), 1, argv)
                          .FromMaybe(v8::Local<v8::Value>());
                    },
                    set_fn);
                set_tpl->SetClassName(ToV8Str(
                    isolate, std::string("set ") + *v8::String::Utf8Value(
                                 isolate, key)));
              }
            }
            tmpl->SetAccessorProperty(key.As<v8::String>(), get_tpl,
                                      set_tpl);
          }
        }
      }
    }

    // ---- G4.20: element canvas surface ----------------------------------
    // Replaces the JS elementMethods canvas block (polyfills3) and the
    // webgl augmentation chunks (polyfills4): deterministic profile-driven
    // contexts with no real GPU work, per the behavior oracle.

    bool CanvasIsCanvasElement(v8::Local<v8::Object> self)
    {
      lxb_dom_node_t* node = NodeOf(self);
      if (node == nullptr) return false;
      const lxb_char_t* name;
      size_t len;
      name = lxb_dom_node_name(node, &len);
      std::string tag((const char*)name, len);
      for (auto& c : tag)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return tag == "canvas";
    }

    std::string CanvasLower(v8::Isolate* isolate, v8::Local<v8::Value> value)
    {
      std::string out = ToUtf8(isolate, value);
      for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return out;
    }

    void CanvasRecord(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> self, v8::Local<v8::Value> name)
    {
      v8::Local<v8::Value> calls;
      if (!self
               ->GetPrivate(context,
                            v8::Private::ForApi(
                                isolate, ToV8Str(isolate, "lv8.canvas.calls")))
               .ToLocal(&calls) ||
          !calls->IsArray())
        return;
      v8::Local<v8::Array> list = calls.As<v8::Array>();
      v8::Local<v8::Array> record = v8::Array::New(isolate, 1);
      (void)record->Set(context, 0, name).FromMaybe(false);
      (void)list->Set(context, list->Length(), record).FromMaybe(false);
    }

    void CanvasRecordCallback(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      CanvasRecord(isolate, context, info.This(), info.Data());
    }

    v8::Local<v8::Value> CanvasPixelData(
        v8::Isolate* isolate, v8::Local<v8::Context> context, double width,
        double height)
    {
      v8::Local<v8::Object> out = v8::Object::New(isolate);
      (void)out->Set(context, ToV8Str(isolate, "width"),
                     v8::Number::New(isolate, width))
          .FromMaybe(false);
      (void)out->Set(context, ToV8Str(isolate, "height"),
                     v8::Number::New(isolate, height))
          .FromMaybe(false);
      size_t bytes = static_cast<size_t>(width > 0 ? width : 0) *
                     static_cast<size_t>(height > 0 ? height : 0) * 4;
      v8::Local<v8::ArrayBuffer> buffer =
          v8::ArrayBuffer::New(isolate, bytes);
      v8::Local<v8::Value> u8_ctor;
      v8::Local<v8::Value> data;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "Uint8ClampedArray"))
              .ToLocal(&u8_ctor) &&
          u8_ctor->IsFunction())
      {
        v8::Local<v8::Value> argv[3] = {
            buffer, v8::Integer::New(isolate, 0),
            v8::Integer::New(isolate, static_cast<int32_t>(bytes))};
        if (u8_ctor.As<v8::Function>()
                ->NewInstance(context, 3, argv)
                .ToLocal(&data))
          (void)out->Set(context, ToV8Str(isolate, "data"), data)
              .FromMaybe(false);
      }
      return out;
    }

    void CanvasGetImageData(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double w = info.Length() >= 3 && info[2]->IsNumber()
          ? info[2]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      double h = info.Length() >= 4 && info[3]->IsNumber()
          ? info[3]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      info.GetReturnValue().Set(
          CanvasPixelData(isolate, context, w, h));
    }

    void CanvasCreateImageData(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double w = info.Length() >= 1 && info[0]->IsNumber()
          ? info[0]->NumberValue(context).FromMaybe(0.0) : 0.0;
      double h = info.Length() >= 2 && info[1]->IsNumber()
          ? info[1]->NumberValue(context).FromMaybe(0.0) : 0.0;
      info.GetReturnValue().Set(CanvasPixelData(isolate, context, w, h));
    }

    v8::Local<v8::Value> CanvasProfileValue(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const char* section, const char* key)
    {
      ContextState* state = GetState(context);
      if (state == nullptr || state->profile_data.IsEmpty())
        return v8::Local<v8::Value>();
      v8::Local<v8::Value> section_obj;
      if (!state->profile_data.Get(isolate)
               ->Get(context, ToV8Str(isolate, section))
               .ToLocal(&section_obj) ||
          !section_obj->IsObject())
        return v8::Local<v8::Value>();
      v8::Local<v8::Value> value;
      if (!section_obj.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, key))
               .ToLocal(&value) ||
          value.IsEmpty() || value->IsUndefined())
        return v8::Local<v8::Value>();
      return value;
    }

    std::string CanvasProfileString(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const char* key, const char* fallback)
    {
      v8::Local<v8::Value> value =
          CanvasProfileValue(isolate, context, "webgl", key);
      if (!value.IsEmpty() && value->IsString())
        return ToUtf8(isolate, value);
      return fallback;
    }

    void CanvasGlGetParameter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      int64_t name = info.Length() >= 1
          ? info[0]->IntegerValue(context).FromMaybe(0)
          : 0;
      if (name == 37445 || name == 7936)
        info.GetReturnValue().Set(ToV8Str(
            isolate, CanvasProfileString(isolate, context, "vendor",
                                         "Google Inc. (NVidia)")));
      else if (name == 37446 || name == 7937)
        info.GetReturnValue().Set(ToV8Str(
            isolate, CanvasProfileString(isolate, context, "renderer",
                                         "NVidia GeForce")));
      else if (name == 7938)
        info.GetReturnValue().Set(ToV8Str(
            isolate, CanvasProfileString(isolate, context, "version",
                                         "WebGL 1.0")));
      else if (name == 35724)
        info.GetReturnValue().Set(ToV8Str(
            isolate, CanvasProfileString(
                         isolate, context, "shadingVersion",
                         "WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)")));
      else if (name == 3379)
        // MAX_TEXTURE_SIZE: reference gold reports a real GPU limit; 0 is an
        // immediately-detectable stub value
        info.GetReturnValue().Set(v8::Integer::New(isolate, 16384));
      else if (name == 3386)
      {
        // MAX_VIEWPORT_DIMS: two-element array (callers .join() it)
        v8::Local<v8::Array> dims = v8::Array::New(isolate, 2);
        (void)dims->Set(context, 0, v8::Integer::New(isolate, 32767))
            .FromMaybe(false);
        (void)dims->Set(context, 1, v8::Integer::New(isolate, 32767))
            .FromMaybe(false);
        info.GetReturnValue().Set(dims);
      }
      else if (name == 33902)
      {
        // ALIASED_LINE_WIDTH_RANGE
        v8::Local<v8::Array> range = v8::Array::New(isolate, 2);
        (void)range->Set(context, 0, v8::Integer::New(isolate, 1))
            .FromMaybe(false);
        (void)range->Set(context, 1, v8::Integer::New(isolate, 1))
            .FromMaybe(false);
        info.GetReturnValue().Set(range);
      }
      else if (name == 34921)
        info.GetReturnValue().Set(v8::Integer::New(isolate, 16)); // MAX_VERTEX_ATTRIBS
      else if (name == 36348)
        info.GetReturnValue().Set(v8::Integer::New(isolate, 30)); // MAX_VARYING_VECTORS
      else if (name == 36347)
        info.GetReturnValue().Set(v8::Integer::New(isolate, 4096)); // MAX_VERTEX_UNIFORM_VECTORS
      else if (name == 36349)
        info.GetReturnValue().Set(v8::Integer::New(isolate, 1024)); // MAX_FRAGMENT_UNIFORM_VECTORS
      else
        info.GetReturnValue().Set(v8::Integer::New(isolate, 0));
    }

    void CanvasGlGetSupportedExtensions(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      // reference gold parity: getSupportedExtensions() reports an empty list
      // while getExtension(name) still resolves — the list key is
      // separate from the getExtension validation key.
      v8::Local<v8::Value> extensions =
          CanvasProfileValue(isolate, context, "webgl", "supportedExtensions");
      if (extensions.IsEmpty() || !extensions->IsArray())
      {
        info.GetReturnValue().Set(v8::Array::New(isolate, 0));
        return;
      }
      v8::Local<v8::Array> list = extensions.As<v8::Array>();
      v8::Local<v8::Array> out = v8::Array::New(isolate, list->Length());
      for (uint32_t i = 0; i < list->Length(); i++)
      {
        v8::Local<v8::Value> item;
        if (list->Get(context, i).ToLocal(&item))
          (void)out->Set(context, i, item).FromMaybe(false);
      }
      info.GetReturnValue().Set(out);
    }

    void CanvasGlGetExtension(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      // JS parity: the query is compared verbatim (case-sensitive)
      std::string name = info.Length() >= 1
          ? ToUtf8(isolate, info[0])
          : std::string();
      v8::Local<v8::Value> extensions =
          CanvasProfileValue(isolate, context, "webgl", "extensions");
      bool supported = false;
      if (!extensions.IsEmpty() && extensions->IsArray())
      {
        v8::Local<v8::Array> list = extensions.As<v8::Array>();
        for (uint32_t i = 0; i < list->Length(); i++)
        {
          v8::Local<v8::Value> item;
          if (list->Get(context, i).ToLocal(&item) && item->IsString() &&
              ToUtf8(isolate, item) == name)
          {
            supported = true;
            break;
          }
        }
      }
      if (!supported)
      {
        info.GetReturnValue().Set(v8::Null(isolate));
        return;
      }
      if (name == "WEBGL_debug_renderer_info")
      {
        v8::Local<v8::Object> info_obj = v8::Object::New(isolate);
        (void)info_obj
            ->Set(context, ToV8Str(isolate, "UNMASKED_VENDOR_WEBGL"),
                  v8::Integer::New(isolate, 0x9245))
            .FromMaybe(false);
        (void)info_obj
            ->Set(context, ToV8Str(isolate, "UNMASKED_RENDERER_WEBGL"),
                  v8::Integer::New(isolate, 0x9246))
            .FromMaybe(false);
        info.GetReturnValue().Set(info_obj);
        return;
      }
      info.GetReturnValue().Set(v8::Object::New(isolate));
    }

    v8::Local<v8::Value> CanvasStubObject(v8::Isolate* isolate,
                                          v8::Local<v8::Context> context,
                                          const std::string& tag);

    void CanvasGlPrecisionGetter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string name = info.Data().IsEmpty() ? std::string()
          : ToUtf8(isolate, info.Data());
      v8::Local<v8::Value> value;
      if (info.This()
              ->GetPrivate(context, v8::Private::ForApi(
                  isolate, ToV8Str(isolate, "lv8.webgl.precision." + name)))
              .ToLocal(&value))
        info.GetReturnValue().Set(value);
    }

    void CanvasGlShaderPrecision(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      // reference returns branded, prototype-backed precision objects.  Floating
      // formats are 127/127/23 and integer formats are 31/30/0.
      const int64_t type = info.Length() >= 2
          ? info[1]->IntegerValue(context).FromMaybe(0) : 0;
      const bool integer = type == 36339 || type == 36340 || type == 36341;
      v8::Local<v8::Object> format = CanvasStubObject(
          isolate, context, "WebGLShaderPrecisionFormat").As<v8::Object>();
      (void)format->SetPrivate(context, v8::Private::ForApi(
          isolate, ToV8Str(isolate, "lv8.webgl.precision.rangeMin")),
          v8::Integer::New(isolate, integer ? 31 : 127)).FromMaybe(false);
      (void)format->SetPrivate(context, v8::Private::ForApi(
          isolate, ToV8Str(isolate, "lv8.webgl.precision.rangeMax")),
          v8::Integer::New(isolate, integer ? 30 : 127)).FromMaybe(false);
      (void)format->SetPrivate(context, v8::Private::ForApi(
          isolate, ToV8Str(isolate, "lv8.webgl.precision.precision")),
          v8::Integer::New(isolate, integer ? 0 : 23)).FromMaybe(false);
      info.GetReturnValue().Set(format);
    }

    void CanvasGlGetError(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 0));
    }

    void CanvasGlGetShaderParameter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    void CanvasGlGetShaderInfoLog(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), ""));
    }

    void CanvasGlGetProgramParameter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    void CanvasGlGetAttribLocation(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      // The oracle's deterministic stub resolves its first attribute to 0.
      info.GetReturnValue().Set(v8::Integer::New(isolate, 0));
    }

    void CanvasGlGetUniformLocation(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string name = info.Length() >= 2
          ? ToUtf8(isolate, info[1])
          : std::string();
      v8::Local<v8::Object> location = CanvasStubObject(
          isolate, context, "WebGLUniformLocation").As<v8::Object>();
      info.GetReturnValue().Set(location);
    }

    v8::Local<v8::Value> CanvasStubObject(v8::Isolate* isolate,
                                          v8::Local<v8::Context> context,
                                          const std::string& tag)
    {
      v8::Local<v8::Object> stub = v8::Object::New(isolate);
      // The generated global interface already installs the reference constructors.
      // Native factories must return instances of those shared prototypes,
      // not plain objects carrying an observable ``_lv8`` marker.
      v8::Local<v8::Value> ctor;
      if (context->Global()->Get(context, ToV8Str(isolate, tag)).ToLocal(&ctor) &&
          ctor->IsFunction())
      {
        v8::Local<v8::Value> proto;
        if (ctor.As<v8::Function>()
                ->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&proto) && proto->IsObject())
          (void)stub->SetPrototype(context, proto).FromMaybe(false);
      }
      (void)stub->DefineOwnProperty(
          context, v8::Symbol::GetToStringTag(isolate), ToV8Str(isolate, tag),
          v8::PropertyAttribute::ReadOnly).FromMaybe(false);
      return stub;
    }

    void CanvasGlCreateBuffer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(CanvasStubObject(
          info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
          "WebGLBuffer"));
    }

    void CanvasGlCreateShader(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(CanvasStubObject(
          info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
          "WebGLShader"));
    }

    void CanvasGlCreateProgram(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(CanvasStubObject(
          info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
          "WebGLProgram"));
    }

    void CanvasMeasureText(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string text = info.Length() >= 1 && !info[0]->IsUndefined()
          ? ToUtf8(isolate, info[0])
          : "";
      v8::Local<v8::Object> metrics = v8::Object::New(isolate);
      // reference's stub is font-independent: UTF-8 byte length times eight.
      // This is also visible to the WAF's canvas fingerprint routine.
      (void)metrics
          ->Set(context, ToV8Str(isolate, "width"),
                v8::Number::New(isolate,
                                static_cast<double>(text.size()) * 8.0))
          .FromMaybe(false);
      info.GetReturnValue().Set(metrics);
    }

    v8::Local<v8::Object> CanvasMake2D(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> canvas)
    {
      v8::Local<v8::Object> obj = v8::Object::New(isolate);
      // The context's constructor and implemented method surface are shared
      // across every canvas.  Keep per-context drawing state on |obj|, but
      // expose the callable WebIDL face through the global prototype.
      v8::Local<v8::Function> shared_ctor;
      v8::Local<v8::Value> existing_ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "CanvasRenderingContext2D"))
              .ToLocal(&existing_ctor) && existing_ctor->IsFunction())
      {
        shared_ctor = existing_ctor.As<v8::Function>();
      }
      else
      {
        v8::Local<v8::FunctionTemplate> ctor_tpl =
            v8::FunctionTemplate::New(isolate);
        ctor_tpl->SetClassName(ToV8Str(isolate, "CanvasRenderingContext2D"));
        shared_ctor = ctor_tpl->GetFunction(context).ToLocalChecked();
        (void)context->Global()
            ->DefineOwnProperty(context,
                                ToV8Str(isolate, "CanvasRenderingContext2D"),
                                shared_ctor,
                                v8::PropertyAttribute::DontEnum)
            .FromMaybe(false);
      }
      v8::Local<v8::Value> shared_proto;
      if (shared_ctor->Get(context, ToV8Str(isolate, "prototype"))
              .ToLocal(&shared_proto) && shared_proto->IsObject())
      {
        // The bootstrap may have supplied the constructor before the first
        // context.  Populate its prototype here rather than only while
        // materializing a new constructor, so both paths have the same face.
        v8::Local<v8::Object> proto = shared_proto.As<v8::Object>();
        auto define_method = [&](const char* name, v8::FunctionCallback callback,
                                 bool record_name) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn = record_name
              ? v8::FunctionTemplate::New(isolate, callback, key)
              : v8::FunctionTemplate::New(isolate, callback);
          fn->SetClassName(key);
          (void)proto->Set(context, key,
                           fn->GetFunction(context).ToLocalChecked())
              .FromMaybe(false);
        };
        static const char* const kRecordMethods[] = {
            "fillRect", "strokeRect", "clearRect", "beginPath", "closePath",
            "moveTo", "lineTo", "arc", "fill", "stroke", "fillText",
            "strokeText", "drawImage", "putImageData", "save", "restore",
            "translate", "scale", "rotate"};
        for (const char* name : kRecordMethods)
          define_method(name, CanvasRecordCallback, true);
        define_method("measureText", CanvasMeasureText, false);
        define_method("getImageData", CanvasGetImageData, false);
        define_method("createImageData", CanvasCreateImageData, false);
        (void)obj->SetPrototype(context, proto).FromMaybe(false);
      }
      (void)obj->Set(context, ToV8Str(isolate, "canvas"), canvas)
          .FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "fillStyle"),
                     ToV8Str(isolate, "#000000"))
          .FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "strokeStyle"),
                     ToV8Str(isolate, "#000000"))
          .FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "lineWidth"),
                     v8::Number::New(isolate, 1))
          .FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "font"),
                     ToV8Str(isolate, "10px sans-serif"))
          .FromMaybe(false);
      (void)obj->Set(context, ToV8Str(isolate, "globalAlpha"),
                     v8::Number::New(isolate, 1))
          .FromMaybe(false);
      (void)obj
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate, ToV8Str(isolate, "lv8.canvas.calls")),
                       v8::Array::New(isolate))
          .FromMaybe(false);
      (void)obj
          ->DefineOwnProperty(context, v8::Symbol::GetToStringTag(isolate),
                              ToV8Str(isolate, "CanvasRenderingContext2D"),
                              v8::PropertyAttribute::ReadOnly)
          .FromMaybe(false);
      return obj;
    }

    v8::Local<v8::Object> CanvasMakeWebGL(v8::Isolate* isolate,
                                          v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> canvas)
    {
      v8::Local<v8::Object> obj = v8::Object::New(isolate);
      (void)obj->Set(context, ToV8Str(isolate, "canvas"), canvas)
          .FromMaybe(false);
      (void)obj
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate, ToV8Str(isolate, "lv8.canvas.calls")),
                       v8::Array::New(isolate))
          .FromMaybe(false);
      static const char* kConstants[] = {
          "ARRAY_BUFFER",       "ELEMENT_ARRAY_BUFFER", "STATIC_DRAW",
          "VERTEX_SHADER",      "FRAGMENT_SHADER",      "COMPILE_STATUS",
          "LINK_STATUS",        "COLOR_BUFFER_BIT",     "TRIANGLES",
          "FLOAT",              "DEPTH_TEST",           "BLEND"};
      for (const char* name : kConstants)
      {
        (void)obj
            ->Set(context, ToV8Str(isolate, name),
                  v8::Integer::New(isolate,
                                   static_cast<int32_t>(0x1000 +
                                                        std::strlen(name))))
            .FromMaybe(false);
      }
      auto record = [&](const char* name) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, CanvasRecordCallback, key);
        fn->SetClassName(key);
        (void)obj
            ->Set(context, key, fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      };
      record("bindBuffer");
      record("bufferData");
      record("shaderSource");
      record("compileShader");
      record("attachShader");
      record("useProgram");
      record("viewport");
      record("clearColor");
      record("clear");
      record("drawArrays");
      record("drawElements");
      auto special = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        (void)obj
            ->Set(context, key, fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      };
      special("getParameter", CanvasGlGetParameter);
      special("getSupportedExtensions", CanvasGlGetSupportedExtensions);
      special("getExtension", CanvasGlGetExtension);
      special("getShaderPrecisionFormat", CanvasGlShaderPrecision);
      special("getError", CanvasGlGetError);
      special("getShaderParameter", CanvasGlGetShaderParameter);
      special("getShaderInfoLog", CanvasGlGetShaderInfoLog);
      special("getProgramParameter", CanvasGlGetProgramParameter);
      special("getAttribLocation", CanvasGlGetAttribLocation);
      special("getUniformLocation", CanvasGlGetUniformLocation);
      special("createBuffer", CanvasGlCreateBuffer);
      special("createShader", CanvasGlCreateShader);
      special("createProgram", CanvasGlCreateProgram);
      // The Python bootstrap publishes the reference-derived WebGL interface at
      // page start.  Native contexts retain their private drawing state, but
      // bind implemented callbacks to that shared prototype so WebIDL
      // methods/constants do not leak as per-context own properties.
      v8::Local<v8::Value> webgl_ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "WebGLRenderingContext"))
              .ToLocal(&webgl_ctor) && webgl_ctor->IsFunction())
      {
        v8::Local<v8::Value> webgl_proto;
        if (webgl_ctor.As<v8::Function>()
                ->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&webgl_proto) && webgl_proto->IsObject())
        {
          v8::Local<v8::Object> proto = webgl_proto.As<v8::Object>();
          static const char* const kNativeMethods[] = {
              "bindBuffer", "bufferData", "shaderSource", "compileShader",
              "attachShader", "useProgram", "viewport", "clearColor", "clear",
              "drawArrays", "drawElements", "getParameter",
              "getSupportedExtensions", "getExtension", "getShaderPrecisionFormat",
              "getError", "getShaderParameter", "getShaderInfoLog",
              "getProgramParameter", "getAttribLocation", "getUniformLocation",
              "createBuffer", "createShader", "createProgram"};
          for (const char* name : kNativeMethods)
          {
            v8::Local<v8::String> key = ToV8Str(isolate, name);
            v8::Local<v8::Value> value;
            if (obj->Get(context, key).ToLocal(&value))
              (void)proto->Set(context, key, value).FromMaybe(false);
            (void)obj->Delete(context, key).FromMaybe(false);
          }
          // The interface generator supplies the three names but only knows
          // their descriptor shape.  Bind their getters to the private values
          // carried by each native precision result.
          v8::Local<v8::Value> precision_ctor;
          if (context->Global()
                  ->Get(context, ToV8Str(isolate, "WebGLShaderPrecisionFormat"))
                  .ToLocal(&precision_ctor) && precision_ctor->IsFunction())
          {
            v8::Local<v8::Value> precision_proto;
            if (precision_ctor.As<v8::Function>()
                    ->Get(context, ToV8Str(isolate, "prototype"))
                    .ToLocal(&precision_proto) && precision_proto->IsObject())
            {
              for (const char* name : {"rangeMin", "rangeMax", "precision"})
              {
                v8::Local<v8::String> key = ToV8Str(isolate, name);
                v8::Local<v8::Function> getter =
                    v8::FunctionTemplate::New(isolate, CanvasGlPrecisionGetter,
                                              key)
                        ->GetFunction(context).ToLocalChecked();
                precision_proto.As<v8::Object>()->SetAccessorProperty(
                    key, getter, v8::Local<v8::Function>(),
                    v8::PropertyAttribute::None);
              }
            }
          }
          // Existing native constants were synthetic placeholders; delete
          // them so reads resolve to the reference-measured values on |proto|.
          for (const char* name : kConstants)
            (void)obj->Delete(context, ToV8Str(isolate, name)).FromMaybe(false);
          (void)obj->SetPrototype(context, proto).FromMaybe(false);
        }
      }
      (void)obj
          ->DefineOwnProperty(context, v8::Symbol::GetToStringTag(isolate),
                              ToV8Str(isolate, "WebGLRenderingContext"),
                              v8::PropertyAttribute::ReadOnly)
          .FromMaybe(false);
      return obj;
    }

    // shared per-canvas context cache: kind must already be lowercased
    v8::Local<v8::Value> CanvasContextFor(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Object> canvas, const std::string& kind)
    {
      v8::Local<v8::Value> cache;
      if (!canvas
               ->GetPrivate(context,
                            v8::Private::ForApi(
                                isolate, ToV8Str(isolate, "lv8.canvas.contexts")))
               .ToLocal(&cache) ||
          !cache->IsObject())
      {
        v8::Local<v8::Object> created = v8::Object::New(isolate);
        (void)canvas
            ->SetPrivate(context,
                         v8::Private::ForApi(
                             isolate, ToV8Str(isolate, "lv8.canvas.contexts")),
                         created)
            .FromMaybe(false);
        cache = created;
      }
      v8::Local<v8::Value> existing;
      if (cache.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, kind))
              .ToLocal(&existing) &&
          !existing->IsUndefined())
        return existing;
      v8::Local<v8::Value> built;
      if (kind == "2d")
        built = CanvasMake2D(isolate, context, canvas);
      else if (kind == "webgl" || kind == "experimental-webgl")
        built = CanvasMakeWebGL(isolate, context, canvas);
      if (built.IsEmpty())
        return v8::Null(isolate);
      (void)cache.As<v8::Object>()
          ->Set(context, ToV8Str(isolate, kind), built)
          .FromMaybe(false);
      return built;
    }

    void CanvasGetContext(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!CanvasIsCanvasElement(info.This()))
        return;
      std::string kind = info.Length() >= 1 && !info[0]->IsUndefined()
          ? CanvasLower(isolate, info[0])
          : std::string();
      info.GetReturnValue().Set(
          CanvasContextFor(isolate, context, info.This(), kind));
    }

    bool CanvasBase64Decode(const std::string& in, std::string& out)
    {
      static const std::string alphabet =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      int val = 0;
      int bits = 0;
      out.clear();
      for (char c : in)
      {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t')
          continue;
        size_t index = alphabet.find(c);
        if (index == std::string::npos)
          return false;
        val = (val << 6) + static_cast<int>(index);
        bits += 6;
        if (bits >= 8)
        {
          bits -= 8;
          out.push_back(static_cast<char>((val >> bits) & 0xff));
        }
      }
      return true;
    }

    void CanvasToDataURL(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string type = "image/png";
      if (info.Length() >= 1 && !info[0]->IsUndefined() &&
          !info[0]->IsNull())
        type = ToUtf8(isolate, info[0]);
      // profile fingerprint overrides: short name ("png") then full type
      std::string short_name = type;
      if (short_name.rfind("image/", 0) == 0)
        short_name = short_name.substr(6);
      size_t semi = short_name.find(';');
      if (semi != std::string::npos)
        short_name = short_name.substr(0, semi);
      v8::Local<v8::Value> fingerprint = CanvasProfileValue(
          isolate, context, "canvas", "fingerprint");
      if (!fingerprint.IsEmpty() && fingerprint->IsObject())
      {
        v8::Local<v8::Value> overrides;
        if (fingerprint.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "toDataURL"))
                .ToLocal(&overrides) &&
            overrides->IsObject())
        {
          v8::Local<v8::Value> hit;
          if (overrides.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, short_name))
                  .ToLocal(&hit) &&
              !hit->IsUndefined())
          {
            info.GetReturnValue().Set(hit);
            return;
          }
          if (overrides.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, type))
                  .ToLocal(&hit) &&
              !hit->IsUndefined())
          {
            info.GetReturnValue().Set(hit);
            return;
          }
        }
      }
      // reference gold: the stub canvas returns an EMPTY data URL (no rendering),
      // and the WAF accepts that; the 1x1 placeholder PNG previously
      // returned here was a detectable divergence.
      info.GetReturnValue().Set(ToV8Str(isolate, ""));
    }

    void CanvasToBlobRunner(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> callback, blob;
      if (!data->Get(context, 0).ToLocal(&callback) ||
          !data->Get(context, 1).ToLocal(&blob) || !callback->IsFunction())
        return;
      v8::Local<v8::Value> argv[1] = {blob};
      v8::TryCatch try_catch(isolate);
      (void)callback.As<v8::Function>()
          ->Call(context, context->Global(), 1, argv)
          .FromMaybe(v8::Local<v8::Value>());
      try_catch.Reset();
    }

    void CanvasToBlob(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      // JS signature: toBlob(callback, type = "image/png")
      std::string type = "image/png";
      if (info.Length() >= 2 && !info[1]->IsUndefined() &&
          !info[1]->IsNull())
        type = ToUtf8(isolate, info[1]);
      v8::Local<v8::Value> callback;
      if (info.Length() >= 1 && info[0]->IsFunction())
        callback = info[0];
      // render the data URL through the element's own toDataURL
      std::string data_url;
      {
        v8::Local<v8::Value> to_data_url;
        if (info.This()
                ->Get(context, ToV8Str(isolate, "toDataURL"))
                .ToLocal(&to_data_url) &&
            to_data_url->IsFunction())
        {
          v8::Local<v8::Value> argv[1] = {ToV8Str(isolate, type)};
          v8::Local<v8::Value> rendered;
          v8::TryCatch try_catch(isolate);
          if (to_data_url.As<v8::Function>()
                  ->Call(context, info.This(), 1, argv)
                  .ToLocal(&rendered) &&
              rendered->IsString())
            data_url = ToUtf8(isolate, rendered);
          try_catch.Reset();
        }
      }
      std::string mime = type;
      std::string payload;
      bool decoded = false;
      std::string bytes;
      const std::string marker = ";base64,";
      if (data_url.rfind("data:", 0) == 0)
      {
        size_t base = data_url.find(marker);
        if (base != std::string::npos)
        {
          mime = data_url.substr(5, base - 5);
          if (mime.empty())
            mime = type;
          if (CanvasBase64Decode(data_url.substr(base + marker.size()),
                                 bytes))
          {
            decoded = true;
            payload = bytes;
          }
        }
      }
      v8::Local<v8::Value> blob_ctor;
      v8::Local<v8::Value> blob;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "Blob"))
              .ToLocal(&blob_ctor) &&
          blob_ctor->IsFunction())
      {
        v8::Local<v8::Array> parts = v8::Array::New(isolate, 1);
        v8::Local<v8::Value> part;
        if (decoded)
        {
          v8::Local<v8::ArrayBuffer> buffer =
              v8::ArrayBuffer::New(isolate, payload.size());
          if (!payload.empty())
            std::memcpy(buffer->GetBackingStore()->Data(), payload.data(),
                        payload.size());
          v8::Local<v8::Value> u8_ctor;
          if (context->Global()
                  ->Get(context, ToV8Str(isolate, "Uint8Array"))
                  .ToLocal(&u8_ctor) &&
              u8_ctor->IsFunction())
          {
            v8::Local<v8::Value> argv[3] = {
                buffer, v8::Integer::New(isolate, 0),
                v8::Integer::New(isolate,
                                 static_cast<int32_t>(payload.size()))};
            if (u8_ctor.As<v8::Function>()
                    ->NewInstance(context, 3, argv)
                    .ToLocal(&part))
            {
              // keep the typed array part
            }
            else
              part.Clear();
          }
        }
        if (part.IsEmpty())
          part = ToV8Str(isolate, "");
        (void)parts->Set(context, 0, part).FromMaybe(false);
        v8::Local<v8::Object> options = v8::Object::New(isolate);
        (void)options->Set(context, ToV8Str(isolate, "type"),
                           ToV8Str(isolate, mime))
            .FromMaybe(false);
        v8::Local<v8::Value> argv[2] = {parts, options};
        if (blob_ctor.As<v8::Function>()
                ->NewInstance(context, 2, argv)
                .ToLocal(&blob))
        {
          if (!callback.IsEmpty())
          {
            v8::Local<v8::Array> payload_pair = v8::Array::New(isolate, 2);
            (void)payload_pair->Set(context, 0, callback).FromMaybe(false);
            (void)payload_pair->Set(context, 1, blob).FromMaybe(false);
            v8::Local<v8::FunctionTemplate> runner =
                v8::FunctionTemplate::New(isolate, CanvasToBlobRunner,
                                          payload_pair);
            runner->SetClassName(ToV8Str(isolate, ""));
            v8::Local<v8::Value> queue;
            if (context->Global()
                    ->Get(context, ToV8Str(isolate, "queueMicrotask"))
                    .ToLocal(&queue) &&
                queue->IsFunction())
            {
              v8::Local<v8::Value> queue_argv[1] = {
                  runner->GetFunction(context).ToLocalChecked()};
              v8::TryCatch try_catch(isolate);
              (void)queue.As<v8::Function>()
                  ->Call(context, context->Global(), 1, queue_argv)
                  .FromMaybe(v8::Local<v8::Value>());
              try_catch.Reset();
            }
          }
          info.GetReturnValue().Set(blob);
          return;
        }
      }
    }

    // ---- OffscreenCanvas (native G4 chunk; replaces the polyfills3 JS
    // class with the same deterministic context-local surface) -----------

    void CanvasTransferControlToOffscreen(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      // width/height live properties fall back to attributes, then 300x150
      auto dimension = [&](const char* prop, double fallback) {
        v8::Local<v8::Value> value;
        if (info.This()
                ->Get(context, ToV8Str(isolate, prop))
                .ToLocal(&value) &&
            value->IsNumber() &&
            value->NumberValue(context).FromMaybe(0.0) > 0)
          return value->NumberValue(context).FromMaybe(fallback);
        v8::Local<v8::Value> attr;
        std::string name = std::string(prop);
        if (info.This()
                ->Get(context,
                      ToV8Str(isolate, "getAttribute"))
                .ToLocal(&attr) &&
            attr->IsFunction())
        {
          v8::Local<v8::Value> argv[1] = {ToV8Str(isolate, prop)};
          v8::Local<v8::Value> raw;
          v8::TryCatch try_catch(isolate);
          if (attr.As<v8::Function>()
                  ->Call(context, info.This(), 1, argv)
                  .ToLocal(&raw) &&
              !raw.IsEmpty() && !raw->IsNullOrUndefined())
          {
            double parsed = std::strtod(ToUtf8(isolate, raw).c_str(),
                                        nullptr);
            try_catch.Reset();
            if (parsed > 0)
              return parsed;
          }
          try_catch.Reset();
        }
        return fallback;
      };
      double width = dimension("width", 300.0);
      double height = dimension("height", 150.0);
      v8::Local<v8::Value> ctor;
      if (!context->Global()
                   ->Get(context, ToV8Str(isolate, "OffscreenCanvas"))
                   .ToLocal(&ctor) ||
          !ctor->IsFunction())
        return;
      v8::Local<v8::Value> argv[2] = {v8::Number::New(isolate, width),
                                      v8::Number::New(isolate, height)};
      v8::Local<v8::Value> offscreen;
      if (ctor.As<v8::Function>()
              ->NewInstance(context, 2, argv)
              .ToLocal(&offscreen))
        info.GetReturnValue().Set(offscreen);
    }

    void OffscreenCanvasCtor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: OffscreenCanvas constructor requires new"));
        return;
      }
      double width = info.Length() >= 1 && info[0]->IsNumber()
          ? info[0]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      double height = info.Length() >= 2 && info[1]->IsNumber()
          ? info[1]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      // M11: dimensions are private-backed prototype accessors - instance
      // own data would pollute the own-face (reference instances are zero-own)
      (void)info.This()
          ->SetPrivate(context,
                       v8::Private::ForApi(isolate,
                                           ToV8Str(isolate, "lv8.oc.width")),
                       v8::Number::New(isolate, width))
          .FromMaybe(false);
      (void)info.This()
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate, ToV8Str(isolate, "lv8.oc.height")),
                       v8::Number::New(isolate, height))
          .FromMaybe(false);
    }

    void OffscreenCanvasGetContext(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string kind = info.Length() >= 1 && !info[0]->IsUndefined()
          ? CanvasLower(isolate, info[0])
          : std::string();
      if (kind != "2d" && kind != "webgl" && kind != "experimental-webgl")
      {
        info.GetReturnValue().Set(v8::Null(isolate));
        return;
      }
      info.GetReturnValue().Set(
          CanvasContextFor(isolate, context, info.This(), kind));
    }

    void OffscreenCanvasTransferToImageBitmap(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "ImageBitmap"))
              .ToLocal(&ctor) &&
          ctor->IsFunction())
      {
        v8::Local<v8::Value> width, height;
        bool ok = info.This()
                      ->Get(context, ToV8Str(isolate, "width"))
                      .ToLocal(&width);
        ok = ok && info.This()
                       ->Get(context, ToV8Str(isolate, "height"))
                       .ToLocal(&height);
        if (ok)
        {
          v8::Local<v8::Value> argv[2] = {width, height};
          v8::Local<v8::Value> bitmap;
          if (ctor.As<v8::Function>()
                  ->NewInstance(context, 2, argv)
                  .ToLocal(&bitmap))
          {
            info.GetReturnValue().Set(bitmap);
            return;
          }
        }
      }
      info.GetReturnValue().Set(v8::Undefined(isolate));
    }

    void OffscreenCanvasConvertToBlob(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      std::string type = "image/png";
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        v8::Local<v8::Value> requested;
        if (info[0].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "type"))
                .ToLocal(&requested) &&
            !requested->IsUndefined())
          type = ToUtf8(isolate, requested);
      }
      v8::Local<v8::Value> blob_ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "Blob"))
              .ToLocal(&blob_ctor) &&
          blob_ctor->IsFunction())
      {
        v8::Local<v8::Array> parts = v8::Array::New(isolate, 1);
        (void)parts->Set(context, 0, ToV8Str(isolate, "")).FromMaybe(false);
        v8::Local<v8::Object> options = v8::Object::New(isolate);
        (void)options->Set(context, ToV8Str(isolate, "type"),
                           ToV8Str(isolate, type))
            .FromMaybe(false);
        v8::Local<v8::Value> argv[2] = {parts, options};
        v8::Local<v8::Value> blob;
        if (blob_ctor.As<v8::Function>()
                ->NewInstance(context, 2, argv)
                .ToLocal(&blob))
        {
          v8::Local<v8::Promise::Resolver> resolver =
              v8::Promise::Resolver::New(context).ToLocalChecked();
          (void)resolver->Resolve(context, blob).FromMaybe(false);
          info.GetReturnValue().Set(resolver->GetPromise());
        }
      }
    }

    void InstallOffscreenCanvasGlobal(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context)
    {
      v8::Local<v8::FunctionTemplate> ctor =
          v8::FunctionTemplate::New(isolate, OffscreenCanvasCtor);
      ctor->SetClassName(ToV8Str(isolate, "OffscreenCanvas"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "OffscreenCanvas"),
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
      method("getContext", OffscreenCanvasGetContext);
      method("transferToImageBitmap", OffscreenCanvasTransferToImageBitmap);
      method("convertToBlob", OffscreenCanvasConvertToBlob);
      // M11: width/height as private-backed prototype accessors (reference
      // reads back the ctor dimensions and accepts writes; the receiver
      // stays zero-own).  The private key name rides in Data.
      auto oc_dimension = [&](const char* name) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        std::string priv_name = std::string("lv8.oc.") + name;
        v8::Local<v8::FunctionTemplate> get_fn = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              v8::Local<v8::Value> v;
              if (!info.Data()->IsString() ||
                  !info.This()
                       ->GetPrivate(ctx,
                                    v8::Private::ForApi(
                                        iso, info.Data().As<v8::String>()))
                       .ToLocal(&v) ||
                  !v->IsNumber())
              {
                info.GetReturnValue().Set(0);
                return;
              }
              info.GetReturnValue().Set(v);
            },
            ToV8Str(isolate, priv_name));
        get_fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        v8::Local<v8::FunctionTemplate> set_fn = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              if (!info.Data()->IsString()) return;
              double v = info.Length() >= 1 && info[0]->IsNumber()
                  ? info[0]->NumberValue(ctx).FromMaybe(0.0)
                  : 0.0;
              (void)info.This()
                  ->SetPrivate(ctx,
                               v8::Private::ForApi(
                                   iso, info.Data().As<v8::String>()),
                               v8::Number::New(iso, v))
                  .FromMaybe(false);
            },
            ToV8Str(isolate, priv_name));
        set_fn->SetClassName(ToV8Str(isolate, std::string("set ") + name));
        ctor->PrototypeTemplate()->SetAccessorProperty(key, get_fn, set_fn);
      };
      oc_dimension("width");
      oc_dimension("height");
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "OffscreenCanvas"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    const ElementMethod kElementMethods[] = {
        {"getAttribute", GetAttribute, 0},
        {"setAttribute", SetAttribute, 0},
        {"hasAttribute", HasAttribute, 0},
        {"removeAttribute", RemoveAttribute, 0},
        {"toggleAttribute", ElementToggleAttribute, 0},
        {"getAttributeNode", ElementGetAttributeNode, 0},
        {"setAttributeNode", ElementSetAttributeNode, 0},
        {"appendChild", AppendChild, 1},
        {"removeChild", RemoveChild, 1},
        {"insertBefore", InsertBefore, 2},
        {"replaceChild", ReplaceChild, 2},
        {"insertAdjacentHTML", ElementInsertAdjacentHTML, 0},
        {"cloneNode", ElementCloneNode, 0},
        {"getElementsByTagName", ElementGetElementsByTagName, 0},
        {"getElementsByClassName", ElementGetElementsByClassName, 0},
        {"querySelector", ElementQuerySelector, 0},
        {"querySelectorAll", ElementQuerySelectorAll, 0},
        {"attachShadow", ElementAttachShadow, 0},
        {"contains", ElementContains, 1},
        {"compareDocumentPosition", ElementCompareDocumentPosition, 1},
        {"addEventListener", ElementAddEventListener, 0},
        {"removeEventListener", ElementRemoveEventListener, 0},
        {"dispatchEvent", ElementDispatchEvent, 0},
        {"getContext", CanvasGetContext, 0},
        {"toDataURL", CanvasToDataURL, 0},
        {"toBlob", CanvasToBlob, 0},
        {"transferControlToOffscreen", CanvasTransferControlToOffscreen, 0},
    };

    void EnsureElementProto(v8::Isolate* isolate,
                            v8::Local<v8::Context> context, DomState* dom)
    {
      if (!dom->element_proto.IsEmpty())
        return;
      // HTMLElement gets its methods through the FunctionTemplate's
      // prototype template: overriding a function's own "prototype"
      // property afterwards is refused by V8, so the auto-generated
      // prototype object IS the shared element surface.
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, "HTMLElement"));
      ConfigureElementInterfaceTemplate(isolate, "HTMLElement", tpl);
      // Mutation wrappers need the unwrapped native callbacks, but exposing
      // duplicate __lv8orig_* keys on HTMLElement.prototype is observable.
      // Keep that implementation table under the already-private control
      // bridge instead.
      v8::Local<v8::Object> original_methods;
      v8::Local<v8::Value> control;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "__iv8__"))
              .ToLocal(&control) && control->IsObject())
      {
        v8::Local<v8::Value> bridge;
        if (control.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "__lv8_internal__"))
                .ToLocal(&bridge) && bridge->IsObject())
        {
          v8::Local<v8::Value> existing;
          if (bridge.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "elementOriginals"))
                  .ToLocal(&existing) && existing->IsObject())
            original_methods = existing.As<v8::Object>();
          else
          {
            original_methods = v8::Object::New(isolate);
            (void)bridge.As<v8::Object>()
                ->Set(context, ToV8Str(isolate, "elementOriginals"),
                      original_methods)
                .FromMaybe(false);
          }
        }
      }
      for (const ElementMethod& m : kElementMethods)
      {
        v8::Local<v8::String> key = ToV8Str(isolate, m.name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, m.cb);
        fn->SetClassName(key);
        fn->SetLength(m.len);
        tpl->PrototypeTemplate()->Set(key, fn);
        // JS patch wrappers call the original through this alias
        v8::Local<v8::FunctionTemplate> orig_fn =
            v8::FunctionTemplate::New(isolate, m.cb);
        orig_fn->SetClassName(key);
        if (!original_methods.IsEmpty())
          (void)original_methods
              ->Set(context, key, orig_fn->GetFunction(context).ToLocalChecked())
              .FromMaybe(false);
      }
      InstallPolyfillPatches(isolate, context, tpl->PrototypeTemplate());
      v8::Local<v8::Function> ctor = tpl->GetFunction(context).ToLocalChecked();
      v8::Local<v8::Value> proto;
      if (!ctor->Get(context, ToV8Str(isolate, "prototype")).ToLocal(&proto) ||
          !proto->IsObject())
        throw std::runtime_error("lv8: HTMLElement prototype missing");
      dom->element_proto.Reset(isolate, proto.As<v8::Object>());
      dom->html_element_ctor.Reset(isolate, ctor);
      dom->html_element_tpl.Reset(isolate, tpl);

      // Element interface above HTMLElement in the prototype chain so
      // `element instanceof Element` holds for every wrapper
      v8::Local<v8::FunctionTemplate> el_tpl =
          v8::FunctionTemplate::New(isolate);
      el_tpl->SetClassName(ToV8Str(isolate, "Element"));
      v8::Local<v8::ObjectTemplate> el_inst = el_tpl->InstanceTemplate();
      el_inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      el_tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "Element"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      v8::Local<v8::Function> el_ctor =
          el_tpl->GetFunction(context).ToLocalChecked();
      v8::Local<v8::Value> el_proto;
      if (el_ctor->Get(context, ToV8Str(isolate, "prototype"))
              .ToLocal(&el_proto) &&
          el_proto->IsObject())
      {
        dom->element_iface_proto.Reset(isolate, el_proto.As<v8::Object>());
        (void)proto.As<v8::Object>()
            ->SetPrototype(context, el_proto.As<v8::Object>())
            .FromMaybe(false);
      }
      dom->element_iface_ctor.Reset(isolate, el_ctor);
      // G4.258: classList/attributes (Element) and dataset (HTMLElement) are
      // getter-only on the reference prototypes.  The live per-instance
      // objects are attached as V8 privates (invisible to gOPN) and read by
      // these accessors.
      {
        auto face_getter =
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              v8::Local<v8::Value> v;
              if (info.This()
                      ->GetPrivate(
                          ctx, v8::Private::ForApi(
                                   iso, info.Data().As<v8::String>()))
                      .ToLocal(&v))
                info.GetReturnValue().Set(v);
            };
        auto face_reg = [&](v8::Local<v8::Object> proto, const char* name,
                            const char* priv_name) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
              isolate, face_getter, ToV8Str(isolate, priv_name));
          fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          v8::Local<v8::Function> getter =
              fn->GetFunction(context).ToLocalChecked();
          proto->SetAccessorProperty(key, getter, v8::Local<v8::Function>());
        };
        if (!dom->element_iface_proto.IsEmpty())
        {
          v8::Local<v8::Object> ep = dom->element_iface_proto.Get(isolate);
          face_reg(ep, "classList", "lv8.el.classList");
          face_reg(ep, "attributes", "lv8.el.attributes");
        }
        if (!dom->element_proto.IsEmpty())
          face_reg(dom->element_proto.Get(isolate), "dataset",
                   "lv8.el.dataset");
      }

      {
        // EventTarget hook lives HERE (after Element.prototype exists) —
        // template inheritance materialization would clobber an earlier
        // manual SetPrototype on the HTMLElement prototype object
        v8::Local<v8::Value> et;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "EventTarget"))
                .ToLocal(&et) && et->IsFunction())
        {
          v8::Local<v8::Value> etp;
          if (et.As<v8::Function>()
                  ->Get(context, ToV8Str(isolate, "prototype"))
                  .ToLocal(&etp) && etp->IsObject())
            (void)el_proto.As<v8::Object>()
                ->SetPrototype(context, etp.As<v8::Object>())
                .FromMaybe(false);
        }
      }
      {
        // Node interface between Element and EventTarget.  Browsers expose
        // Node as a function whose prototype carries the canonical node
        // methods; the WAF SDK grabs exactly such borrowed natives
        // (Node.prototype.appendChild et al.) into its VM data table at
        // bootstrap.  A missing Node.prototype made those grabs resolve to
        // undefined and the signature callback later crashed on
        // undefined['call'] — the page-signature root cause.
        v8::Local<v8::FunctionTemplate> node_tpl =
            v8::FunctionTemplate::New(isolate);
        node_tpl->SetClassName(ToV8Str(isolate, "Node"));
        v8::Local<v8::ObjectTemplate> node_inst = node_tpl->InstanceTemplate();
        node_inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        node_tpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "Node"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        v8::Local<v8::Function> node_ctor =
            node_tpl->GetFunction(context).ToLocalChecked();
        // node-type constants live on the constructor (browser layout)
        static const std::pair<const char*, double> kNodeConsts[] = {
            {"ELEMENT_NODE", 1}, {"ATTRIBUTE_NODE", 2}, {"TEXT_NODE", 3},
            {"CDATA_SECTION_NODE", 4}, {"ENTITY_REFERENCE_NODE", 5},
            {"ENTITY_NODE", 6}, {"PROCESSING_INSTRUCTION_NODE", 7},
            {"COMMENT_NODE", 8}, {"DOCUMENT_NODE", 9},
            {"DOCUMENT_TYPE_NODE", 10}, {"DOCUMENT_FRAGMENT_NODE", 11},
            {"NOTATION_NODE", 12},
            {"DOCUMENT_POSITION_DISCONNECTED", 1},
            {"DOCUMENT_POSITION_PRECEDING", 2},
            {"DOCUMENT_POSITION_FOLLOWING", 4},
            {"DOCUMENT_POSITION_CONTAINS", 8},
            {"DOCUMENT_POSITION_CONTAINED_BY", 16},
            {"DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC", 32},
            {"NOTATION", 12}, {"ELEMENT", 1}, {"TEXT", 3}, {"COMMENT", 8},
            {"DOCUMENT", 9}, {"DOCUMENT_FRAGMENT", 11}, {"ATTRIBUTE", 2},
        };
        for (const auto& kc : kNodeConsts)
        {
          (void)node_ctor
              ->DefineOwnProperty(context, ToV8Str(isolate, kc.first),
                                  v8::Number::New(isolate, kc.second),
                                  static_cast<v8::PropertyAttribute>(
                                      v8::PropertyAttribute::DontEnum |
                                      v8::PropertyAttribute::DontDelete))
              .FromMaybe(false);
        }
        v8::Local<v8::Value> node_proto;
        if (node_ctor->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&node_proto) &&
            node_proto->IsObject())
        {
          auto node_proto_obj = node_proto.As<v8::Object>();
          // G4.240: the reference wheel's Node.prototype carries 18 numeric
          // constants (read-only, enumerable), 14 getter-only accessors and
          // 15 methods with real arities; measured in
          // tools/_nodeface_ab.py (work/reference-traces/_nodeface_ref.json).
          for (size_t ci = 0; ci < 18 && ci < std::size(kNodeConsts); ++ci)
          {
            (void)node_proto_obj
                ->DefineOwnProperty(context, ToV8Str(isolate, kNodeConsts[ci].first),
                                    v8::Number::New(isolate, kNodeConsts[ci].second),
                                    v8::PropertyAttribute::ReadOnly)
                .FromMaybe(false);
          }
          auto node_getter = [&](const char* name, v8::FunctionCallback cb) {
            v8::Local<v8::FunctionTemplate> fn =
                v8::FunctionTemplate::New(isolate, cb);
            fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
            v8::Local<v8::Function> getter =
                fn->GetFunction(context).ToLocalChecked();
            node_proto_obj->SetAccessorProperty(ToV8Str(isolate, name), getter,
                                               v8::Local<v8::Function>());
          };
          node_getter("nodeType", GetNodeType);
          node_getter("nodeName", GetNodeName);
          node_getter("baseURI", GetBaseURI);
          node_getter("isConnected", ElementIsConnected);
          node_getter("ownerDocument", GetOwnerDocument);
          node_getter("parentNode", GetParentNode);
          node_getter("parentElement", GetParentElement);
          node_getter("childNodes", GetChildNodes);
          node_getter("firstChild", GetFirstChild);
          node_getter("lastChild", GetLastChild);
          node_getter("previousSibling", GetPreviousSibling);
          node_getter("nextSibling", GetNextSibling);
          // G4.260: nodeValue is read/write like the reference wheel
          {
            v8::Local<v8::FunctionTemplate> nv_get =
                v8::FunctionTemplate::New(isolate, GetNodeValue);
            nv_get->SetClassName(ToV8Str(isolate, "get nodeValue"));
            v8::Local<v8::FunctionTemplate> nv_set =
                v8::FunctionTemplate::New(isolate, SetNodeValue);
            nv_set->SetClassName(ToV8Str(isolate, "set nodeValue"));
            node_proto_obj->SetAccessorProperty(
                ToV8Str(isolate, "nodeValue"),
                nv_get->GetFunction(context).ToLocalChecked(),
                nv_set->GetFunction(context).ToLocalChecked());
          }
          node_getter("textContent", GetTextContent);
          {
            struct NodeMember
            {
              const char* name;
              v8::FunctionCallback cb;
              int len;
            };
            static const NodeMember kNodeMembers[] = {
                {"appendChild", AppendChild, 1},
                {"removeChild", RemoveChild, 1},
                {"insertBefore", InsertBefore, 2},
                {"replaceChild", ReplaceChild, 2},
                {"cloneNode", ElementCloneNode, 0},
                {"contains", ElementContains, 1},
                {"compareDocumentPosition", ElementCompareDocumentPosition, 1},
                {"getRootNode", GetRootNode, 0},
                {"hasChildNodes", HasChildNodes, 0},
                {"isDefaultNamespace", IsDefaultNamespace, 1},
                {"isEqualNode", IsEqualNode, 1},
                {"isSameNode", IsSameNode, 1},
                {"lookupNamespaceURI", LookupNamespaceURI, 1},
                {"lookupPrefix", LookupPrefix, 1},
                {"normalize", Normalize, 0},
            };
            for (const NodeMember& m : kNodeMembers)
            {
              v8::Local<v8::FunctionTemplate> fn =
                  v8::FunctionTemplate::New(isolate, m.cb);
              fn->SetClassName(ToV8Str(isolate, m.name));
              fn->SetLength(m.len);
              v8::Local<v8::Function> mf =
                  fn->GetFunction(context).ToLocalChecked();
              (void)node_proto_obj
                  ->DefineOwnProperty(context, ToV8Str(isolate, m.name), mf)
                  .FromMaybe(false);
            }
          }
          dom->node_iface_proto.Reset(isolate, node_proto.As<v8::Object>());
          dom->node_iface_ctor.Reset(isolate, node_ctor);
          // chain: Element.prototype -> Node.prototype -> EventTarget.prototype
          v8::Local<v8::Value> etn;
          if (context->Global()
                  ->Get(context, ToV8Str(isolate, "EventTarget"))
                  .ToLocal(&etn) && etn->IsFunction())
          {
            v8::Local<v8::Value> etpn;
            if (etn.As<v8::Function>()
                    ->Get(context, ToV8Str(isolate, "prototype"))
                    .ToLocal(&etpn) && etpn->IsObject())
              (void)node_proto.As<v8::Object>()
                  ->SetPrototype(context, etpn.As<v8::Object>())
                  .FromMaybe(false);
          }
          (void)el_proto.As<v8::Object>()
              ->SetPrototype(context, node_proto.As<v8::Object>())
              .FromMaybe(false);
          // replace the placeholder constants object with the interface
          (void)context
              ->Global()
              ->DefineOwnProperty(context, ToV8Str(isolate, "Node"),
                                  node_ctor,
                                  static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
              .FromMaybe(false);
        }
      }
      // Set Symbol.toStringTag on each prototype object AFTER the chain
      // is fully linked. This ensures Object.prototype.toString.call()
      // returns the correct interface name at every level (the page SDK
      // checks these during prototype walks).
      {
        auto set_tag = [&](v8::Local<v8::Object> obj, const char* name) {
          obj->Set(context, v8::Symbol::GetToStringTag(isolate),
                   ToV8Str(isolate, name)).FromMaybe(false);
        };
        // HTMLElement.prototype (dom->element_proto was set from proto)
        v8::Local<v8::Value> html_proto;
        if (dom->element_proto.IsEmpty() == false)
          html_proto = dom->element_proto.Get(isolate);
        if (!html_proto.IsEmpty() && html_proto->IsObject())
          set_tag(html_proto.As<v8::Object>(), "HTMLElement");
        // Element.prototype (dom->element_iface_proto)
        v8::Local<v8::Value> el_p;
        if (dom->element_iface_proto.IsEmpty() == false)
          el_p = dom->element_iface_proto.Get(isolate);
        if (!el_p.IsEmpty() && el_p->IsObject())
          set_tag(el_p.As<v8::Object>(), "Element");
        // Node.prototype (dom->node_iface_proto)
        v8::Local<v8::Value> nd_p;
        if (dom->node_iface_proto.IsEmpty() == false)
          nd_p = dom->node_iface_proto.Get(isolate);
        if (!nd_p.IsEmpty() && nd_p->IsObject())
          set_tag(nd_p.As<v8::Object>(), "Node");
        // EventTarget.prototype from global
        v8::Local<v8::Value> et_c;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "EventTarget"))
                .ToLocal(&et_c) && et_c->IsFunction())
        {
          v8::Local<v8::Value> et_p;
          if (et_c.As<v8::Function>()
                  ->Get(context, ToV8Str(isolate, "prototype"))
                  .ToLocal(&et_p) && et_p->IsObject())
            set_tag(et_p.As<v8::Object>(), "EventTarget");
        }
      }
      {
        // Element.prototype carries the raw shared method surface so
        // prototype borrows (Element.prototype.getAttribute, ...) resolve.
        for (const ElementMethod& m : kElementMethods)
        {
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, m.cb);
          fn->SetClassName(ToV8Str(isolate, m.name));
          fn->SetLength(m.len);
          v8::Local<v8::Function> mf =
              fn->GetFunction(context).ToLocalChecked();
          (void)el_proto.As<v8::Object>()
              ->DefineOwnProperty(
                  context, ToV8Str(isolate, m.name), mf,
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::DontEnum))
              .FromMaybe(false);
        }
      }
    }

    // Fire a lifecycle callback of |name| on the wrapper if one exists on
    // its prototype chain. User exceptions propagate to Python.
    void FireLifecycle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Object> instance, const char* name)
    {
      v8::TryCatch try_catch(isolate);
      v8::Local<v8::Value> cb;
      if (!instance->Get(context, ToV8Str(isolate, name)).ToLocal(&cb) ||
          !cb->IsFunction())
        return;
      (void)cb.As<v8::Function>()->Call(context, instance, 0, nullptr)
          .FromMaybe(v8::Local<v8::Value>());
      if (try_catch.HasCaught())
        CJavascriptException::ThrowIf(isolate, try_catch);
    }

    // Run a custom element constructor over |instance|: class constructors
    // refuse plain calls, so `new` builds a plain object whose own
    // properties (this.x = ...) are migrated onto the wrapper, then the
    // wrapper's prototype becomes the user prototype (extends HTMLElement
    // -> shared element surface stays reachable).
    void CustomConstruct(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> instance,
                         v8::Local<v8::Function> ctor)
    {
      v8::TryCatch try_catch(isolate);
      v8::Local<v8::Object> created;
      if (!ctor->NewInstance(context).ToLocal(&created))
      {
        if (try_catch.HasCaught())
          CJavascriptException::ThrowIf(isolate, try_catch);
        return;
      }
      // constructor must see `this` as the wrapper: rebind by calling the
      // constructor body is impossible for classes, so migrate instead
      v8::Local<v8::Array> names =
          created->GetOwnPropertyNames(context).ToLocalChecked();
      for (uint32_t i = 0; i < names->Length(); i++)
      {
        v8::Local<v8::Value> key =
            names->Get(context, i).FromMaybe(v8::Local<v8::Value>());
        v8::Local<v8::Value> value;
        if (!key->IsString())
          continue;
        if (!created->Get(context, key).ToLocal(&value))
          continue;
        (void)instance->Set(context, key, value).FromMaybe(false);
      }
      v8::Local<v8::Value> proto;
      if (ctor->Get(context, ToV8Str(isolate, "prototype")).ToLocal(&proto) &&
          proto->IsObject())
      {
        (void)instance->SetPrototype(context, proto.As<v8::Object>())
            .FromMaybe(false);
      }
      if (try_catch.HasCaught())
        CJavascriptException::ThrowIf(isolate, try_catch);
    }

    void CustomElementsDefine(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr || dom->doc == nullptr)
        return;
      if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: customElements.define(name, constructor)"));
        return;
      }
      std::string tag = ToUtf8(isolate, info[0]);
      if (tag.find('-') == std::string::npos)
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "SyntaxError: custom element names must contain a dash"));
        return;
      }
      for (auto& c : tag)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      v8::Local<v8::Function> ctor = info[1].As<v8::Function>();
      dom->custom_definitions[tag].Reset(isolate, ctor);

      // upgrade already-parsed elements: rebuild their wrappers so the
      // identity changes (browser parity per the reference oracle), then
      // run constructor + connectedCallback
      lxb_dom_node_t* root =
          lxb_dom_document_root(&dom->doc->dom_document);
      if (root == nullptr)
        return;
      std::vector<lxb_dom_node_t*> found;
      if (!SelectNodes(dom, root, tag, found))
        return;
      for (lxb_dom_node_t* node : found)
      {
        dom->wraps.erase(node); // force a fresh wrapper
        v8::Local<v8::Object> fresh = WrapNode(isolate, context, node);
        if (fresh.IsEmpty())
          continue;
        FireLifecycle(isolate, context, fresh, "connectedCallback");
      }
    }

    void CustomElementsGet(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().SetUndefined();
        return;
      }
      std::string tag = ToUtf8(isolate, info[0]);
      for (auto& c : tag)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      auto it = dom->custom_definitions.find(tag);
      if (it == dom->custom_definitions.end())
      {
        info.GetReturnValue().SetUndefined();
        return;
      }
      info.GetReturnValue().Set(it->second.Get(isolate));
    }

    // connectedCallback for |node| and any custom descendants (the wrap
    // cache makes repeated wrapping of the same node idempotent)
    void FireConnectedIfCustom(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               lxb_dom_node_t* node)
    {
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return;
      DomState* dom = GetDomState(context);
      if (dom == nullptr || dom->custom_definitions.empty())
        return;
      std::vector<lxb_dom_node_t*> pending{node};
      size_t i = 0;
      while (i < pending.size())
      {
        lxb_dom_node_t* current = pending[i++];
        std::string tag;
        if (current->type == LXB_DOM_NODE_TYPE_ELEMENT)
        {
          const lxb_char_t* name;
          size_t len = 0;
          name = lxb_dom_node_name(current, &len);
          tag.assign((const char*)name, len);
          for (auto& c : tag)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (dom->custom_definitions.find(tag) !=
            dom->custom_definitions.end())
        {
          v8::Local<v8::Object> wrapper = WrapNode(isolate, context, current);
          if (!wrapper.IsEmpty())
            FireLifecycle(isolate, context, wrapper, "connectedCallback");
        }
        for (lxb_dom_node_t* c = current->first_child; c != nullptr;
             c = c->next)
          pending.push_back(c);
      }
    }

    // ------------------------------------------------- document surface

    void GetReadyState(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      lxb_dom_node_t* self = NodeOf(info.This());
      // documents parsed by DOMParser are complete the moment they exist
      if (dom != nullptr && dom->doc != nullptr && self != nullptr &&
          self != lxb_dom_interface_node(&dom->doc->dom_document))
      {
        info.GetReturnValue().Set(ToV8Str(isolate, "complete"));
        return;
      }
      info.GetReturnValue().Set(
          ToV8Str(isolate,
                  dom == nullptr ? "loading" : dom->ready_state));
    }

    void GetDocumentElement(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (doc_node == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      // Use the same lookup path as document.querySelector('html').  The
      // tree-builder root can otherwise retain a provisional public wrapper
      // when PAGE_LOAD_PRELUDE has already materialized element wrappers.
      lxb_dom_node_t* root = nullptr;
      DomState* dom = GetDomState(context);
      std::vector<lxb_dom_node_t*> selected;
      if (dom != nullptr && SelectNodes(dom, doc_node, "html", selected) &&
          !selected.empty())
        root = selected.front();
      if (root == nullptr)
        root = lxb_dom_document_root(lxb_dom_interface_document(doc_node));
      if (root == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, root));
    }

    // G4.222: the reference exposes the ParentNode element accessors on
    // the document (firstElementChild/lastElementChild resolve to the
    // document element; childElementCount to 1) - lv8 lacked them and the
    // d8y2 state machine's document-structure fields diverged.
    void GetDocFirstElementChild(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (doc_node == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      lxb_dom_node_t* root =
          lxb_dom_document_root(lxb_dom_interface_document(doc_node));
      if (root == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, root));
    }

    void GetDocLastElementChild(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      GetDocFirstElementChild(info);
    }

    void GetDocChildElementCount(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      double count = 0;
      if (doc_node != nullptr)
      {
        lxb_dom_node_t* root =
            lxb_dom_document_root(lxb_dom_interface_document(doc_node));
        if (root != nullptr)
          count = 1;
      }
      info.GetReturnValue().Set(v8::Number::New(isolate, count));
    }

    void GetDocDir(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      std::string dir;
      if (doc_node != nullptr)
      {
        lxb_dom_node_t* root =
            lxb_dom_document_root(lxb_dom_interface_document(doc_node));
        if (root != nullptr && root->type == LXB_DOM_NODE_TYPE_ELEMENT)
          dir = ElementAttribute(lxb_dom_interface_element(root), "dir");
      }
      info.GetReturnValue().Set(ToV8Str(isolate, dir));
    }

    void GetDocFullscreenEnabled(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), false));
    }

    void GetDocTimeline(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    void GetDocNull(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    void GetDocAdoptedStyleSheets(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(v8::Array::New(info.GetIsolate()));
    }

    void GetBody(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (doc_node == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      lxb_html_document_t* html_doc = lxb_html_interface_document(
          lxb_dom_interface_document(doc_node));
      if (html_doc->body == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(
          WrapNode(isolate, context,
                   lxb_dom_interface_node(html_doc->body)));
    }

    void GetHead(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (doc_node == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      lxb_html_document_t* html_doc = lxb_html_interface_document(
          lxb_dom_interface_document(doc_node));
      if (html_doc->head == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(
          WrapNode(isolate, context,
                   lxb_dom_interface_node(html_doc->head)));
    }

    // B1: undetectable document.all ---------------------------------------
    //
    // Browsers expose document.all as the one falsy, typeof-"undefined"
    // callable object.  The previous JS shim (Proxy over a function) answered
    // typeof "function", and the WAF VM branches on exactly that.  V8's
    // MarkAsUndetectable + SetCallAsFunctionHandler give the real semantics.

    lxb_dom_node_t* AllDocumentNode(v8::Local<v8::Context> context)
    {
      DomState* dom = GetDomState(context);
      if (dom == nullptr || dom->doc == nullptr)
        return nullptr;
      return lxb_dom_interface_node(&dom->doc->dom_document);
    }

    lxb_dom_node_t* AllElementAt(lxb_dom_node_t* node, int64_t& index)
    {
      for (lxb_dom_node_t* child = node->first_child; child != nullptr;
           child = child->next)
      {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
        {
          if (index == 0)
            return child;
          index--;
        }
        lxb_dom_node_t* hit = AllElementAt(child, index);
        if (hit != nullptr)
          return hit;
      }
      return nullptr;
    }

    int64_t AllCountElements(lxb_dom_node_t* node)
    {
      int64_t count = 0;
      for (lxb_dom_node_t* child = node->first_child; child != nullptr;
           child = child->next)
      {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
          count++;
        count += AllCountElements(child);
      }
      return count;
    }

    lxb_dom_node_t* AllNamedElement(lxb_dom_node_t* node,
                                    const std::string& wanted)
    {
      for (lxb_dom_node_t* child = node->first_child; child != nullptr;
           child = child->next)
      {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
        {
          lxb_dom_element_t* el = lxb_dom_interface_element(child);
          for (const char* attr : {"name", "id"})
          {
            size_t len = 0;
            const lxb_char_t* value = lxb_dom_element_get_attribute(
                el, (const lxb_char_t*)attr, std::strlen(attr), &len);
            if (value != nullptr &&
                wanted == std::string((const char*)value, len))
              return child;
          }
        }
        lxb_dom_node_t* hit = AllNamedElement(child, wanted);
        if (hit != nullptr)
          return hit;
      }
      return nullptr;
    }

    v8::Intercepted AllIndexedGet(
        uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = AllDocumentNode(context);
      if (doc_node == nullptr)
        return v8::Intercepted::kNo;
      int64_t remaining = static_cast<int64_t>(index);
      lxb_dom_node_t* hit = AllElementAt(doc_node, remaining);
      if (hit == nullptr)
        return v8::Intercepted::kNo;
      info.GetReturnValue().Set(WrapNode(isolate, context, hit));
      return v8::Intercepted::kYes;
    }

    void AllLength(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
      lxb_dom_node_t* doc_node = AllDocumentNode(context);
      info.GetReturnValue().Set(v8::Integer::New(
          info.GetIsolate(),
          static_cast<int32_t>(doc_node == nullptr
                                   ? 0
                                   : AllCountElements(doc_node))));
    }

    void AllItem(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = AllDocumentNode(context);
      if (doc_node == nullptr || info.Length() < 1)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      int64_t index = info[0]->IntegerValue(context).FromMaybe(-1);
      lxb_dom_node_t* hit = AllElementAt(doc_node, index);
      if (hit == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, hit));
    }

    void AllNamedItem(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = AllDocumentNode(context);
      if (doc_node == nullptr || info.Length() < 1 || !info[0]->IsString())
      {
        info.GetReturnValue().SetNull();
        return;
      }
      lxb_dom_node_t* hit =
          AllNamedElement(doc_node, ToUtf8(isolate, info[0]));
      if (hit == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, hit));
    }

    void AllCallHandler(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      // Legacy call behaviour: document.all(i) indexes, document.all("x")
      // names. With no arguments the collection itself is expected (which in
      // practice no probe relies on), so return null like an empty lookup.
      if (info.Length() < 1)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      if (info[0]->IsString())
        AllNamedItem(info);
      else
        AllItem(info);
    }

    v8::Intercepted GetAllNamed(v8::Local<v8::Name> name,
                                const v8::PropertyCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = AllDocumentNode(context);
      if (doc_node == nullptr || !name->IsString())
        return v8::Intercepted::kNo;
      lxb_dom_node_t* hit =
          AllNamedElement(doc_node, ToUtf8(isolate, name));
      if (hit == nullptr)
        return v8::Intercepted::kNo;
      info.GetReturnValue().Set(WrapNode(isolate, context, hit));
      return v8::Intercepted::kYes;
    }

    void GetAll(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      if (!dom->all_collection.IsEmpty())
      {
        info.GetReturnValue().Set(dom->all_collection.Get(isolate));
        return;
      }
      v8::Local<v8::ObjectTemplate> tpl = v8::ObjectTemplate::New(isolate);
      tpl->MarkAsUndetectable();
      tpl->SetInternalFieldCount(0);
      tpl->Set(v8::Symbol::GetToStringTag(isolate),
               ToV8Str(isolate, "HTMLAllCollection"),
               v8::PropertyAttribute::ReadOnly);
      tpl->SetCallAsFunctionHandler(AllCallHandler);
      tpl->SetHandler(v8::IndexedPropertyHandlerConfiguration(AllIndexedGet));
      tpl->SetHandler(v8::NamedPropertyHandlerConfiguration(GetAllNamed));
      {
        v8::Local<v8::String> key = ToV8Str(isolate, "length");
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, AllLength);
        fn->SetClassName(ToV8Str(isolate, "get length"));
        tpl->SetAccessorProperty(key, fn, v8::Local<v8::FunctionTemplate>());
      }
      {
        v8::Local<v8::String> key = ToV8Str(isolate, "item");
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, AllItem);
        fn->SetClassName(key);
        tpl->Set(key, fn);
      }
      {
        v8::Local<v8::String> key = ToV8Str(isolate, "namedItem");
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, AllNamedItem);
        fn->SetClassName(key);
        tpl->Set(key, fn);
      }
      v8::Local<v8::Object> obj = tpl->NewInstance(context).ToLocalChecked();
      dom->all_collection.Reset(isolate, obj);
      info.GetReturnValue().Set(obj);
    }

    void DocumentCreateElement(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (doc_node == nullptr)
      {
        // shell document: auto-create a blank HTML document
        DomState* st = GetDomState(context);
        if (st == nullptr)
        {
          isolate->ThrowException(ToV8Str(
              isolate, "Error: no document is loaded"));
          return;
        }
        if (st->doc == nullptr)
        {
          st->doc = lxb_html_document_create();
          if (st->doc == nullptr)
          {
            isolate->ThrowException(ToV8Str(
                isolate, "Error: document allocation failed"));
            return;
          }
          // parse a minimal document so lxb_dom_document_root works
          (void)lxb_html_document_parse(st->doc, (const lxb_char_t*)"<html></html>", 13);
          st->ready_state = "complete";
        }
        doc_node = lxb_dom_interface_node(&st->doc->dom_document);
      }
      if (info.Length() < 1 || !info[0]->IsString())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: createElement requires a tag name"));
        return;
      }
      // HTML documents normalize element names to lowercase
      std::string tag = ToUtf8(isolate, info[0]);
      for (auto& c : tag)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (tag.empty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "DOMException: InvalidCharacterError: invalid tag name"));
        return;
      }
      lxb_dom_element_t* el = lxb_dom_document_create_element(
          lxb_dom_interface_document(doc_node), (const lxb_char_t*)tag.c_str(),
          tag.size(), nullptr);
      if (el == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: element allocation failed"));
        return;
      }
      info.GetReturnValue().Set(
          WrapNode(isolate, context, lxb_dom_interface_node(el)));
    }

    // ------------------------------------------------ G3.21 doctype/implementation
    v8::Local<v8::Object> MakeDocumentObject(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        lxb_dom_document_t* doc_node, bool html_tag = true);

    void DocumentTypeName(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      lxb_dom_node_t* node = NodeOf(info.This());
      if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_DOCUMENT_TYPE)
      {
        info.GetReturnValue().Set(ToV8Str(isolate, ""));
        return;
      }
      size_t len = 0;
      const lxb_char_t* name = lxb_dom_document_type_name(
          lxb_dom_interface_document_type(node), &len);
      if (name == nullptr)
      {
        info.GetReturnValue().Set(ToV8Str(isolate, ""));
        return;
      }
      info.GetReturnValue().Set(
          ToV8Str(isolate, std::string((const char*)name, len)));
    }

    void DocumentGetDoctype(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (doc_node == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      for (lxb_dom_node_t* child = doc_node->first_child; child != nullptr;
           child = child->next)
      {
        if (child->type == LXB_DOM_NODE_TYPE_DOCUMENT_TYPE)
        {
          info.GetReturnValue().Set(WrapNode(isolate, context, child));
          return;
        }
      }
      info.GetReturnValue().SetNull();
    }

    void DocumentGetTitle(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      DomState* dom = GetDomState(context);
      if (doc_node == nullptr || dom == nullptr)
      {
        info.GetReturnValue().Set(ToV8Str(isolate, ""));
        return;
      }
      lxb_dom_node_t* root =
          lxb_dom_document_root(lxb_dom_interface_document(doc_node));
      std::vector<lxb_dom_node_t*> found;
      if (root != nullptr) (void)SelectNodes(dom, root, "title", found);
      if (found.empty())
      {
        info.GetReturnValue().Set(ToV8Str(isolate, ""));
        return;
      }
      size_t len = 0;
      const lxb_char_t* text = lxb_dom_node_text_content(found[0], &len);
      info.GetReturnValue().Set(ToV8Str(
          isolate,
          text == nullptr ? "" : std::string((const char*)text, len)));
    }

    void DocumentSetTitle(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      DomState* dom = GetDomState(context);
      if (doc_node == nullptr || dom == nullptr || info.Length() < 1) return;
      lxb_dom_node_t* root =
          lxb_dom_document_root(lxb_dom_interface_document(doc_node));
      std::vector<lxb_dom_node_t*> found;
      if (root != nullptr) (void)SelectNodes(dom, root, "title", found);
      if (found.empty()) return;
      std::string title = ToUtf8(isolate, info[0]);
      lxb_dom_node_text_content_set(found[0], (const lxb_char_t*)title.c_str(),
                                    title.size());
    }

    void ImplCreateHTMLDocument(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr) return;
      std::string title = info.Length() >= 1
          ? ToUtf8(isolate, info[0]) : "";
      std::string escaped;
      for (char c : title)
      {
        if (c == '&') escaped += "&amp;";
        else if (c == '<') escaped += "&lt;";
        else if (c == '>') escaped += "&gt;";
        else escaped += c;
      }
      std::string html = "<!DOCTYPE html><html><head><title>" + escaped +
                         "</title></head><body></body></html>";
      lxb_html_document_t* parsed = lxb_html_document_create();
      if (parsed == nullptr ||
          lxb_html_document_parse(
              parsed, (const lxb_char_t*)html.c_str(), html.size()) !=
              LXB_STATUS_OK)
      {
        if (parsed != nullptr) lxb_html_document_destroy(parsed);
        isolate->ThrowException(ToV8Str(isolate, "Error: doc alloc failed"));
        return;
      }
      dom->parsed_docs.push_back(parsed);
      // G4.389: derived documents are accessor-free instances chained to the
      // shared Document.prototype (the reference keeps zero own members on
      // them; every member resolves at the shared level).
      info.GetReturnValue().Set(
          MakeDocumentObject(isolate, context, &parsed->dom_document,
                             /*html_tag=*/false));
    }

    void ImplCreateDocument(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr) return;
      // qualifiedName drives the root element (GetTagName uppercases for
      // display), the optional DocumentType name drives a real <!DOCTYPE>
      // node so typed.doctype reads through the standard document accessor
      std::string qname = info.Length() >= 2 && info[1]->IsString()
          ? ToUtf8(isolate, info[1]) : "";
      std::string dt_name;
      if (info.Length() >= 3 && info[2]->IsObject())
      {
        v8::Local<v8::Value> name_v;
        if (info[2].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "name"))
                .ToLocal(&name_v) && name_v->IsString())
          dt_name = ToUtf8(isolate, name_v);
      }
      for (auto& c : qname)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      for (auto& c : dt_name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      std::string html = dt_name.empty()
          ? "<html></html>"
          : "<!DOCTYPE " + dt_name + "><html></html>";
      lxb_html_document_t* parsed = lxb_html_document_create();
      if (parsed == nullptr ||
          lxb_html_document_parse(
              parsed, (const lxb_char_t*)html.c_str(), html.size()) !=
              LXB_STATUS_OK)
      {
        if (parsed != nullptr) lxb_html_document_destroy(parsed);
        isolate->ThrowException(ToV8Str(isolate, "Error: doc alloc failed"));
        return;
      }
      // swap the tree-builder-synthesized <html> root for the
      // qualifiedName element
      if (!qname.empty() && qname != "html")
      {
        lxb_dom_document_t* ldoc = &parsed->dom_document;
        lxb_dom_node_t* root = lxb_dom_interface_node(
            lxb_dom_document_root(ldoc));
        lxb_dom_element_t* el = qname.empty() ? nullptr
            : lxb_dom_document_create_element(
                ldoc, (const lxb_char_t*)qname.c_str(), qname.size(),
                nullptr);
        if (root != nullptr && el != nullptr)
        {
          lxb_dom_node_insert_before(root, lxb_dom_interface_node(el));
          lxb_dom_node_remove(root);
        }
      }
      dom->parsed_docs.push_back(parsed);
      info.GetReturnValue().Set(
          MakeDocumentObject(isolate, context, &parsed->dom_document,
                             /*html_tag=*/false));
    }

    void ImplCreateDocumentType(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> doctype = v8::Object::New(isolate);
      v8::Local<v8::Value> empty = ToV8Str(isolate, "");
      (void)doctype
          ->Set(context, ToV8Str(isolate, "name"),
                info.Length() >= 1
                    ? v8::Local<v8::Value>(info[0])
                    : v8::Local<v8::Value>(empty))
          .FromMaybe(false);
      (void)doctype
          ->Set(context, ToV8Str(isolate, "publicId"),
                info.Length() >= 2
                    ? v8::Local<v8::Value>(info[1])
                    : v8::Local<v8::Value>(empty))
          .FromMaybe(false);
      (void)doctype
          ->Set(context, ToV8Str(isolate, "systemId"),
                info.Length() >= 3
                    ? v8::Local<v8::Value>(info[2])
                    : v8::Local<v8::Value>(empty))
          .FromMaybe(false);
      info.GetReturnValue().Set(doctype);
    }

    void DocumentCreateTextNode(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (doc_node == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: no document is loaded"));
        return;
      }
      std::string data = info.Length() >= 1 && !info[0]->IsUndefined()
                             ? ToUtf8(isolate, info[0])
                             : std::string();
      lxb_dom_text_t* text = lxb_dom_document_create_text_node(
          lxb_dom_interface_document(doc_node),
          (const lxb_char_t*)data.c_str(), data.size());
      if (text == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: text node allocation failed"));
        return;
      }
      info.GetReturnValue().Set(
          WrapNode(isolate, context, lxb_dom_interface_node(text)));
    }

    // G4.393: document.createComment used to fall through to the inert
    // JS stub (returns null); the reference answers a live Comment wrapper.
    void DocumentCreateComment(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (doc_node == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: no document is loaded"));
        return;
      }
      std::string data = info.Length() >= 1 && !info[0]->IsUndefined()
                             ? ToUtf8(isolate, info[0])
                             : std::string();
      lxb_dom_comment_t* comment = lxb_dom_document_create_comment(
          lxb_dom_interface_document(doc_node),
          (const lxb_char_t*)data.c_str(), data.size());
      if (comment == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: comment node allocation failed"));
        return;
      }
      info.GetReturnValue().Set(
          WrapNode(isolate, context, lxb_dom_interface_node(comment)));
    }

    // document.write during inline-script execution inserts right after the
    // running script (the insertion point advances with every write so
    // multiple writes keep document order). After load, writes append to the
    // body end (the browser's implicit document.open() wipe is a parse-time
    // concept lv8 pages do not need).
    void DocumentWriteImpl(const v8::FunctionCallbackInfo<v8::Value>& info,
                           bool add_newline)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
      if (dom == nullptr || doc_node == nullptr || dom->doc == nullptr)
        return;
      std::string html;
      for (int i = 0; i < info.Length(); i++)
        html += ToUtf8(isolate, info[i]);
      if (add_newline)
        html += "\n";
      if (html.empty())
        return;
      lxb_html_document_t* html_doc = lxb_html_interface_document(
          lxb_dom_interface_document(doc_node));
      if (html_doc->body == nullptr)
        return;
      lxb_dom_node_t* body_node = lxb_dom_interface_node(html_doc->body);
      lxb_dom_node_t* frag = lxb_html_document_parse_fragment(
          dom->doc, lxb_dom_interface_element(html_doc->body),
          (const lxb_char_t*)html.c_str(), html.size());
      if (frag == nullptr)
        return;
      // insertion point: chained after the previous write during parsing,
      // else at body end
      lxb_dom_node_t* point =
          dom->write_point != nullptr ? dom->write_point
                                      : body_node->last_child;
      while (frag->first_child != nullptr)
      {
        lxb_dom_node_t* child = frag->first_child;
        lxb_dom_node_remove(child);
        if (point != nullptr)
        {
          if (point->next != nullptr)
            lxb_dom_node_insert_before(point->next, child);
          else if (point->parent != nullptr)
            lxb_dom_node_insert_child(point->parent, child);
          else
            lxb_dom_node_insert_child(body_node, child);
        }
        else
          lxb_dom_node_insert_child(body_node, child);
        point = child;
      }
      dom->write_point = point;
      lxb_dom_node_destroy(frag);
    }

    void DocumentWrite(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      DocumentWriteImpl(info, false);
    }

    void DocumentWriteln(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      DocumentWriteImpl(info, true);
    }

    // -------------------------------------------------------- Range surface

    // Boundary points travel in Private symbols on the range object:
    // lv8.range.sn/so (start node/offset) and lv8.range.en/eo (end).

    bool IsTextLike(lxb_dom_node_t* node)
    {
      return node != nullptr &&
             (node->type == LXB_DOM_NODE_TYPE_TEXT ||
              node->type == LXB_DOM_NODE_TYPE_CDATA_SECTION);
    }

    bool IsAncestorOf(lxb_dom_node_t* a, lxb_dom_node_t* b)
    {
      for (lxb_dom_node_t* n = b; n != nullptr; n = n->parent)
      {
        if (n == a)
          return true;
      }
      return false;
    }

    // strict document order between nodes of one tree
    bool NodeBefore(lxb_dom_node_t* a, lxb_dom_node_t* b)
    {
      if (a == nullptr || b == nullptr || a == b)
        return false;
      if (IsAncestorOf(a, b))
        return true;
      std::vector<lxb_dom_node_t*> pa;
      std::vector<lxb_dom_node_t*> pb;
      for (lxb_dom_node_t* n = a; n != nullptr; n = n->parent)
        pa.push_back(n);
      for (lxb_dom_node_t* n = b; n != nullptr; n = n->parent)
        pb.push_back(n);
      size_t i = pa.size();
      size_t j = pb.size();
      while (i > 0 && j > 0 && pa[i - 1] == pb[j - 1])
      {
        i--;
        j--;
      }
      if (i == 0 || j == 0)
        return false;
      // sibling order of the divergent nodes
      for (lxb_dom_node_t* n = pb[j - 1]->prev; n != nullptr; n = n->prev)
      {
        if (n == pa[i - 1])
          return true;
      }
      return false;
    }

    lxb_dom_node_t* ChildAtIndex(lxb_dom_node_t* node, int32_t index)
    {
      int32_t i = 0;
      for (lxb_dom_node_t* c = node->first_child; c != nullptr;
           c = c->next, i++)
      {
        if (i == index)
          return c;
      }
      return nullptr;
    }

    int32_t IndexOfChild(lxb_dom_node_t* child)
    {
      int32_t i = 0;
      for (lxb_dom_node_t* c = child->prev; c != nullptr; c = c->prev)
        i++;
      return i;
    }

    // index of the direct child of |container| on |node|'s ancestor chain
    int32_t IndexWithin(lxb_dom_node_t* container, lxb_dom_node_t* node)
    {
      lxb_dom_node_t* n = node;
      while (n != nullptr && n->parent != container)
        n = n->parent;
      return n == nullptr ? -1 : IndexOfChild(n);
    }

    lxb_dom_node_t* NextAfterSubtree(lxb_dom_node_t* n)
    {
      while (n != nullptr)
      {
        if (n->next != nullptr)
          return n->next;
        n = n->parent;
      }
      return nullptr;
    }

    lxb_dom_node_t* NextInDocOrder(lxb_dom_node_t* n)
    {
      if (n->first_child != nullptr)
        return n->first_child;
      return NextAfterSubtree(n);
    }

    lxb_dom_node_t* CommonAncestor(lxb_dom_node_t* a, lxb_dom_node_t* b)
    {
      for (lxb_dom_node_t* x = a; x != nullptr; x = x->parent)
      {
        for (lxb_dom_node_t* y = b; y != nullptr; y = y->parent)
        {
          if (x == y)
            return x;
        }
      }
      return nullptr;
    }

    v8::Local<v8::Private> RangeKey(v8::Isolate* isolate, const char* name)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, name));
    }

    bool RangeGetBoundary(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> self, bool start,
                          lxb_dom_node_t** node_out, int32_t* offset_out)
    {
      const char* node_key = start ? "lv8.range.sn" : "lv8.range.en";
      const char* off_key = start ? "lv8.range.so" : "lv8.range.eo";
      v8::Local<v8::Value> nv;
      v8::Local<v8::Value> ov;
      if (!self->GetPrivate(context, RangeKey(isolate, node_key))
               .ToLocal(&nv) ||
          !nv->IsExternal())
        return false;
      if (!self->GetPrivate(context, RangeKey(isolate, off_key))
               .ToLocal(&ov) ||
          !ov->IsNumber())
        return false;
      *node_out =
          static_cast<lxb_dom_node_t*>(nv.As<v8::External>()->Value());
      *offset_out = ov->Int32Value(context).FromMaybe(0);
      return *node_out != nullptr;
    }

    void RangeSetBoundary(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> self, bool start,
                          lxb_dom_node_t* node, int32_t offset)
    {
      (void)self
          ->SetPrivate(context,
                       RangeKey(isolate, start ? "lv8.range.sn"
                                               : "lv8.range.en"),
                       v8::External::New(isolate, node))
          .FromMaybe(false);
      (void)self
          ->SetPrivate(context,
                       RangeKey(isolate, start ? "lv8.range.so"
                                               : "lv8.range.eo"),
                       v8::Integer::New(isolate, offset))
          .FromMaybe(false);
    }

    std::string PartialTextFrom(lxb_dom_node_t* node, int32_t offset)
    {
      if (IsTextLike(node))
      {
        std::string data = NodeText(node);
        if (offset < 0)
          offset = 0;
        if (offset >= (int32_t)data.size())
          return "";
        return data.substr(offset);
      }
      std::string out;
      int32_t i = 0;
      for (lxb_dom_node_t* c = node->first_child; c != nullptr;
           c = c->next, i++)
      {
        if (i >= offset)
          out += NodeText(c);
      }
      return out;
    }

    std::string PartialTextTo(lxb_dom_node_t* node, int32_t offset)
    {
      if (IsTextLike(node))
      {
        std::string data = NodeText(node);
        if (offset <= 0)
          return "";
        if (offset > (int32_t)data.size())
          offset = (int32_t)data.size();
        return data.substr(0, offset);
      }
      std::string out;
      int32_t i = 0;
      for (lxb_dom_node_t* c = node->first_child; c != nullptr;
           c = c->next, i++)
      {
        if (i >= offset)
          break;
        out += NodeText(c);
      }
      return out;
    }

    void RangeSetBoundaryCb(const v8::FunctionCallbackInfo<v8::Value>& info,
                            bool start)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2 || !info[0]->IsObject() ||
          NodeOf(info[0].As<v8::Object>()) == nullptr)
        return;
      int32_t offset = info[1]->Int32Value(context).FromMaybe(0);
      RangeSetBoundary(isolate, context, info.This(), start,
                       NodeOf(info[0].As<v8::Object>()), offset);
    }

    void RangeSetStart(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      RangeSetBoundaryCb(info, true);
    }

    void RangeSetEnd(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      RangeSetBoundaryCb(info, false);
    }

    void RangeCollapse(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node;
      int32_t offset;
      bool to_start =
          info.Length() >= 1 ? info[0]->BooleanValue(isolate) : false;
      if (!RangeGetBoundary(isolate, context, info.This(), to_start, &node,
                            &offset))
        return;
      RangeSetBoundary(isolate, context, info.This(), true, node, offset);
      RangeSetBoundary(isolate, context, info.This(), false, node, offset);
    }

    std::string RangeText(lxb_dom_node_t* sc, int32_t so, lxb_dom_node_t* ec,
                          int32_t eo)
    {
      if (sc == nullptr || ec == nullptr)
        return "";
      if (sc == ec)
      {
        if (IsTextLike(sc))
        {
          std::string data = NodeText(sc);
          if (so < 0)
            so = 0;
          if (eo > (int32_t)data.size())
            eo = (int32_t)data.size();
          if (so >= eo)
            return "";
          return data.substr(so, eo - so);
        }
        std::string out;
        int32_t i = 0;
        for (lxb_dom_node_t* c = sc->first_child; c != nullptr;
             c = c->next, i++)
        {
          if (i >= so && i < eo)
            out += NodeText(c);
        }
        return out;
      }
      // cross-container: start partial, document-order walk, end partial
      std::string out;
      out += PartialTextFrom(sc, so);
      lxb_dom_node_t* n =
          IsTextLike(sc) ? NextAfterSubtree(sc) : ChildAtIndex(sc, so);
      if (n == nullptr && !IsTextLike(sc))
        n = NextAfterSubtree(sc);
      while (n != nullptr)
      {
        if (n == ec)
          break;
        bool inside_end = IsAncestorOf(ec, n);
        if (!inside_end && !NodeBefore(n, ec))
          break; // past the end
        if (IsTextLike(n))
        {
          if (!inside_end)
            out += NodeText(n);
          else if (IndexWithin(ec, n) < eo)
            out += NodeText(n);
        }
        n = NextInDocOrder(n);
      }
      if (IsTextLike(ec))
        out += PartialTextTo(ec, eo);
      return out;
    }

    void RangeToString(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* sc;
      lxb_dom_node_t* ec;
      int32_t so;
      int32_t eo;
      if (!RangeGetBoundary(isolate, context, info.This(), true, &sc, &so) ||
          !RangeGetBoundary(isolate, context, info.This(), false, &ec, &eo))
      {
        info.GetReturnValue().SetEmptyString();
        return;
      }
      info.GetReturnValue().Set(ToV8Str(isolate, RangeText(sc, so, ec, eo)));
    }

    // append a deep clone of |child| into |into|
    void AppendClone(lxb_dom_document_t* doc, lxb_dom_node_t* into,
                     lxb_dom_node_t* child)
    {
      lxb_dom_node_t* clone = lxb_dom_node_clone(child, true);
      if (clone != nullptr)
        lxb_dom_node_insert_child(into, clone);
    }

    void RangeCloneContents(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      lxb_dom_node_t* sc;
      lxb_dom_node_t* ec;
      int32_t so;
      int32_t eo;
      if (dom == nullptr || dom->doc == nullptr ||
          !RangeGetBoundary(isolate, context, info.This(), true, &sc, &so) ||
          !RangeGetBoundary(isolate, context, info.This(), false, &ec, &eo))
      {
        info.GetReturnValue().SetNull();
        return;
      }
      lxb_dom_document_fragment_t* frag =
          lxb_dom_document_create_document_fragment(&dom->doc->dom_document);
      if (frag == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      lxb_dom_node_t* into = lxb_dom_interface_node(frag);
      lxb_dom_document_t* doc = &dom->doc->dom_document;
      if (sc == ec)
      {
        if (IsTextLike(sc))
        {
          std::string piece = RangeText(sc, so, ec, eo);
          lxb_dom_text_t* text = lxb_dom_document_create_text_node(
              doc, (const lxb_char_t*)piece.c_str(), piece.size());
          if (text != nullptr)
            lxb_dom_node_insert_child(
                into, lxb_dom_interface_node(text));
        }
        else
        {
          int32_t i = 0;
          for (lxb_dom_node_t* c = sc->first_child; c != nullptr;
               c = c->next, i++)
          {
            if (i >= so && i < eo)
              AppendClone(doc, into, c);
          }
        }
      }
      else
      {
        // start partial at sc
        if (IsTextLike(sc))
        {
          std::string piece = PartialTextFrom(sc, so);
          lxb_dom_text_t* text = lxb_dom_document_create_text_node(
              doc, (const lxb_char_t*)piece.c_str(), piece.size());
          if (text != nullptr)
            lxb_dom_node_insert_child(into, lxb_dom_interface_node(text));
        }
        else
        {
          int32_t i = 0;
          for (lxb_dom_node_t* c = sc->first_child; c != nullptr;
               c = c->next, i++)
          {
            if (i >= so)
              AppendClone(doc, into, c);
          }
        }
        // middle: siblings after each sc ancestor below the common ancestor
        lxb_dom_node_t* common = CommonAncestor(sc, ec);
        if (common != nullptr)
        {
          for (lxb_dom_node_t* a = sc; a != common; a = a->parent)
          {
            for (lxb_dom_node_t* s = a->next; s != nullptr; s = s->next)
              AppendClone(doc, into, s);
          }
          // end side: siblings before each ec ancestor, collected in reverse
          std::vector<lxb_dom_node_t*> end_siblings;
          for (lxb_dom_node_t* b = ec; b != common; b = b->parent)
          {
            for (lxb_dom_node_t* s = b->prev; s != nullptr; s = s->prev)
              end_siblings.push_back(s);
          }
          for (size_t k = end_siblings.size(); k-- > 0;)
            AppendClone(doc, into, end_siblings[k]);
          // partial end at ec
          if (IsTextLike(ec))
          {
            std::string piece = PartialTextTo(ec, eo);
            lxb_dom_text_t* text = lxb_dom_document_create_text_node(
                doc, (const lxb_char_t*)piece.c_str(), piece.size());
            if (text != nullptr)
              lxb_dom_node_insert_child(into,
                                        lxb_dom_interface_node(text));
          }
          else
          {
            int32_t i = 0;
            for (lxb_dom_node_t* c = ec->first_child; c != nullptr;
                 c = c->next, i++)
            {
              if (i < eo)
                AppendClone(doc, into, c);
            }
          }
        }
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, into));
    }

    void RangeDeleteContents(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      lxb_dom_node_t* sc;
      lxb_dom_node_t* ec;
      int32_t so;
      int32_t eo;
      if (dom == nullptr ||
          !RangeGetBoundary(isolate, context, info.This(), true, &sc, &so) ||
          !RangeGetBoundary(isolate, context, info.This(), false, &ec, &eo))
        return;
      if (sc == ec)
      {
        if (IsTextLike(sc))
        {
          std::string data = NodeText(sc);
          if (so < 0)
            so = 0;
          if (eo > (int32_t)data.size())
            eo = (int32_t)data.size();
          if (so < eo)
          {
            std::string updated = data.substr(0, so) + data.substr(eo);
            lxb_dom_node_text_content_set(
                sc, (const lxb_char_t*)updated.c_str(), updated.size());
          }
        }
        else
        {
          int32_t i = 0;
          lxb_dom_node_t* c = sc->first_child;
          while (c != nullptr)
          {
            lxb_dom_node_t* next = c->next;
            if (i >= so && i < eo)
            {
              lxb_dom_node_remove(c);
              dom->detached.push_back(c);
            }
            c = next;
            i++;
          }
        }
      }
      else
      {
        // start partial
        if (IsTextLike(sc))
        {
          std::string data = NodeText(sc);
          if (so < (int32_t)data.size())
          {
            std::string head = so > 0 ? data.substr(0, so) : "";
            lxb_dom_node_text_content_set(
                sc, (const lxb_char_t*)head.c_str(), head.size());
          }
        }
        else
        {
          int32_t i = 0;
          lxb_dom_node_t* c = sc->first_child;
          while (c != nullptr)
          {
            lxb_dom_node_t* next = c->next;
            if (i >= so)
            {
              lxb_dom_node_remove(c);
              dom->detached.push_back(c);
            }
            c = next;
            i++;
          }
        }
        // end partial
        if (IsTextLike(ec))
        {
          std::string data = NodeText(ec);
          if (eo > 0)
          {
            std::string kept = eo < (int32_t)data.size()
                                   ? data.substr(eo)
                                   : "";
            lxb_dom_node_text_content_set(
                sc == ec ? sc : ec, (const lxb_char_t*)kept.c_str(),
                kept.size());
          }
          else
          {
            lxb_dom_node_text_content_set(ec, (const lxb_char_t*)"", 0);
          }
        }
        else
        {
          int32_t i = 0;
          lxb_dom_node_t* c = ec->first_child;
          while (c != nullptr)
          {
            lxb_dom_node_t* next = c->next;
            if (i >= eo)
            {
              lxb_dom_node_remove(c);
              dom->detached.push_back(c);
            }
            c = next;
            i++;
          }
        }
        // middle: remove fully contained nodes in document order
        lxb_dom_node_t* n =
            IsTextLike(sc) ? NextAfterSubtree(sc) : NextAfterSubtree(sc);
        while (n != nullptr && n != ec)
        {
          bool inside_end = IsAncestorOf(ec, n);
          if (!inside_end && !NodeBefore(n, ec))
            break; // past the end
          if (inside_end && IndexWithin(ec, n) >= eo)
          {
            n = NextAfterSubtree(n); // outside the end partial: skip subtree
            continue;
          }
          if (IsTextLike(n))
          {
            lxb_dom_node_t* next = NextAfterSubtree(n);
            lxb_dom_node_remove(n);
            dom->detached.push_back(n);
            n = next;
          }
          else
          {
            lxb_dom_node_t* next = NextAfterSubtree(n);
            lxb_dom_node_remove(n);
            dom->detached.push_back(n);
            n = next;
          }
        }
      }
      // collapse to the start point
      RangeSetBoundary(isolate, context, info.This(), false, sc, so);
    }

    void RangeInsertNode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* sc;
      int32_t so;
      if (info.Length() < 1 || !info[0]->IsObject() ||
          NodeOf(info[0].As<v8::Object>()) == nullptr ||
          !RangeGetBoundary(isolate, context, info.This(), true, &sc, &so))
        return;
      lxb_dom_node_t* fresh = NodeOf(info[0].As<v8::Object>());
      if (IsTextLike(sc))
      {
        DomState* dom = GetDomState(context);
        if (dom == nullptr || dom->doc == nullptr)
          return;
        std::string data = NodeText(sc);
        if (so < 0)
          so = 0;
        if (so > (int32_t)data.size())
          so = (int32_t)data.size();
        std::string head = data.substr(0, so);
        std::string tail = data.substr(so);
        // insert |fresh| at the split point, keeping the tail readable
        lxb_dom_node_t* tail_node = nullptr;
        if (!tail.empty())
        {
          lxb_dom_text_t* tail_text = lxb_dom_document_create_text_node(
              &dom->doc->dom_document, (const lxb_char_t*)tail.c_str(),
              tail.size());
          if (tail_text != nullptr)
          {
            tail_node = lxb_dom_interface_node(tail_text);
            if (sc->next != nullptr)
              lxb_dom_node_insert_before(sc->next, tail_node);
            else if (sc->parent != nullptr)
              lxb_dom_node_insert_child(sc->parent, tail_node);
          }
        }
        lxb_dom_node_text_content_set(sc, (const lxb_char_t*)head.c_str(),
                                      head.size());
        if (tail_node != nullptr)
          lxb_dom_node_insert_before(tail_node, fresh);
        else if (sc->next != nullptr)
          lxb_dom_node_insert_before(sc->next, fresh);
        else if (sc->parent != nullptr)
          lxb_dom_node_insert_child(sc->parent, fresh);
      }
      else
      {
        lxb_dom_node_t* at = ChildAtIndex(sc, so);
        if (at != nullptr)
          lxb_dom_node_insert_before(at, fresh);
        else
          lxb_dom_node_insert_child(sc, fresh);
      }
    }

    void RangeContainerGetter(const v8::FunctionCallbackInfo<v8::Value>& info,
                              bool start)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* node;
      int32_t offset;
      if (!RangeGetBoundary(isolate, context, info.This(), start, &node,
                            &offset))
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, node));
    }

    void RangeStartContainer(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      RangeContainerGetter(info, true);
    }

    void RangeEndContainer(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      RangeContainerGetter(info, false);
    }

    void RangeCommonAncestorContainer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      lxb_dom_node_t* sc;
      lxb_dom_node_t* ec;
      int32_t so;
      int32_t eo;
      if (!RangeGetBoundary(isolate, context, info.This(), true, &sc, &so) ||
          !RangeGetBoundary(isolate, context, info.This(), false, &ec, &eo))
      {
        info.GetReturnValue().SetNull();
        return;
      }
      lxb_dom_node_t* common = CommonAncestor(sc, ec);
      if (common == nullptr)
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(WrapNode(isolate, context, common));
    }

    v8::Local<v8::Object> MakeRange(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context)
    {
      DomState* dom = GetDomState(context);
      if (dom == nullptr)
        return v8::Local<v8::Object>();
      if (dom->range_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
        tpl->SetClassName(ToV8Str(isolate, "Range"));
        v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        tpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "Range"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        // reference parity: range accessors live on the prototype - the
        // instance own-face stays empty (own accessors would enumerate)
        auto accessor = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          tpl->PrototypeTemplate()->SetAccessorProperty(
              key, fn, v8::Local<v8::FunctionTemplate>());
        };
        accessor("startContainer", RangeStartContainer);
        accessor("endContainer", RangeEndContainer);
        accessor("commonAncestorContainer", RangeCommonAncestorContainer);
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          tpl->PrototypeTemplate()->Set(key, fn);
        };
        method("setStart", RangeSetStart);
        method("setEnd", RangeSetEnd);
        method("collapse", RangeCollapse);
        method("toString", RangeToString);
        method("cloneContents", RangeCloneContents);
        method("deleteContents", RangeDeleteContents);
        method("insertNode", RangeInsertNode);
        dom->range_tpl.Reset(isolate, tpl);
      }
      return dom->range_tpl.Get(isolate)
          ->InstanceTemplate()
          ->NewInstance(context)
          .ToLocalChecked();
    }

    void DocumentCreateRange(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> range = MakeRange(isolate, context);
      if (range.IsEmpty())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "Error: no document is loaded"));
        return;
      }
      info.GetReturnValue().Set(range);
    }

    // document.URL / documentURI: the resolved page url
    void GetDocumentURL(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      DomState* dom = GetDomState(context);
      // G4.344: detached documents (DOMParser results, `new Document()`
      // shells) answer 'about:blank' like the reference - only the MAIN
      // page document resolves the navigation entries URL.  G4.393h: the
      // G4.349 shell product carries NO node private (a plain
      // Object.create(Document.prototype) object), so a null receiver node
      // is also a detached answer, never the page URL.
      lxb_dom_node_t* self_node = NodeOf(info.This());
      if (dom != nullptr && dom->doc != nullptr &&
          (self_node == nullptr ||
           (self_node->type == LXB_DOM_NODE_TYPE_DOCUMENT &&
            self_node != lxb_dom_interface_node(dom->doc))))
      {
        info.GetReturnValue().Set(ToV8Str(isolate, "about:blank"));
        return;
      }
      if (state != nullptr && !state->entries.empty())
      {
        info.GetReturnValue().Set(ToV8Str(
            isolate, state->entries[state->index].url));
        return;
      }
      info.GetReturnValue().Set(ToV8Str(isolate, "about:blank"));
    }

    // document.location is the same Location object exposed on the Window in
    // this single-document runtime.  WAF SDKs routinely inspect it through
    // both access paths.
    void GetDocumentLocation(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> location;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "location"))
              .ToLocal(&location))
        info.GetReturnValue().Set(location);
    }

    // A freshly loaded lv8 document has no predecessor navigation.  Keep
    // the browser-visible referrer surface explicit rather than relying on
    // an absent interceptor property.
    void GetDocumentReferrer(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), ""));
    }

    // In standards mode the root element is the document scrolling element.
    // Reuse the documentElement host wrapper so identity comparisons hold.
    void GetDocumentScrollingElement(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      GetDocumentElement(info);
    }

    // B2 surface: document.characterSet / charset / inputEncoding
    void GetDocumentCharacterSet(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Local<v8::Value> value = ToV8Str(info.GetIsolate(), "UTF-8");
      MonitorRead(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
                  "document.characterSet", value);
      info.GetReturnValue().Set(value);
    }

    // B2 surface: document.domain from the current page host
    void GetDocumentDomain(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      std::string url = state != nullptr && !state->entries.empty()
                            ? state->entries[state->index].url
                            : std::string("about:blank");
      size_t scheme = url.find("://");
      std::string host = url;
      if (scheme != std::string::npos)
        host = url.substr(scheme + 3);
      size_t path = host.find_first_of("/?#");
      if (path != std::string::npos)
        host = host.substr(0, path);
      size_t port = host.find(':');
      if (port != std::string::npos)
        host = host.substr(0, port);
      v8::Local<v8::Value> value = ToV8Str(isolate, host);
      MonitorRead(isolate, context, "document.domain", value);
      info.GetReturnValue().Set(value);
    }

    // B2 surface: document.lastModified in the spec "MM/DD/YYYY HH:MM:SS"
    void GetDocumentLastModified(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      std::time_t now = std::time(nullptr);
      std::tm* tmv = std::localtime(&now);
      char buffer[64] = {0};
      if (tmv != nullptr)
        std::strftime(buffer, sizeof(buffer), "%m/%d/%Y %H:%M:%S", tmv);
      v8::Local<v8::Value> value = ToV8Str(info.GetIsolate(),
                                           std::string(buffer));
      MonitorRead(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
                  "document.lastModified", value);
      info.GetReturnValue().Set(value);
    }

    // B2 surface: document.designMode / dir / prerendering / wasDiscarded
    void GetDocumentDesignMode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Local<v8::Value> value = ToV8Str(info.GetIsolate(), "off");
      MonitorRead(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
                  "document.designMode", value);
      info.GetReturnValue().Set(value);
    }
    void GetDocumentDir(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Local<v8::Value> value = ToV8Str(info.GetIsolate(), "ltr");
      MonitorRead(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
                  "document.dir", value);
      info.GetReturnValue().Set(value);
    }
    void GetDocumentPrerendering(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Local<v8::Value> value =
          v8::Boolean::New(info.GetIsolate(), false);
      MonitorRead(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
                  "document.prerendering", value);
      info.GetReturnValue().Set(value);
    }
    void GetDocumentWasDiscarded(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Local<v8::Value> value =
          v8::Boolean::New(info.GetIsolate(), false);
      MonitorRead(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(),
                  "document.wasDiscarded", value);
      info.GetReturnValue().Set(value);
    }

    // document.currentScript: the inline/external script running now,
    // null outside script execution (reference semantics)
    void GetDocumentCurrentScript(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom != nullptr && dom->current_script != nullptr)
      {
        info.GetReturnValue().Set(
            WrapNode(isolate, context, dom->current_script));
        return;
      }
      info.GetReturnValue().SetNull();
    }

    // document.cookie rides on the shared per-context cookie jar
    // (LV8Storage.cpp); see GetCookieString/SetCookieFromDocument
    void DocumentGetCookie(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      info.GetReturnValue().Set(
          ToV8Str(isolate, GetCookieString(isolate, context)));
    }

    void DocumentSetCookie(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1 || !info[0]->IsString())
        return;
      SetCookieFromDocument(isolate, context, ToUtf8(isolate, info[0]));
    }

    // Build a document host object bound to |doc_node| via the shared
    // Private-symbol node pointer. Used for the page document and for
    // DOMParser documents alike.
    v8::Local<v8::Object> MakeDocumentObject(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        lxb_dom_document_t* doc_node, bool html_tag)
    {
      v8::Local<v8::FunctionTemplate> doc_tpl = v8::FunctionTemplate::New(isolate);
      doc_tpl->SetClassName(ToV8Str(isolate, "HTMLDocument"));
      v8::Local<v8::ObjectTemplate> doc_tmpl = doc_tpl->InstanceTemplate();
      // zero internal fields (see NodeOf in this file)
      doc_tmpl->SetInternalFieldCount(0);
      // M6: DOMParser results (html_tag=false) read [object Document] with
      // zero own symbols (tag on the fresh per-call prototype).  The page
      // document keeps the historical instance tag: its prototype object is
      // aliased as window.Document.prototype by the polyfills8 shim (lv8's
      // document chain is single-level; the reference has doc ->
      // HTMLDocument.prototype -> Document.prototype), so a prototype-level
      // tag here would leak the wrong value into that face - the two-level
      // chain re-shape is its own follow-up unit.
      if (html_tag)
      {
        // M10: zero own symbols on instances (tag on prototype)
        doc_tpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "HTMLDocument"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
      }
      else
      {
        doc_tpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "Document"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
      }

      // G4.389: only the page/boot document carries an instance face.
      // Derived documents (DOMParser / implementation.createHTMLDocument /
      // createDocument / iframe about:blank) are accessor-free instances
      // chained to the shared Document.prototype - the reference keeps zero
      // own members on them, so instance accessors would both shadow the
      // shared level and leak a 40-key own face to gOPN/for-in.
      if (html_tag) {
      auto doc_accessor = [&](const char* prop_name,
                              v8::FunctionCallback callback) {
        v8::Local<v8::String> key = ToV8Str(isolate, prop_name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, callback);
        fn->SetClassName(ToV8Str(isolate, std::string("get ") + prop_name));
        doc_tmpl->SetAccessorProperty(key, fn,
                                      v8::Local<v8::FunctionTemplate>());
      };
      doc_accessor("readyState", GetReadyState);
      doc_accessor("documentElement", GetDocumentElement);
      doc_accessor("body", GetBody);
      doc_accessor("head", GetHead);
      doc_accessor("all", GetAll);
      doc_accessor("childNodes", GetChildNodes);
      doc_accessor("children", GetChildren);
      doc_accessor("firstElementChild", GetDocFirstElementChild);
      doc_accessor("lastElementChild", GetDocLastElementChild);
      doc_accessor("childElementCount", GetDocChildElementCount);
      doc_accessor("dir", GetDocDir);
      doc_accessor("fullscreenEnabled", GetDocFullscreenEnabled);
      doc_accessor("timeline", GetDocTimeline);
      doc_accessor("pictureInPictureElement", GetDocNull);
      doc_accessor("pointerLockElement", GetDocNull);
      doc_accessor("featurePolicy", GetDocNull);
      doc_accessor("adoptedStyleSheets", GetDocAdoptedStyleSheets);
      doc_accessor("forms", GetForms);
      doc_accessor("images", GetImages);
      doc_accessor("links", GetLinks);
      doc_accessor("anchors", GetAnchors);
      doc_accessor("scripts", GetScripts);
      doc_accessor("firstChild", GetFirstChild);
      doc_accessor("URL", GetDocumentURL);
      doc_accessor("documentURI", GetDocumentURL);
      doc_accessor("location", GetDocumentLocation);
      doc_accessor("scrollingElement", GetDocumentScrollingElement);
      doc_accessor("currentScript", GetDocumentCurrentScript);
      doc_accessor("domain", GetDocumentDomain);
      doc_accessor("lastModified", GetDocumentLastModified);
      doc_accessor("characterSet", GetDocumentCharacterSet);
      doc_accessor("charset", GetDocumentCharacterSet);
      doc_accessor("inputEncoding", GetDocumentCharacterSet);
      doc_accessor("designMode", GetDocumentDesignMode);
      doc_accessor("dir", GetDocumentDir);
      doc_accessor("prerendering", GetDocumentPrerendering);
      doc_accessor("wasDiscarded", GetDocumentWasDiscarded);
      {
        // document.cookie accepts writes (unlike the other doc accessors)
        v8::Local<v8::String> key = ToV8Str(isolate, "cookie");
        v8::Local<v8::FunctionTemplate> getter =
            v8::FunctionTemplate::New(isolate, DocumentGetCookie);
        getter->SetClassName(ToV8Str(isolate, "get cookie"));
        v8::Local<v8::FunctionTemplate> setter =
            v8::FunctionTemplate::New(isolate, DocumentSetCookie);
        setter->SetClassName(ToV8Str(isolate, "set cookie"));
        doc_tmpl->SetAccessorProperty(key, getter, setter);
      }

      auto doc_method = [&](const char* method_name,
                            v8::FunctionCallback callback) {
        v8::Local<v8::String> key = ToV8Str(isolate, method_name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, callback);
        fn->SetClassName(key);
        doc_tpl->PrototypeTemplate()->Set(key, fn);
      };
      doc_method("getElementById", GetElementById);
      doc_method("getElementsByTagName", GetElementsByTagName);
      doc_method("getElementsByClassName", GetElementsByClassName);
      doc_method("createElement", DocumentCreateElement);
      doc_method("createTextNode", DocumentCreateTextNode);
      doc_method("createComment", DocumentCreateComment);
      doc_method("createDocumentFragment", DocumentCreateDocumentFragment);
      doc_method("createElementNS", DocumentCreateElementNS);
      doc_method("createRange", DocumentCreateRange);
      doc_method("write", DocumentWrite);
      doc_method("writeln", DocumentWriteln);
      doc_method("querySelector", QuerySelector);
      doc_method("querySelectorAll", QuerySelectorAll);
      doc_method("contains", DocumentContains);
      doc_method("addEventListener", DocumentAddEventListener);
      doc_method("removeEventListener", DocumentRemoveEventListener);
      doc_method("dispatchEvent", DocumentDispatchEvent);
      {
        v8::Local<v8::String> dt_key = ToV8Str(isolate, "doctype");
        v8::Local<v8::FunctionTemplate> dt_fn =
            v8::FunctionTemplate::New(isolate, DocumentGetDoctype);
        dt_fn->SetClassName(ToV8Str(isolate, "get doctype"));
        doc_tmpl->SetAccessorProperty(dt_key, dt_fn,
                                      v8::Local<v8::FunctionTemplate>());
        v8::Local<v8::String> title_key = ToV8Str(isolate, "title");
        v8::Local<v8::FunctionTemplate> title_get =
            v8::FunctionTemplate::New(isolate, DocumentGetTitle);
        title_get->SetClassName(ToV8Str(isolate, "get title"));
        v8::Local<v8::FunctionTemplate> title_set =
            v8::FunctionTemplate::New(isolate, DocumentSetTitle);
        title_set->SetClassName(ToV8Str(isolate, "set title"));
        doc_tmpl->SetAccessorProperty(title_key, title_get, title_set);
      }

      // polyfill-registered document surface (getSelection/...)
      {
        v8::Local<v8::Value> dpatches;
        dpatches = InternalBridgeGet(isolate, context, "documentPatches");
        if (dpatches->IsObject())
        {
          v8::Local<v8::Array> dkeys = dpatches.As<v8::Object>()
              ->GetOwnPropertyNames(context).ToLocalChecked();
          for (uint32_t di = 0; di < dkeys->Length(); di++)
          {
            v8::Local<v8::Value> dkey = dkeys->Get(context, di).ToLocalChecked();
            v8::Local<v8::Value> dfn;
            if (dkey->IsString() &&
                dpatches.As<v8::Object>()->Get(context, dkey).ToLocal(&dfn) &&
                dfn->IsFunction())
            {
              // "get <name>" keys install as accessor properties
              v8::String::Utf8Value dname_v(isolate, dkey);
              std::string dname(*dname_v, dname_v.length());
              const bool is_accessor =
                  dname.rfind("get ", 0) == 0;
              v8::Local<v8::String> prop_key = is_accessor
                  ? ToV8Str(isolate, dname.substr(4))
                  : dkey.As<v8::String>();
              v8::Local<v8::FunctionTemplate> dmethod_tpl =
                  v8::FunctionTemplate::New(
                      isolate,
                      [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                        v8::Isolate* iso = info.GetIsolate();
                        v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                        v8::Local<v8::Function> patch =
                            info.Data().As<v8::Function>();
                        std::vector<v8::Local<v8::Value>> args;
                        for (int i = 0; i < info.Length(); i++)
                          args.push_back(info[i]);
                        v8::Local<v8::Value> result;
                        if (patch->Call(ctx, info.This(),
                                        info.Length(),
                                        args.empty() ? nullptr : args.data())
                                .ToLocal(&result))
                          info.GetReturnValue().Set(result);
                      },
                      dfn);
              dmethod_tpl->SetClassName(prop_key);
              if (is_accessor)
                doc_tmpl->SetAccessorProperty(
                    prop_key, dmethod_tpl,
                    v8::Local<v8::FunctionTemplate>());
              else
                doc_tmpl->Set(prop_key, dmethod_tpl);
            }
          }
        }
      }
      } // html_tag: end of the derived-document instance-face gate

      v8::Local<v8::Object> document_obj =
          doc_tmpl->NewInstance(context).ToLocalChecked();
      // G4.246 bisect: register the wrapper so WrapNode(doc_node) returns
      // this object (reference identity: html.parentNode === document)
      if (doc_node != nullptr)
      {
        DomState* wrap_dom = GetDomState(context);
        if (wrap_dom != nullptr &&
            wrap_dom->wraps.find(lxb_dom_interface_node(doc_node)) ==
                wrap_dom->wraps.end())
          wrap_dom->wraps[lxb_dom_interface_node(doc_node)] =
              v8::Global<v8::Object>(isolate, document_obj);
      }
      // global getSelection delegates to the document surface (T4 ledger
      // chunk; the stateful Selection lives in the document patches).
      // G4.393n: native-method form (RemovePrototype) so every per-document
      // re-install of this global stays prototype-less like the reference.
      {
        v8::Local<v8::FunctionTemplate> get_selection =
            v8::FunctionTemplate::New(
                isolate,
                [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                  v8::Isolate* iso = info.GetIsolate();
                  v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                  v8::Local<v8::Value> document;
                  if (!ctx->Global()
                               ->Get(ctx, ToV8Str(iso, "document"))
                               .ToLocal(&document) ||
                      !document->IsObject())
                    return;
                  v8::Local<v8::Value> get;
                  if (!document.As<v8::Object>()
                               ->Get(ctx, ToV8Str(iso, "getSelection"))
                               .ToLocal(&get) ||
                      !get->IsFunction())
                    return;
                  v8::Local<v8::Value> result;
                  v8::TryCatch try_catch(iso);
                  if (get.As<v8::Function>()
                          ->Call(ctx, document, 0, nullptr)
                          .ToLocal(&result))
                    info.GetReturnValue().Set(result);
                  try_catch.Reset();
                });
        get_selection->SetClassName(ToV8Str(isolate, "getSelection"));
        get_selection->RemovePrototype();
        (void)context
            ->Global()
            ->DefineOwnProperty(
                context, ToV8Str(isolate, "getSelection"),
                get_selection->GetFunction(context).ToLocalChecked(),
                static_cast<v8::PropertyAttribute>(0))
            .FromMaybe(false);
      }
      {
        // the document is an EventTarget too: hook EventTarget.prototype
        // onto the document PROTOTYPE object (methods live there, so the
        // instance's own chain must keep it)
        v8::Local<v8::Value> et;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "EventTarget"))
                .ToLocal(&et) && et->IsFunction())
        {
          v8::Local<v8::Value> etp;
          if (et.As<v8::Function>()
                  ->Get(context, ToV8Str(isolate, "prototype"))
                  .ToLocal(&etp) && etp->IsObject())
          {
            v8::Local<v8::Value> dp = document_obj->GetPrototype();
            if (dp->IsObject())
              (void)dp.As<v8::Object>()
                  ->SetPrototype(context, etp.As<v8::Object>())
                  .FromMaybe(false);
          }
        }
      }
      // the document node travels in the same Private symbol as element
      // nodes so every accessor can reuse NodeOf
      (void)document_obj
          ->SetPrivate(context,
                       v8::Private::ForApi(isolate,
                                           ToV8Str(isolate, "lv8.dom.node")),
                       v8::External::New(isolate, static_cast<void*>(doc_node)))
          .FromMaybe(false);
      // document.implementation: one real object per document.  M6: built
      // from a FunctionTemplate so the prototype carries the toString tag
      // (zero own symbols on the instance); members set on the instance
      // template stay own - a plain FunctionTemplate would put them on
      // .prototype, invisible to document.implementation.createHTMLDocument
      {
        v8::Local<v8::FunctionTemplate> impl_ftpl =
            v8::FunctionTemplate::New(isolate);
        impl_ftpl->SetClassName(ToV8Str(isolate, "DOMImplementation"));
        impl_ftpl->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DOMImplementation"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        v8::Local<v8::ObjectTemplate> impl_tmpl =
            impl_ftpl->InstanceTemplate();
        impl_tmpl->SetInternalFieldCount(0);
        auto impl_method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          impl_tmpl->Set(key, fn);
        };
        impl_method("createHTMLDocument", ImplCreateHTMLDocument);
        impl_method("createDocument", ImplCreateDocument);
        impl_method("createDocumentType", ImplCreateDocumentType);
        impl_method("hasFeature",
                    [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                      info.GetReturnValue().Set(v8::Boolean::New(
                          info.GetIsolate(), true));
                    });
        v8::Local<v8::Object> impl =
            impl_tmpl->NewInstance(context).ToLocalChecked();
        (void)document_obj
            // The boot document already installed a getter-only implementation
            // member on the shared prototype.  Define an own data slot so a
            // newly-created document retains its per-document implementation
            // until the JS face installer re-homes it.
            ->DefineOwnProperty(context, ToV8Str(isolate, "implementation"), impl)
            .FromMaybe(false);
        // G4.391: the JS installer parks itself on the internal string-keyed
        // bridge (a Symbol.for globalThis key is getOwnPropertySymbols
        // visible; reference's window carries zero own symbols).
        v8::Local<v8::Value> implementation_installer =
            InternalBridgeGet(isolate, context,
                              "lv8DocumentImplementationInstall");
        if (implementation_installer->IsFunction())
        {
          v8::Local<v8::Value> implementation_argv[] = {document_obj, impl};
          v8::TryCatch try_catch(isolate);
          (void)implementation_installer.As<v8::Function>()->Call(
              context, context->Global(), 2, implementation_argv);
          try_catch.Reset();
        }
      }
      // G4.368: parsed/iframe documents get fresh per-call prototypes, so a
      // one-shot JS install can never cover them.  The page prelude
      // registers an installer on the internal string-keyed bridge
      // (G4.391: was a Symbol.for globalThis key - getOwnPropertySymbols
      // visible, reference window carries zero own symbols);
      // hand it the new document so the document-level members land on the
      // fresh face level exactly as the page document got them.
      {
        v8::Local<v8::Value> installer = InternalBridgeGet(
            isolate, context, "lv8XPathInstallDoc");
        if (installer->IsFunction())
        {
          v8::Local<v8::Value> argv[] = {document_obj};
          v8::TryCatch try_catch(isolate);
          (void)installer.As<v8::Function>()->Call(context,
                                                   context->Global(), 1,
                                                   argv);
          try_catch.Reset();
        }
      }
      // The page chain installer keeps implementation instances in a
      // closure-backed WeakMap.  Parsed and iframe documents are made after
      // that installer and must enroll before becoming observable.
      {
        v8::Local<v8::Value> installer = InternalBridgeGet(
            isolate, context, "lv8DocumentImplementationInstall");
        if (installer->IsFunction())
        {
          v8::Local<v8::Value> argv[] = {document_obj};
          v8::TryCatch try_catch(isolate);
          (void)installer.As<v8::Function>()->Call(context,
                                                   context->Global(), 1,
                                                   argv);
          try_catch.Reset();
        }
      }
      // G4.389: derived documents share the page Document.prototype from
      // creation (reference answers every member at the shared level; the fresh
      // per-call template prototype carries nothing).  Done here so a
      // document parsed by a load1 page script - before any JS installer
      // runs - still resolves the full face.
      if (!html_tag)
      {
        v8::Local<v8::Value> doc_ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "Document"))
                .ToLocal(&doc_ctor) &&
            doc_ctor->IsFunction())
        {
          v8::Local<v8::Value> dp;
          if (doc_ctor.As<v8::Function>()
                  ->Get(context, ToV8Str(isolate, "prototype"))
                  .ToLocal(&dp) &&
              dp->IsObject())
          {
            (void)document_obj->SetPrototype(context, dp).FromMaybe(false);
          }
        }
      }
      return document_obj;
    }

    void DOMParserParseFromString(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      DomState* dom = GetDomState(context);
      if (dom == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "Error: DOMParser needs a loaded page document"));
        return;
      }
      if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: parseFromString(string, type) requires strings"));
        return;
      }
      std::string type = ToUtf8(isolate, info[1]);
      if (type != "text/html")
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "NotSupportedError: only text/html parsing is supported"));
        return;
      }
      std::string html = ToUtf8(isolate, info[0]);
      lxb_html_document_t* parsed = lxb_html_document_create();
      if (parsed == nullptr ||
          lxb_html_document_parse(
              parsed, (const lxb_char_t*)html.c_str(), html.size()) !=
              LXB_STATUS_OK)
      {
        if (parsed != nullptr)
          lxb_html_document_destroy(parsed);
        isolate->ThrowException(
            ToV8Str(isolate, "SyntaxError: html parse failed"));
        return;
      }
      // the parsed document lives (and dies) with the context
      dom->parsed_docs.push_back(parsed);
      info.GetReturnValue().Set(
          MakeDocumentObject(isolate, context, &parsed->dom_document,
                             /*html_tag=*/false));
    }

    void XMLSerializerSerializeToString(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      if (info.Length() < 1 || !info[0]->IsObject() ||
          NodeOf(info[0].As<v8::Object>()) == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: serializeToString requires a DOM node"));
        return;
      }
      lxb_dom_node_t* node = NodeOf(info[0].As<v8::Object>());
      info.GetReturnValue().Set(ToV8Str(isolate, NodeOuterHTML(node)));
    }
    } // namespace

  // element-level queries (defined in the anonymous namespace above)
  void ElementQuerySelector(const v8::FunctionCallbackInfo<v8::Value>& info);
  void ElementQuerySelectorAll(const v8::FunctionCallbackInfo<v8::Value>& info);
  void ElementGetElementsByTagName(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  void ElementGetElementsByClassName(
      const v8::FunctionCallbackInfo<v8::Value>& info);

  // ------------------------------------------------------------ wrap cache

  // B1: per-iframe barren realm --------------------------------------------
  //
  // Real browsers give every iframe its own realm: separate intrinsics
  // (Array/Object/eval), a WindowProxy-typed global, and a contentDocument.
  // lv8's single-context runtime previously aliased contentWindow to the main
  // window, which the WAF SDK's environment checks answer differently.  The
  // first slice is intentionally barren (document/navigator/location shared
  // with the host page + window self-aliases + a "Window" toStringTag); the
  // Phase A trace shows the SDK only probing realm identity and document
  // presence through contentWindow.

  v8::Local<v8::Context> EnsureIframeRealm(v8::Isolate* isolate,
                                           v8::Local<v8::Context> context,
                                           DomState* dom,
                                           lxb_dom_node_t* node)
  {
    auto it = dom->iframe_realms.find(node);
    if (it != dom->iframe_realms.end())
      return it->second.Get(isolate);
    // Same-origin with the host page: V8 gives each context a unique
    // security token by default, which would deny cross-realm global
    // property access ("no access"); browser frames of one origin share it.
    v8::Local<v8::Value> token = context->GetSecurityToken();
    v8::Local<v8::Context> realm =
        v8::Context::New(isolate, nullptr, GlobalTemplateWithNamedAccess(isolate));
    realm->SetSecurityToken(token);
    v8::Local<v8::Object> global = realm->Global();
    {
      v8::Context::Scope realm_scope(realm);
      // window named reads inside the realm answer against the host page
      // document (reference: cw.frameid === <iframe>); park the host DomState
      if (DomState* host_dom = GetDomState(context))
      {
        (void)global
            ->SetPrivate(realm,
                         v8::Private::ForApi(
                             isolate, ToV8Str(isolate, "lv8.realm.named-dom")),
                         v8::External::New(isolate, host_dom))
            .FromMaybe(false);
      }
      // Object.prototype.toString.call(contentWindow) -> "[object Window]"
      (void)global
          ->DefineOwnProperty(
              realm, v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "Window"),
              static_cast<v8::PropertyAttribute>(
                  v8::ReadOnly | v8::DontEnum | v8::DontDelete))
          .FromMaybe(false);
      // barren window surface: share the host page wrappers for now.
      // document is NOT shared: reference gold gives each iframe its own
      // about:blank document ("<head></head><body></body>"), and the
      // challenge SDK fingerprints iframe.contentDocument - exposing the
      // host page there leaks the whole DOM into its collector.
      for (const char* name : {"navigator", "location",
                               "localStorage", "sessionStorage", "history",
                               "console", "screen", "performance"})
      {
        v8::Local<v8::Value> host;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, name))
                .ToLocal(&host) &&
            !host->IsUndefined())
        {
          (void)global->Set(realm, ToV8Str(isolate, name), host)
              .FromMaybe(false);
        }
      }
      // own about:blank document for this frame
      lxb_html_document_t* blank = lxb_html_document_create();
      if (blank != nullptr)
      {
        static const char kBlankDoc[] =
            "<html><head></head><body></body></html>";
        if (lxb_html_document_parse(
                blank, (const lxb_char_t*)kBlankDoc,
                sizeof(kBlankDoc) - 1) == LXB_STATUS_OK)
        {
          dom->parsed_docs.push_back(blank);
          v8::Local<v8::Value> blank_doc = MakeDocumentObject(
              isolate, context, &blank->dom_document, /*html_tag=*/false);
          // G4.389: the reference iframe document is an HTMLDocument over
          // the two-level chain (doc -> HTMLDocument.prototype ->
          // Document.prototype); MakeDocumentObject parked it on the shared
          // Document.prototype above, so lift it one level here.
          {
            v8::Local<v8::Value> hd_ctor;
            if (context->Global()
                    ->Get(context, ToV8Str(isolate, "HTMLDocument"))
                    .ToLocal(&hd_ctor) &&
                hd_ctor->IsFunction())
            {
              v8::Local<v8::Value> hp;
              if (hd_ctor.As<v8::Function>()
                      ->Get(context, ToV8Str(isolate, "prototype"))
                      .ToLocal(&hp) &&
                  hp->IsObject())
              {
                (void)blank_doc.As<v8::Object>()
                    ->SetPrototype(context, hp)
                    .FromMaybe(false);
              }
            }
          }
          InstallDocumentAccessor(
              isolate, realm, blank_doc.As<v8::Object>());
        }
        else
        {
          lxb_html_document_destroy(blank);
        }
      }
      // window self-aliases
      for (const char* name : {"window", "self", "parent", "top"})
      {
        (void)global->Set(realm, ToV8Str(isolate, name), global)
            .FromMaybe(false);
      }
      // surface audit G4.153: reference/Chrome answer
      // contentWindow.frameElement === <iframe> with the host element.
      // The wrapper must be created in the HOST context: the realm has
      // no DomState, and the shared wrap cache makes the identity
      // check hold.
      (void)global
          ->DefineOwnProperty(
              realm, ToV8Str(isolate, "frameElement"),
              WrapNode(isolate, context, node),
              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    dom->iframe_realms[node].Reset(isolate, realm);
    return realm;
  }

  void GetIframeContentWindow(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    DomState* dom = GetDomState(context);
    lxb_dom_node_t* node = NodeOf(info.This());
    // Match reference's verified behaviour: a detached iframe exposes no
    // contentWindow; the realm materializes once the node is in the tree.
    if (dom == nullptr || node == nullptr || node->parent == nullptr)
    {
      info.GetReturnValue().SetNull();
      return;
    }
    v8::Local<v8::Context> realm =
        EnsureIframeRealm(isolate, context, dom, node);
    info.GetReturnValue().Set(realm->Global());
  }

  void GetIframeContentDocument(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    DomState* dom = GetDomState(context);
    lxb_dom_node_t* node = NodeOf(info.This());
    if (dom == nullptr || node == nullptr || node->parent == nullptr)
    {
      info.GetReturnValue().SetNull();
      return;
    }
    v8::Local<v8::Context> realm =
        EnsureIframeRealm(isolate, context, dom, node);
    v8::Local<v8::Value> doc;
    if (realm->Global()->Get(realm, ToV8Str(isolate, "document")).ToLocal(&doc))
      info.GetReturnValue().Set(doc);
  }

  void IframeGetSVGDocument(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    // An HTML iframe has no SVG document in the deterministic single-realm
    // host.  The reference exposes the method and returns null for this case.
    info.GetReturnValue().SetNull();
  }

  // B1: HTMLFormElement named access ([LegacyOverrideBuiltIns]).  A control
  // whose name/id matches the requested property shadows even built-in
  // members ("form.textContent" returns the input named textContent).

  // HTML form-associated listed elements (form.elements / form.length);
  // the challenge reads form.length and form[i] around its form probe.
  bool FormControlTag(lxb_dom_node_t* node)
  {
    size_t len = 0;
    const lxb_char_t* name =
        lxb_dom_element_local_name(lxb_dom_interface_element(node), &len);
    if (name == nullptr) return false;
    switch (len)
    {
      case 3: return std::memcmp(name, "img", 3) == 0;
      case 5: return std::memcmp(name, "input", 5) == 0;
      case 6: return std::memcmp(name, "select", 6) == 0 ||
                     std::memcmp(name, "button", 6) == 0 ||
                     std::memcmp(name, "object", 6) == 0 ||
                     std::memcmp(name, "output", 6) == 0;
      case 8: return std::memcmp(name, "textarea", 8) == 0 ||
                     std::memcmp(name, "fieldset", 8) == 0;
      default: return false;
    }
  }

  int64_t FormControlCount(lxb_dom_node_t* node)
  {
    int64_t count = 0;
    for (lxb_dom_node_t* child = node->first_child; child != nullptr;
         child = child->next)
    {
      if (child->type == LXB_DOM_NODE_TYPE_ELEMENT && FormControlTag(child))
        count++;
      count += FormControlCount(child);
    }
    return count;
  }

  lxb_dom_node_t* FormControlAt(lxb_dom_node_t* node, int64_t* index)
  {
    for (lxb_dom_node_t* child = node->first_child; child != nullptr;
         child = child->next)
    {
      if (child->type == LXB_DOM_NODE_TYPE_ELEMENT && FormControlTag(child))
      {
        if (*index == 0) return child;
        (*index)--;
      }
      lxb_dom_node_t* hit = FormControlAt(child, index);
      if (hit != nullptr) return hit;
    }
    return nullptr;
  }

  lxb_dom_node_t* FormFindControl(lxb_dom_node_t* node,
                                  const std::string& wanted)
  {
    for (lxb_dom_node_t* child = node->first_child; child != nullptr;
         child = child->next)
    {
      if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
      {
        lxb_dom_element_t* el = lxb_dom_interface_element(child);
        for (const char* attr : {"name", "id"})
        {
          size_t len = 0;
          const lxb_char_t* value = lxb_dom_element_get_attribute(
              el, (const lxb_char_t*)attr, std::strlen(attr), &len);
          if (value != nullptr &&
              wanted == std::string((const char*)value, len))
            return child;
        }
      }
      lxb_dom_node_t* hit = FormFindControl(child, wanted);
      if (hit != nullptr)
        return hit;
    }
    return nullptr;
  }

  v8::Intercepted FormNamedGetter(
      v8::Local<v8::Name> name,
      const v8::PropertyCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    if (!name->IsString())
      return v8::Intercepted::kNo;
    lxb_dom_node_t* node = NodeOf(info.This());
    if (node == nullptr)
      return v8::Intercepted::kNo;
    std::string wanted = ToUtf8(isolate, name);
    lxb_dom_node_t* control = FormFindControl(node, wanted);
    if (control == nullptr)
      return v8::Intercepted::kNo;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> control_wrapper =
        WrapNode(isolate, context, control);
    if (control_wrapper.IsEmpty())
      return v8::Intercepted::kNo;
    info.GetReturnValue().Set(control_wrapper);
    return v8::Intercepted::kYes;
  }

  void FormLength(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    lxb_dom_node_t* node = NodeOf(info.This());
    if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    {
      info.GetReturnValue().Set(0);
      return;
    }
    info.GetReturnValue().Set(
        static_cast<double>(FormControlCount(node)));
  }

  // form.elements: an HTMLFormControlsCollection-like view over the
  // form's controls.  Reference-wheel parity (probed on reference): length,
  // named access, namedItem and the collection toStringTag; indexed own
  // access reads undefined there too.  The page business core's
  // detached-form fingerprint stage reads form.elements on a
  // programmatic (never-attached) form before anything else - without
  // this accessor the stage bailed on lv8 and the page-signature signature
  // diverged (WAF 400 on every signed POST; G4.221).
  void FormElementsNamedItem(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    lxb_dom_node_t* node = NodeOf(info.This());
    if (node == nullptr || info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().SetNull();
      return;
    }
    lxb_dom_node_t* control = FormFindControl(node, ToUtf8(isolate, info[0]));
    if (control == nullptr)
    {
      info.GetReturnValue().SetNull();
      return;
    }
    v8::Local<v8::Object> control_wrapper = WrapNode(isolate, context, control);
    if (control_wrapper.IsEmpty())
      info.GetReturnValue().SetNull();
    else
      info.GetReturnValue().Set(control_wrapper);
  }

  v8::Intercepted FormElementsNamed(
      v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    if (!name->IsString())
      return v8::Intercepted::kNo;
    std::string wanted = ToUtf8(isolate, name);
    if (wanted == "length" || wanted == "namedItem" ||
        wanted == "constructor")
      return v8::Intercepted::kNo;  // real own properties below
    lxb_dom_node_t* node = NodeOf(info.This());
    if (node == nullptr)
      return v8::Intercepted::kNo;
    lxb_dom_node_t* control = FormFindControl(node, wanted);
    if (control == nullptr)
      return v8::Intercepted::kNo;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> control_wrapper = WrapNode(isolate, context, control);
    if (control_wrapper.IsEmpty())
      return v8::Intercepted::kNo;
    info.GetReturnValue().Set(control_wrapper);
    return v8::Intercepted::kYes;
  }

  void FormElementsGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    lxb_dom_node_t* node = NodeOf(info.This());
    if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
    {
      info.GetReturnValue().SetNull();
      return;
    }
    v8::Local<v8::ObjectTemplate> impl = v8::ObjectTemplate::New(isolate);
    impl->SetHandler(v8::NamedPropertyHandlerConfiguration(FormElementsNamed));
    v8::Local<v8::Object> collection =
        impl->NewInstance(context).FromMaybe(v8::Local<v8::Object>());
    if (collection.IsEmpty())
    {
      info.GetReturnValue().SetNull();
      return;
    }
    // the collection walks the form's controls through the same node
    // private the form wrapper carries
    (void)collection
        ->SetPrivate(context,
                     v8::Private::ForApi(isolate,
                                         ToV8Str(isolate, "lv8.dom.node")),
                     v8::External::New(isolate, node))
        .FromMaybe(false);
    (void)collection
        ->DefineOwnProperty(context, ToV8Str(isolate, "length"),
                            v8::Number::New(
                                isolate,
                                static_cast<double>(FormControlCount(node))),
                            v8::PropertyAttribute::None)
        .FromMaybe(false);
    v8::Local<v8::FunctionTemplate> named_item_tpl =
        v8::FunctionTemplate::New(isolate, FormElementsNamedItem);
    named_item_tpl->SetClassName(ToV8Str(isolate, "namedItem"));
    v8::Local<v8::Function> named_item =
        named_item_tpl->GetFunction(context).ToLocalChecked();
    (void)collection
        ->DefineOwnProperty(context, ToV8Str(isolate, "namedItem"),
                            named_item, v8::PropertyAttribute::DontEnum)
        .FromMaybe(false);
    (void)collection
        ->DefineOwnProperty(context, v8::Symbol::GetToStringTag(isolate),
                            ToV8Str(isolate, "HTMLFormControlsCollection"),
                            v8::PropertyAttribute::DontEnum)
        .FromMaybe(false);
    info.GetReturnValue().Set(collection);
  }

  v8::Intercepted FormIndexedGetter(
      uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info)
  {    lxb_dom_node_t* node = NodeOf(info.This());
    if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
      return v8::Intercepted::kNo;
    int64_t remaining = static_cast<int64_t>(index);
    lxb_dom_node_t* control = FormControlAt(node, &remaining);
    if (control == nullptr)
      return v8::Intercepted::kNo;
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> control_wrapper = WrapNode(isolate, context, control);
    if (control_wrapper.IsEmpty())
      return v8::Intercepted::kNo;
    info.GetReturnValue().Set(control_wrapper);
    return v8::Intercepted::kYes;
  }

  v8::Local<v8::Object> WrapNode(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 lxb_dom_node_t* node)
  {
    DomState* dom = GetDomState(context);
    if (dom == nullptr || node == nullptr)
      return v8::Local<v8::Object>();

    auto it = dom->wraps.find(node);
    if (it != dom->wraps.end())
    {
      v8::Local<v8::Object> cached = it->second.Get(isolate);
      // PAGE_LOAD_PRELUDE can touch parsed nodes before its public hierarchy
      // pass finishes. Their cached wrapper then retains the provisional
      // FunctionTemplate prototype while fresh DOMParser wrappers use the
      // current interface prototype. Repair that cache hit lazily so both
      // paths expose the same live hierarchy.
      // G4.389: only repair TRULY provisional wrappers.  A provisional
      // template prototype chains straight to Object.prototype, while every
      // deliberate shape reaches Node.prototype - the bare embed/object
      // element (own toStringTag, direct Node.prototype link, G4.262) and
      // custom-element prototypes included.  The previous unconditional
      // SetPrototype re-parented those onto the interface prototype and
      // broke the plain-element chain (test_engine_g2).
      if (node->type == LXB_DOM_NODE_TYPE_ELEMENT)
      {
        v8::Local<v8::Value> node_ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "Node"))
                .ToLocal(&node_ctor) &&
            node_ctor->IsFunction())
        {
          v8::Local<v8::Value> node_proto;
          if (node_ctor.As<v8::Function>()
                  ->Get(context, ToV8Str(isolate, "prototype"))
                  .ToLocal(&node_proto) &&
              node_proto->IsObject())
          {
            v8::Local<v8::Object> node_proto_obj = node_proto.As<v8::Object>();
            v8::Local<v8::Value> cur = cached->GetPrototype();
            bool reaches_node = false;
            for (int hop = 0; hop < 8 && !cur.IsEmpty() && cur->IsObject();
                 hop++)
            {
              if (cur.As<v8::Object>()->SameValue(node_proto_obj))
              {
                reaches_node = true;
                break;
              }
              cur = cur.As<v8::Object>()->GetPrototype();
            }
            if (!reaches_node)
            {
              std::string iface = InterfaceNameFor(node);
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
                  (void)cached->SetPrototype(context, proto.As<v8::Object>())
                      .FromMaybe(false);
              }
            }
          }
        }
      }
      return cached;
    }

    std::string interface_name = InterfaceNameFor(node);
    // G4.262: text/comment/cdata nodes have an empty own face in the
    // reference.  Give them a minimal cached template (skipping the element
    // accessor block below) and note the interface prototype to chain the
    // instance onto.
    const bool is_element_node = node->type == LXB_DOM_NODE_TYPE_ELEMENT;
    const bool is_leaf_node =
        node->type == LXB_DOM_NODE_TYPE_TEXT ||
        node->type == LXB_DOM_NODE_TYPE_COMMENT ||
        node->type == LXB_DOM_NODE_TYPE_CDATA_SECTION;
    v8::Local<v8::Value> non_elt_proto;
    if (is_leaf_node)
    {
      v8::Local<v8::Value> gctor;
      if (context->Global()
                  ->Get(context, ToV8Str(isolate, interface_name))
                  .ToLocal(&gctor) &&
          gctor->IsFunction())
      {
        v8::Local<v8::Value> gp;
        if (gctor.As<v8::Function>()
                ->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&gp) &&
            gp->IsObject())
          non_elt_proto = gp;
      }
      if (dom->tag_ctors.find(interface_name) == dom->tag_ctors.end())
      {
        v8::Local<v8::FunctionTemplate> ntpl =
            v8::FunctionTemplate::New(isolate);
        ntpl->SetClassName(ToV8Str(isolate, interface_name));
        v8::Local<v8::ObjectTemplate> ninst = ntpl->InstanceTemplate();
        ninst->SetInternalFieldCount(0);
        ninst->Set(v8::Symbol::GetToStringTag(isolate),
                   ToV8Str(isolate, interface_name),
                   v8::PropertyAttribute::ReadOnly);
        dom->tag_ctors[interface_name].Reset(isolate, ntpl);
      }
    }

    auto shared_it = dom->tag_ctors.find(interface_name);
    const bool has_shared_template = shared_it != dom->tag_ctors.end();
    v8::Local<v8::FunctionTemplate> tpl = has_shared_template
        ? shared_it->second.Get(isolate) : v8::FunctionTemplate::New(isolate);
    if (!has_shared_template)
      tpl->SetClassName(ToV8Str(isolate, interface_name));
    v8::Local<v8::ObjectTemplate> instance_tmpl = tpl->InstanceTemplate();
    if (!has_shared_template)
    {
    // keep zero internal fields (see NodeOf); the node pointer is a Private
    instance_tmpl->SetInternalFieldCount(0);
    instance_tmpl->Set(v8::Symbol::GetToStringTag(isolate),
                       ToV8Str(isolate, interface_name),
                       v8::PropertyAttribute::ReadOnly);

    auto add_accessor = [&](const char* prop_name,
                            v8::FunctionCallback callback) {
      v8::Local<v8::String> key = ToV8Str(isolate, prop_name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, callback);
      fn->SetClassName(ToV8Str(isolate, std::string("get ") + prop_name));
      instance_tmpl->SetAccessorProperty(key, fn,
                                         v8::Local<v8::FunctionTemplate>());
    };
    // id/className accept writes (IDL attribute setters)
    auto add_rw_accessor = [&](const char* prop_name,
                               v8::FunctionCallback getter,
                               v8::FunctionCallback setter) {
      v8::Local<v8::String> key = ToV8Str(isolate, prop_name);
      v8::Local<v8::FunctionTemplate> get_fn =
          v8::FunctionTemplate::New(isolate, getter);
      get_fn->SetClassName(ToV8Str(isolate, std::string("get ") + prop_name));
      v8::Local<v8::FunctionTemplate> set_fn =
          v8::FunctionTemplate::New(isolate, setter);
      set_fn->SetClassName(ToV8Str(isolate, std::string("set ") + prop_name));
      instance_tmpl->SetAccessorProperty(key, get_fn, set_fn);
    };
    add_rw_accessor("checked", GetCheckedAttr, SetCheckedAttr);
    // G4.232: ResolvedAttrGetter recovers the attribute name from info.Data();
    // a plain add_rw_accessor leaves Data unset, so an absent iframe src
    // resolved the empty string against the page URL instead of answering "".
    // reference answers "" for a fresh iframe (SDK then writes about:blank); lv8's
    // page-URL answer flipped the SDK's iframe branch and left a tracking
    // iframe attached to HEAD (also window.length/frames face divergence).
    auto add_rw_named_accessor = [&](const char* prop_name,
                                     v8::FunctionCallback getter,
                                     v8::FunctionCallback setter,
                                     const char* attr_name) {
      v8::Local<v8::String> key = ToV8Str(isolate, prop_name);
      v8::Local<v8::FunctionTemplate> get_fn =
          v8::FunctionTemplate::New(isolate, getter,
                                    ToV8Str(isolate, attr_name));
      get_fn->SetClassName(ToV8Str(isolate, std::string("get ") + prop_name));
      v8::Local<v8::FunctionTemplate> set_fn =
          v8::FunctionTemplate::New(isolate, setter);
      set_fn->SetClassName(ToV8Str(isolate, std::string("set ") + prop_name));
      instance_tmpl->SetAccessorProperty(key, get_fn, set_fn);
    };
    if (interface_name == "HTMLButtonElement" ||
        interface_name == "HTMLInputElement" ||
        interface_name == "HTMLSelectElement" ||
        interface_name == "HTMLTextAreaElement" ||
        interface_name == "HTMLSlotElement" ||
        // form.name reflects the content attribute (window named access
        // rides on it: the challenge renames a live form via form.name)
        interface_name == "HTMLFormElement")
    {
      add_rw_accessor("name", GetNameAttr, SetNameAttr);
    }
    add_rw_accessor("id", GetId, SetId);
    add_rw_accessor("className", GetClassName, SetClassName);
    add_rw_accessor("textContent", GetTextContent, SetTextContent);
    add_accessor("tagName", GetTagName);
    add_rw_accessor("outerHTML", GetOuterHTML, SetOuterHTML);
    add_accessor("parentElement", GetParentElement);
    add_accessor("firstElementChild", GetFirstElementChild);
    add_accessor("lastElementChild", GetLastElementChild);
    add_accessor("lastChild", GetLastChild);
    add_accessor("nextSibling", GetNextSibling);
    add_accessor("previousSibling", GetPreviousSibling);
    add_accessor("parentNode", GetParentNode);
    add_accessor("firstChild", GetFirstChild);
    add_accessor("shadowRoot", GetShadowRoot);
    add_accessor("childNodes", GetChildNodes);
    add_accessor("children", GetChildren);
    add_accessor("isConnected", ElementIsConnected);
    // per-tag specialized accessors carry the attribute name as Data
    auto add_named_accessor = [&](const char* prop_name,
                                  v8::FunctionCallback cb,
                                  const char* attr_name) {
      v8::Local<v8::String> key = ToV8Str(isolate, prop_name);
      v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
          isolate, cb, ToV8Str(isolate, attr_name));
      fn->SetClassName(ToV8Str(isolate, std::string("get ") + prop_name));
      instance_tmpl->SetAccessorProperty(key, fn,
                                         v8::Local<v8::FunctionTemplate>());
    };
    {
      std::string tag_lower;
      size_t name_len = 0;
      const lxb_char_t* raw_name = lxb_dom_node_name(node, &name_len);
      tag_lower.assign((const char*)raw_name, name_len);
      for (auto& c : tag_lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_TYPE)
      {
        add_accessor("name", DocumentTypeName);
      }
      if (node->type == LXB_DOM_NODE_TYPE_COMMENT ||
          node->type == LXB_DOM_NODE_TYPE_TEXT)
      {
        add_accessor("data", GetTextContent);
      }
      if (tag_lower == "img")
      {
        // G4.259: reference wheel exposes these as read/write
        add_rw_named_accessor("src", ResolvedAttrGetter, SetSrcAttr, "src");
        add_rw_named_accessor("width", NumberAttrGetter,
                              SetNamedAttr, "width");
        add_rw_named_accessor("height", NumberAttrGetter,
                              SetNamedAttr, "height");
      }
      else if (tag_lower == "a")
      {
        add_named_accessor("href", ResolvedAttrGetter, "href");
        add_named_accessor("target", PlainAttrGetter, "target");
      }
      else if (tag_lower == "script")
      {
        // G4.259: read/write like the reference wheel
        add_rw_named_accessor("src", ResolvedAttrGetter, SetSrcAttr, "src");
        // HTMLScriptElement.text is the raw child text (same storage as
        // textContent); the challenge SDK fingerprints inline script
        // sources through it (reference gold reads the full bootstrap there).
        add_rw_accessor("text", GetTextContent, SetTextContent);
      }
      else if (tag_lower == "link")
      {
        add_named_accessor("href", ResolvedAttrGetter, "href");
      }
      else if (tag_lower == "iframe")
      {
        add_accessor("contentWindow", GetIframeContentWindow);
        add_accessor("contentDocument", GetIframeContentDocument);
        // HTMLIFrameElement.src is a reflected attribute: the challenge
        // SDK assigns frame.src='about:blank' and reads it back through
        // getAttribute (reference gold reflects the write).
        add_rw_named_accessor("src", ResolvedAttrGetter, SetSrcAttr, "src");
      }
      else if (tag_lower == "form")
      {
        instance_tmpl->SetHandler(
            v8::NamedPropertyHandlerConfiguration(FormNamedGetter));
        instance_tmpl->SetHandler(
            v8::IndexedPropertyHandlerConfiguration(FormIndexedGetter));
        add_accessor("length", FormLength);
      }
    }
    // template.content: DocumentFragment (undefined on other elements);
    // for META elements, .content reads the content attribute instead
    // (HTMLMetaElement IDL; the challenge SDK reads meta.content as a
    // property and fingerprints the value).
    if (interface_name == "HTMLMetaElement")
    {
      add_named_accessor("content", PlainAttrGetter, "content");
      add_named_accessor("name", PlainAttrGetter, "name");
      add_named_accessor("httpEquiv", PlainAttrGetter, "http-equiv");
    }
    else
    {
      add_accessor("content", GetTemplateContent);
    }
    add_accessor("nodeType", GetNodeType);
    add_accessor("nodeName", GetNodeName);
    // slot surface: name idl attribute + assignment queries
    if (interface_name == "HTMLSlotElement")
    {
      v8::Local<v8::String> name_key = ToV8Str(isolate, "name");
      v8::Local<v8::FunctionTemplate> name_get =
          v8::FunctionTemplate::New(isolate, SlotGetName);
      name_get->SetClassName(ToV8Str(isolate, "get name"));
      v8::Local<v8::FunctionTemplate> name_set =
          v8::FunctionTemplate::New(isolate, SlotSetName);
      name_set->SetClassName(ToV8Str(isolate, "set name"));
      instance_tmpl->SetAccessorProperty(name_key, name_get, name_set);
      v8::Local<v8::FunctionTemplate> an_tpl = v8::FunctionTemplate::New(
          isolate, SlotAssignedNodes);
      an_tpl->SetClassName(ToV8Str(isolate, "assignedNodes"));
      instance_tmpl->Set(ToV8Str(isolate, "assignedNodes"), an_tpl);
      v8::Local<v8::FunctionTemplate> ae_tpl = v8::FunctionTemplate::New(
          isolate, SlotAssignedElements);
      ae_tpl->SetClassName(ToV8Str(isolate, "assignedElements"));
      instance_tmpl->Set(ToV8Str(isolate, "assignedElements"), ae_tpl);
    }
    // innerHTML has both a getter and a setter
    {
      v8::Local<v8::String> key = ToV8Str(isolate, "innerHTML");
      v8::Local<v8::FunctionTemplate> getter =
          v8::FunctionTemplate::New(isolate, GetInnerHTML);
      getter->SetClassName(ToV8Str(isolate, "get innerHTML"));
      v8::Local<v8::FunctionTemplate> setter =
          v8::FunctionTemplate::New(isolate, SetInnerHTML);
      setter->SetClassName(ToV8Str(isolate, "set innerHTML"));
      instance_tmpl->SetAccessorProperty(key, getter, setter);
    }

    auto add_method = [&](const char* method_name,
                          v8::FunctionCallback callback, int length) {
      v8::Local<v8::String> key = ToV8Str(isolate, method_name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, callback);
      fn->SetClassName(key);
      fn->SetLength(length);
      tpl->PrototypeTemplate()->Set(key, fn);
    };
    for (const ElementMethod& m : kElementMethods)
      add_method(m.name, m.cb, m.len);
    }


    v8::Local<v8::Object> instance =
        instance_tmpl->NewInstance(context).ToLocalChecked();
    if (is_leaf_node && non_elt_proto->IsObject())
      (void)instance->SetPrototype(context, non_elt_proto.As<v8::Object>())
          .FromMaybe(false);

    // classList: live DOMTokenList over the class attribute
    EnsureInterfaceTemplates(isolate, context, dom);
    if (is_element_node || node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
    {
    {
      v8::Local<v8::Object> class_list =
          dom->tl_tpl.Get(isolate)->InstanceTemplate()->NewInstance(context)
              .ToLocalChecked();
      (void)class_list
          ->SetPrivate(context,
                       v8::Private::ForApi(isolate,
                                           ToV8Str(isolate, "lv8.tl.el")),
                       instance)
          .FromMaybe(false);
      (void)instance
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate, ToV8Str(isolate, "lv8.el.classList")),
                       class_list)
          .FromMaybe(false);
    }
    }  // G4.262: element-only block

    (void)instance
        ->SetPrivate(context,
                     v8::Private::ForApi(isolate,
                                         ToV8Str(isolate, "lv8.dom.node")),
                     v8::External::New(isolate, node))
        .FromMaybe(false);

    // every element wrapper hangs off the shared HTMLElement.prototype so
    // custom element prototypes (which extend HTMLElement) slot in cleanly
    EnsureElementProto(isolate, context, dom);
    if (is_element_node || node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
    {
    {
      std::string iface = InterfaceNameFor(node);
      // reference (work/reference-traces/window-surface.json): embed/object elements
      // are bare wrappers - the prototype walk reads exactly
      // [Element, Node, EventTarget, Object], i.e. an own toStringTag
      // "Element" with Node.prototype as the direct [[Prototype]] and no
      // Element/HTMLElement levels between.  Window named access and
      // form.elements keep their tag-name logic (both key on the local
      // name, not the interface).
      bool bare_element = false;
      {
        size_t tag_len = 0;
        const lxb_char_t* raw_tag = lxb_dom_node_name(node, &tag_len);
        std::string tag_lower((const char*)raw_tag, tag_len);
        for (auto& c : tag_lower)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bare_element =
            iface == "Element" &&
            (tag_lower == "embed" || tag_lower == "object") &&
            dom->node_iface_proto.IsEmpty() == false;
        if (bare_element)
        {
          (void)instance
              ->DefineOwnProperty(
                  context, v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "Element"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum))
              .FromMaybe(false);
          (void)instance
              ->SetPrototype(context, dom->node_iface_proto.Get(isolate))
              .FromMaybe(false);
        }
      }
      auto ctor_it = dom->tag_ctors.find(iface);
      if (ctor_it == dom->tag_ctors.end() && !bare_element)
      {
        v8::Local<v8::FunctionTemplate> ct_tpl =
            v8::FunctionTemplate::New(isolate);
        ct_tpl->SetClassName(ToV8Str(isolate, iface));
        // dynamic wrappers need their own toStringTag (DocumentType and
        // friends otherwise inherit HTMLElement's from the chain)
        ct_tpl->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate), ToV8Str(isolate, iface),
            v8::PropertyAttribute::ReadOnly);
        v8::Local<v8::Function> ct_fn =
            ct_tpl->GetFunction(context).ToLocalChecked();
        v8::Local<v8::Value> proto;
        if (ct_fn->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&proto) &&
            proto->IsObject())
        {
          // SVG graphics interfaces chain through SVGGraphicsElement
          v8::Local<v8::Value> proto_target =
              dom->element_proto.Get(isolate);
          const bool is_svg =
              (dom->svg_namespaces.find(node) !=
               dom->svg_namespaces.end()) ||
              node->ns == LXB_NS_SVG;
          if (is_svg)
          {
            auto gfx = dom->tag_ctors.find("SVGGraphicsElement");
            v8::Local<v8::Function> gfx_ctor;
            if (gfx != dom->tag_ctors.end())
            {
              gfx_ctor =
                  gfx->second.Get(isolate)->GetFunction(context).ToLocalChecked();
            }
            else
            {
              v8::Local<v8::FunctionTemplate> gfx_tpl =
                  v8::FunctionTemplate::New(isolate);
              gfx_tpl->SetClassName(ToV8Str(isolate, "SVGGraphicsElement"));
              gfx_ctor =
                  gfx_tpl->GetFunction(context).ToLocalChecked();
              v8::Local<v8::Value> gfx_proto;
              if (gfx_ctor
                      ->Get(context, ToV8Str(isolate, "prototype"))
                      .ToLocal(&gfx_proto) &&
                  gfx_proto->IsObject())
                (void)gfx_proto.As<v8::Object>()
                    ->SetPrototype(context,
                                   dom->element_proto.Get(isolate))
                    .FromMaybe(false);
              dom->tag_ctors["SVGGraphicsElement"].Reset(isolate, gfx_tpl);
              (void)context
                  ->Global()
                  ->DefineOwnProperty(
                      context, ToV8Str(isolate, "SVGGraphicsElement"),
                      gfx_ctor,
                      static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
                  .FromMaybe(false);
            }
            v8::Local<v8::Value> gfx_proto2;
            if (gfx_ctor->Get(context, ToV8Str(isolate, "prototype"))
                    .ToLocal(&gfx_proto2) &&
                gfx_proto2->IsObject())
              proto_target = gfx_proto2;
          }
          (void)proto.As<v8::Object>()
              ->SetPrototype(context, proto_target)
              .FromMaybe(false);
        }
        dom->tag_ctors[iface].Reset(isolate, ct_tpl);
        {
          // never clobber an already-exposed interface global (the dynamic
          // "Element" wrapper for HTML-namespace <svg> must not replace the
          // real Element constructor)
          v8::Local<v8::Value> existing;
          if (!context->Global()
                    ->Get(context, ToV8Str(isolate, iface))
                    .ToLocal(&existing) ||
              !existing->IsFunction())
          {
            (void)context
                ->Global()
                ->DefineOwnProperty(context, ToV8Str(isolate, iface), ct_fn,
                                    static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
                .FromMaybe(false);
          }
        }
        // expose HTMLElement once
        if (!dom->html_element_ctor.IsEmpty())
        {
          v8::Local<v8::Value> existing;
          if (!context->Global()
                    ->Get(context, ToV8Str(isolate, "HTMLElement"))
                    .ToLocal(&existing) ||
              !existing->IsFunction())
          {
            (void)context
                ->Global()
                ->DefineOwnProperty(
                    context, ToV8Str(isolate, "HTMLElement"),
                    dom->html_element_ctor.Get(isolate),
                    static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
                .FromMaybe(false);
          }
        }
      }
      ctor_it = dom->tag_ctors.find(iface);
      if (ctor_it != dom->tag_ctors.end() && !bare_element)
      {
        v8::Local<v8::FunctionTemplate> ct = ctor_it->second.Get(isolate);
        v8::Local<v8::Function> fn =
            ct->GetFunction(context).ToLocalChecked();
        v8::Local<v8::Value> proto;
        if (fn->Get(context, ToV8Str(isolate, "prototype")).ToLocal(&proto) &&
            proto->IsObject())
        {
          (void)instance
              ->SetPrototype(context, proto.As<v8::Object>())
              .FromMaybe(false);
        }
      }
    }
    }  // G4.262: element-only block

    // SVG instance surface
    if (is_element_node || node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
    {
    {
      auto ns_it = dom->svg_namespaces.find(node);
      const bool is_svg_node =
          (ns_it != dom->svg_namespaces.end()) || node->ns == LXB_NS_SVG;
      if (is_svg_node)
      {
        std::string namespace_uri = "http://www.w3.org/2000/svg";
        if (ns_it != dom->svg_namespaces.end())
          namespace_uri = ns_it->second;
        (void)instance
            ->Set(context, ToV8Str(isolate, "namespaceURI"),
                  ToV8Str(isolate, namespace_uri))
            .FromMaybe(false);
        {
          v8::Local<v8::String> key = ToV8Str(isolate, "ownerSVGElement");
          v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                lxb_dom_node_t* n = NodeOf(info.This());
                for (lxb_dom_node_t* cur = n != nullptr ? n->parent : nullptr;
                     cur != nullptr; cur = cur->parent)
                {
                  if (cur->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
                  const lxb_char_t* cname;
                  size_t clen = 0;
                  cname = lxb_dom_node_name(cur, &clen);
                  std::string tag((const char*)cname, clen);
                  for (auto& ch : tag)
                    ch = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(ch)));
                  if (tag == "svg")
                  {
                    info.GetReturnValue().Set(
                        WrapNode(iso, ctx, cur));
                    return;
                  }
                }
                info.GetReturnValue().SetNull();
              });
          fn->SetClassName(ToV8Str(isolate, "get ownerSVGElement"));
          instance->SetAccessorProperty(
              key, fn->GetFunction(context).ToLocalChecked(),
              v8::Local<v8::Function>());
        }
        {
          v8::Local<v8::String> key = ToV8Str(isolate, "getBBox");
          v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                lxb_dom_node_t* n = NodeOf(info.This());
                auto attr_number = [&](const char* name) -> double {
                  if (n == nullptr ||
                      n->type != LXB_DOM_NODE_TYPE_ELEMENT)
                    return 0.0;
                  const lxb_char_t* value = lxb_dom_element_get_attribute(
                      lxb_dom_interface_element(n),
                      (const lxb_char_t*)name, strlen(name), nullptr);
                  if (value == nullptr) return 0.0;
                  return std::atof((const char*)value);
                };
                double width = attr_number("width");
                double height = attr_number("height");
                double x = attr_number("x");
                if (x == 0.0) x = attr_number("cx");
                double y = attr_number("y");
                if (y == 0.0) y = attr_number("cy");
                v8::Local<v8::Object> bbox = v8::Object::New(iso);
                (void)bbox->Set(ctx, ToV8Str(iso, "x"),
                                v8::Number::New(iso, x))
                    .FromMaybe(false);
                (void)bbox->Set(ctx, ToV8Str(iso, "y"),
                                v8::Number::New(iso, y))
                    .FromMaybe(false);
                (void)bbox->Set(ctx, ToV8Str(iso, "width"),
                                v8::Number::New(iso, width))
                    .FromMaybe(false);
                (void)bbox->Set(ctx, ToV8Str(iso, "height"),
                                v8::Number::New(iso, height))
                    .FromMaybe(false);
                info.GetReturnValue().Set(bbox);
              });
          fn->SetClassName(ToV8Str(isolate, "getBBox"));
          (void)instance
              ->Set(context, key, fn->GetFunction(context).ToLocalChecked())
              .FromMaybe(false);
        }
        if (interface_name == "SVGSVGElement")
        {
          v8::Local<v8::String> key = ToV8Str(isolate, "createSVGPoint");
          v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                v8::Isolate* iso = info.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                DomState* dom_state = GetDomState(ctx);
                if (dom_state == nullptr) return;
                if (dom_state->svg_point_factory.IsEmpty())
                {
                  v8::Local<v8::String> src = ToV8Str(
                      iso,
                      "(function (DOMPoint) { return function () {"
                      " let x = 0, y = 0;"
                      " return {"
                      " get x() { return x; }, set x(v) { x = Number(v); },"
                      " get y() { return y; }, set y(v) { y = Number(v); },"
                      " get z() { return 0; }, get w() { return 1; },"
                      " matrixTransform(m) {"
                      " return new DOMPoint(m.a * x + m.c * y + m.e,"
                      " m.b * x + m.d * y + m.f, 0, 1); } }; }; })");
                  v8::ScriptOrigin origin(ToV8Str(iso, "<anonymous>"));
                  v8::Local<v8::Script> script =
                      v8::Script::Compile(ctx, src, &origin).ToLocalChecked();
                  v8::Local<v8::Value> maker =
                      script->Run(ctx).ToLocalChecked();
                  v8::Local<v8::Value> dom_point;
                  if (!ctx->Global()
                           ->Get(ctx, ToV8Str(iso, "DOMPoint"))
                           .ToLocal(&dom_point) ||
                      !dom_point->IsFunction())
                    return;
                  v8::Local<v8::Value> fargs[] = {dom_point};
                  v8::Local<v8::Value> factory =
                      maker.As<v8::Function>()
                          ->Call(ctx, ctx->Global(), 1, fargs)
                          .ToLocalChecked();
                  dom_state->svg_point_factory.Reset(
                      iso, factory.As<v8::Function>());
                }
                v8::Local<v8::Function> factory =
                    dom_state->svg_point_factory.Get(iso);
                v8::Local<v8::Value> point =
                    factory->Call(ctx, ctx->Global(), 0, nullptr)
                        .ToLocalChecked();
                info.GetReturnValue().Set(point);
              });
          fn->SetClassName(ToV8Str(isolate, "createSVGPoint"));
          (void)instance
              ->Set(context, key, fn->GetFunction(context).ToLocalChecked())
              .FromMaybe(false);
        }
      }
    }
    }  // G4.262: element-only block

    // dataset: data-* attribute mapping via named property handlers
    // (camelCase keys; Object.keys goes through the enumerator)
    if (is_element_node || node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
    {
    {
      v8::Local<v8::ObjectTemplate> ds_tmpl = v8::ObjectTemplate::New(isolate);
      ds_tmpl->SetInternalFieldCount(0);
      v8::NamedPropertyHandlerConfiguration ds_handler(
          DatasetGetter, DatasetSetter, DatasetQuery, DatasetDeleter,
          DatasetEnumerator, instance);
      ds_tmpl->SetHandler(ds_handler);
      v8::Local<v8::Object> ds =
          ds_tmpl->NewInstance(context).ToLocalChecked();
      (void)instance
          ->SetPrivate(
              context,
              v8::Private::ForApi(isolate, ToV8Str(isolate,
                                                   "lv8.el.dataset")),
              ds)
          .FromMaybe(false);
    }
    }  // G4.262: element-only block
    // attributes: shared NamedNodeMap template (instanceof + global ctor)
    {
      v8::Local<v8::Object> nnm =
          dom->nnm_tpl.Get(isolate)->InstanceTemplate()->NewInstance(context)
              .ToLocalChecked();
      (void)nnm
          ->SetPrivate(context,
                       v8::Private::ForApi(
                           isolate, ToV8Str(isolate, "lv8.nnm.el")),
                       instance)
          .FromMaybe(false);
      (void)instance
          ->SetPrivate(
              context,
              v8::Private::ForApi(isolate, ToV8Str(isolate,
                                                   "lv8.el.attributes")),
              nnm)
          .FromMaybe(false);
    }

    // cache BEFORE running user code
    v8::Global<v8::Object> holder(isolate, instance);
    dom->wraps[node] = std::move(holder);

    // user-defined custom element: run the constructor and swap the
    // prototype to the user class prototype
    {
      std::string tag;
      if (node->type == LXB_DOM_NODE_TYPE_ELEMENT)
      {
        const lxb_char_t* name;
        size_t len = 0;
        name = lxb_dom_node_name(node, &len);
        tag.assign((const char*)name, len);
        for (auto& c : tag)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      auto def = dom->custom_definitions.find(tag);
      if (def != dom->custom_definitions.end())
        CustomConstruct(isolate, context, instance, def->second.Get(isolate));
    }
    // Web Animations: the reference carries NO animate/getAnimations on
    // element instances or prototypes (only document.getAnimations) - the
    // former own-property attach was an reference divergence measured by the
    // d8y2 read-order A/B (gOPN(element) must stay empty).

    return instance;
  }
  // ---

  // ------------------------------------------ Window named properties
  //
  // "Named access on the Window object": connected elements become
  // readable on window.  This is the channel the challenge SDK's form
  // probe rides on (form.id = "__Zm9ybS5pZAo__" then dw[74][formId],
  // where dw[74] IS window - reference answers the element, an unimplemented
  // window answers undefined and the VM writes detection marker 74).
  //
  // reference-measured semantics (work/reference-traces/window-named-probe.json):
  //   - any connected element's id qualifies; the name attribute
  //     qualifies only for form/img/embed/object (NOT a/area/applet/
  //     frameset/map/input the way the HTML spec would suggest)
  //   - real own window properties always win, so the interceptor is
  //     registered kNonMasking: it is consulted only after the whole
  //     lookup (own properties + prototype chain) has failed
  //   - Object.getOwnPropertyNames / Object.keys / for-in do NOT list
  //     named properties and gOPD answers undefined (no enumerator or
  //     descriptor callback, matching reference)
  //   - duplicate ids answer an HTMLCollection
  //   - assignment installs an own property that shadows the named one;
  //     delete answers true and the named read keeps working (no
  //     deleter callback: V8 answers true for interceptor names)
  //   - barren iframe realms answer against the host page document,
  //     matching reference's contentWindow.frameid === <iframe>

  bool WindowNamedWhitelistTag(lxb_dom_node_t* node)
  {
    // reference registers the name attribute only for form/img/embed/object
    size_t len = 0;
    const lxb_char_t* name = lxb_dom_element_local_name(
        lxb_dom_interface_element(node), &len);
    if (name == nullptr) return false;
    if (len == 4)
      return std::memcmp(name, "form", 4) == 0;
    if (len == 3)
      return std::memcmp(name, "img", 3) == 0;
    if (len == 5)
      return std::memcmp(name, "embed", 5) == 0;
    if (len == 6)
      return std::memcmp(name, "object", 6) == 0;
    return false;
  }

  void CollectWindowNamed(lxb_dom_node_t* node, const std::string& key,
                          std::vector<lxb_dom_node_t*>& matches)
  {
    for (lxb_dom_node_t* child = node->first_child; child != nullptr;
         child = child->next)
    {
      if (child->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
      lxb_dom_element_t* el = lxb_dom_interface_element(child);
      size_t len = 0;
      const lxb_char_t* value =
          lxb_dom_element_get_attribute(el, (const lxb_char_t*)"id", 2, &len);
      bool hit = value != nullptr &&
                 key == std::string((const char*)value, len);
      if (!hit && WindowNamedWhitelistTag(child))
      {
        value = lxb_dom_element_get_attribute(el, (const lxb_char_t*)"name",
                                              4, &len);
        hit = value != nullptr && len > 0 &&
              key == std::string((const char*)value, len);
      }
      if (hit) matches.push_back(child);
      CollectWindowNamed(child, key, matches);
    }
  }

  // Barren iframe realms carry no ContextState of their own; reference answers
  // named reads there against the host page document
  // (cw.frameid === <iframe>, work/reference-traces/window-named-probe.json).
  // EnsureIframeRealm parks the host DomState in a private symbol.
  DomState* WindowNamedDom(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    if (DomState* dom = GetDomState(context))
      return dom;
    v8::Local<v8::Private> key = v8::Private::ForApi(
        isolate, ToV8Str(isolate, "lv8.realm.named-dom"));
    v8::Local<v8::Value> host;
    if (!context->Global()->GetPrivate(context, key).ToLocal(&host) ||
        !host->IsExternal())
      return nullptr;
    return static_cast<DomState*>(host.As<v8::External>()->Value());
  }

  v8::Intercepted WindowNamedGetter(
      v8::Local<v8::Name> name,
      const v8::PropertyCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    if (!name->IsString())
      return v8::Intercepted::kNo;
    std::string key = ToUtf8(isolate, name);
    if (key.empty())
      return v8::Intercepted::kNo;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    DomState* dom = WindowNamedDom(isolate, context);
    if (dom == nullptr || dom->doc == nullptr)
      return v8::Intercepted::kNo;
    std::vector<lxb_dom_node_t*> matches;
    CollectWindowNamed(lxb_dom_interface_node(&dom->doc->dom_document), key,
                       matches);
    if (matches.empty())
      return v8::Intercepted::kNo;
    EnsureCollectionTemplates(isolate, context, dom);
    if (matches.size() == 1)
    {
      v8::Local<v8::Object> wrapper = WrapNode(isolate, context, matches[0]);
      if (wrapper.IsEmpty())
        return v8::Intercepted::kNo;
      info.GetReturnValue().Set(wrapper);
      return v8::Intercepted::kYes;
    }
    std::vector<v8::Local<v8::Value>> items;
    items.reserve(matches.size());
    for (lxb_dom_node_t* node : matches)
    {
      v8::Local<v8::Object> wrapper = WrapNode(isolate, context, node);
      if (wrapper.IsEmpty())
        return v8::Intercepted::kNo;
      items.push_back(wrapper);
    }
    info.GetReturnValue().Set(
        MakeCollectionInstance(isolate, context, dom->hc_tpl, items));
    return v8::Intercepted::kYes;
  }

  // Assignment over a named property: reference answers an enumerable own data
  // property (window.shadowme = 42 -> gOPD {value:42, enumerable:true}).
  // Falling through with kNotIntercepted lands in SetSuperProperty's
  // interceptor branch, whose CreateDataProperty detour answers a
  // non-enumerable property - so define it here instead.  Names with no
  // backing element fall through untouched (plain global store semantics,
  // TDZ checks included).
  v8::Intercepted WindowNamedSetter(v8::Local<v8::Name> name,
                                    v8::Local<v8::Value> value,
                                    const v8::PropertyCallbackInfo<void>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    if (!name->IsString())
      return v8::Intercepted::kNo;
    std::string key = ToUtf8(isolate, name);
    if (key.empty())
      return v8::Intercepted::kNo;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    DomState* dom = WindowNamedDom(isolate, context);
    if (dom == nullptr || dom->doc == nullptr)
      return v8::Intercepted::kNo;
    std::vector<lxb_dom_node_t*> matches;
    CollectWindowNamed(lxb_dom_interface_node(&dom->doc->dom_document), key,
                       matches);
    if (matches.empty())
      return v8::Intercepted::kNo;
    // CreateDataProperty re-enters this setter (its define runs with kSet
    // semantics, which calls SetPropertyWithInterceptor first).  The
    // nested call must decline: DefineOwnPropertyIgnoreAttributes then
    // finishes via OWN_SKIP_INTERCEPTOR and lands the property with NONE
    // attributes (enumerable/writable/configurable, matching reference).
    static thread_local bool defining = false;
    if (defining)
      return v8::Intercepted::kNo;
    defining = true;
    const bool defined =
        info.Holder()->CreateDataProperty(context, name, value).FromMaybe(false);
    defining = false;
    if (defined)
    {
      info.GetReturnValue().Set(value);
      return v8::Intercepted::kYes;
    }
    return v8::Intercepted::kNo;
  }

  // ------------------------------------------------------- query methods

  // Common implementation: query against |root| and return a NodeList
  // instance (browser collection shape: length/item/forEach/iterator,
  // instanceof NodeList against the shared per-context constructor).
  // An invalid selector throws SyntaxError and yields an empty Local.
  v8::Local<v8::Value> QueryToList(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> into,
                                   lxb_dom_node_t* root,
                                   const std::string& selector)
  {
    DomState* dom = GetDomState(context);
    if (dom == nullptr)
      return v8::Undefined(isolate);
    std::vector<lxb_dom_node_t*> found;
    if (!SelectNodes(dom, root, selector, found))
    {
      isolate->ThrowException(ToV8Str(
          isolate,
          "SyntaxError: \"" + selector + "\" is not a valid selector"));
      return v8::Local<v8::Value>();
    }
    EnsureCollectionTemplates(isolate, context, dom);
    std::vector<v8::Local<v8::Value>> items;
    for (lxb_dom_node_t* node : found)
      items.push_back(WrapNode(isolate, context, node));
    return MakeCollectionInstance(isolate, context, dom->nl_tpl, items);
  }

  // Wrap the elements of a lexbor collection as an HTMLCollection instance.
  v8::Local<v8::Value> CollectionFromLxb(
      v8::Isolate* isolate, v8::Local<v8::Context> context, DomState* dom,
      lxb_dom_collection_t* collection)
  {
    EnsureCollectionTemplates(isolate, context, dom);
    std::vector<v8::Local<v8::Value>> items;
    for (size_t i = 0; i < lxb_dom_collection_length(collection); i++)
    {
      lxb_dom_element_t* el = lxb_dom_collection_element(collection, i);
      items.push_back(
          WrapNode(isolate, context, lxb_dom_interface_node(el)));
    }
    lxb_dom_collection_destroy(collection, true);
    return MakeCollectionInstance(isolate, context, dom->hc_tpl, items);
  }

  lxb_dom_node_t* RootNodeOf(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> self)
  {
    // document objects carry their own doc node (DOMParser docs included);
    // elements and fragments query their own subtree (DOM semantics)
    lxb_dom_node_t* node = NodeOf(self);
    if (node != nullptr)
    {
      if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT ||
          node->type == LXB_DOM_NODE_TYPE_ELEMENT ||
          node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
        return node;
    }
    DomState* dom = GetDomState(context);
    if (dom == nullptr || dom->doc == nullptr)
      return nullptr;
    return lxb_dom_interface_node(&dom->doc->dom_document);
  }

  void GetElementById(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().SetNull();
      return;
    }
    lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
    if (doc_node == nullptr)
    {
      info.GetReturnValue().SetNull();
      return;
    }
    std::string id = ToUtf8(isolate, info[0]);
    lxb_dom_node_t* root =
        lxb_dom_document_root(lxb_dom_interface_document(doc_node));
    if (root == nullptr)
    {
      info.GetReturnValue().SetNull();
      return;
    }
    // depth-first search for an element with the id attribute
    lxb_dom_node_t* found = nullptr;
    std::vector<lxb_dom_node_t*> stack{root};
    while (!stack.empty() && found == nullptr)
    {
      lxb_dom_node_t* node = stack.back();
      stack.pop_back();
      if (node->type == LXB_DOM_NODE_TYPE_ELEMENT)
      {
        size_t len = 0;
        const lxb_char_t* value = lxb_dom_element_get_attribute(
            lxb_dom_interface_element(node), (const lxb_char_t*)"id", 2,
            &len);
        if (value != nullptr && id == std::string((const char*)value, len))
        {
          found = node;
          break;
        }
      }
      // push children in reverse so traversal order stays document order
      for (lxb_dom_node_t* child = node->last_child; child != nullptr;
           child = child->prev)
        stack.push_back(child);
    }
    if (found == nullptr)
    {
      info.GetReturnValue().SetNull();
      return;
    }
    info.GetReturnValue().Set(WrapNode(isolate, context, found));
  }

  void GetElementsByTagName(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
    if (doc_node == nullptr)
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    std::string tag = ToUtf8(isolate, info[0]);
    for (auto& c : tag)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    lxb_dom_document_t* ddoc = lxb_dom_interface_document(doc_node);
    lxb_dom_node_t* root = lxb_dom_document_root(ddoc);
    lxb_dom_collection_t* collection = lxb_dom_collection_make(ddoc, 32);
    if (collection == nullptr)
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    if (root != nullptr)
    {
      (void)lxb_dom_elements_by_tag_name(
          lxb_dom_interface_element(root), collection,
          (const lxb_char_t*)tag.c_str(), tag.size());
    }
    info.GetReturnValue().Set(
        CollectionFromLxb(isolate, context, GetDomState(context), collection));
  }

  void GetElementsByClassName(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    lxb_dom_node_t* doc_node = DocNodeOf(info.This(), context);
    if (doc_node == nullptr)
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    std::string cls = ToUtf8(isolate, info[0]);
    lxb_dom_document_t* ddoc = lxb_dom_interface_document(doc_node);
    lxb_dom_node_t* root = lxb_dom_document_root(ddoc);
    lxb_dom_collection_t* collection = lxb_dom_collection_make(ddoc, 32);
    if (collection == nullptr)
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    if (root != nullptr)
    {
      (void)lxb_dom_elements_by_class_name(
          lxb_dom_interface_element(root), collection,
          (const lxb_char_t*)cls.c_str(), cls.size());
    }
    info.GetReturnValue().Set(
        CollectionFromLxb(isolate, context, GetDomState(context), collection));
  }

  // Element-scoped variants root the walk at |this| and return
  // HTMLCollection instances like their document-level siblings.
  void ElementGetElementsByTagName(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    lxb_dom_node_t* root = NodeOf(info.This());
    DomState* dom = GetDomState(context);
    if (root == nullptr || dom == nullptr || dom->doc == nullptr ||
        info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    std::string tag = ToUtf8(isolate, info[0]);
    for (auto& c : tag)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    lxb_dom_collection_t* collection =
        lxb_dom_collection_make(&dom->doc->dom_document, 32);
    if (collection == nullptr)
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    (void)lxb_dom_elements_by_tag_name(
        lxb_dom_interface_element(root), collection,
        (const lxb_char_t*)tag.c_str(), tag.size());
    info.GetReturnValue().Set(
        CollectionFromLxb(isolate, context, dom, collection));
  }

  void ElementGetElementsByClassName(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    lxb_dom_node_t* root = NodeOf(info.This());
    DomState* dom = GetDomState(context);
    if (root == nullptr || dom == nullptr || dom->doc == nullptr ||
        info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    std::string cls = ToUtf8(isolate, info[0]);
    lxb_dom_collection_t* collection =
        lxb_dom_collection_make(&dom->doc->dom_document, 32);
    if (collection == nullptr)
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    (void)lxb_dom_elements_by_class_name(
        lxb_dom_interface_element(root), collection,
        (const lxb_char_t*)cls.c_str(), cls.size());
    info.GetReturnValue().Set(
        CollectionFromLxb(isolate, context, dom, collection));
  }

  void QuerySelector(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    MonitorCall(isolate, context, info.This(), "document.querySelector");
    if (info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().SetNull();
      return;
    }
    lxb_dom_node_t* root = RootNodeOf(isolate, context, info.This());
    if (root == nullptr)
    {
      info.GetReturnValue().SetNull();
      return;
    }
    std::vector<lxb_dom_node_t*> found;
    if (!SelectNodes(GetDomState(context), root, ToUtf8(isolate, info[0]),
                     found))
    {
      isolate->ThrowException(ToV8Str(
          isolate, "SyntaxError: \"" +
                       ToUtf8(isolate, info[0]) +
                       "\" is not a valid selector"));
      return;
    }
    if (found.empty())
    {
      info.GetReturnValue().SetNull();
      return;
    }
    info.GetReturnValue().Set(WrapNode(isolate, context, found[0]));
  }

  void QuerySelectorAll(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    lxb_dom_node_t* root = RootNodeOf(isolate, context, info.This());
    if (root == nullptr)
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    v8::Local<v8::Value> list = QueryToList(
        isolate, context, info.This(), root, ToUtf8(isolate, info[0]));
    if (list.IsEmpty())
      return; // SyntaxError pending
    info.GetReturnValue().Set(list);
  }

  // element-level querySelector/querySelectorAll callbacks reuse the same
  // helpers but root at the element; registered in WrapNode via prototype.
  void ElementQuerySelector(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    lxb_dom_node_t* root = NodeOf(info.This());
    if (root == nullptr || info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().SetNull();
      return;
    }
    std::vector<lxb_dom_node_t*> found;
    if (!SelectNodes(GetDomState(context), root, ToUtf8(isolate, info[0]),
                     found))
    {
      isolate->ThrowException(ToV8Str(
          isolate, "SyntaxError: \"" +
                       ToUtf8(isolate, info[0]) +
                       "\" is not a valid selector"));
      return;
    }
    if (found.empty())
    {
      info.GetReturnValue().SetNull();
      return;
    }
    info.GetReturnValue().Set(WrapNode(isolate, context, found[0]));
  }

  void ElementQuerySelectorAll(
      const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    lxb_dom_node_t* root = NodeOf(info.This());
    if (root == nullptr || info.Length() < 1 || !info[0]->IsString())
    {
      info.GetReturnValue().Set(v8::Array::New(isolate));
      return;
    }
    v8::Local<v8::Value> list = QueryToList(
        isolate, context, info.This(), root, ToUtf8(isolate, info[0]));
    if (list.IsEmpty())
      return; // SyntaxError pending
    info.GetReturnValue().Set(list);
  }

  // -------------------------------------------------------------- install

  void InstallDocument(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       const std::string& html, const std::string& base_url)
  {
    (void)html;
    (void)base_url;
    // "document" is installed by LoadDocument; here we only expose the
    // object's surface. Nothing to do when the document already exists.
  }

  // B2: browsers expose every DOM interface as a window global from day
  // one.  lv8 installed an interface constructor lazily when the first
  // element of that interface got wrapped, so `window.HTMLInputElement`
  // stayed undefined until an input existed — and the WAF VM's constructor
  // probes (typeof + .call) read exactly those globals before creating the
  // matching elements.  Pre-install the family at document load.

  void EnsureIllegalCtor(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    info.GetIsolate()->ThrowException(
        ToV8Str(info.GetIsolate(), "Illegal constructor"));
  }

  // Install the invariant DOM-IDL face before GetFunction() materializes the
  // constructor.  V8 forbids adding instance handlers afterwards; doing this
  // here makes one interface template serve every wrapper and puts members on
  // the observable prototype, as the reference wheel does.
  void ConfigureElementInterfaceTemplate(v8::Isolate* isolate,
                                         const std::string& iface,
                                         v8::Local<v8::FunctionTemplate> tpl)
  {
    v8::Local<v8::ObjectTemplate> proto = tpl->PrototypeTemplate();
    // The common DOM/HTMLElement surface belongs on the one shared
    // HTMLElement prototype.  Concrete interfaces inherit that template;
    // installing it again here turns every member into an observable own
    // property of (for example) HTMLCanvasElement.prototype.
    const bool is_shared_html_surface = iface == "HTMLElement";
    auto ro = [&](const char* name, v8::FunctionCallback cb) {
      v8::Local<v8::FunctionTemplate> get = v8::FunctionTemplate::New(isolate, cb);
      get->SetClassName(ToV8Str(isolate, std::string("get ") + name));
      proto->SetAccessorProperty(ToV8Str(isolate, name), get,
                                 v8::Local<v8::FunctionTemplate>());
    };
    auto rw = [&](const char* name, v8::FunctionCallback get_cb,
                  v8::FunctionCallback set_cb) {
      v8::Local<v8::FunctionTemplate> get = v8::FunctionTemplate::New(isolate, get_cb);
      v8::Local<v8::FunctionTemplate> set = v8::FunctionTemplate::New(isolate, set_cb);
      get->SetClassName(ToV8Str(isolate, std::string("get ") + name));
      set->SetClassName(ToV8Str(isolate, std::string("set ") + name));
      proto->SetAccessorProperty(ToV8Str(isolate, name), get, set);
    };
    auto named = [&](const char* name, v8::FunctionCallback cb, const char* attr) {
      v8::Local<v8::FunctionTemplate> get = v8::FunctionTemplate::New(
          isolate, cb, ToV8Str(isolate, attr));
      get->SetClassName(ToV8Str(isolate, std::string("get ") + name));
      proto->SetAccessorProperty(ToV8Str(isolate, name), get,
                                 v8::Local<v8::FunctionTemplate>());
    };
    auto named_rw = [&](const char* name, v8::FunctionCallback get_cb,
                        v8::FunctionCallback set_cb, const char* attr) {
      v8::Local<v8::Value> data = ToV8Str(isolate, attr);
      v8::Local<v8::FunctionTemplate> get =
          v8::FunctionTemplate::New(isolate, get_cb, data);
      v8::Local<v8::FunctionTemplate> set =
          v8::FunctionTemplate::New(isolate, set_cb, data);
      get->SetClassName(ToV8Str(isolate, std::string("get ") + name));
      set->SetClassName(ToV8Str(isolate, std::string("set ") + name));
      proto->SetAccessorProperty(ToV8Str(isolate, name), get, set);
    };
    if (is_shared_html_surface) {
      rw("checked", GetCheckedAttr, SetCheckedAttr);
      rw("id", GetId, SetId);
      rw("className", GetClassName, SetClassName);
      rw("textContent", GetTextContent, SetTextContent);
      rw("outerHTML", GetOuterHTML, SetOuterHTML);
      for (const auto& pair : std::vector<std::pair<const char*, v8::FunctionCallback>>{
               {"tagName", GetTagName},
               {"parentElement", GetParentElement}, {"firstElementChild", GetFirstElementChild},
               {"lastElementChild", GetLastElementChild}, {"lastChild", GetLastChild},
               {"nextSibling", GetNextSibling}, {"previousSibling", GetPreviousSibling},
               {"parentNode", GetParentNode}, {"firstChild", GetFirstChild},
               {"shadowRoot", GetShadowRoot}, {"childNodes", GetChildNodes},
               {"children", GetChildren}, {"isConnected", ElementIsConnected},
               {"nodeType", GetNodeType}, {"nodeName", GetNodeName}})
        ro(pair.first, pair.second);
      rw("innerHTML", GetInnerHTML, SetInnerHTML);
      ro("content", GetTemplateContent);
    }
    if (iface == "HTMLButtonElement" || iface == "HTMLInputElement" ||
        iface == "HTMLSelectElement" || iface == "HTMLTextAreaElement" ||
        iface == "HTMLSlotElement" ||
        // form.name reflects the content attribute (window named access
        // rides on it: the challenge renames a live form via form.name)
        iface == "HTMLFormElement")
      rw("name", GetNameAttr, SetNameAttr);
    if (iface == "HTMLImageElement") {
      named_rw("src", ResolvedAttrGetter, SetSrcAttr, "src");
      named_rw("width", NumberAttrGetter, SetNamedAttr, "width");
      named_rw("height", NumberAttrGetter, SetNamedAttr, "height");
    } else if (iface == "HTMLAnchorElement") {
      named("href", ResolvedAttrGetter, "href"); named("target", PlainAttrGetter, "target");
    } else if (iface == "HTMLScriptElement") {
      named_rw("src", ResolvedAttrGetter, SetSrcAttr, "src");
      rw("text", GetTextContent, SetTextContent);
    } else if (iface == "HTMLLinkElement") {
      named("href", ResolvedAttrGetter, "href");
    } else if (iface == "HTMLIFrameElement") {
      ro("contentWindow", GetIframeContentWindow); ro("contentDocument", GetIframeContentDocument);
      named_rw("src", ResolvedAttrGetter, SetSrcAttr, "src");
      named_rw("allow", PlainAttrGetter, SetNamedAttr, "allow");
      named_rw("allowFullscreen", PlainAttrGetter, SetNamedAttr, "allowfullscreen");
      named_rw("height", PlainAttrGetter, SetNamedAttr, "height");
      named_rw("loading", PlainAttrGetter, SetNamedAttr, "loading");
      named_rw("name", PlainAttrGetter, SetNamedAttr, "name");
      named_rw("referrerPolicy", PlainAttrGetter, SetNamedAttr, "referrerpolicy");
      named("sandbox", PlainAttrGetter, "sandbox");
      named_rw("srcdoc", PlainAttrGetter, SetNamedAttr, "srcdoc");
      named_rw("width", PlainAttrGetter, SetNamedAttr, "width");
      v8::Local<v8::FunctionTemplate> get_svg_document =
          v8::FunctionTemplate::New(isolate, IframeGetSVGDocument);
      get_svg_document->SetClassName(ToV8Str(isolate, "getSVGDocument"));
      proto->Set(ToV8Str(isolate, "getSVGDocument"), get_svg_document);
    } else if (iface == "HTMLCanvasElement") {
      named_rw("width", CanvasDimensionGetter, SetNamedAttr, "width");
      named_rw("height", CanvasDimensionGetter, SetNamedAttr, "height");
      for (const ElementMethod& method : kElementMethods) {
        if (std::string(method.name) != "getContext" &&
            std::string(method.name) != "toDataURL" &&
            std::string(method.name) != "toBlob" &&
            std::string(method.name) != "transferControlToOffscreen")
          continue;
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, method.cb);
        fn->SetClassName(ToV8Str(isolate, method.name));
        fn->SetLength(method.len);
        proto->Set(ToV8Str(isolate, method.name), fn);
      }
      v8::Local<v8::FunctionTemplate> capture =
          v8::FunctionTemplate::New(isolate, CanvasCaptureStream);
      capture->SetClassName(ToV8Str(isolate, "captureStream"));
      proto->Set(ToV8Str(isolate, "captureStream"), capture);
    } else if (iface == "HTMLMetaElement") {
      named("content", PlainAttrGetter, "content"); named("name", PlainAttrGetter, "name");
      named("httpEquiv", PlainAttrGetter, "http-equiv");
    }
    if (iface == "HTMLFormElement")
    {
      tpl->InstanceTemplate()->SetHandler(
          v8::NamedPropertyHandlerConfiguration(FormNamedGetter));
      tpl->InstanceTemplate()->SetHandler(
          v8::IndexedPropertyHandlerConfiguration(FormIndexedGetter));
      ro("length", FormLength);
      ro("elements", FormElementsGetter);
    }
    if (is_shared_html_surface) {
      for (const ElementMethod& method : kElementMethods) {
        v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(isolate, method.cb);
        fn->SetClassName(ToV8Str(isolate, method.name));
        fn->SetLength(method.len);
        proto->Set(ToV8Str(isolate, method.name), fn);
      }
    }
  }

  // M11: reference quirk (work/reference-traces/_m11_values.json) - progress instances
  // chain STRAIGHT to Element.prototype there: tag reads [object Element],
  // the instance is zero-own, and `new HTMLProgressElement() instanceof
  // HTMLProgressElement` is false (its own 5-member prototype is orphaned).
  // The generic inherited-HTMLElement wrapper answered
  // [object HTMLProgressElement] - not the reference shape.
  void ProgressElementCtor(const v8::FunctionCallbackInfo<v8::Value>& info)
  {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall())
    {
      isolate->ThrowException(ToV8Str(
          isolate,
          "TypeError: HTMLProgressElement constructor requires new"));
      return;
    }
    DomState* st = GetDomState(context);
    if (st == nullptr)
    {
      isolate->ThrowException(ToV8Str(isolate,
                                      "Error: no document is loaded"));
      return;
    }
    if (st->doc == nullptr)
    {
      st->doc = lxb_html_document_create();
      if (st->doc == nullptr)
      {
        isolate->ThrowException(ToV8Str(isolate,
                                        "Error: document allocation failed"));
        return;
      }
      (void)lxb_html_document_parse(st->doc, (const lxb_char_t*)"<html></html>",
                                    13);
      st->ready_state = "complete";
    }
    lxb_dom_element_t* el = lxb_dom_document_create_element(
        lxb_dom_interface_document(&st->doc->dom_document),
        (const lxb_char_t*)"progress", 8, nullptr);
    if (el == nullptr)
    {
      isolate->ThrowException(ToV8Str(isolate,
                                      "Error: element allocation failed"));
      return;
    }
    v8::Local<v8::Object> inst = v8::Object::New(isolate);
    (void)inst
        ->SetPrivate(context,
                     v8::Private::ForApi(
                         isolate, ToV8Str(isolate, "lv8.dom.node")),
                     v8::External::New(isolate,
                                       lxb_dom_interface_node(el)))
        .FromMaybe(false);
    // NOTE: element_proto is the shared HTMLElement.prototype; the reference
    // progress chain hangs off Element.prototype (element_iface_proto)
    if (!st->element_iface_proto.IsEmpty())
      (void)inst
          ->SetPrototype(context, st->element_iface_proto.Get(isolate))
          .FromMaybe(false);
    info.GetReturnValue().Set(inst);
  }

  void EnsureInterfaceConstructorSurface(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context,
                                         DomState* dom)
  {
    static const char* const kElementIfaces[] = {
        "HTMLHtmlElement",   "HTMLHeadElement",   "HTMLBodyElement",
        "HTMLTitleElement",  "HTMLScriptElement", "HTMLDivElement",
        "HTMLSpanElement",   "HTMLParagraphElement", "HTMLAnchorElement",
        "HTMLImageElement",  "HTMLInputElement",  "HTMLButtonElement",
        "HTMLFormElement",   "HTMLIFrameElement", "HTMLStyleElement",
        "HTMLLinkElement",   "HTMLTemplateElement", "HTMLCanvasElement",
        "HTMLDialogElement", "HTMLAudioElement",  "HTMLVideoElement",
        "HTMLMediaElement",  "HTMLSourceElement", "HTMLTrackElement",
        "HTMLEmbedElement",  "HTMLObjectElement", "HTMLParamElement",
        "HTMLDataListElement", "HTMLOptionElement", "HTMLOptGroupElement",
        "HTMLProgressElement", "HTMLMeterElement", "HTMLFieldSetElement",
        "HTMLLegendElement", "HTMLBaseElement",   "HTMLMapElement",
        "HTMLAreaElement",   "HTMLTableCaptionElement", "HTMLTableColElement",
        "HTMLTableSectionElement", "HTMLDataElement", "HTMLTimeElement",
        "HTMLDetailsElement",
        "HTMLHRElement",     "HTMLPreElement",    "HTMLFontElement",
        "HTMLModElement",    "HTMLQuoteElement",  "HTMLDListElement",
        "HTMLMenuElement",   "HTMLPictureElement", "HTMLMetaElement",
        "HTMLHeadingElement", "HTMLUListElement", "HTMLOListElement",
        "HTMLTableElement",  "HTMLTableRowElement", "HTMLTableCellElement",
        "HTMLDataElement",   "HTMLTimeElement",     "HTMLDetailsElement",
        "HTMLBRElement",
        "HTMLLabelElement",  "HTMLSelectElement", "HTMLTextAreaElement",
        "HTMLSlotElement",   "SVGGraphicsElement", "SVGSVGElement",
        "SVGCircleElement",  "SVGPathElement",    "SVGGElement",
        "SVGEllipseElement", "SVGLineElement",    "SVGRectElement",
        "SVGPolygonElement", "SVGPolylineElement", "SVGTextElement",
    };
    static const char* const kStubIfaces[] = {
        "Document",          "Window",            "StorageEvent",
        "PermissionStatus",  "CSSStyleDeclaration", "Range",
        "NodeIterator",      "TreeWalker",        "XPathEvaluator",
        "XPathExpression",   "XPathResult",
    };
    EnsureElementProto(isolate, context, dom);
    auto install = [&](const char* name, bool element) {
      v8::Local<v8::Value> existing;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, name))
              .ToLocal(&existing) &&
          existing->IsFunction())
        return;
      auto known = dom->tag_ctors.find(name);
      if (known != dom->tag_ctors.end())
      {
        v8::Local<v8::Function> fn =
            known->second.Get(isolate)->GetFunction(context).ToLocalChecked();
        // Browser interface globals are enumerable own properties of window
        // (Object.keys(window) lists them); the WAF VM fingerprints that list.
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, name), fn,
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
        return;
      }
      v8::Local<v8::FunctionTemplate> tpl;
      if (std::string(name) == "HTMLProgressElement" && element)
      {
        // M11: see ProgressElementCtor - reference routes progress instances to
        // Element.prototype
        tpl = v8::FunctionTemplate::New(isolate, ProgressElementCtor);
        tpl->SetClassName(ToV8Str(isolate, name));
      }
      else
      {
        tpl = element ? v8::FunctionTemplate::New(isolate)
                      : v8::FunctionTemplate::New(isolate, EnsureIllegalCtor);
        tpl->SetClassName(ToV8Str(isolate, name));
        // V8 FunctionTemplate inheritance: element types inherit from
        // HTMLElement template so prototype walks find the correct chain.
        // This makes each interface prototype have its own toStringTag.
        if (element && !dom->html_element_tpl.IsEmpty())
          tpl->Inherit(dom->html_element_tpl.Get(isolate));
        if (element)
          ConfigureElementInterfaceTemplate(isolate, name, tpl);
      }
      tpl->PrototypeTemplate()->Set(
          v8::Symbol::GetToStringTag(isolate), ToV8Str(isolate, name),
          v8::PropertyAttribute::ReadOnly);
      v8::Local<v8::Function> fn = tpl->GetFunction(context).ToLocalChecked();
      v8::Local<v8::Value> proto;
      if (fn->Get(context, ToV8Str(isolate, "prototype")).ToLocal(&proto) &&
          proto->IsObject())
      {
        // SVG geometry interfaces inherit through SVGGraphicsElement, which
        // itself hangs off HTMLElement.prototype (mirrors WrapNode's lazy
        // SVG chaining so pre-created ctors behave identically).
        v8::Local<v8::Value> proto_target = dom->element_proto.Get(isolate);
        if (std::string(name) != "SVGGraphicsElement" &&
            std::string(name).rfind("SVG", 0) == 0)
        {
          auto gfx = dom->tag_ctors.find("SVGGraphicsElement");
          if (gfx != dom->tag_ctors.end())
          {
            v8::Local<v8::Function> gfx_fn =
                gfx->second.Get(isolate)->GetFunction(context).ToLocalChecked();
            v8::Local<v8::Value> gfx_proto;
            if (gfx_fn->Get(context, ToV8Str(isolate, "prototype"))
                    .ToLocal(&gfx_proto) &&
                gfx_proto->IsObject())
              proto_target = gfx_proto;
          }
        }
        (void)proto.As<v8::Object>()
            ->SetPrototype(context, proto_target)
            .FromMaybe(false);
      }
      dom->tag_ctors[name].Reset(isolate, tpl);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, name), fn,
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    };
    // G4.393h: NodeFilter in the reference is a native-method-form function
    // (NO prototype property, the 16 filter/show constants as read-only own
    // data properties).  The JS global-interface stub carries a prototype +
    // arguments/caller and no constants - replace it unconditionally.
    {
      v8::Local<v8::FunctionTemplate> nf_tpl =
          v8::FunctionTemplate::New(isolate, EnsureIllegalCtor);
      nf_tpl->SetClassName(ToV8Str(isolate, "NodeFilter"));
      nf_tpl->RemovePrototype();
      v8::Local<v8::Function> nf =
          nf_tpl->GetFunction(context).ToLocalChecked();
      static const struct
      {
        const char* name;
        int value;
      } k_node_filter_consts[] = {
          {"FILTER_ACCEPT", 1},           {"FILTER_REJECT", 2},
          {"FILTER_SKIP", 3},             {"SHOW_ALL", -1},
          {"SHOW_ELEMENT", 1},            {"SHOW_ATTRIBUTE", 2},
          {"SHOW_TEXT", 4},               {"SHOW_CDATA_SECTION", 8},
          {"SHOW_ENTITY_REFERENCE", 16},  {"SHOW_ENTITY", 32},
          {"SHOW_PROCESSING_INSTRUCTION", 64}, {"SHOW_COMMENT", 128},
          {"SHOW_DOCUMENT", 256},         {"SHOW_DOCUMENT_TYPE", 512},
          {"SHOW_DOCUMENT_FRAGMENT", 1024}, {"SHOW_NOTATION", 2048},
      };
      for (const auto& c : k_node_filter_consts)
      {
        (void)nf
            ->DefineOwnProperty(
                context, ToV8Str(isolate, c.name),
                v8::Integer::New(isolate, c.value),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly))
            .FromMaybe(false);
      }
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "NodeFilter"), nf,
                              static_cast<v8::PropertyAttribute>(
                                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }
    for (const char* name : kElementIfaces)
      install(name, true);
    for (const char* name : kStubIfaces)
      install(name, false);
  }

  void LoadDocument(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    const std::string& html, const std::string& base_url,
                    const std::vector<std::pair<std::string, std::string>>& page_headers)
  {
    ContextState* state = EnsureState(isolate, context);
    if (state->dom_state == nullptr)
      state->dom_state.reset(new DomState());
    DomState* dom = state->dom_state.get();

    // a fresh parse per load
    if (dom->doc != nullptr)
    {
      // reference parity: a navigation tears down the previous page's realm -
      // its pending timers/RAF and queued network deliveries die with it.
      // lv8 is single-context, so kill them here.  Without this, the
      // challenge loader's late async stages survive into the business
      // page and interfere with the business core's fingerprint sequence
      // (its telemetry append landed in the parked body and the font
      // measurement stage skipped itself - the page-signature signature then
      // diverged and the WAF blocked every signed POST; G4.221).
      state->timers.clear();
      state->raf_queue.clear();
      state->pending_responses.clear();
      state->pending_network_tasks = 0;
      // the previous page's event listeners die with its realm too - the
      // challenge loader rides them back to life on the new page's events
      state->listeners.clear();
      // G4.222: the reference's IDB keeps the database/store SCHEMA
      // across navigations (the business page's open sees the store via
      // objectStoreNames.contains and skips the upgrade) but the RECORDS
      // written by the previous page do not survive - its storage state
      // machine therefore sees a first run, initializes $_YWTU/$_YVTX,
      // and proceeds into the fingerprint stage.  lv8's context-global
      // records survived the reload, the machine took the
      // already-initialized path and skipped the stage (the page-signature
      // divergence).  Clear records, keep schema.
      for (auto& idb_db : state->idb_databases)
        idb_db.records.clear();
      state->pending_idb.clear();
      // detached nodes from the previous page belong to its document and
      // die with it
      dom->detached.clear();
      // Park the previous tree instead of destroying it: timers surviving
      // the reload still hold wrappers whose private node pointers point
      // into it (browsers tear down the whole realm on navigation; lv8 is
      // single-context).  Freeing here made those reads use-after-free —
      // a nondeterministic crash at the second page.load.  Parked docs are
      // released with the DomState.
      dom->parsed_docs.push_back(dom->doc);
      dom->doc = nullptr;
    }
    // NOTE: iframe_realms are intentionally NOT cleared here.  Surviving
    // timers may hold a realm's global object; the realm's node keys stay
    // valid now that parked documents are kept alive.
    dom->wraps.clear();
    dom->ready_state = "loading";
    dom->page_url = base_url;

    // HTML5 parser-blocking semantics: scripts execute as the parser passes
    // them, so live collections inside early scripts reflect only the markup
    // parsed so far.  Parse-then-execute made the WAF VM's script-count
    // probe diverge from a real browser (2 vs 4) and split its state machine.
    struct PendingScript
    {
      std::string code;
      std::string code_url;
      std::string src_attr;
      int line_off = 0;
      int col_off = 0;
    };
    std::vector<PendingScript> pending_scripts;
    // Parser-blocking execution (HTML5 semantics; reference-parity): every
    // <script> runs as the parser passes it, so a head script observes
    // document.body == null.  The page SDK probes exactly that read to
    // pick its init branch - frame-trace evidence (L=11 pc=91-92) shows
    // reference reports null there, while a parse-then-execute schedule sends
    // the SDK down its "already-parsed" path and aborts the registry
    // build.  readyState walks loading during the parse, then
    // interactive / complete with the DOMContentLoaded and load events
    // once the last script has run.
    auto execute_script = [&](const PendingScript& ps,
                              lxb_dom_node_t* script_node)
    {
      v8::TryCatch try_catch(isolate);
      lxb_html_element_t* cur_el = nullptr;
      if (script_node == nullptr)
        cur_el = lxb_html_document_create_element(
            dom->doc, (const lxb_char_t*)"script", 6, nullptr);
      dom->current_script =
          script_node != nullptr ? script_node
                                 : (cur_el != nullptr ? &cur_el->element.node
                                                      : nullptr);
      if (cur_el != nullptr && !ps.code.empty())
      {
        lxb_dom_text_t* text_node = lxb_dom_document_create_text_node(
            lxb_dom_interface_document(dom->doc),
            (const lxb_char_t*)ps.code.c_str(), ps.code.size());
        if (text_node != nullptr)
          (void)lxb_dom_node_insert_child(
              &cur_el->element.node, lxb_dom_interface_node(text_node));
      }
      v8::Local<v8::Value> origin_name;
      if (!ps.code_url.empty())
      {
        origin_name = ToV8Str(isolate, ps.code_url);
      }
      else
      {
        origin_name = ToV8Str(isolate, "inline-script");
      }
      v8::ScriptOrigin origin(origin_name, ps.line_off, ps.col_off);
      v8::Local<v8::String> compile_src = ToV8Str(isolate, ps.code);
      // reference parity: no //# sourceURL on page-inline scripts - the bottom
      // stack frame must render as "inline-script:L:C", not the page URL
      // (surface-audit stack capture, G4.166)
      v8::MaybeLocal<v8::Script> compiled =
          v8::Script::Compile(context, compile_src, &origin);
      {
        // G4.231: the reference dispatches an 'error' ErrorEvent at the
        // Window for every uncaught page-script exception (message shaped
        // "Uncaught <detail>"); the SDK battery consumes those events as a
        // state input, so they must not be swallowed here.  The load still
        // proceeds - later scripts and DOMContentLoaded are unaffected.
        v8::TryCatch run_tc(isolate);
        v8::Local<v8::Value> run_result;
        bool ran = false;
        if (!compiled.IsEmpty())
          ran = compiled.ToLocalChecked()->Run(context).ToLocal(&run_result);
        if (!ran)
        {
          v8::Local<v8::Value> exc;
          if (!run_tc.Exception().IsEmpty())
            exc = run_tc.Exception();
          else if (!try_catch.Exception().IsEmpty())
            exc = try_catch.Exception();
          if (!exc.IsEmpty())
          {
            v8::Local<v8::String> detail;
            if (exc->ToString(context).ToLocal(&detail))
            {
              std::string msg = "Uncaught " + ToUtf8(isolate, detail);
              std::vector<std::pair<std::string, v8::Local<v8::Value>>> props;
              props.emplace_back("type", ToV8Str(isolate, "error"));
              props.emplace_back("message", ToV8Str(isolate, msg));
              props.emplace_back("error", exc);
              props.emplace_back("filename", origin_name);
              props.emplace_back("target", context->Global());
              // document-anchored listeners participate in the window
              // error propagation (the SDK battery registers there)
              DispatchEvent(isolate, context, "ErrorEvent", props, true);
              // Legacy hooks: browsers invoke window.onerror and
              // document.onerror for uncaught script exceptions - the SDK
              // battery registers the document-side handler.
              v8::Local<v8::Value> argv[5] = {
                  ToV8Str(isolate, msg), origin_name,
                  v8::Int32::New(isolate, ps.line_off + 1),
                  v8::Int32::New(isolate, ps.col_off + 1), exc};
              v8::Local<v8::Value> doc_value;
              bool has_doc =
                  context->Global()
                      ->Get(context, ToV8Str(isolate, "document"))
                      .ToLocal(&doc_value) &&
                  doc_value->IsObject();
              for (int hi = 0; hi < 2; hi++)
              {
                v8::Local<v8::Value> hv;
                if (hi == 0)
                  hv = context->Global()
                           ->Get(context, ToV8Str(isolate, "onerror"))
                           .FromMaybe(v8::Local<v8::Value>());
                else if (has_doc)
                  hv = doc_value.As<v8::Object>()
                           ->Get(context, ToV8Str(isolate, "onerror"))
                           .FromMaybe(v8::Local<v8::Value>());
                if (hv.IsEmpty() || !hv->IsFunction())
                  continue;
                v8::TryCatch htc(isolate);
                v8::Local<v8::Function> hf = hv.As<v8::Function>();
                (void)hf->Call(context, context->Global(), 5, argv)
                    .FromMaybe(v8::Local<v8::Value>());
              }
            }
          }
        }
      }
      dom->current_script = nullptr;
      // Real-browser/reference parity: an uncaught page-script error must not
      // abort the load - later scripts and DOMContentLoaded still run.
      try_catch.Reset();
    };
    lxb_html_parser_t *page_parser = lxb_html_parser_create();
    if (page_parser == nullptr ||
        lxb_html_parser_init(page_parser) != LXB_STATUS_OK)
      throw std::runtime_error("lv8: html parser allocation failed");
    dom->doc = lxb_html_parse_chunk_begin(page_parser);
    if (dom->doc == nullptr)
    {
      lxb_html_parser_destroy(page_parser);
      throw std::runtime_error("lv8: document allocation failed");
    }
    dom->doc->dom_document.scripting = 1;
    lxb_status_t status = LXB_STATUS_OK;

    // page-level headers (set-cookie) apply as the document loads
    for (const auto& h : page_headers)
    {
      std::string lower;
      for (size_t i = 0; i < h.first.size() && i < 64; i++)
        lower.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(h.first[i]))));
      if (lower == "set-cookie")
        SetCookieFromDocument(isolate, context, h.second);
    }

    // readyState stays "loading" while the chunked parse runs scripts;
    // it flips to "interactive" only after the last script executed.

    // document host object: dedicated surface (readyState/documentElement/
    // body/head/queries/event listeners), node pointer in a Private symbol
    lxb_dom_document_t* doc_node = &dom->doc->dom_document;
    v8::Local<v8::Object> document_obj =
        MakeDocumentObject(isolate, context, doc_node);
    InstallDocumentAccessor(isolate, context, document_obj);

    // B2: full interface-constructor family before inline scripts run
    EnsureInterfaceConstructorSurface(isolate, context, dom);

    // G4.372: the realm reset restores the boot-time Node constants bag on
    // every load after the first, while EnsureElementProto caches its
    // interface re-link - head scripts would see a prototype-less Node and
    // borrowed-native grabs (Node.prototype.removeChild et al.) resolve to
    // undefined.  Re-assert the interface constructor (idempotent when the
    // global already holds it).
    if (!dom->node_iface_ctor.IsEmpty())
    {
      v8::Local<v8::Function> node_ctor =
          dom->node_iface_ctor.Get(isolate);
      (void)context
          ->Global()
          ->DefineOwnProperty(
              context, ToV8Str(isolate, "Node"), node_ctor,
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // expose global constructors BEFORE inline scripts run (pages define
    // custom elements and check instanceof during load)
    auto expose_collection = [&](const char* name,
                                 v8::Global<v8::FunctionTemplate>& slot) {
      EnsureCollectionTemplates(isolate, context, dom);
      v8::Local<v8::Function> fn =
          slot.Get(isolate)->GetFunction(context).ToLocalChecked();
      (void)context->Global()->DefineOwnProperty(
          context, ToV8Str(isolate, name), fn,
          static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));
    };
    expose_collection("NodeList", dom->nl_tpl);
    expose_collection("HTMLCollection", dom->hc_tpl);

    // DOMParser / XMLSerializer global constructors
    auto expose_global_fn = [&](const char* name, const char* method_name,
                                v8::FunctionCallback method) {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      tpl->SetClassName(ToV8Str(isolate, name));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate), ToV8Str(isolate, name),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      v8::Local<v8::String> key = ToV8Str(isolate, method_name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, method);
      fn->SetClassName(key);
      tpl->PrototypeTemplate()->Set(key, fn);
      v8::Local<v8::Function> ctor =
          tpl->GetFunction(context).ToLocalChecked();
      (void)context->Global()->DefineOwnProperty(
          context, ToV8Str(isolate, name), ctor,
          static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));
    };
    expose_global_fn("DOMParser", "parseFromString", DOMParserParseFromString);
    expose_global_fn("XMLSerializer", "serializeToString",
                     XMLSerializerSerializeToString);

    // HTMLElement constructor + customElements registry. The constructor's
    // prototype IS the shared element surface, so `class X extends
    // HTMLElement` gives user prototypes the full element API.
    EnsureElementProto(isolate, context, dom);
    {
      (void)context->Global()->DefineOwnProperty(
          context, ToV8Str(isolate, "HTMLElement"),
          dom->html_element_ctor.Get(isolate),
          static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));

      v8::Local<v8::Object> registry = v8::Object::New(isolate);
      auto registry_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        (void)registry
            ->Set(context, key, fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      };
      registry_method("define", CustomElementsDefine);
      registry_method("get", CustomElementsGet);
      (void)context->Global()->DefineOwnProperty(
          context, ToV8Str(isolate, "customElements"), registry,
          static_cast<v8::PropertyAttribute>(0));
    }

    // G3.13 interface constructors: NamedNodeMap / DOMTokenList / Attr /
    // Element / Image
    {
      EnsureInterfaceTemplates(isolate, context, dom);
      auto expose_ctor_tpl = [&](const char* name,
                                 v8::Global<v8::FunctionTemplate>& slot) {
        v8::Local<v8::Function> fn =
            slot.Get(isolate)->GetFunction(context).ToLocalChecked();
        (void)context->Global()->DefineOwnProperty(
            context, ToV8Str(isolate, name), fn,
            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));
      };
      expose_ctor_tpl("NamedNodeMap", dom->nnm_tpl);
      expose_ctor_tpl("DOMTokenList", dom->tl_tpl);
      expose_ctor_tpl("Attr", dom->attr_tpl);
      if (!dom->element_iface_ctor.IsEmpty())
      {
        (void)context->Global()->DefineOwnProperty(
            context, ToV8Str(isolate, "Element"),
            dom->element_iface_ctor.Get(isolate),
            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));
      }
      // new Image(w, h) -> detached img element wrapper
      {
        v8::Local<v8::FunctionTemplate> img_tpl =
            v8::FunctionTemplate::New(isolate, ImageConstructor);
        img_tpl->SetClassName(ToV8Str(isolate, "Image"));
        (void)context->Global()->DefineOwnProperty(
            context, ToV8Str(isolate, "Image"),
            img_tpl->GetFunction(context).ToLocalChecked(),
            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));
      }
    }

    // navigation state resets to the page URL first (baseURL drives
    // location and external-script relative resolution). Without an
    // explicit baseURL the context keeps its current location (reference
    // parity: load(html) parses into the existing browsing context).
    if (!base_url.empty())
      InstallNavigation(isolate, context, base_url);

    // run scripts with reference-parity detached semantics: each script executes
    // when the parser reaches it, with its OWN node not attached to the tree
    // (the oracle's script-count probes see only the markup parsed before
    // the script).  src= scripts pull from the offline resource table and
    // log the request; currentScript is a detached <script> node carrying
    // the src attribute.  document.write is not anchored during detached
    // runs (write_point null), matching the missing-resource path.
    {
      auto ieq_ci = [&](size_t pos, const char* lit, size_t len)
      {
        if (pos + len > html.size())
          return false;
        for (size_t k = 0; k < len; k++)
        {
          if (std::tolower(static_cast<unsigned char>(html[pos + k])) !=
              std::tolower(static_cast<unsigned char>(lit[k])))
            return false;
        }
        return true;
      };

      struct ScriptRange
      {
        size_t open_start, open_end, close_start, close_end;
      };
      std::vector<ScriptRange> script_ranges;
      {
        size_t i = 0;
        while (i < html.size())
        {
          if (ieq_ci(i, "<!--", 4))
          {
            size_t end = html.find("-->", i + 4);
            i = (end == std::string::npos) ? html.size() : end + 3;
            continue;
          }
          if (ieq_ci(i, "<script", 7))
          {
            char next = i + 7 < html.size() ? html[i + 7] : '>';
            if (next == '>' ||
                std::isspace(static_cast<unsigned char>(next)))
            {
              size_t gt = html.find('>', i);
              if (gt == std::string::npos)
                break;
              size_t close = gt + 1;
              while (close < html.size() && !ieq_ci(close, "</script", 8))
                close++;
              if (close >= html.size())
                break;
              size_t close_gt = html.find('>', close);
              if (close_gt == std::string::npos)
                break;
              script_ranges.push_back(
                  {i, gt + 1, close, close_gt + 1});
              i = close_gt + 1;
              continue;
            }
          }
          i++;
        }
      }

      auto run_detached_script = [&](const ScriptRange& r)
      {
        std::string open_tag =
            html.substr(r.open_start, r.open_end - r.open_start);
        std::string src_attr;
        {
          size_t p = open_tag.find("src");
          if (p != std::string::npos)
          {
            size_t eq = open_tag.find('=', p + 3);
            if (eq != std::string::npos)
            {
              size_t v = eq + 1;
              while (v < open_tag.size() &&
                     std::isspace(static_cast<unsigned char>(open_tag[v])))
                v++;
              if (v < open_tag.size() &&
                  (open_tag[v] == '"' || open_tag[v] == '\''))
              {
                char q = open_tag[v++];
                size_t e = open_tag.find(q, v);
                if (e != std::string::npos)
                  src_attr = open_tag.substr(v, e - v);
              }
              else
              {
                size_t e = v;
                while (e < open_tag.size() &&
                       !std::isspace(static_cast<unsigned char>(open_tag[e])))
                  e++;
                src_attr = open_tag.substr(v, e - v);
              }
            }
          }
        }

        std::string code;
        std::string code_url;
        if (!src_attr.empty())
        {
          std::string url = ResolveUrlString(isolate, context, src_attr);
          const ContextState::ResourceEntry* resource =
              state != nullptr && !url.empty() ? FindNetResource(state, url)
                                               : nullptr;
          if (resource == nullptr)
            return; // missing external scripts are skipped silently
          ContextState::NetEntry entry;
          entry.method = "GET";
          entry.url = url;
          entry.cookie_header = GetCookieString(isolate, context);
          AppendNetEntry(isolate, context, state, entry);
          code = resource->body;
          code_url = url;
        }
        else
        {
          if (r.close_start > r.open_end)
            code = html.substr(r.open_end, r.close_start - r.open_end);
        }
        if (code.empty())
          return;

        // detached <script> node so document.currentScript keeps working
        lxb_html_element_t* detached = lxb_html_document_create_element(
            dom->doc, (const lxb_char_t*)"script", 6, nullptr);
        if (detached != nullptr && !src_attr.empty())
        {
          (void)lxb_dom_element_set_attribute(
              lxb_dom_interface_element(&detached->element),
              (const lxb_char_t*)"src", 3,
              (const lxb_char_t*)src_attr.c_str(), src_attr.size());
        }
        dom->current_script =
            detached != nullptr ? &detached->element.node : nullptr;
        dom->write_point = nullptr;
        v8::TryCatch try_catch(isolate);
        // reference names page-inline scripts "inline-script" (surface-audit stack
        // parity: the bottom frame must not leak the page URL)
        v8::ScriptOrigin origin(
            ToV8Str(isolate,
                    !code_url.empty()
                        ? code_url
                        : (dom->page_url.empty() ? "inline-script"
                                                 : "inline-script")));
        v8::MaybeLocal<v8::Script> compiled =
            v8::Script::Compile(context, ToV8Str(isolate, code), &origin);
        if (!compiled.IsEmpty())
          (void)compiled.ToLocalChecked()->Run(context)
              .FromMaybe(v8::Local<v8::Value>());
        dom->current_script = nullptr;
        // same real-browser/reference parity as execute_script: swallow
        try_catch.Reset();
      };

      size_t fed = 0;
      for (const auto& range : script_ranges)
      {
        std::string open_tag =
            html.substr(range.open_start, range.open_end - range.open_start);
        std::string src_attr;
        {
          size_t p = open_tag.find("src");
          if (p != std::string::npos)
          {
            size_t eq = open_tag.find('=', p + 3);
            if (eq != std::string::npos)
            {
              size_t v = eq + 1;
              while (v < open_tag.size() &&
                     std::isspace(static_cast<unsigned char>(open_tag[v])))
                v++;
              if (v < open_tag.size() &&
                  (open_tag[v] == '"' || open_tag[v] == '\''))
              {
                char q = open_tag[v++];
                size_t e = open_tag.find(q, v);
                if (e != std::string::npos)
                  src_attr = open_tag.substr(v, e - v);
              }
              else
              {
                size_t e = v;
                while (e < open_tag.size() &&
                       !std::isspace(static_cast<unsigned char>(open_tag[e])))
                  e++;
                src_attr = open_tag.substr(v, e - v);
              }
            }
          }
        }
        bool external = !src_attr.empty();

        // HTML5 type filtering (reference/real-browser parity): a <script> whose
        // type names a non-JS MIME type joins the DOM but never runs.
        bool js_type = true;
        {
          size_t tp = open_tag.find("type");
          if (tp != std::string::npos)
          {
            size_t eq = open_tag.find('=', tp + 4);
            if (eq != std::string::npos)
            {
              size_t v = eq + 1;
              while (v < open_tag.size() &&
                     std::isspace(static_cast<unsigned char>(open_tag[v])))
                v++;
              std::string type_attr;
              if (v < open_tag.size() &&
                  (open_tag[v] == '"' || open_tag[v] == '\''))
              {
                char q = open_tag[v++];
                size_t e = open_tag.find(q, v);
                type_attr = open_tag.substr(
                    v, e == std::string::npos ? std::string::npos : e - v);
              }
              else
              {
                size_t e = v;
                while (e < open_tag.size() &&
                       !std::isspace(static_cast<unsigned char>(open_tag[e])) &&
                       open_tag[e] != '>')
                  e++;
                type_attr = open_tag.substr(v, e - v);
              }
              for (auto& c : type_attr)
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
              static const char* const kJsTypes[] = {
                  "text/javascript", "application/javascript",
                  "application/ecmascript", "text/ecmascript",
                  "text/javascript1.5", "text/jscript", "text/livescript",
                  "module"};
              js_type = false;
              for (const char* jt : kJsTypes)
                if (type_attr == jt)
                {
                  js_type = true;
                  break;
                }
            }
          }
        }

        // The parser creates the script element before executing it.  Page
        // code sees document.currentScript in the tree, including src=
        // scripts, while later markup remains unparsed until it completes.
        size_t feed_to = range.close_end;
        size_t search_from = fed;
        if (feed_to > fed)
        {
          status = lxb_html_parse_chunk_process(
              page_parser, (const lxb_char_t*)html.c_str() + fed,
              feed_to - fed);
          if (status != LXB_STATUS_OK)
          {
            lxb_html_parser_destroy(page_parser);
            throw std::runtime_error("lv8: html chunk parse failed");
          }
          fed = feed_to;
        }

        std::string code;
        std::string code_url;
        lxb_dom_node_t* script_node = nullptr;
        if (external)
        {
          std::string url = ResolveUrlString(isolate, context, src_attr);
          const ContextState::ResourceEntry* resource =
              state != nullptr && !url.empty() ? FindNetResource(state, url)
                                               : nullptr;
          if (resource != nullptr)
          {
            ContextState::NetEntry entry;
            entry.method = "GET";
            entry.url = url;
            entry.cookie_header = GetCookieString(isolate, context);
            AppendNetEntry(isolate, context, state, entry);
            code = resource->body;
            code_url = url;
          }
          // missing external scripts are skipped silently
        }
        else
        {
          if (range.close_start > range.open_end)
            code = html.substr(range.open_end,
                               range.close_start - range.open_end);
        }
        std::vector<lxb_dom_node_t*> scripts;
        (void)SelectNodes(dom, lxb_dom_interface_node(doc_node), "script",
                          scripts);
        if (!scripts.empty())
          script_node = scripts.back();
        if (code.empty())
          continue;

        PendingScript ps;
        ps.code = code;
        ps.code_url = code_url;
        ps.src_attr = src_attr;
        // reference parity: page scripts report script-relative positions
        // (surface-audit stack capture - reference renders a null.x inside the first
        // page script at its script column, not its document column), so
        // no line/col origin offsets are applied to inline scripts.
        if (code_url.empty())
        {
          ps.line_off = 0;
          ps.col_off = 0;
        }
        execute_script(ps, script_node);
        // Browser task checkpoint between parser-blocking scripts: requests
        // opened by this script can settle before the next script executes.
        const bool had_idb = PendingIdbTasks(isolate, context) > 0;
        DispatchIdbTasks(isolate, context, true);
        isolate->PerformMicrotaskCheckpoint();
        if (had_idb)
        {
          RunTimers(isolate, context, 0.0);
          isolate->PerformMicrotaskCheckpoint();
        }
        // The engine's own binder script must leave the tree as soon as it
        // ran: the challenge SDK fingerprints gEBTN('script') during
        // bootstrap, and the binder is an lv8-only node reference never had.
        // Regular page scripts stay attached (reference gold shows earlier
        // scripts in the bootstrap-time enumeration).
        if (script_node != nullptr && script_node->parent != nullptr &&
            NodeText(script_node).find("__lv8_bind_document_location") !=
                std::string::npos)
        {
          (void)lxb_dom_node_remove(script_node);
        }
        pending_scripts.push_back(std::move(ps));
      }
      if (fed < html.size())
      {
        status = lxb_html_parse_chunk_process(
            page_parser, (const lxb_char_t*)html.c_str() + fed,
            html.size() - fed);
        if (status != LXB_STATUS_OK)
        {
          lxb_html_parser_destroy(page_parser);
          throw std::runtime_error("lv8: html chunk parse failed");
        }
      }
      if (lxb_html_parse_chunk_end(page_parser) != LXB_STATUS_OK)
      {
        lxb_html_parser_destroy(page_parser);
        throw std::runtime_error("lv8: html chunk parse end failed");
      }
      lxb_html_parser_destroy(page_parser);
    }



    // Real lifecycle order (reference/Chrome): the parse ran the scripts as the
    // parser passed them; readyState flips to "interactive" once the last
    // script executed, DOMContentLoaded fires, then "complete" and load.
    // Post-load cleanup: detach the last script node (currentScript
    // returns null after all scripts finish) and clear the executed_scripts
    // filter (all collections enumerate normally after load)
    if (dom->pending_detach != nullptr)
    {
      (void)lxb_dom_node_remove(dom->pending_detach);
      dom->pending_detach = nullptr;
    }
    dom->executed_scripts.clear();
    // Parser scripts can open IndexedDB before DOMContentLoaded.  The
    // reference settles those requests before the load handlers build the
    // challenge token; leaving them for the later drain reverses that order.
    DispatchIdbTasks(isolate, context, true);
    isolate->PerformMicrotaskCheckpoint();
    dom->ready_state = "interactive";
    DispatchEvent(isolate, context, "Event",
                  {{"type", ToV8Str(isolate, "readystatechange")}}, true);
    DispatchEvent(isolate, context, "Event",
                  {{"type", ToV8Str(isolate, "DOMContentLoaded")}}, true);
    dom->ready_state = "complete";
    DispatchEvent(isolate, context, "Event",
                  {{"type", ToV8Str(isolate, "readystatechange")}}, true);
    DispatchEvent(isolate, context, "Event",
                  {{"type", ToV8Str(isolate, "load")}}, true);
  }
  // G4.231: create the window.document accessor property on demand.  The
  // boot sequence calls this right before the "history" profile key so
  // the for-in walk order matches the reference (document between
  // navigation and history) - the SDK's key battery branches on that
  // order.  Safe to call repeatedly: the slot refreshes, the property is
  // created once (flag-guarded).
  void EnsureDocumentAccessorProperty(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context)
  {
    v8::Local<v8::Object> global = context->Global();
    v8::Local<v8::Value> slot;
    if (!global->GetPrivate(context, DocumentSlotKey(isolate))
              .ToLocal(&slot) ||
        !slot->IsObject())
    {
      ContextState* state = EnsureState(isolate, context);
      if (state->dom_state == nullptr)
        state->dom_state.reset(new DomState());
      v8::Local<v8::Object> document_obj =
          MakeDocumentObject(isolate, context, nullptr);
      (void)global->SetPrivate(context, DocumentSlotKey(isolate),
                               document_obj)
          .FromMaybe(false);
    }
    v8::Local<v8::Value> placed;
    if (global->GetPrivate(context, DocumentAccessorFlagKey(isolate))
            .ToLocal(&placed) &&
        placed->IsTrue())
      return;
    // G4.231: create the accessor CONFIGURABLE.  The reference window walk
    // orders "document" between "navigation" and "history" (own-key index
    // 224/225), i.e. inside the enumerable key sequence the polyfill tranche
    // pins down; only a configurable property can be relocated there by that
    // pass, which then re-defines it non-configurable to match the reference
    // descriptor face.
    v8::Local<v8::FunctionTemplate> tpl =
        v8::FunctionTemplate::New(isolate, DocumentSlotGetter);
    tpl->SetClassName(ToV8Str(isolate, "get document"));
    v8::Local<v8::Function> getter =
        tpl->GetFunction(context).ToLocalChecked();
    (void)global->SetAccessorProperty(
        ToV8Str(isolate, "document"), getter, v8::Local<v8::Function>(),
        static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::None));
    (void)global->SetPrivate(context, DocumentAccessorFlagKey(isolate),
                             v8::True(isolate))
        .FromMaybe(false);
  }
  void InstallDocumentShell(v8::Isolate* isolate,
                            v8::Local<v8::Context> context)
  {
    ContextState* state = EnsureState(isolate, context);
    if (state->dom_state == nullptr)
      state->dom_state.reset(new DomState());
    // EnsureDocumentAccessorProperty CREATE the accessor MOVABLE in the page
    // context: the window-surface tranche re-defines it at the reference
    // insertion point and makes it non-configurable there.
    v8::Local<v8::Object> document_obj =
        MakeDocumentObject(isolate, context, nullptr);
    InstallDocumentAccessor(isolate, context, document_obj, true);

    // DOMParser / XMLSerializer available before any page load
    auto expose_shell_parser = [&](const char* name, const char* method_name,
                                   v8::FunctionCallback method) {
      v8::Local<v8::Value> existing;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, name))
              .ToLocal(&existing) &&
          existing->IsFunction())
        return;
      v8::Local<v8::FunctionTemplate> g_tpl =
          v8::FunctionTemplate::New(isolate);
      g_tpl->SetClassName(ToV8Str(isolate, name));
      v8::Local<v8::ObjectTemplate> g_inst = g_tpl->InstanceTemplate();
      g_inst->SetInternalFieldCount(0);
      g_inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, name), v8::PropertyAttribute::ReadOnly);
      v8::Local<v8::String> g_key = ToV8Str(isolate, method_name);
      v8::Local<v8::FunctionTemplate> g_fn =
          v8::FunctionTemplate::New(isolate, method);
      g_fn->SetClassName(g_key);
      g_tpl->PrototypeTemplate()->Set(g_key, g_fn);
      (void)context->Global()->DefineOwnProperty(
          context, ToV8Str(isolate, name),
          g_tpl->GetFunction(context).ToLocalChecked(),
          static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum));
    };
    expose_shell_parser("DOMParser", "parseFromString",
                        DOMParserParseFromString);
    expose_shell_parser("XMLSerializer", "serializeToString",
                        XMLSerializerSerializeToString);
  }
  void InstallCanvasSurface(v8::Isolate* isolate,
                            v8::Local<v8::Context> context)
  {
    InstallOffscreenCanvasGlobal(isolate, context);
  }

  v8::Local<v8::ObjectTemplate> GlobalTemplateWithNamedAccess(
      v8::Isolate* isolate)
  {
    // Every context (main page and barren iframe realms) instantiates its
    // global from this template.  Without lv8 DomState attached the
    // interceptor is inert, so plain stpyv8 consumers are unaffected.
    v8::Local<v8::ObjectTemplate> tpl = v8::ObjectTemplate::New(isolate);
    tpl->SetHandler(v8::NamedPropertyHandlerConfiguration(
        WindowNamedGetter, WindowNamedSetter, nullptr, nullptr, nullptr,
        v8::Local<v8::Value>(), v8::PropertyHandlerFlags::kNonMasking));
    return tpl;
  }
} // namespace LV8
