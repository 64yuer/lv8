import io

# ---------- 1. LV8.h: dom_state on ContextState + WrapNode/LoadDocument decls
p = 'LV8.h'
s = io.open(p, encoding='utf-8').read()
old = '''    // raw profile kept for data-only consumers (matchMedia, orientation)
    v8::Global<v8::Object> profile_data;'''
new = '''    // raw profile kept for data-only consumers (matchMedia, orientation)
    v8::Global<v8::Object> profile_data;

    // G2: parsed document (lexbor) + node wrapper identity cache; owned via
    // shared_ptr so the weak cleanup frees lexbor allocations.
    struct DomState;
    std::shared_ptr<DomState> dom_state;'''
assert old in s, 'h-domstate'
s = s.replace(old, new, 1)

old = '''  void InstallMonitorSurface(v8::Isolate* isolate,
                             v8::Local<v8::Context> context);'''
new = '''  void InstallMonitorSurface(v8::Isolate* isolate,
                             v8::Local<v8::Context> context);

  // G2 DOM: parse |html| into the context's document, run inline scripts in
  // document order, fire DOMContentLoaded, set readyState to "complete".
  // The context must be entered. Throws std::runtime_error on parse failure.
  void LoadDocument(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    const std::string& html, const std::string& base_url);

  // Wrap a lexbor node as a native element object (identity-cached).
  v8::Local<v8::Object> WrapNode(v8::Isolate* isolate,
                                 v8::Local<v8::Object> into,
                                 lxb_dom_node_t* node);

  // Internal: attach the document surface onto the global ("document").
  void InstallDocument(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       const std::string& html, const std::string& base_url);'''
assert old in s, 'h-decls'
s = s.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('LV8.h ok')

# ---------- 2. LV8.cpp: include DomState definition into LV8.h-visible type
# (DomState is defined in LV8Dom.cpp; LV8.h forward-declares it and the
# shared_ptr member only needs the forward declaration.)
print('LV8.cpp: no change needed')
