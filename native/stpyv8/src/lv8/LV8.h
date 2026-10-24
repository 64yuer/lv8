// lv8 G1: native host-object layer.
//
// Everything installed here is a real V8 FunctionTemplate surface: methods and
// accessors are natively "[native code]", leave no JS source or stack frames,
// and need none of the JS-level masking the old bootstrap required.
//
// Per-context state (navigation entries, event listeners, timers, logical
// clock) lives in LV8::ContextState, kept in a registry keyed by the raw
// v8::Context pointer. A weak persistent handle erases the entry when the
// context is garbage collected, so neither the context nor its isolate is
// kept alive by lv8 state.

#ifndef LV8_HOSTOBJECTS_H
#define LV8_HOSTOBJECTS_H

#include <map>
#include <memory>
#include <chrono>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "v8.h"

// lexbor forward declarations (full headers are included by LV8Dom.cpp)
typedef struct lxb_dom_node lxb_dom_node_t;

namespace LV8
  {
  // global-registry event fallbacks (LV8Events.cpp): serve receivers that
  // resolve event methods through EventTarget.prototype but are not DOM
  // nodes (the window itself)
  void WindowEventAdd(const v8::FunctionCallbackInfo<v8::Value>& info);
  void WindowEventRemove(const v8::FunctionCallbackInfo<v8::Value>& info);
  void WindowEventDispatch(const v8::FunctionCallbackInfo<v8::Value>& info);

  // string helpers shared by every lv8 translation unit
  inline std::string ToUtf8(v8::Isolate* isolate, v8::Local<v8::Value> value)
  {
    v8::String::Utf8Value utf8(isolate, value);
    return std::string(*utf8 ? *utf8 : "", utf8.length());
  }

  inline v8::Local<v8::String> ToV8Str(v8::Isolate* isolate,
                                       const std::string& s)
  {
    return v8::String::NewFromUtf8(isolate, s.c_str()).ToLocalChecked();
  }

  // Bootstrap-only bridges are kept beneath reference's already-public control
  // object instead of adding lv8-branded names to window.  Native DOM and
  // networking code reaches those registrations through this single lookup.
  inline v8::Local<v8::Value> InternalBridgeGet(
      v8::Isolate* isolate, v8::Local<v8::Context> context,
      const char* name)
  {
    v8::Local<v8::Value> control;
    if (!context->Global()
             ->Get(context, ToV8Str(isolate, "__iv8__"))
             .ToLocal(&control) || !control->IsObject())
      return v8::Undefined(isolate);
    v8::Local<v8::Value> bridge;
    if (!control.As<v8::Object>()
             ->Get(context, ToV8Str(isolate, "__lv8_internal__"))
             .ToLocal(&bridge) || !bridge->IsObject())
      return v8::Undefined(isolate);
    return bridge.As<v8::Object>()
        ->Get(context, ToV8Str(isolate, name))
        .FromMaybe(v8::Local<v8::Value>(v8::Undefined(isolate)));
  }

  // Extract the lexbor node from an element/document wrapper
  inline lxb_dom_node_t* NodeOf(v8::Local<v8::Object> self)
  {
    v8::Isolate* iso = v8::Isolate::GetCurrent();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Private> key =
        v8::Private::ForApi(iso, ToV8Str(iso, "lv8.dom.node"));
    v8::Local<v8::Value> payload;
    if (!self->GetPrivate(ctx, key).ToLocal(&payload) ||
        !payload->IsExternal())
      return nullptr;
    return static_cast<lxb_dom_node_t*>(
        payload.As<v8::External>()->Value());
  }
  // G2: parsed-document state (defined in LV8Dom.cpp)
  struct DomState;

  // Global object template carrying the Window named-property interceptor
  // ("named access on the Window object", non-masking: real own window
  // properties always win).  Inert for contexts without lv8 DomState, so
  // plain stpyv8 contexts can share it.  Used by every context creation
  // site (Context.cpp and the barren iframe realms).
  v8::Local<v8::ObjectTemplate> GlobalTemplateWithNamedAccess(
      v8::Isolate* isolate);

  // window.document: reference answers gOPD(window,'document') with a
  // getter-only accessor {name: "get document", enumerable: true,
  // configurable: false} - not the configurable data property a plain
  // Set would create.  The current wrapper is parked on a global private
  // slot so a reload (LoadDocument) only swaps the slot; the
  // non-configurable accessor itself is installed once per realm.
  void InstallDocumentAccessor(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Object> document_obj,
                               bool configurable = false);

  // Logical timers (setTimeout/setInterval). A callback may reschedule.
  struct Timer
  {
    int id = 0;
    double when_ms = 0.0;
    double interval_ms = 0.0;
    bool repeating = false;
    v8::Global<v8::Function> fn;
    std::vector<v8::Global<v8::Value>> args;
  };

  // One session-history entry for location/history emulation.
  struct HistoryEntry
  {
    std::string url;
    std::string key;
    v8::Global<v8::Value> state;
  };

  // All per-context lv8 state. Attached to the context global via a Private
  // symbol (see LV8.cpp); freed by a weak callback when the context dies.
  struct ContextState
  {
    // logical clock, advanced by RunTimers
    double clock_ms = 0.0;
    // auto-advance steps: performance.now()/Date.now() reads nudge the
    // logical clock (reference eventLoop semantics).  The reference wheel
    // advances performance.now by ~0.001ms per read in logical mode - its
    // busy-wait pace loops rely on that progress; a frozen clock spins
    // them forever.
    double perf_advance_step = 0.001;
    double date_advance_step = 0.0;
    // system time_mode: performance reads the real clock
    bool system_time = false;
    // G4.369: system-mode effective timer clock = this base + real elapsed
    // since install (the reference wheel's load pushes ~200ms of virtual
    // time and its timer clock then tracks real drain time); clock_ms stays
    // the logical control clock the host explicitly advances
    double system_clock_base = 0.0;
    // hybrid time_mode ("reference wheel parity"): Date stays on the logical
    // clock while performance.now reports real steady-clock milliseconds
    bool perf_real = false;
    // last value returned by the paced system-mode Date.now (read pacing
    // keeps interpreter-polled phase durations in real-work territory)
    double last_paced_date_ms = 0.0;
    // paced system-mode Date.now minimum read interval (diagnostic knob,
    // LV8_DATE_PACE_MS); raises interpreter-polled phase durations toward
    // the reference engine's real-work territory
    double date_pace_ms = 1.0;
    // precise creation-epoch anchor (sub-millisecond): performance
    // .timeOrigin reports this in every time mode
    double system_origin_ms_precise = 0.0;
    double system_origin_ms = 0.0;
    // high-resolution monotonic anchor captured with system_origin_ms;
    // performance.now subtracts it so tight loops advance at microsecond
    // scale like the reference wheel (ms-truncated reads freeze instead)
    std::chrono::steady_clock::time_point system_origin_steady =
        std::chrono::steady_clock::now();
    int next_timer_id = 1;
    int next_history_key = 1;
    std::vector<Timer> timers;

    // page-invisible audit log of every formatted Error.stack (live-chain
    // tooling reads it back via __lv8_stack_dump; the challenge cannot see
    // the buffer - no observable surface is touched)
    std::vector<std::string> stack_log;
    // on-demand heap string harvest (__lv8_heap_dump): surfaces every
    // retained JS string (length-filtered) so tooling can read the
    // challenge VM's serialized value tables post-run
    std::vector<std::string> heap_strings;
    // bootstrap self-reads (NetworkInformation parity snapshot, ...) must
    // stay page-invisible: set while bootstrap scripts run, checked by
    // MonitorRecord alongside monitor_enabled
    bool monitor_suspended = false;

    // session history: entries[index] is current
    std::vector<HistoryEntry> entries;
    int index = 0;

    // G4.17: performance mark/measure store + registered observers
    struct PerfEntry
    {
      std::string name;
      std::string type;
      double start;
      double duration;
    };
    std::vector<PerfEntry> perf_entries;
    struct PerfObserver
    {
      v8::Global<v8::Object> instance;
      v8::Global<v8::Function> callback;
    };
    std::vector<PerfObserver> perf_observers;

    // G4.19: Resize/Intersection observer registry (kind split by flag)
    struct DomObserver
    {
      v8::Global<v8::Object> instance;
      v8::Global<v8::Function> callback;
      bool intersection = false;
      std::vector<v8::Global<v8::Object>> targets;
    };
    std::vector<DomObserver> dom_observers;

    // global event listeners, registration order preserved
    std::vector<std::pair<std::string, v8::Global<v8::Function>>> listeners;

    // G3: shared FormData template (global ctor + Response.formData agree)
    v8::Global<v8::FunctionTemplate> fd_tpl;

    // G4: native Blob / File templates (instance shape; the global
    // constructors inherit these so HasInstance() identifies them)
    v8::Global<v8::FunctionTemplate> blob_tpl;
    v8::Global<v8::FunctionTemplate> file_tpl;
    v8::Global<v8::FunctionTemplate> file_list_tpl;

    // G4 ledger chunk 1: geometry + DataTransfer surfaces (LV8DomExtras)
    v8::Global<v8::FunctionTemplate> domrect_tpl;
    // matchMedia results: media/matches on prototype accessors, values in
    // internal fields (instance own-face empty, reference parity)
    v8::Global<v8::FunctionTemplate> mql_tpl;
    v8::Global<v8::FunctionTemplate> dompointro_tpl;
    v8::Global<v8::FunctionTemplate> dompoint_tpl;
    v8::Global<v8::FunctionTemplate> dommatrix_tpl;
    v8::Global<v8::FunctionTemplate> dommatrixro_tpl;
    v8::Global<v8::FunctionTemplate> domquad_tpl;
    v8::Global<v8::FunctionTemplate> dt_tpl;
    v8::Global<v8::FunctionTemplate> dtil_tpl;
    v8::Global<v8::FunctionTemplate> dt_item_tpl;
    v8::Global<v8::FunctionTemplate> css_keyword_tpl;
    v8::Global<v8::FunctionTemplate> css_unit_tpl;

    // G4 ledger chunk 3: event constructor family (LV8Events)
    v8::Global<v8::FunctionTemplate> event_ctor_tpl;
    v8::Global<v8::FunctionTemplate> touchlist_tpl;

    // G4 ledger chunk 4: URL / URLSearchParams / URLPattern (LV8Net)
    v8::Global<v8::FunctionTemplate> usp_tpl;
    v8::Global<v8::FunctionTemplate> url_tpl;
    v8::Global<v8::FunctionTemplate> urlpattern_tpl;

    // G4 ledger chunk 5: user-facing Headers (LV8Net)
    v8::Global<v8::FunctionTemplate> headers_tpl;

    // G4 ledger chunk 6: native FileReader (LV8Net)
    v8::Global<v8::FunctionTemplate> filereader_tpl;

    // G4 ledger chunk 7c: streams (LV8Net)
    v8::Global<v8::FunctionTemplate> readable_tpl;
    v8::Global<v8::FunctionTemplate> readablectl_tpl;
    v8::Global<v8::FunctionTemplate> writable_tpl;
    v8::Global<v8::FunctionTemplate> transform_tpl;

    // G4 ledger chunk 7d-1: getComputedStyle (LV8DomExtras)
    v8::Global<v8::FunctionTemplate> gcs_tpl;

    // G4 ledger chunk 7d-2: MutationObserver (LV8DomExtras)
    v8::Global<v8::FunctionTemplate> mutationobserver_tpl;

    // G4 ledger chunk 8: polyfills4 scattered declarations (LV8Net)
    v8::Global<v8::FunctionTemplate> imagebitmap_tpl;
    v8::Global<v8::FunctionTemplate> request_tpl;
    v8::Global<v8::FunctionTemplate> midiport_tpl;
    v8::Global<v8::FunctionTemplate> midiinput_tpl;
    v8::Global<v8::FunctionTemplate> midioutput_tpl;
    v8::Global<v8::FunctionTemplate> midiaccess_tpl;
    v8::Global<v8::FunctionTemplate> lockmanager_tpl;

    // G4 ledger chunk 7b: offline workers (LV8Net)
    v8::Global<v8::FunctionTemplate> worker_tpl;
    v8::Global<v8::FunctionTemplate> sharedworker_tpl;
    v8::Global<v8::FunctionTemplate> portshim_tpl;

    // M4 (G4.309): dedicated-worker runtimes.  Each Worker instance owns
    // an independent context whose global carries the resolved profile
    // snapshot (navigator fields, no page state) and two message queues;
    // everything pumps through the page's microtask queue, so delivery is
    // deterministic with ctx.drain().  The boundary is the reference-style
    // structuredish clone (functions/undefined drop).  Released with the
    // owning context (v8::Global members reset on destruction).
    struct WorkerRuntime
    {
      v8::Global<v8::Context> context;
      v8::Global<v8::Context> main_context;
      v8::Global<v8::Object> worker;
      std::deque<std::string> to_worker;
      std::deque<std::string> to_main;
      bool started = false;
      bool terminated = false;
      std::string url;
      std::string script_body;
      std::vector<std::unique_ptr<v8::Global<v8::Function>>> timers;
    };
    std::vector<std::unique_ptr<WorkerRuntime>> worker_runtimes;

    // G4 ledger chunk 7a: web audio value layer (LV8DomExtras)
    v8::Global<v8::FunctionTemplate> audiobuffer_tpl;
    v8::Global<v8::FunctionTemplate> audionode_tpl;
    v8::Global<v8::FunctionTemplate> audioparam_tpl;
    v8::Global<v8::FunctionTemplate> baseaudioctx_tpl;
    v8::Global<v8::FunctionTemplate> audioctx_tpl;
    v8::Global<v8::FunctionTemplate> offlineaudioctx_tpl;

    // the raw installed profile (media/devicePixelRatio/battery/... data-only
    // groups live here for matchMedia, window.orientation, ...)
    v8::Global<v8::Object> profile_data;

    // API monitor (debug mode): records host-object reads/calls
    bool monitor_enabled = false;
    std::vector<std::pair<std::string, std::string>> monitor_entries;
    // value snapshots parallel to monitor_entries (debug mode; "" when the
    // call site does not carry a value)
    std::vector<std::string> monitor_values;
    std::vector<std::string> ignore_paths;
    v8::Global<v8::Object> monitor_object;

    // G2: parsed document (lexbor) + wrapper identity cache
    std::shared_ptr<DomState> dom_state;

    // G3: web storage (insertion-ordered string pairs) and cookie jar
    std::vector<std::pair<std::string, std::string>> local_storage;
    std::vector<std::pair<std::string, std::string>> session_storage;
    // The two Storage instances identify the backing map for the shared
    // Storage.prototype native methods.
    v8::Global<v8::Object> local_storage_object;
    v8::Global<v8::Object> session_storage_object;
    struct Cookie
    {
      std::string name;
      std::string value;
      std::string path;
    };
    std::vector<Cookie> cookies;
    // navigator.storage profile values (estimate/persisted/persist)
    double storage_quota = 2147483648.0;
    double storage_usage = 0.0;
    bool storage_persistent = false;

    // G3: offline network bridge (resource table, request log, caches)
    struct ResourceEntry
    {
      std::string url;
      std::string body;
      std::vector<std::pair<std::string, std::string>> headers;
      int status = 200;
    };
    std::vector<ResourceEntry> resources;
    struct NetEntry
    {
      std::string method;
      std::string url;
      std::vector<std::pair<std::string, std::string>> headers;
      std::string cookie_header;
      bool has_body = false;
      std::string body;
    };
    std::vector<NetEntry> net_log;
    // fetches resolve their promise; XHRs run state transitions + onload
    struct PendingDelivery
    {
      v8::Global<v8::Promise::Resolver> resolver;  // fetch kind
      v8::Global<v8::Object> xhr;                  // xhr kind (mutually exclusive)
      int status = 200;
      std::vector<std::pair<std::string, std::string>> headers;
      std::string body;
      // fetch kind only: reject with a TypeError(body) instead of
      // resolving with a Response (offline resource miss)
      bool reject = false;
      // bridge-parked: a Python-side handler decides the outcome at drain
      bool bridged = false;
      std::string method;
      std::string url;
      std::vector<std::pair<std::string, std::string>> request_headers;
      std::string cookie_header;
      bool has_request_body = false;
      std::string request_body;
      std::string bridge_initiator = "fetch";
      // abort signal (fetch kind): checked at delivery time
      v8::Global<v8::Object> signal;
    };
    std::vector<PendingDelivery> pending_responses;
    int pending_network_tasks = 0;
    // network bridge: Python handler consulted for resource-table misses
    bool bridge_enabled = false;
    struct CacheEntry
    {
      std::string url;
      std::vector<std::pair<std::string, std::string>> headers;
      std::string body;
    };
    struct CacheStore
    {
      std::string name;
      std::vector<CacheEntry> entries;
    };
    std::vector<CacheStore> cache_stores;
    struct PerfResource
    {
      std::string initiator_type;
      std::string url;
      size_t body_size = 0;
    };
    std::vector<PerfResource> perf_resources;
    // cached JS surfaces so repeated alias reads keep object identity
    v8::Global<v8::Object> netlog_object;
    v8::Global<v8::Array> netlog_entries;

    // G3: storage buckets (isolated cache namespaces) + credentials
    struct BucketStore
    {
      std::string name;
      std::string durability;
      // cache name -> entries (buckets isolate their cache namespaces)
      std::map<std::string, std::vector<CacheEntry>> caches;
    };
    std::vector<BucketStore> buckets;
    v8::Global<v8::Object> stored_credential;

    // G3: memory clipboard seed/context text
    std::string clipboard_text;
    // set by the last structured eval when it settled a promise
    bool last_eval_was_promise = false;
    struct SchedulerTask
    {
      v8::Global<v8::Function> fn;
      v8::Global<v8::Object> signal;
    };
    std::vector<SchedulerTask> scheduler_tasks;
    int next_raf_id = 1;
    struct RafEntry
    {
      int id;
      double fire_at;
      v8::Global<v8::Function> fn;
    };
    std::vector<RafEntry> raf_queue;
    double animation_frame_interval = 16.0;

    // G3: in-memory indexedDB + virtual file tree
    struct IdbStore
    {
      std::string name;
      std::string key_path;
    };
    struct IdbRecord
    {
      double key = 0;
      bool key_is_string = false;
      std::string string_key;
      v8::Global<v8::Value> value;
    };
    struct IdbDatabase
    {
      std::string name;
      double version = 1.0;
      std::vector<IdbStore> stores;
      // store name -> records (numeric key + object); the shared_ptr
      // keeps map node construction free of the move-only Global member
      std::map<std::string,
               std::shared_ptr<std::vector<IdbRecord>>> records;
    };
    std::vector<IdbDatabase> idb_databases;
    struct IdbTask
    {
      v8::Global<v8::Object> request;      // open / put / get request
      int kind = 0;  // 0 = open (upgrade+success), 1 = plain success
      std::string db_name;
      double db_version = 1.0;
      bool need_upgrade = false;
    };
    std::vector<IdbTask> pending_idb;
    struct FsNode
    {
      bool dir = true;
      std::string content;
      std::map<std::string, FsNode> children;
    };
    FsNode fs_root;
    // shared FS handle templates (methods + instanceof target)
    v8::Global<v8::FunctionTemplate> fs_dir_tpl;
    v8::Global<v8::FunctionTemplate> fs_file_tpl;
  };

  // Return the state attached to |context|, or nullptr when the context never
  // went through InstallHostObjects.
  ContextState* GetState(v8::Local<v8::Context> context);

  // Create (or reset) the state attached to |context|.
  ContextState* EnsureState(v8::Isolate* isolate, v8::Local<v8::Context> context);

  // ---------------------------------------------------------------- install

  // Parse profile_json and install the full G1 surface onto the context:
  //   - profile-driven host objects (navigator, screen, ... native accessors
  //     plus optional "$methods" native methods),
  //   - console with native-appearing methods,
  //   - global event target (addEventListener/removeEventListener/
  //     dispatchEvent) and Event/PopStateEvent/HashChangeEvent/CustomEvent
  //     constructors,
  //   - location + history driven by a session-history model (initial URL:
  //     profile.location.href, default "about:blank"),
  //   - logical-clock timers (setTimeout/setInterval/...).
  // Throws std::runtime_error on malformed profiles.
  void InstallHostObjects(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          const std::string& profile_json);

  // Build a host object of |class_name| mirroring every own key of |data| as
  // a native accessor property; "$methods" entries (name -> fixed return
  // value) become prototype methods with native appearance. |path| is the
  // object's address inside the profile tree ("navigator", "location", ...)
  // used by the API monitor and ignore_apis; empty for anonymous objects.
  v8::Local<v8::Object> HostObjectFromProfile(v8::Isolate* isolate,
                                              v8::Local<v8::Context> context,
                                              const char* class_name,
                                              v8::Local<v8::Object> data,
                                              const std::string& path = "");

  // Individual installers (called by InstallHostObjects, public for tests).
  void InstallConsole(v8::Isolate* isolate, v8::Local<v8::Context> context);
  void InstallEventTarget(v8::Isolate* isolate, v8::Local<v8::Context> context);
  void InstallNavigation(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         const std::string& initial_url);
  void InstallTimers(v8::Isolate* isolate, v8::Local<v8::Context> context);
  void InstallMediaQuery(v8::Isolate* isolate, v8::Local<v8::Context> context);
  void InstallWindowSurface(v8::Isolate* isolate,
                            v8::Local<v8::Context> context);
  void InstallMonitorSurface(v8::Isolate* isolate,
                             v8::Local<v8::Context> context);

  // G3: web storage + cookie jar. Reads "localStorage"/"sessionStorage"
  // objects from |profile| as pre-configured values (insertion order kept).
  void InstallStorage(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> profile);
  void InstallCookieStore(v8::Isolate* isolate,
                          v8::Local<v8::Context> context);
  // G3: navigator.storageBuckets (isolated cache namespaces) and
  // navigator.credentials (context-local credential store).
  void InstallBucketsAndCredentials(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context);
  // G3: offline declaration surfaces (serviceWorker/mediaDevices/webrtc)
  // and the memory clipboard.
  void InstallOfflineDeclarations(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context);
  // G3: window.chrome (loadTimes/csi/app), document.hasFocus,
  // navigator.getGamepads and speechSynthesis.
  void InstallBrowserChrome(v8::Isolate* isolate,
                            v8::Local<v8::Context> context);
  bool LastEvalWasPromise(v8::Isolate* isolate,
                          v8::Local<v8::Context> context);
  // G3: dataset interceptors and NamedNodeMap methods
  v8::Intercepted DatasetGetter(
      v8::Local<v8::Name> name,
      const v8::PropertyCallbackInfo<v8::Value>& info);
  v8::Intercepted DatasetSetter(
      v8::Local<v8::Name> name, v8::Local<v8::Value> value,
      const v8::PropertyCallbackInfo<void>& info);
  v8::Intercepted DatasetQuery(
      v8::Local<v8::Name> name,
      const v8::PropertyCallbackInfo<v8::Integer>& info);
  v8::Intercepted DatasetDeleter(
      v8::Local<v8::Name> name,
      const v8::PropertyCallbackInfo<v8::Boolean>& info);
  void DatasetEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info);
  void NNM_Item(const v8::FunctionCallbackInfo<v8::Value>& info);
  v8::Intercepted NNM_IndexedGetter(uint32_t index,
      const v8::PropertyCallbackInfo<v8::Value>& info);
  void NNM_GetNamedItem(const v8::FunctionCallbackInfo<v8::Value>& info);
  void NNM_Length(const v8::FunctionCallbackInfo<v8::Value>& info);
  void InstallCanvasSurface(v8::Isolate* isolate, v8::Local<v8::Context> context);
  void InstallDomExtras(v8::Isolate* isolate,
                        v8::Local<v8::Context> context);
  int RegisterAnimationFrame(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Function> callback);
  void CancelAnimationFrame(v8::Isolate* isolate,
                            v8::Local<v8::Context> context, int id);
  int DispatchAnimationFrames(v8::Isolate* isolate,
                               v8::Local<v8::Context> context);
  void DispatchSchedulerTasks(v8::Isolate* isolate,
                              v8::Local<v8::Context> context);
  void SetLastEvalWasPromise(v8::Isolate* isolate,
                             v8::Local<v8::Context> context, bool value);
  // G3: in-memory indexedDB and file system access handles.
  void InstallIdbAndFs(v8::Isolate* isolate, v8::Local<v8::Context> context);
  int PendingIdbTasks(v8::Isolate* isolate, v8::Local<v8::Context> context);
  void DispatchIdbTasks(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        bool open_only = false);
  // Response factory shared by fetch/caches/buckets.
  v8::Local<v8::Object> MakeResponse(
      v8::Isolate* isolate, v8::Local<v8::Context> context, int status,
      const std::vector<std::pair<std::string, std::string>>& headers,
      const std::string& body);
  // G3: document surface before any page load (cookie jar etc. work;
  // DOM accessors report an empty loading document). LoadDocument replaces it.
  void InstallDocumentShell(v8::Isolate* isolate,
                            v8::Local<v8::Context> context);
  // G4.231: create the window.document accessor property on demand at the
  // reference walk position (between navigation and history).  Idempotent:
  // the property is created once, the document slot refreshes each call.
  void EnsureDocumentAccessorProperty(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context);
  std::string GetCookieString(v8::Isolate* isolate,
                              v8::Local<v8::Context> context);
  void SetCookieFromDocument(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             const std::string& text);

  // G3: crypto (subtle.digest SHA-1/SHA-256, getRandomValues, randomUUID)
  // and TextEncoder -- offline, no host CSPRNG beyond seeding.
  void InstallCrypto(v8::Isolate* isolate, v8::Local<v8::Context> context);

  // G3: offline network bridge. Installs fetch/caches/performance plus the
  // __lv8__.netLog control surface; requests hit the per-context resource
  // table (AddResource) and never touch a real socket.
  void InstallNet(v8::Isolate* isolate, v8::Local<v8::Context> context);
  int AddResource(v8::Isolate* isolate, v8::Local<v8::Context> context,
                  const std::string& url, const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& headers,
                  int status);
  // Number of fetches issued but not yet dispatched to their promise.
  int PendingNetworkTasks(v8::Isolate* isolate, v8::Local<v8::Context> context);
  void ClearPendingNetworkTasks(v8::Isolate* isolate,
                                v8::Local<v8::Context> context);
  // Deliver queued fetch responses (explicit drain semantics).
  void SetBridgeEnabled(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        bool enabled);
  std::string PendingBridgedRequests(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context);
  void ResolveBridged(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      const std::string& url, const std::string& outcome,
                      const std::string& body, const std::string& headers_json,
                      int status);
  int DispatchPendingResponses(v8::Isolate* isolate,
                               v8::Local<v8::Context> context);
  const ContextState::ResourceEntry* FindNetResource(ContextState* state,
                                                     const std::string& url);
  void AppendNetEntry(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      ContextState* state,
                      const ContextState::NetEntry& entry);

  // Resolve |relative| against the context's current location.href.
  // Returns "" when parsing fails.
  std::string ResolveUrlString(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               const std::string& relative);

  // G2 DOM: parse |html| into the context document, run inline scripts in
  // document order, fire DOMContentLoaded, set readyState to "complete".
  // The context must be entered. Throws std::runtime_error on parse failure.
  void LoadDocument(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    const std::string& html, const std::string& base_url,
                    const std::vector<std::pair<std::string, std::string>>& page_headers);

  // Wrap a lexbor node as a native element object (identity-cached).
  v8::Local<v8::Object> WrapNode(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 lxb_dom_node_t* node);

  // Attach the document surface onto the global ("document").
  void InstallDocument(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       const std::string& html, const std::string& base_url);

  // document.addEventListener/removeEventListener forward to the global
  // listener table (document is the only DOM event target in G2.0).
  void DocumentAddEventListener(const v8::FunctionCallbackInfo<v8::Value>& info);
  void DocumentRemoveEventListener(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  void DocumentDispatchEvent(const v8::FunctionCallbackInfo<v8::Value>& info);

  // -------------------------------------------------------- event dispatch

  // Create an event object of |class_name| whose |props| become native
  // accessor properties (backed by a private data object), then invoke every
  // listener registered for the event's "type" property in registration
  // order. Listeners receive the event object as their only argument.
  // With |document_target_first| the document object's own dispatchEvent
  // runs first (browser order: document at-target listeners fire before
  // the window's bubble listeners for DOMContentLoaded/readystatechange).
  void DispatchEvent(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     const std::string& class_name,
                     const std::vector<std::pair<std::string, v8::Local<v8::Value>>>& props,
                     bool document_target_first = false);

  // G3.13 element dispatch support: write a dispatch-time field
  // (target/currentTarget/eventPhase/isTrusted) onto an event object's
  // private data, and read back type/bubbles/stop flags.
  void SetEventField(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> event, const char* name,
                     v8::Local<v8::Value> value);
  bool EventStopRequested(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> event);
  bool EventImmediateStopRequested(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> event);
  std::string EventTypeOf(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> event);
  bool EventBubbles(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Object> event);

  // ---------------------------------------------------- time / locale / zone

  // Advance the logical clock by |advance_ms| and run every timer that comes
  // due (repeating timers reschedule). Returns the number of callbacks run.
  // The context must be entered (stpyv8 contract). JS exceptions inside
  // timer callbacks propagate as CJavascriptException.
  int RunTimers(v8::Isolate* isolate, v8::Local<v8::Context> context,
                double advance_ms);

  // Number of scheduled-but-not-yet-run timers.
  int PendingTimers(v8::Isolate* isolate, v8::Local<v8::Context> context);

  // G3.14 eventLoop control surface: move the clock without running
  // timers, and run at most |limit| due timers without moving it.
  void AdvanceClock(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    double ms);
  int RunDueTimersLimit(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        int limit);

  // Record a "call" monitor entry for |self|'s path + |method| (no-op when
  // the monitor is disabled). Used by the navigation layer.
  void MonitorCall(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> self, const char* method);

  // Record a "get" monitor entry with the read's value snapshot (fingerprint
  // value-by-value comparison; no-op when the monitor is disabled).
  void MonitorRead(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   const std::string& path, v8::Local<v8::Value> value);

  // Current logical clock in milliseconds.
  double LogicalClock(v8::Isolate* isolate, v8::Local<v8::Context> context);

  // Override the process-default ICU time zone with an IANA name (e.g.
  // "Asia/Shanghai"). V8 per-isolate date caches are reset so Date/Intl
  // observations reflect the zone natively (DST included). Throws
  // std::runtime_error for unknown zone ids.
  void SetTimeZoneOverride(v8::Isolate* isolate, const std::string& iana_name);

  // Override the process-default ICU locale (BCP 47, e.g. "en-US") so Intl
  // default-resolution matches the profile natively. Throws
  // std::runtime_error for malformed locale ids.
  void SetDefaultLocale(v8::Isolate* isolate, const std::string& locale);

  // Restore the machine zone/locale captured before the first override
  // (multi-context isolation: unconfigured contexts re-apply the host
  // defaults when entered). Never throws.
  void RestoreHostTimeZone(v8::Isolate* isolate);
  void RestoreHostLocale(v8::Isolate* isolate);

  // G4: materialize a FileList snapshot over |files| (native FileList
  // instances; shared by the FileList constructor and DataTransfer).
  v8::Local<v8::Object> MakeFileList(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     v8::Local<v8::Array> files);
}

#endif // LV8_HOSTOBJECTS_H
