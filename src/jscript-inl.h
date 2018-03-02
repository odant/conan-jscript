#include "jscript.h"

namespace jscript {
using namespace node;

const std::wstring executeFilePath;
const std::wstring executeFolderPath;
const std::wstring instanceScript;

std::atomic<bool> is_initilized = false;

static const int kContextJSInstanceDataIndex = NODE_CONTEXT_EMBEDDER_DATA_INDEX + 1;

int    argc = 0;
char** argv = nullptr;

int          exec_argc = 0;
const char** exec_argv = nullptr;

class JSInstanceImpl: public JSInstance,
                      public NodeInstanceData
{
public:
    JSInstanceImpl(NodeInstanceType node_instance_type,
                   uv_loop_t* event_loop,
                   int argc,
                   const char** argv,
                   int exec_argc,
                   const char** exec_argv,
                   bool use_debug_agent_flag)
            : NodeInstanceData(node_instance_type,
                               event_loop,
                               argc,
                               argv,
                               exec_argc,
                               exec_argv,
                               use_debug_agent_flag) { }

    ~JSInstanceImpl() {
        try {
            uv_loop_t* event_loop_ = event_loop();
            if (event_loop_ != uv_default_loop())
                uv_loop_delete(event_loop_);
        }
        catch (...) { }
    }

    void StartNodeInstance();

    Mutex        _isolate_mutex;
    Isolate*     _isolate = nullptr;
    Environment* _env = nullptr;
    std::thread  _thread;

    bool is_shutdown() const {
        return _is_shutdown;
    }

    void set_shutdown(bool state) {
        _is_shutdown = state;
    }

private:
    void LoadEnvironment();

    std::atomic<bool> _is_shutdown = false;
};

void JSInstanceImpl::LoadEnvironment() {
    HandleScope handle_scope(_env->isolate());

    _env->isolate()->SetFatalErrorHandler(node::OnFatalError);
    _env->isolate()->AddMessageListener(OnMessage);

    atexit(AtProcessExit);

    TryCatch try_catch(_env->isolate());

    // Disable verbose mode to stop FatalException() handler from trying
    // to handle the exception. Errors this early in the start-up phase
    // are not safe to ignore.
    try_catch.SetVerbose(false);

    // Execute the lib/internal/bootstrap_node.js file which was included as a
    // static C string in node_natives.h by node_js2c.
    // 'internal_bootstrap_node_native' is the string containing that source code.
    Local<String> script_name = FIXED_ONE_BYTE_STRING(_env->isolate(),
                                                      "bootstrap_node.js");
    Local<Value> f_value = ExecuteString(_env, MainSource(_env), script_name);
    if (try_catch.HasCaught()) {
        ReportException(_env, try_catch);
        exit(10);
    }
    // The bootstrap_node.js file returns a function 'f'
    CHECK(f_value->IsFunction());
    Local<Function> f = Local<Function>::Cast(f_value);

    // Add a reference to the global object
    Local<Object> global = _env->context()->Global();

#if defined HAVE_DTRACE || defined HAVE_ETW
    InitDTrace(_env, global);
#endif

#if defined HAVE_LTTNG
    InitLTTNG(_env, global);
#endif

#if defined HAVE_PERFCTR
    InitPerfCounters(_env, global);
#endif

    // Enable handling of uncaught exceptions
    // (FatalException(), break on uncaught exception in debugger)
    //
    // This is not strictly necessary since it's almost impossible
    // to attach the debugger fast enought to break on exception
    // thrown during process startup.
    try_catch.SetVerbose(true);

    _env->SetMethod(_env->process_object(), "_rawDebug", RawDebug);

    // Expose the global object as a property on itself
    // (Allows you to set stuff on `global` from anywhere in JavaScript.)
    global->Set(FIXED_ONE_BYTE_STRING(_env->isolate(), "global"), global);

    // Now we call 'f' with the 'process' variable that we've built up with
    // all our bindings. Inside bootstrap_node.js and internal/process we'll
    // take care of assigning things to their places.

    // We start the process this way in order to be more modular. Developers
    // who do not like how bootstrap_node.js sets up the module system but do
    // like Node's I/O bindings may want to replace 'f' with their own function.
    Local<Value> arg = _env->process_object();
    f->Call(Null(_env->isolate()), 1, &arg);
}


void JSInstanceImpl::StartNodeInstance() {
    Isolate::CreateParams params;
    ArrayBufferAllocator* array_buffer_allocator = new ArrayBufferAllocator();
    params.array_buffer_allocator = array_buffer_allocator;

#ifdef NODE_ENABLE_VTUNE_PROFILING
    params.code_event_handler = vTune::GetVtuneCodeEventHandler();
#endif

    _isolate = Isolate::New(params);

    {
        Mutex::ScopedLock scoped_lock(node_isolate_mutex);
        if (is_main()) {
            //CHECK_EQ(node_isolate, nullptr);
            node_isolate = _isolate;
        }
    }

    if (track_heap_objects) {
        _isolate->GetHeapProfiler()->StartTrackingHeapObjects(true);
    }

    {
        Locker locker(_isolate);
        Isolate::Scope isolate_scope(_isolate);
        HandleScope handle_scope(_isolate);

        Local<Context> context = Context::New(_isolate);
        _env = CreateEnvironment(_isolate, context, this);

        context->SetAlignedPointerInEmbedderData(kContextJSInstanceDataIndex, this);

        array_buffer_allocator->set_env(_env);
        Context::Scope context_scope(context);

        _isolate->SetAbortOnUncaughtExceptionCallback(ShouldAbortOnUncaughtException);

        // Start debug agent when argv has --debug
        if (use_debug_agent()) {
            const char* path = argc() > 1
                               ? argv()[1]
                               : nullptr;
            StartDebug(_env, path, debug_wait_connect);
            if (use_inspector && !debugger_running) {
                exit(12);
            }
        }

        {
            Environment::AsyncCallbackScope callback_scope(_env);
            LoadEnvironment();
        }

        _env->set_trace_sync_io(trace_sync_io);

        // Enable debugger
        if (use_debug_agent())
            EnableDebug(_env);

        {
            SealHandleScope seal(_isolate);
            bool more;
            do {
                v8_platform.PumpMessageLoop(_isolate);
                more = uv_run(_env->event_loop(), UV_RUN_ONCE) /* && !is_shutdown() */;

                if (is_shutdown()) {
                    //uv_print_active_handles(_env->event_loop(), stderr);

                    uv_walk(_env->event_loop(),
                        [] (uv_handle_t* handle, void* arg) {
                            if (uv_is_closing(handle) == 0 /* && uv_is_active(handle) != 0 */)
                                uv_close(handle, nullptr);
                        }
                    , nullptr);

                    int res = uv_run(_env->event_loop(), UV_RUN_DEFAULT);

                    set_shutdown(false);
                }

                if (more == false) {
                    v8_platform.PumpMessageLoop(_isolate);
                    EmitBeforeExit(_env);

                    // Emit `beforeExit` if the loop became alive either after emitting
                    // event, or after running some callbacks.
                    more = uv_loop_alive(_env->event_loop()) /* && !is_shutdown() */;
                    if (uv_run(_env->event_loop(), UV_RUN_NOWAIT) != 0)
                        more = true; /*  !is_shutdown() */;
                }
            }
            while (more == true);
        }

        _env->set_trace_sync_io(false);

        int exit_code = EmitExit(_env);
        if (is_main())
            set_exit_code(exit_code);
        
        RunAtExit(_env);

        WaitForInspectorDisconnect(_env);

#if defined(LEAK_SANITIZER)
        __lsan_do_leak_check();
#endif

        context->SetAlignedPointerInEmbedderData(kContextJSInstanceDataIndex, nullptr);

        array_buffer_allocator->set_env(nullptr);
        _env->Dispose();
        _env = nullptr;
    }

    {
        Mutex::ScopedLock scoped_lock(node_isolate_mutex);
        if (node_isolate == _isolate)
            node_isolate = nullptr;
    }

    CHECK_NE(_isolate, nullptr);
    _isolate->Dispose();
    _isolate = nullptr;

    delete array_buffer_allocator;
}


JSCRIPT_EXTERN void Initialize(int argc, const wchar_t* const wargv[]) {
    if (is_initilized.exchange(true))
        return;

    PlatformInit();

    CHECK_GT(argc, 0);

    // Convert wargv to to UTF8
    argv = new char*[argc + 1];
    for (int i = 0; i < argc; i++) {
        // Compute the size of the required buffer
        DWORD size = WideCharToMultiByte(CP_UTF8,
                                         0,
                                         wargv[i],
                                         -1,
                                         nullptr,
                                         0,
                                         nullptr,
                                         nullptr);
        if (size == 0) {
            // This should never happen.
            fprintf(stderr, "Could not convert arguments to utf8.");
            exit(1);
        }
        // Do the actual conversion
        argv[i] = new char[size];
        DWORD result = WideCharToMultiByte(CP_UTF8,
                                           0,
                                           wargv[i],
                                           -1,
                                           argv[i],
                                           size,
                                           nullptr,
                                           nullptr);
        if (result == 0) {
            // This should never happen.
            fprintf(stderr, "Could not convert arguments to utf8.");
            exit(1);
        }
    }
    argv[argc] = nullptr;

    // Hack around with the argv pointer. Used for process.title = "blah".
    argv = uv_setup_args(argc, argv);

    // This needs to run *before* V8::Initialize().  The const_cast is not
    // optional, in case you're wondering.
    Init(&argc, const_cast<const char**>(argv), &exec_argc, &exec_argv);

#if HAVE_OPENSSL
#ifdef NODE_FIPS_MODE
    // In the case of FIPS builds we should make sure
    // the random source is properly initialized first.
    OPENSSL_init();
#endif  // NODE_FIPS_MODE
    // V8 on Windows doesn't have a good source of entropy. Seed it from
    // OpenSSL's pool.
    V8::SetEntropySource(crypto::EntropySource);
#endif

    v8_platform.Initialize(v8_thread_pool_size);
    V8::Initialize();
}

JSCRIPT_EXTERN void Initialize(const std::wstring& origin, const std::wstring& externalOrigin) {
    argc = 0;
    std::array<const wchar_t*, 20> wargv;

    //          ƒобавление параметров командной строки

    // добавление пути к исполн€емому файлу
    wargv[argc++] = executeFilePath.c_str();
    CHECK_LT(argc, wargv.size());

    // построение пути расположени€ €дра odant.js
    std::wstring corePath = executeFolderPath;
    std::replace(corePath.begin(), corePath.end(), L'\\', L'/');
    corePath += L"/web/core/odant.js";

    // добавление основного скрипта JSInstance через параметры командной строки
    wargv[argc++] = L"-e";

    const_cast<std::wstring&>(instanceScript) =
        L"'use strict';\r\n"
        L"process.on('uncaughtException', err => {\r\n"
        L"    console.log(err);\r\n"
        L"});\r\n"
        L"process.on('unhandledRejection', err => {\r\n"
        L"    console.log(err);\r\n"
        L"});\r\n";
    
    if (!origin.empty())
        const_cast<std::wstring&>(instanceScript) +=
            L"global.DEFAULTORIGIN = '" + origin + L"';\r\n";

    if (!externalOrigin.empty())
        const_cast<std::wstring&>(instanceScript) +=
            L"global.EXTERNALORIGIN = '" + externalOrigin + L"';\r\n";

    const_cast<std::wstring&>(instanceScript) +=
        L"console.log('Start load framework.');\r\n"
        L"global.odantFramework = require('" + corePath + L"');\r\n"
        L"global.odantFramework.then(core => {\r\n"
        L"  console.log('framework loaded!');\r\n"
#ifdef _DEBUG
        L"  console.log(root.DEFAULTORIGIN);\r\n"
#endif
        L"}).catch((error)=>{\r\n"
        L"  console.log(error);\r\n"
        L"});\r\n"
        L"var infiniteFunction = function() {\r\n"
        L"  setTimeout(function() {\r\n"
        L"      infiniteFunction();\r\n"
        L"  }, 1000);\r\n"
        L"};\r\n"
        L"infiniteFunction();";

    wargv[argc++] = instanceScript.c_str();

    wargv[argc] = { nullptr };
    
    Initialize(argc, wargv.data());
}

JSCRIPT_EXTERN void Uninitilize() {
    if (!is_initilized.exchange(false))
        return;

    V8::Dispose();

    v8_platform.Dispose();

    delete[] exec_argv;
    exec_argv = nullptr;
}

JSCRIPT_EXTERN int RunInstance() {
    JSInstanceImpl instance_data(NodeInstanceType::MAIN,
                                 uv_loop_new(),
                                 argc,
                                 const_cast<const char**>(argv),
                                 exec_argc,
                                 exec_argv,
                                 use_debug_agent);
    instance_data.StartNodeInstance();
    return instance_data.exit_code();
}

JSCRIPT_EXTERN result_t CreateInstance(JSInstance** outNewInstance) {
    JSInstanceImpl* instance = new JSInstanceImpl(NodeInstanceType::MAIN,
                                                  uv_loop_new(),
                                                  argc,
                                                  const_cast<const char**>(argv),
                                                  exec_argc,
                                                  exec_argv,
                                                  use_debug_agent);
    if (instance == nullptr)
        return JS_ERROR;

    instance->_thread = std::thread([=] () { 
                            instance->StartNodeInstance();
                            delete instance;
                        });

    *outNewInstance = instance;

    return JS_SUCCESS;
}

JSCRIPT_EXTERN result_t RunScriptText(const wchar_t* script) {
    return RunScriptText(nullptr, script, { nullptr} );
}
JSCRIPT_EXTERN result_t RunScriptText(const wchar_t* script, JSCallbackInfo* callbacks[]) {
    return RunScriptText(nullptr, script, callbacks);
}
JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance, const wchar_t* script) {
    return RunScriptText(instance, script, { nullptr });
}

