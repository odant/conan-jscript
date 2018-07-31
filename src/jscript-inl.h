#pragma once

#include "jscript.h"

#include <cstring>
#include <atomic>
#include <iostream>
#include <thread>
#include <array>
#include <memory>

// For setenv
#ifndef _WIN32
#include <stdlib.h>
#include <errno.h>
#endif

namespace node {
namespace jscript {
using namespace ::node;

const std::string instanceScript;

std::atomic<bool> is_initilized{false};

int    argc = 0;
char** argv = nullptr;

int          exec_argc = 0;
const char** exec_argv = nullptr;


class ExecutorCounter
{
public:
    class ScopeExecute {
    public:
        ScopeExecute() {
            node::Mutex::ScopedLock lock(ExecutorCounter::global()._mutex);
            ++ExecutorCounter::global()._count;
        }

        ~ScopeExecute() {
            node::Mutex::ScopedLock lock(ExecutorCounter::global()._mutex);
            --ExecutorCounter::global()._count;
            ExecutorCounter::global()._cv.Broadcast(lock);
        }
    };

    static ExecutorCounter& global() {
        static ExecutorCounter executorController;
        return executorController;
    }

    void waitAllStop() {
        node::Mutex::ScopedLock lock(_mutex);
        while (_count != 0)
            _cv.Wait(lock);
    }

private:
    node::Mutex             _mutex;
    node::ConditionVariable _cv;
    volatile std::size_t    _count{ 0 };
};

class NodeInstanceData {
public:
    NodeInstanceData(int argc,
                     const char** argv,
                     int exec_argc,
                     const char** exec_argv)
        : exit_code_(1),
          argc_(argc),
          argv_(argv),
          exec_argc_(exec_argc),
          exec_argv_(exec_argv) {
        event_loop_init_ = uv_loop_init(event_loop()) == 0;
        assert(event_loop_init_);
    }

    ~NodeInstanceData() {
        close_loop();
    }

    uv_loop_t* event_loop() {
        return &event_loop_;
    }

    void print_handles() {
        fprintf(stderr, "\r\n%p\r\n", event_loop());
        uv_print_all_handles(event_loop(), stderr);
        fprintf(stderr, "\r\n");
    }

    void close_loop() {
        if (event_loop_init_) {

            while (uv_loop_close(event_loop()) == UV_EBUSY) {
                uv_walk(event_loop(),
                    []
                    (uv_handle_t* handle, void* arg) {
                        if (uv_is_closing(handle) == 0)
                            uv_close(handle, nullptr);
                    }
                , nullptr);

                uv_run(event_loop(), UV_RUN_DEFAULT);
            }

            event_loop_init_ = false;
        }
    }

    int exit_code() {
        return exit_code_;
    }

    void set_exit_code(int exit_code) {
        exit_code_ = exit_code;
    }

    int argc() {
        return argc_;
    }

    const char** argv() {
        return argv_;
    }

    int exec_argc() {
        return exec_argc_;
    }

    const char** exec_argv() {
        return exec_argv_;
    }

private:
    uv_loop_t event_loop_;
    bool      event_loop_init_;

