// Standalone V8 probe: replicates the init sequence used by stpyv8's
// CPlatform::Init() and CContext constructor, with no boost::python in the
// picture. Used to isolate whether a crash lives in V8/ICU or in the binding.

#include <v8.h>
#include <libplatform/libplatform.h>

#include <cstdio>
#include <memory>

int main(int argc, char** argv)
{
    const char* icu = (argc > 1) ? argv[1]
                                 : "C:\\Users\\PC\\AppData\\Roaming\\STPyV8\\icudtl.dat";
    printf("icu path: %s\n", icu);

    v8::V8::InitializeICUDefaultLocation("probe", icu);
    v8::V8::InitializeExternalStartupData("probe");

    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();
    printf("v8 initialized\n");

    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate* isolate = v8::Isolate::New(params);
    printf("isolate created: %p\n", (void*)isolate);

    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);

        v8::Local<v8::Context> context = v8::Context::New(isolate);
        printf("context created\n");

        v8::Context::Scope context_scope(context);
        v8::Local<v8::String> source =
            v8::String::NewFromUtf8(isolate, "6*7").ToLocalChecked();
        v8::Local<v8::Script> script =
            v8::Script::Compile(context, source).ToLocalChecked();
        v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
        printf("result = %d\n", result->Int32Value(context).FromJust());

        printf("Intl: %d\n",
               v8::String::NewFromUtf8(isolate, "new Intl.DateTimeFormat().resolvedOptions().timeZone")
                   .ToLocalChecked()
                   ->Length());
    }

    printf("probe ok\n");
    return 0;
}