struct JSCallBackInfoInternal {
    std::unique_ptr<char> name;
    JSCallback            function = nullptr;
    void*                 external = nullptr;

    JSCallBackInfoInternal() { }
    JSCallBackInfoInternal(JSCallBackInfoInternal && moved) : name(std::move(moved.name)),
                                                              function(moved.function),
                                                              external(moved.external) { }
};

struct JSAsyncInfo {
    std::unique_ptr<char>             script;
    std::list<JSCallBackInfoInternal> callbacks;
    JSInstanceImpl*                   instance;
    uv_async_t                        async_handle;

    static JSAsyncInfo* create() {
        return new JSAsyncInfo();
    }

    void Dispose() {
        delete this; 
    }
};

void _async_stop_instance(uv_async_t* handle) {
    JSAsyncInfo* async_info = ContainerOf(&JSAsyncInfo::async_handle, handle);
    if (async_info == nullptr)
        return;
    
    async_info->instance->set_shutdown(true);
    uv_stop(async_info->instance->event_loop());

    if (uv_is_closing((uv_handle_t*) handle) == 0)
        uv_close((uv_handle_t*) handle,
            [](uv_handle_t* handle){
                JSAsyncInfo* async_info = ContainerOf(&JSAsyncInfo::async_handle, (uv_async_t*) handle);
                if (async_info != nullptr)
                    async_info->Dispose();
            }
        );
}