    int exit_code_;
    const int argc_;
    const char** argv_;
    const int exec_argc_;
    const char** exec_argv_;
};


class JSInstanceImpl: public JSInstance,
                      public NodeInstanceData
{
    struct _constructor_tag {};

public:
    typedef std::shared_ptr<JSInstanceImpl> Ptr;
    typedef std::weak_ptr<JSInstanceImpl>   WeakPtr;

    JSInstanceImpl(int argc,
                   const char** argv,
                   int exec_argc,
                   const char** exec_argv,
                   _constructor_tag)
            : NodeInstanceData(argc,
                               argv,
                               exec_argc,
                               exec_argv)
            {}

    static JSInstanceImpl::Ptr create(int argc,
                                      const char** argv,
                                      int exec_argc,
                                      const char** exec_argv) {
        return std::make_shared<JSInstanceImpl>(argc, argv, exec_argc, exec_argv, _constructor_tag());
    }

    void StartNodeInstance();

    Mutex        _isolate_mutex;
    Isolate*     _isolate{ nullptr };
    Environment* _env{ nullptr };
    std::thread  _thread;

    bool isStopping() const {
        return _state == STOPPING;
    }

    void stopping() {
        _state = STOPPING;
    }

private:

    enum state_t {
        CREATE,
        START,
        STOPPING,
        STOP
    };

    std::atomic<state_t> _state{ CREATE };
};


void JSInstanceImpl::StartNodeInstance() {
    std::unique_ptr<ArrayBufferAllocator, decltype(&FreeArrayBufferAllocator)>
        allocator(CreateArrayBufferAllocator(), &FreeArrayBufferAllocator);
    _isolate = NewIsolate(allocator.get());
    if (_isolate == nullptr)
        return;  // Signal internal error.

    {
        Mutex::ScopedLock scoped_lock(node_isolate_mutex);
        if (node_isolate == nullptr)
            node_isolate = _isolate;
    }

    {
        Locker locker(_isolate);
        Isolate::Scope isolate_scope(_isolate);
        HandleScope handle_scope(_isolate);
        std::unique_ptr<IsolateData, decltype(&FreeIsolateData)> isolate_data(
            CreateIsolateData(
                _isolate,
                event_loop(),
                v8_platform.Platform(),
                allocator.get()),
            &FreeIsolateData);
        
        if (track_heap_objects) {
            _isolate->GetHeapProfiler()->StartTrackingHeapObjects(true);
        }

        Local<Context> context = NewContext(_isolate);
        Context::Scope context_scope(context);
        Environment env(isolate_data.get(), context, v8_platform.GetTracingAgent());
        env.Start(argc(), argv(), exec_argc(), exec_argv(), v8_is_profiling);
        _env = &env;

        char name_buffer[512];
        if (uv_get_process_title(name_buffer, sizeof(name_buffer)) == 0) {
            // Only emit the metadata event if the title can be retrieved successfully.
            // Ignore it otherwise.
            TRACE_EVENT_METADATA1("__metadata", "process_name", "name",
                TRACE_STR_COPY(name_buffer));
        }
        TRACE_EVENT_METADATA1("__metadata", "version", "node", NODE_VERSION_STRING);
        TRACE_EVENT_METADATA1("__metadata", "thread_name", "name",
            "JavaScriptMainThread");

        // создается асинхронных хендл один на все экземпл¤ры Environment из-за одинакового threadId == 0
        // его принудительна¤ остановка приводит к ошибке, Environment считает что это дочерний Worker

        //const char* path = argc() > 1 ? argv()[1] : nullptr;
        //StartInspector(&env, path, debug_options);
        //if (debug_options.inspector_enabled() && !v8_platform.InspectorStarted(&env))
        //    return;  // Signal internal error.

        env.set_abort_on_uncaught_exception(abort_on_uncaught_exception);

        if (no_force_async_hooks_checks) {
            env.async_hooks()->no_force_checks();
        }

        {
            Environment::AsyncCallbackScope callback_scope(&env);
            env.async_hooks()->push_async_ids(1, 0);
            LoadEnvironment(&env);
            env.async_hooks()->pop_async_id(1);
        }

        env.set_trace_sync_io(trace_sync_io);

        {
            SealHandleScope seal(_isolate);
            bool more;
            env.performance_state()->Mark(
                node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_START);
            do {
                uv_run(env.event_loop(), UV_RUN_DEFAULT);

                v8_platform.DrainVMTasks(_isolate);

                more = uv_loop_alive(env.event_loop()) && !isStopping();
                if (more)
                    continue;

                RunBeforeExit(&env);

                // Emit `beforeExit` if the loop became alive either after emitting
                // event, or after running some callbacks.
                more = uv_loop_alive(env.event_loop()) && !isStopping();
            } while (more == true);
            env.performance_state()->Mark(
                node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_EXIT);
        }
        env.set_trace_sync_io(false);

        const int exit_code = EmitExit(&env);

        // Отключено синхронно с StartInspector
        //WaitForInspectorDisconnect(&env);

        env.set_can_call_into_js(false);
        env.stop_sub_worker_contexts();
        uv_tty_reset_mode();
        env.RunCleanup();
        RunAtExit(&env);

        v8_platform.DrainVMTasks(_isolate);
        v8_platform.CancelVMTasks(_isolate);
#if defined(LEAK_SANITIZER)
        __lsan_do_leak_check();
#endif        

        set_exit_code(exit_code);

        _env = nullptr;
    }

    close_loop();

    {
        Mutex::ScopedLock scoped_lock(node_isolate_mutex);
        if (node_isolate == _isolate)
            node_isolate = nullptr;
    }

    _isolate->Dispose();
    _isolate = nullptr;
}


JSCRIPT_EXTERN void Initialize(int argc, const char* const argv_[]) {
    if (is_initilized.exchange(true))
        return;

    atexit([]() { uv_tty_reset_mode(); });
    PlatformInit();
    performance::performance_node_start = PERFORMANCE_NOW();

    CHECK_GT(argc, 0);

    // Copy args, use continuous memory
    // See deps/uv/src/unix/proctitle.c:53: uv_setup_args: Assertion `process_title.len + 1 == size' failed.
    std::size_t args_len = 0;
    for (int i = 0; i < argc; i++) {
        args_len += std::strlen(argv_[i]) + 1;
    }
    char** argv = new char*[argc];
    char* args_ptr = new char[args_len];
    for (int i = 0; i < argc; i++) {
        argv[i] = args_ptr;
        std::strcpy(argv[i], argv_[i]);
        args_ptr += std::strlen(argv_[i]) + 1; // +1 for null-terminate
    }

    // Hack around with the argv pointer. Used for process.title = "blah".
    argv = uv_setup_args(argc, argv);

    // This needs to run *before* V8::Initialize().  The const_cast is not
    // optional, in case you're wondering.
    int exec_argc;
    const char** exec_argv;
    Init(&argc, const_cast<const char**>(argv), &exec_argc, &exec_argv);

#if HAVE_OPENSSL
    {
        std::string extra_ca_certs;
        if (SafeGetenv("NODE_EXTRA_CA_CERTS", &extra_ca_certs))
            crypto::UseExtraCaCerts(extra_ca_certs);
    }
#ifdef NODE_FIPS_MODE
    // In the case of FIPS builds we should make sure
    // the random source is properly initialized first.
    OPENSSL_init();
#endif  // NODE_FIPS_MODE
    // V8 on Windows doesn't have a good source of entropy. Seed it from
    // OpenSSL's pool.
    V8::SetEntropySource(crypto::EntropySource);
#endif  // HAVE_OPENSSL

    v8_platform.Initialize(v8_thread_pool_size);
    V8::Initialize();
    performance::performance_v8_start = PERFORMANCE_NOW();
    v8_initialized = true;
}

void empty_handler(int param) { }

JSCRIPT_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               const std::string& executeFile, const std::string& coreFolder, const std::string& nodeFolder) {

    auto h1 = signal(SIGKILL, empty_handler);
    auto h2 = signal(SIGABRT, empty_handler);
    auto h3 = signal(SIGTERM, empty_handler);

#if defined(_M_X64) && _MSC_VER == 1800
                // Disable AVX for MSVC 2013
                _set_FMA3_enable(0);
#endif

    // Path to node modules
#ifdef _WIN32
    const int nodeFolderW_len = ::MultiByteToWideChar(CP_UTF8, NULL, nodeFolder.c_str(), nodeFolder.length(), NULL, 0);
    auto nodeFolderW = std::make_unique<wchar_t[]>(nodeFolderW_len + 1); // +1 for null-terminate
    ::MultiByteToWideChar(CP_UTF8, NULL, nodeFolder.c_str(), nodeFolder.length(), nodeFolderW.get(), nodeFolderW_len);
    BOOL res = ::SetEnvironmentVariableW(L"NODE_PATH", nodeFolderW.get());
    CHECK_NE(res, 0);
#else
    if (setenv("NODE_PATH", nodeFolder.c_str(), 1) != 0) {
        std::cerr << "Set NODE_PATH failed, errno: " << errno << ", exit." << std::endl;
        exit(EXIT_FAILURE);
    }
#endif

    int argc = 0;
    std::array<const char*, 20> argv;

    // Path to executable file
    argv[argc++] = executeFile.c_str();
    CHECK_LT(argc, argv.size());

    // Add main script JSInstance
    argv[argc++] = "-e";

    // Path to odant.js
    const std::string coreScript = coreFolder + "/web/core/odant.js";
    const_cast<std::string&>(instanceScript) =
        "'use strict';\r\n"
        "process.on('uncaughtException', err => {\r\n"
        "    console.log(err);\r\n"
        "});\r\n"
        "process.on('unhandledRejection', err => {\r\n"
        "    console.log(err);\r\n"
        "});\r\n";

    if (!origin.empty())
        const_cast<std::string&>(instanceScript) +=
            "global.DEFAULTORIGIN = '" + origin + "';\r\n";

    if (!externalOrigin.empty())
        const_cast<std::string&>(instanceScript) +=
            "global.EXTERNALORIGIN = '" + externalOrigin + "';\r\n";

    const_cast<std::string&>(instanceScript) +=
        "console.log('Start load framework.');\r\n"
        "global.odantFramework = require('" + coreScript + "');\r\n"
        "global.odantFramework.then(core => {\r\n"
        "  console.log('framework loaded!');\r\n"
#ifdef _DEBUG
        "  console.log(root.DEFAULTORIGIN);\r\n"
#endif
        "}).catch((error)=>{\r\n"
        "  console.log(error);\r\n"
        "});\r\n"
        "var infiniteFunction = function() {\r\n"
        "  setTimeout(function() {\r\n"
        "      infiniteFunction();\r\n"
        "  }, 1000);\r\n"
        "};\r\n"
        "infiniteFunction();";

    argv[argc++] = instanceScript.c_str();

    Initialize(argc, argv.data());
}

JSCRIPT_EXTERN void Uninitilize() {
    if (!is_initilized.exchange(false))
        return;

    ExecutorCounter::global().waitAllStop();

    v8_platform.StopTracingAgent();
    v8_initialized = false;
    V8::Dispose();

    // uv_run cannot be called from the time before the beforeExit callback
    // runs until the program exits unless the event loop has any referenced
    // handles after beforeExit terminates. This prevents unrefed timers
    // that happen to terminate during shutdown from being run unsafely.
    // Since uv_run cannot be called, uv_async handles held by the platform
    // will never be fully cleaned up.
    v8_platform.Dispose();

    delete[] exec_argv;
    exec_argv = nullptr;
}

JSCRIPT_EXTERN int RunInstance() {
    ExecutorCounter::ScopeExecute scopeExecute;

    JSInstanceImpl::Ptr instance = JSInstanceImpl::create(argc,
                                                          const_cast<const char**>(argv),
                                                          exec_argc,
                                                          exec_argv);
    instance->StartNodeInstance();
    return instance->exit_code();
}

JSCRIPT_EXTERN result_t CreateInstance(JSInstance** outNewInstance) {
    JSInstanceImpl::Ptr instance = JSInstanceImpl::create(argc,
                                                          const_cast<const char**>(argv),
                                                          exec_argc,
                                                          exec_argv);
    if (!instance)
        return JS_ERROR;

    instance->_thread = std::thread(
        [instance]
        () {
            ExecutorCounter::ScopeExecute scopeExecute;
            instance->StartNodeInstance();
        }
    );

    *outNewInstance = instance.get();

    return JS_SUCCESS;
}

JSCRIPT_EXTERN result_t RunScriptText(const char* script) {
    return RunScriptText(nullptr, script, nullptr);
}
JSCRIPT_EXTERN result_t RunScriptText(const char* script, JSCallbackInfo* callbacks[]) {
    return RunScriptText(nullptr, script, callbacks);
}
JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance, const char* script) {
    return RunScriptText(instance, script, nullptr);
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
    if (async_info) {
        async_info->instance->stopping();
        uv_stop(async_info->instance->event_loop());
    }

    if (uv_is_closing((uv_handle_t*) handle) == 0)
        uv_close((uv_handle_t*) handle,
            []
            (uv_handle_t* handle){
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
    static std::atomic<unsigned int> s_async_id{0};

    JSAsyncInfo* async_info = ContainerOf(&JSAsyncInfo::async_handle, handle);
    if (async_info) {
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
                v8::Local<v8::FunctionTemplate> functionTemplate = v8::FunctionTemplate::New(env->isolate(), callbackInfo.function, external /*, signature */);

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
            }
            else if (!compile_result.IsEmpty()) {
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


JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance, const char* script, JSCallbackInfo* callbacks[]) {
    if (script == nullptr)
        return JS_ERROR;

    std::unique_ptr<JSAsyncInfo> info(JSAsyncInfo::create());

    // Copy script
    const std::size_t len = std::strlen(script);
    info->script.reset(new char[len + 1]); // +1 for null-terminate
    std::strcpy(info->script.get(), script);

    if (callbacks != nullptr) {
        for (JSCallbackInfo** callbackPtr = callbacks; *callbackPtr != nullptr; ++callbackPtr) {
            JSCallbackInfo* callback = *callbackPtr;
            if (callback == nullptr)
                break;
            if (callback->name == nullptr)
                break;
            if (callback->function == nullptr)
                break;

            JSCallBackInfoInternal callback_info;

            // Copy name
            const std::size_t len = std::strlen(callback->name);
            callback_info.name.reset(new char[len + 1]); // +1 for null-terminate
            std::strcpy(callback_info.name.get(), callback->name);

            callback_info.external = callback->external;
            callback_info.function = callback->function;
            info->callbacks.push_back(std::move(callback_info));
        }
    }

    if (instance != nullptr)
        info->instance = static_cast<JSInstanceImpl*>(instance);
    else {
        // Create instance
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

}   // namespace jscript
}   // namsepace node