JSCRIPT_EXTERN result_t StopInstance(JSInstance* instance) {
    if (instance == nullptr)
        return JS_ERROR;
    
    std::unique_ptr<JSAsyncInfo> info(JSAsyncInfo::create());

    info->instance = static_cast<JSInstanceImpl*>(instance);

    int res_init = uv_async_init(info->instance->event_loop(), &info->async_handle, _async_stop_instance);
    CHECK_EQ(0, res_init);

    if (res_init != 0)
        return JS_ERROR;

    uv_unref(reinterpret_cast<uv_handle_t*>(&info->async_handle));

    int sendResult = uv_async_send(&info->async_handle);

    if (sendResult == 0) {
        info.release();
        return JS_SUCCESS;
    }

    return JS_ERROR;

}

void _async_execute_script(uv_async_t* handle) {
    static std::atomic<unsigned int> s_async_id = 0;

    JSAsyncInfo* async_info = ContainerOf(&JSAsyncInfo::async_handle, handle);
    if (async_info == nullptr)
        return;

    node::Mutex::ScopedLock scoped_lock(async_info->instance->_isolate_mutex);
    node::Environment* env = async_info->instance->_env;

    //    v8::Locker locker(async_info->instance->_isolate);

    v8::Isolate::Scope isolate_scope(env->isolate());
    v8::HandleScope scope(env->isolate());

    v8::Local<v8::Context> context = env->context();
    v8::Context::Scope context_scope(context);

    v8::Local<v8::String> source = v8::String::NewFromUtf8(async_info->instance->_isolate, async_info->script.get(), v8::NewStringType::kNormal).ToLocalChecked();
    if (!source.IsEmpty()) {
        v8::TryCatch trycatch(async_info->instance->_isolate);
        trycatch.SetVerbose(false);

        for (auto& callbackInfo : async_info->callbacks) {
            v8::Local<v8::String> name = v8::String::NewFromUtf8(env->isolate(), callbackInfo.name.get(), v8::NewStringType::kInternalized).ToLocalChecked();

            v8::Local<v8::External> external;
            if (callbackInfo.external != nullptr)
                external = v8::External::New(env->isolate(), callbackInfo.external);
            v8::Local<v8::FunctionTemplate> functionTemplate = v8::FunctionTemplate::New(env->isolate(), callbackInfo.function, external /*, signature */ );

            v8::Local<v8::Function> function = functionTemplate->GetFunction(context).ToLocalChecked();
            function->SetName(name);

            v8::Local<v8::Object> global = context->Global();
            global->Set(name, function);
        }

        v8::MaybeLocal<v8::Script> compile_result = v8::Script::Compile(context, source);
        if (trycatch.HasCaught()) {
            v8::Handle<v8::Value> exception = trycatch.Exception();
            v8::String::Utf8Value message(exception);
            std::cerr << "exception: " << *message << std::endl;
            return;
        }

        if (!compile_result.IsEmpty()) {
            v8::Local<v8::Script> script;
            compile_result.ToLocal(&script);

            auto test = compile_result.ToLocalChecked();

            if (!script.IsEmpty()) {
                v8::Handle<v8::Value> result = script->Run();

                if (trycatch.HasCaught()) {
                    v8::Handle<v8::Value> exception = trycatch.Exception();
                    v8::String::Utf8Value message(exception);
                    std::cerr << "exception: " << *message << std::endl;
                }
            }
        }
    }

    if (uv_is_closing((uv_handle_t*) handle) == 0)
        uv_close((uv_handle_t*) handle,
            [](uv_handle_t* handle){
                JSAsyncInfo* async_info = ContainerOf(&JSAsyncInfo::async_handle, (uv_async_t*) handle);
                if (async_info != nullptr)
                    async_info->Dispose();
            }
        );

    //
    //        char cpp_cb_name_buf[256];
    //        sprintf(cpp_cb_name_buf, "cpp_cb_%u", s_async_id.fetch_add(1));
    //        v8::Local<v8::String> cpp_cb_name = v8::String::NewFromUtf8(isolate, cpp_cb_name_buf);
    //
    //        v8::Local<v8::FunctionTemplate> functionTemplate = env->NewFunctionTemplate(test_cpp_cb);
    //        v8::Local<v8::Function> function = functionTemplate->GetFunction(context).ToLocalChecked();
    //        const v8::NewStringType type = v8::NewStringType::kInternalized;
    //        function->SetName(cpp_cb_name);
    //        v8::Local<v8::Object> global = context->Global();
    //
    //        global->Set(cpp_cb_name, function);
    //
    //        v8::Local<v8::Value> require_result = global->Get(context, FIXED_ONE_BYTE_STRING(env->isolate(), "NativeModule")).ToLocalChecked();
    //        v8::Local<v8::String> typeOf = require_result->TypeOf(env->isolate());
    //        v8::String::Utf8Value typeOfStr(typeOf);
    //        const char* qqq = *typeOfStr;
    //
}


JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance, const wchar_t* script, JSCallbackInfo* callbacks[]) {
    if (script == nullptr)
        return JS_ERROR;
    
    std::unique_ptr<JSAsyncInfo> info(JSAsyncInfo::create());

    // Compute the size of the required buffer
    DWORD size = WideCharToMultiByte(CP_UTF8,
                                     0,
                                     script,
                                     -1,
                                     nullptr,
                                     0,
                                     nullptr,
                                     nullptr);
    if (size == 0) {
        // This should never happen.
        return JS_ERROR;
    }
    
    // Do the actual conversion
    info->script.reset(new char[size]);
    DWORD result = WideCharToMultiByte(CP_UTF8,
                                       0,
                                       script,
                                       -1,
                                       info->script.get(),
                                       size,
                                       nullptr,
                                       nullptr);
    if (result == 0) {
        return JS_ERROR;
    }

    if (callbacks != nullptr) {
        for (JSCallbackInfo** callbackPtr = callbacks; *callbackPtr != nullptr; ++callbackPtr) {
            JSCallbackInfo* callback = *callbackPtr;
            if (callback == nullptr)
                break;
            if (callback->name == nullptr)
                break;
            if (callback->function == nullptr)
                break;

            DWORD size = WideCharToMultiByte(CP_UTF8,
                                             0,
                                             callback->name,
                                             -1,
                                             nullptr,
                                             0,
                                             nullptr,
                                             nullptr);
            if (size == 0)
                return JS_ERROR;

            JSCallBackInfoInternal callback_info;

            callback_info.name.reset(new char[size]);
            DWORD result = WideCharToMultiByte(CP_UTF8,
                                               0,
                                               callback->name,
                                               -1,
                                               callback_info.name.get(),
                                               size,
                                               nullptr,
                                               nullptr);
            if (result == 0)
                return JS_ERROR;

            callback_info.external = callback->external;
            callback_info.function = callback->function;
            info->callbacks.push_back(std::move(callback_info));
        }
    }

    if (instance != nullptr)
        info->instance = static_cast<JSInstanceImpl*>(instance);
    else {
        // создание инстанса
        JSInstance* newInstance = nullptr;
        auto createResult = CreateInstance(&newInstance);
        if (createResult != JS_SUCCESS)
            return createResult;
        info->instance = static_cast<JSInstanceImpl*>(newInstance);
    }
    
    int res_init = uv_async_init(info->instance->event_loop(), &info->async_handle, _async_execute_script);
    CHECK_EQ(0, res_init);

    if (res_init != 0)
        return JS_ERROR;

    uv_unref(reinterpret_cast<uv_handle_t*>(&info->async_handle));
    int sendResult = uv_async_send(&info->async_handle);
    
    if (sendResult == 0) {
        info.release();
        return JS_SUCCESS;
    }

    return JS_ERROR;
}


}

void empty_handler(int param) { }

extern "C" {
    BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved) {
        switch (dwReason) {
            case DLL_PROCESS_ATTACH: {
                auto h1 = signal(SIGKILL, empty_handler);
                auto h2 = signal(SIGABRT, empty_handler);
                auto h3 = signal(SIGTERM, empty_handler);

#if defined(_M_X64) && _MSC_VER == 1800
                // отключение AVX дл€ VS 2013 из-за ошибки
                _set_FMA3_enable(0);
#endif
                // ѕолучение пути к исполн€емому файлу
                std::array<wchar_t, MAX_PATH> filePath;
                DWORD length = ::GetModuleFileNameW(NULL, filePath.data(), filePath.size());
                if (length == 0) {
                    // This should never happen.
                    fprintf(stderr, "Could not get file path.");
                    exit(1);
                }
                const_cast<std::wstring&>(jscript::executeFilePath) = std::move(std::wstring(filePath.data(), length));

                std::size_t slashPos = jscript::executeFilePath.find_last_of(L"/\\");
                if (slashPos != std::wstring::npos)
                    const_cast<std::wstring&>(jscript::executeFolderPath) = jscript::executeFilePath.substr(0, slashPos);
                else
                    const_cast<std::wstring&>(jscript::executeFolderPath) = jscript::executeFilePath;

                BOOL res = ::SetEnvironmentVariableW(L"NODE_PATH", (jscript::executeFolderPath + L"\\web\\node_modules").c_str());
                CHECK_NE(res, 0);
            } break;
            
            case DLL_THREAD_ATTACH: { } break;

            case DLL_THREAD_DETACH: { } break;
            
            case DLL_PROCESS_DETACH: {
                jscript::Uninitilize();
            } break;
        }
        return TRUE;
    }
}