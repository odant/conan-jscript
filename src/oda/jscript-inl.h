#pragma once

#include "jscript.h"
#include "node_errors.h"
#include "node_internals.h"

#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <cstdio>
#include <algorithm>

#include <mutex>
#include <condition_variable>
#include <chrono>

namespace node {
namespace jscript {
using namespace ::node;

std::atomic<bool> is_initilized{false};

std::vector<std::string> args;
std::vector<std::string> exec_args;
std::vector<std::string> errors;

class ExecutorCounter {
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
    while (_count != 0) _cv.Wait(lock);
  }

 private:
  node::Mutex _mutex;
  node::ConditionVariable _cv;
  std::atomic_size_t _count{0};
};

class RefCounter {
 public:
  RefCounter() = default;

  friend inline void addRefecence(const RefCounter* p);
  friend inline void releaseRefecence(const RefCounter* p);

  template <typename Derived,
            typename = typename std::enable_if<
                std::is_convertible<Derived*, RefCounter*>::value,
                void>::type>
  class Ptr {
   public:
    Ptr() : _p(nullptr) {}
    Ptr(Derived* p) : _p(p) { addRefecence(_p); }
    Ptr(Ptr const& other) : _p(other._p) { addRefecence(_p); }
    Ptr(Ptr&& other) : _p(other._p) { other._p = nullptr; }

    Ptr& operator=(Ptr const& other) {
      reset(other._p);
      return *this;
    }
    Ptr& operator=(Ptr&& other) {
      _p = other._p;
      other._p = nullptr;
      return *this;
    }

    ~Ptr() { releaseRefecence(_p); }

    void reset() {
      releaseRefecence(_p);
      _p = nullptr;
    }

    void reset(Derived* p) {
      releaseRefecence(_p);
      _p = p;
      addRefecence(_p);
    }

    void adopt(Derived* p) {
      releaseRefecence(_p);
      _p = p;
    }

    Derived* get() const { return _p; }

    Derived* detach() {
      Derived* p = _p;
      _p = nullptr;
      return p;
    }

    Derived& operator*() const {
      assert(_p != nullptr);
      return *_p;
    }

    Derived* operator->() const { return _p; }

    explicit operator bool() const { return _p != nullptr; }

   private:
    Derived* _p;
  };

 protected:
  virtual ~RefCounter() = default;

 private:
  RefCounter(const RefCounter&) = delete;
  RefCounter& operator=(const RefCounter&) = delete;

  inline void addRef() const { ++_refCount; }

  inline std::uint32_t releaseRef() const { return --_refCount; }

  mutable std::atomic<std::uint32_t> _refCount = ATOMIC_VAR_INIT(0);
};

inline void addRefecence(const RefCounter* p) {
  if (p != nullptr) p->addRef();
}

inline void releaseRefecence(const RefCounter* p) {
  if (p != nullptr) {
    const auto refCount = p->releaseRef();
    if (refCount == 0) delete p;
  }
}

class NodeInstanceData : public RefCounter {
 public:
  NodeInstanceData() : exit_code_(1) {
    event_loop_init_ = uv_loop_init(event_loop()) == 0;
    assert(event_loop_init_);
  }

  ~NodeInstanceData() { close_loop(); }

  uv_loop_t* event_loop() { return &event_loop_; }

  void print_handles() {
    fprintf(stderr, "\r\n%p\r\n", event_loop());
    uv_print_all_handles(event_loop(), stderr);
    fprintf(stderr, "\r\n");
  }

  void close_loop() {
    if (event_loop_init_) {
      while (uv_loop_close(event_loop()) == UV_EBUSY) {
        uv_walk(event_loop(),
                [](uv_handle_t* handle, void* arg) {
                  if (uv_is_closing(handle) == 0) uv_close(handle, nullptr);
                },
                nullptr);

        uv_run(event_loop(), UV_RUN_DEFAULT);
      }

      event_loop_init_ = false;
    }
  }

  int exit_code() { return exit_code_; }

  void set_exit_code(int exit_code) { exit_code_ = exit_code; }

 protected:

  std::unique_ptr<IsolateData> isolate_data_;
  const bool deserialize_mode_ = false;


 private:
  uv_loop_t event_loop_;
  bool event_loop_init_;

  int exit_code_;
};

class JSInstanceImpl : public JSInstance, public NodeInstanceData {
  struct _constructor_tag {};

 public:
  using Ptr = RefCounter::Ptr<JSInstanceImpl>;
#ifdef ERROR
  #undef ERROR
#endif
  enum state_t { CREATE, RUN, STOPPING, STOP, TIMEOUT, ERROR };
  using AutoResetState = std::unique_ptr<void, std::function<void(void*)>>;
  AutoResetState createAutoReset(state_t);

  JSInstanceImpl(_constructor_tag) : NodeInstanceData() {}

  static JSInstanceImpl::Ptr create() {
    return JSInstanceImpl::Ptr(new JSInstanceImpl(_constructor_tag()));
  }

  void StartNodeInstance();
  void SetLogCallback(JSLogCallback& cb);

  static const std::string defaultOrigin;
  static const std::string externalOrigin;

  Mutex _isolate_mutex;
  Isolate* _isolate{nullptr};
  Environment* _env{nullptr};
  std::thread _thread;

  bool isStopping() const {
    const bool envStopped = !(_env && _env->is_stopping());
    return _state == STOPPING || envStopped;
  }

  bool isRun() const {
    const bool envNotStopped = _env && !(_env->is_stopping());
    return _state == RUN && envNotStopped;
  }

  bool isInitialize() const { return _state != CREATE; }

  std::mutex _state_mutex;
  std::condition_variable _state_cv;

  void setState(state_t state) {
    _state = state;
    _state_cv.notify_all();
  }

  JSLogCallback& logCallback() { return _logCallback; }

 private:
  std::unique_ptr<Environment> CreateEnvironment(int*);
  void addSetStates(v8::Local<v8::Context> context);
  void addSetState(v8::Local<v8::Context> context, const char* name, state_t state);
  void addGlobalStringValue(v8::Local<v8::Context> context, const std::string& name, const std::string& value);
  void overrideConsole(Environment* env);
  void overrideConsole(Environment* env,
                       const char* name,
                       const JSLogType type);

  std::atomic<state_t> _state = ATOMIC_VAR_INIT(CREATE);

  JSLogCallback _logCallback;
};

const std::string JSInstanceImpl::defaultOrigin;
const std::string JSInstanceImpl::externalOrigin;

void JSInstanceImpl::SetLogCallback(JSLogCallback& cb) {
  _logCallback = std::move(cb);
}

void JSInstanceImpl::overrideConsole(Environment* env) {
  overrideConsole(env, u8"log", JSLogType::LOG_TYPE);
  overrideConsole(env, u8"warn", JSLogType::WARN_TYPE);
  overrideConsole(env, u8"error", JSLogType::ERROR_TYPE);
}

void JSInstanceImpl::overrideConsole(Environment* env,
                                     const char* name,
                                     const JSLogType type) {
  v8::HandleScope handle_scope(env->isolate());
  v8::TryCatch try_catch(env->isolate());
  try_catch.SetVerbose(true);
  Local<Object> global = env->context()->Global();
  if (global.IsEmpty()) {
    return;
  }

  v8::Local<v8::String> consoleName =
    v8::String::NewFromUtf8(env->isolate(), u8"console", v8::NewStringType::kNormal).ToLocalChecked();
  if (consoleName.IsEmpty()) {
    return;
  }
  v8::Local<v8::String> functionName =
    v8::String::NewFromUtf8(env->isolate(), name, v8::NewStringType::kNormal).ToLocalChecked();
  if (functionName.IsEmpty()) {
    return;
  }

  v8::Local<v8::Value> globalConsole =
    global->Get(env->context(), consoleName).ToLocalChecked();
  if (globalConsole.IsEmpty() || !globalConsole->IsObject()) {
    return;
  }
  v8::Local<v8::Object> globalConsoleObj = globalConsole.As<v8::Object>();
  if (globalConsoleObj.IsEmpty()) {
    return;
  }

  v8::Local<v8::Value> globalFunctionValue =
    globalConsoleObj->Get(env->context(), functionName).ToLocalChecked();
  if (globalFunctionValue.IsEmpty() || !globalFunctionValue->IsFunction()) {
    return;
  }
  v8::Local<v8::Function> globalFunction =
    globalFunctionValue.As<v8::Function>();
  if (globalFunction.IsEmpty()) {
    return;
  }

  //TODO: add check re-override control

  v8::Local<v8::External> instanceExt =
    v8::External::New(env->isolate(), this);
  if (instanceExt.IsEmpty()) {
    return;
  }

  v8::Local<v8::External> typeExt =
    v8::External::New(env->isolate(),
                      reinterpret_cast<void*>(static_cast<std::size_t>(type)));
  if (typeExt.IsEmpty()) {
    return;
  }

  v8::Local<v8::Array> array = v8::Array::New(env->isolate(), 3);
  if (array.IsEmpty()) {
    return;
  }

  array->Set(env->context(), 0, globalFunction).Check();
  array->Set(env->context(), 1, instanceExt).Check();
  array->Set(env->context(), 2, typeExt).Check();

  const auto callback = [](const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);

    v8::Local<v8::Value> data = args.Data();
    if (data.IsEmpty() || !data->IsArray()) {
      return;
    }
    v8::Local<v8::Array> array = data.As<v8::Array>();
    if (array.IsEmpty()) {
      return;
    }
    if (array->Length() < 3) {
      return;
    }

    v8::Local<v8::Context> context = array->CreationContext();

    v8::Local<v8::Function> globalLogFunc = array->Get(context, 0).ToLocalChecked().As<v8::Function>();
    if (globalLogFunc.IsEmpty()) {
      return;
    }

    v8::TryCatch trycatch(isolate);
    trycatch.SetVerbose(true);

    std::unique_ptr<v8::Local<v8::Value>[]> info{
        new v8::Local<v8::Value>[args.Length()]
    };

    for (int i = 0; i < args.Length(); ++i) {
      info[i] = args[i];
    }

    globalLogFunc->Call(isolate->GetCurrentContext(),
      v8::Null(isolate),
      args.Length(),
      info.get());

    v8::Local<v8::External> instanceExt = array->Get(context, 1).ToLocalChecked().As<v8::External>();
    if (instanceExt.IsEmpty()) {
      return;
    }
    JSInstanceImpl* instance = reinterpret_cast<JSInstanceImpl*>(instanceExt->Value());
    if (!instance) {
      return;
    }

    v8::Local<v8::External> typeExt = array->Get(context, 2).ToLocalChecked().As<v8::External>();
    if (typeExt.IsEmpty()) {
      return;
    }
    JSLogType type = static_cast<JSLogType>(reinterpret_cast<std::size_t>(typeExt->Value()));

    auto& logCallback = instance->logCallback();
    if (logCallback) {
      logCallback(args, type);
    }
  };

  v8::Local<v8::Function> overrideFunction =
    v8::Function::New(env->context(),
                      callback,
                      array
  ).ToLocalChecked();

  globalConsoleObj->Set(
    env->context(),
    functionName,
    overrideFunction
  ).Check();

  v8::Local<v8::String> odantFrameworkName = v8::String::NewFromUtf8(
      env->isolate(), u8"odantFramework", v8::NewStringType::kNormal).ToLocalChecked();
  v8::Local<v8::Value> odantFramework =
      global->Get(env->context(), odantFrameworkName).ToLocalChecked();
  if (odantFramework.IsEmpty() || !odantFramework->IsObject()) {
    return;
  }
  v8::Local<v8::Object> odantFrameworkObj = odantFramework.As<v8::Object>();
  if (odantFrameworkObj.IsEmpty()) {
    return;
  }
  v8::Local<v8::Value> odantFrameworkConsole =
      odantFrameworkObj->Get(env->context(), consoleName).ToLocalChecked();
  if (odantFrameworkConsole.IsEmpty() || !odantFrameworkConsole->IsObject()) {
    v8::Local<v8::Object> consoleObj = v8::Object::New(env->isolate());
    if (consoleObj.IsEmpty()) {
      return;
    }
    if (!odantFrameworkObj->Set(env->context(), consoleName, consoleObj).ToChecked()) {
      return;
    }
    odantFrameworkConsole =
      odantFrameworkObj->Get(env->context(), consoleName).ToLocalChecked();
    if (odantFrameworkConsole.IsEmpty() || !odantFrameworkConsole->IsObject()) {
      return;
    }
  }

  v8::Local<v8::Object> odantFrameworkconsoleObj = odantFrameworkConsole.As<v8::Object>();
  if (odantFrameworkconsoleObj.IsEmpty()) {
    return;
  }

  v8::Local<v8::Value> function =
    odantFrameworkconsoleObj->Get(env->context(), functionName).ToLocalChecked();
  if (function.IsEmpty() || !function->IsFunction()) {
    odantFrameworkconsoleObj->Set(env->context(),
                    functionName,
                    overrideFunction
    ).Check();
  }
}

JSInstanceImpl::AutoResetState JSInstanceImpl::createAutoReset(state_t state) {
  const auto deleter = [that = JSInstanceImpl::Ptr{ this }, state](void*) {
    that->setState(state);
  };

  return AutoResetState{ this, deleter };
}

void JSInstanceImpl::StartNodeInstance() {
  auto autoResetState = createAutoReset(state_t::STOP);

  v8::Isolate::CreateParams params;
  auto allocator = ArrayBufferAllocator::Create();
  MultiIsolatePlatform* platform = per_process::v8_platform.Platform();

  // Following code from NodeMainInstance::NodeMainInstance

  params.array_buffer_allocator = allocator.get();
  _isolate = Isolate::Allocate();
  CHECK_NOT_NULL(_isolate);
  // Register the isolate on the platform before the isolate gets initialized,
  // so that the isolate can access the platform during initialization.
  platform->RegisterIsolate(_isolate, event_loop());
  SetIsolateCreateParamsForNode(&params);
  Isolate::Initialize(_isolate, params);

  // deserialize_mode_ = per_isolate_data_indexes != nullptr;
  // If the indexes are not nullptr, we are not deserializing
  //CHECK_IMPLIES(deserialize_mode_, params.external_references != nullptr);
  {
    // ctor IsolateData call Isolate::GetCurrent, need enter
    v8::Locker locker{ _isolate };
    isolate_data_ = std::make_unique<IsolateData>(
      _isolate, event_loop(), platform, allocator.get());
  }

  IsolateSettings s;
  SetIsolateMiscHandlers(_isolate, s);
  if (!deserialize_mode_) {
    // If in deserialize mode, delay until after the deserialization is
    // complete.
    SetIsolateErrorHandlers(_isolate, s);
  }

  // Following code from NodeMainInstance::Run

  int exit_code = 0;
  {
    v8::Locker locker(_isolate);
    v8::Isolate::Scope isolate_scope(_isolate);
    v8::HandleScope handle_scope(_isolate);

    auto env = this->CreateEnvironment(&exit_code);
    _env = env.get();

    CHECK_NOT_NULL(env);
    v8::Context::Scope context_scope(env->context());
    if (exit_code == 0) {
      LoadEnvironment(env.get());
      this->overrideConsole(env.get());

      env->set_trace_sync_io(env->options()->trace_sync_io);

      {
        v8::SealHandleScope seal(_isolate);
        bool more;
        env->performance_state()->Mark(node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_START);

        do {
          uv_run(env->event_loop(), UV_RUN_DEFAULT);

          per_process::v8_platform.DrainVMTasks(_isolate);


          more = uv_loop_alive(env->event_loop());
          if (more && !env->is_stopping()) {
            continue;
          }

          if (!uv_loop_alive(env->event_loop())) {
            EmitBeforeExit(env.get());
          }

          // Emit `beforeExit` if the loop became alive either after emitting
          // event, or after running some callbacks.
          more = uv_loop_alive(env->event_loop());
        } while (more == true && !env->is_stopping());

        env->performance_state()->Mark(node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_EXIT);
        setState(JSInstanceImpl::STOP);
      }

      env->set_trace_sync_io(false);
      exit_code = EmitExit(env.get());
    }

    env->set_can_call_into_js(false);
    env->stop_sub_worker_contexts();
    ResetStdio();
    env->RunCleanup();

    RunAtExit(env.get());

    per_process::v8_platform.DrainVMTasks(_isolate);

#if defined(LEAK_SANITIZER)
    __lsan_do_leak_check();
#endif
  }
  set_exit_code(exit_code);
  _env = nullptr;

  per_process::v8_platform.Platform()->UnregisterIsolate(_isolate);
  _isolate->Dispose();

  _isolate = nullptr;
  close_loop();
}

  std::unique_ptr<Environment> JSInstanceImpl::CreateEnvironment(
      int* exit_code) {
    *exit_code = 0;  // Reset the exit code to 0

    v8::HandleScope handle_scope(_isolate);

    // TODO(addaleax): This should load a real per-Isolate option, currently
    // this is still effectively per-process.
    if (isolate_data_->options()->track_heap_objects) {
      _isolate->GetHeapProfiler()->StartTrackingHeapObjects(true);
    }

    const size_t kNodeContextIndex = 0;
    v8::Local<v8::Context> context;
    if (deserialize_mode_) {
      context = v8::Context::FromSnapshot(_isolate, kNodeContextIndex).ToLocalChecked();
      InitializeContextRuntime(context);
      SetIsolateErrorHandlers(_isolate, {});
    }
    else {
      context = NewContext(_isolate);
    }
    CHECK(!context.IsEmpty());

    v8::Context::Scope context_scope(context);

    std::unique_ptr<Environment> env = std::make_unique<Environment>(
        isolate_data_.get(),
        context,
        args,
        exec_args,
        static_cast<Environment::Flags>(Environment::kIsMainThread |
                                        Environment::kOwnsProcessState |
                                        Environment::kOwnsInspector));
    env->InitializeLibuv(per_process::v8_is_profiling);
    env->InitializeDiagnostics();

    addSetStates(context);

    const std::string defaultOriginName{ "DEFAULTORIGIN" };
    addGlobalStringValue(context, defaultOriginName, defaultOrigin);

    const std::string externalOriginName{ "EXTERNALORIGIN" };
    addGlobalStringValue(context, externalOriginName, externalOrigin);

    // TODO(joyeecheung): when we snapshot the bootstrapped context,
    // the inspector and diagnostics setup should after after deserialization.
#if HAVE_INSPECTOR
    //*exit_code = env->InitializeInspector({});
#endif
    if (*exit_code != 0) {
      return env;
    }

    if (env->RunBootstrapping().IsEmpty()) {
      *exit_code = 1;
    }

    return env;
  }

void JSInstanceImpl::addSetStates(v8::Local<v8::Context> context)
{
  static const char* __oda_setRunState{ "__oda_setRunState" };
  addSetState(context, __oda_setRunState, JSInstanceImpl::RUN);
  static const char* __oda_setErrorState{ "__oda_setErrorState" };
  addSetState(context, __oda_setErrorState, JSInstanceImpl::ERROR);
}

void JSInstanceImpl::addSetState(v8::Local<v8::Context> context, const char* name, state_t state) {
    v8::Local<v8::Object> global = context->Global();
    CHECK(!global.IsEmpty());

    v8::Local<v8::String> functionName = v8::String::NewFromUtf8(_isolate, name, v8::NewStringType::kNormal).ToLocalChecked();

    const auto callback = [](const v8::FunctionCallbackInfo<v8::Value>& args) -> void {
          v8::Isolate* isolate = args.GetIsolate();
          v8::HandleScope handle_scope(isolate);

          v8::Local<v8::Value> data = args.Data();
          if (data.IsEmpty() || !data->IsArray()) {
              return;
          }
          v8::Local<v8::Array> array = data.As<v8::Array>();
          if (array.IsEmpty()) {
              return;
          }
          if (array->Length() < 2) {
              return;
          }

          v8::Local<v8::Context> context = array->CreationContext();

          v8::Local<v8::External> instanceExt = array->Get(context, 0).ToLocalChecked().As<v8::External>();
          if (instanceExt.IsEmpty()) {
              return;
          }

          JSInstanceImpl* instance = reinterpret_cast<JSInstanceImpl*>(instanceExt->Value());
          if (!instance) {
              return;
          }

          v8::Local<v8::Int32> stateCode = array->Get(context, 1).ToLocalChecked().As<v8::Int32>();
          if (stateCode.IsEmpty()) {
              return;
          }

          instance->setState(static_cast<JSInstanceImpl::state_t>(stateCode-> Value()));
    };  // callback

    v8::Local<v8::External> instanceExt = v8::External::New(_isolate, this);
    v8::Local<v8::Int32> stateCode = v8::Integer::New(_isolate, static_cast<int32_t>(state))->ToInt32(context).ToLocalChecked();
    v8::Local<v8::Array> array = v8::Array::New(_isolate, 2);
    array->Set(context, 0, instanceExt).Check();
    array->Set(context, 1, stateCode).Check();

    v8::MaybeLocal<v8::Function> setRunStateFunction = v8::Function::New(context, callback, array);
    global->Set(context, functionName, setRunStateFunction.ToLocalChecked()).Check();
}

void JSInstanceImpl::addGlobalStringValue(v8::Local<v8::Context> context, const std::string& name, const std::string& value) {
  v8::Local<v8::Object> global = context->Global();
  CHECK(!global.IsEmpty());

  v8::Local<v8::String> stringName = v8::String::NewFromUtf8(_isolate, name.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
  v8::Local<v8::String> stringValue = v8::String::NewFromUtf8(_isolate, value.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
  global->Set(context, stringName, stringValue).Check();
}

JSCRIPT_EXTERN void Initialize(int argc, const char** argv) {
  if (is_initilized.exchange(true)) return;

  // Initialized the enabled list for Debug() calls with system
  // environment variables.
  per_process::enabled_debug_list.Parse(nullptr);

  atexit(ResetStdio);
  PlatformInit();

  CHECK_GT(argc, 0);

  // Hack around with the argv pointer. Used for process.title = "blah".
  // argv = uv_setup_args(argc, argv);

  args = std::vector<std::string>(argv, argv + argc);

  // This needs to run *before* V8::Initialize().
  {
    const int exit_code = InitializeNodeWithArgs(&args, &exec_args, &errors);
    for (const std::string& error : errors)
      fprintf(stderr, "%s: %s\n", args.at(0).c_str(), error.c_str());
    if (exit_code != 0) std::abort();  // return exit_code;
  }

  if (per_process::cli_options->use_largepages == "on" ||
      per_process::cli_options->use_largepages == "silent") {
    int result = node::MapStaticCodeToLargePages();
    if (per_process::cli_options->use_largepages == "on" && result != 0) {
      fprintf(stderr, "%s\n", node::LargePagesError(result));
    }
  }

#if HAVE_OPENSSL
  {
    std::string extra_ca_certs;
    if (credentials::SafeGetenv("NODE_EXTRA_CA_CERTS", &extra_ca_certs))
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

  per_process::v8_platform.Initialize(
      per_process::cli_options->v8_thread_pool_size);
  V8::Initialize();
  performance::performance_v8_start = PERFORMANCE_NOW();
  per_process::v8_initialized = true;
}


namespace {

std::string findModule(const std::string& folder, const std::string& name);
const std::string& getInitScript(const std::string& odaFrameworkPath);

}


JSCRIPT_EXTERN void Initialize(
    const std::string& origin,
    const std::string& externalOrigin,
    std::string executeFile,
    std::string coreFolder,
    std::string nodeFolder,
    std::function<void(const std::string&)> logCallback) {

#if defined(_M_X64) && _MSC_VER == 1800
  // Disable AVX for MSVC 2013
  _set_FMA3_enable(0);
#endif

  std::replace(std::begin(executeFile), std::end(executeFile), '\\', '/');
  std::replace(std::begin(nodeFolder), std::end(nodeFolder), '\\', '/');
  std::replace(std::begin(coreFolder), std::end(coreFolder), '\\', '/');

  // Paths to node modules
  CHECK_EQ(::uv_os_setenv("NODE_PATH", nodeFolder.c_str()), 0);

  // categories is ','-separated list of C++ core debug categories that should print debug output
  // 'none' - category for oda log callback
  CHECK_EQ(::uv_os_setenv("NODE_DEBUG_NATIVE", "none"), 0);

  int argc = 0;
  std::array<const char*, 20> argv;

  // Path to executable file
  argv[argc++] = executeFile.c_str();

  // Path to modules-loader.js
  argv[argc++] = "--experimental-vm-modules";

  std::string modulesLoader = findModule(coreFolder + "/web", "modules-loader");
  if (!modulesLoader.empty()) {
      argv[argc++] = "--experimental-loader";
#ifdef _WIN32
          static const std::string fileScheme{ "file:///" };
#else
          static const std::string fileScheme{ "file://" };
#endif
      modulesLoader = fileScheme + modulesLoader;
      argv[argc++] = modulesLoader.c_str();
  }
  
  const_cast<std::string&>(JSInstanceImpl::defaultOrigin) = origin;
  const_cast<std::string&>(JSInstanceImpl::externalOrigin) = externalOrigin;

  const std::string moduleInit = findModule(coreFolder + "/web", "jscript-init");
  if (moduleInit.empty()) {
    argv[argc++] = "-e";
    const std::string& initScript = getInitScript(coreFolder + "/web/core/odant.js");
    argv[argc++] = initScript.c_str();
  }
  else {
    argv[argc++] = moduleInit.c_str();
  }

  CHECK_LT(static_cast<std::size_t>(argc), argv.size());

  Initialize(argc, argv.data());
  SetRedirectFPrintF(std::move(logCallback));
}


namespace {


std::string findModule(const std::string& folder, const std::string& name) {
          FILE* file;

          const std::string esModulesLoader{ folder + '/' + name + ".mjs" };
          file = ::fopen(esModulesLoader.c_str(), "r");
          if (file != nullptr) {
              ::fclose(file);
              return esModulesLoader;
          }

          const std::string commonModulesLoader{ folder + '/' + name + ".cjs"};
          file = ::fopen(commonModulesLoader.c_str(), "r");
          if (file != nullptr) {
              ::fclose(file);
              return commonModulesLoader;
          }

          const std::string jsModulesLoader{ folder + '/' + name + ".js" };
          file = ::fopen(jsModulesLoader.c_str(), "r");
          if (file != nullptr) {
              ::fclose(file);
              return jsModulesLoader;
          }

          return std::string{};
}

const std::string& getInitScript(const std::string& odaFrameworkPath) {
  static const std::string cache =  ""
    "'use strict';\n"
    "process.stdout.write = (msg) => {\n"
    "   process._rawDebug(msg);\n"
    "};\n"
    "process.stderr.write = (msg) => {\n"
    "   process._rawDebug(msg);\n"
    "};\n"
    "process.on('uncaughtException', err => {\n"
    "    console.log(err);\n"
    "});\n"
    "process.on('unhandledRejection', err => {\n"
    "    console.log(err);\n"
    "});\n"
    "console.log('Start load framework.');\n"
#ifdef _DEBUG
    "console.log('global.DEFAULTORIGIN=%s', global.DEFAULTORIGIN);\n"
    "console.log('global.EXTERNALORIGIN=%s', global.EXTERNALORIGIN);\n"
#endif
    "global.odantFramework = require('" + odaFrameworkPath + "');\n"
    "global.odantFramework.then(core => {\n"
    "  var infiniteFunction = function() {\n"
    "    setTimeout(function() {\n"
    "        infiniteFunction();\n"
    "    }, 1000);\n"
    "  };\n"
    "  infiniteFunction();\n"
    "  console.log('framework loaded!');\n"
#ifdef _DEBUG
    "  console.log('core.DEFAULTORIGIN=%s', core.DEFAULTORIGIN);\n"
    "  console.log('core.EXTERNALORIGIN=%s', core.EXTERNALORIGIN);\n"
#endif
    "  global.__oda_setRunState();"
    "}).catch((error)=>{\n"
    "  global.__oda_setErrorState();\n"
    "  console.log(error);\n"
    "});\n"
  ;

  return cache;
}


} // Anonymouse namespace


JSCRIPT_EXTERN void Initialize(
    const std::string& origin,
    const std::string& externalOrigin,
    std::string executeFile,
    std::string coreFolder,
    const std::vector<std::string>& nodeFolders,
    std::function<void(const std::string&)> logCallback) {
#ifdef _WIN32
    const char delimiter = ';';
#else
    const char delimiter = ':';
#endif
    std::string nodePaths;
    for (const std::string& folder : nodeFolders) {
        if (!nodePaths.empty()) {
            nodePaths += delimiter;
        }
        nodePaths += folder;
    }
    Initialize(origin, externalOrigin, std::move(executeFile), std::move(coreFolder), std::move(nodePaths));
}

JSCRIPT_EXTERN void SetLogCallback(JSInstance* instance, JSLogCallback& cb) {
  if (!is_initilized) return;
  if (instance == nullptr) return;
  JSInstanceImpl* instanceImpl = static_cast<JSInstanceImpl*>(instance);
  instanceImpl->SetLogCallback(cb);
}

JSCRIPT_EXTERN void Uninitilize() {
  if (!is_initilized.exchange(false)) return;

  ExecutorCounter::global().waitAllStop();

  per_process::v8_initialized = false;
  V8::Dispose();

  // uv_run cannot be called from the time before the beforeExit callback
  // runs until the program exits unless the event loop has any referenced
  // handles after beforeExit terminates. This prevents unrefed timers
  // that happen to terminate during shutdown from being run unsafely.
  // Since uv_run cannot be called, uv_async handles held by the platform
  // will never be fully cleaned up.
  per_process::v8_platform.Dispose();
}

JSCRIPT_EXTERN result_t CreateInstance(JSInstance** outNewInstance) {
  JSInstanceImpl::Ptr instance = JSInstanceImpl::create();
  if (!instance) return JS_ERROR;

  instance->_thread = std::thread([instance]() {
    ExecutorCounter::ScopeExecute scopeExecute;
    instance->StartNodeInstance();
    instance->_thread.detach();
  });

  const auto timeout = std::chrono::seconds(30);
  std::unique_lock<std::mutex> lock(instance->_state_mutex);
  while (!instance->isInitialize()) {
      if (instance->_state_cv.wait_for(lock, timeout) ==  std::cv_status::timeout) {
          instance->setState(JSInstanceImpl::TIMEOUT);
      }
  }

  *outNewInstance = instance.detach();

  return JS_SUCCESS;
}


struct JSAsyncInfo
{
  std::string script;
  std::vector<JSCallbackInfo> callbacks;
  JSInstanceImpl::Ptr instance;
  uv_async_t async_handle;

  static JSAsyncInfo* create();
  void Dispose();
};

JSAsyncInfo* JSAsyncInfo::create() {
    return new JSAsyncInfo;
}

void JSAsyncInfo::Dispose() {
    delete this;
}


JSCRIPT_EXTERN result_t StopInstance(JSInstance* instance_) {
  if (instance_ == nullptr) return JS_ERROR;

  JSInstanceImpl::Ptr instance;
  instance.adopt(static_cast<JSInstanceImpl*>(instance_));
  if (instance->isRun()) {
    auto env = instance->_env;
    if (env != nullptr) env->ExitEnv();
  }

  return JS_SUCCESS;
}


namespace {

void createCallbacks(v8::Local<v8::Context>, const JSCallbackInfo&);

}


void _async_execute_script(uv_async_t* handle) {
  static std::atomic<unsigned int> s_async_id{0};

  JSAsyncInfo* asyncInfo = ContainerOf(&JSAsyncInfo::async_handle, handle);
  CHECK_NOT_NULL(asyncInfo);

  JSInstanceImpl::Ptr instance = asyncInfo->instance;
  CHECK(instance);

  node::Mutex::ScopedLock scopedLock{instance->_isolate_mutex};

  node::Environment* env = instance->_env;
  CHECK_NOT_NULL(env);

  v8::Isolate* isolate = env->isolate();
  CHECK_NOT_NULL(isolate);

  //v8::Locker locker(async_info->instance->_isolate);

  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope scope(isolate);

  v8::Local<v8::Context> context = env->context();
  v8::Context::Scope contextScope(context);

  v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate,
                                                         asyncInfo->script.c_str(),
                                                         v8::NewStringType::kNormal)
          .ToLocalChecked();

  v8::TryCatch tryCatch{isolate};
  tryCatch.SetVerbose(false);

  for (const auto& callbackInfo : asyncInfo->callbacks) {
    createCallbacks(context, callbackInfo);
  }

  v8::MaybeLocal<v8::Script> compileResult = v8::Script::Compile(context, source);
  if (tryCatch.HasCaught()) {
      v8::Local<v8::Value> exception = tryCatch.Exception();
      v8::String::Utf8Value message(isolate, exception);
      node::Debug(env, node::DebugCategory::NONE, *message);
  }
  else if (!compileResult.IsEmpty()) {
    v8::Local<v8::Script> script = compileResult.ToLocalChecked();

    if (!script.IsEmpty()) {
      v8::MaybeLocal<v8::Value> result = script->Run(context);
      if (result.IsEmpty()) {
        node::Debug(env, node::DebugCategory::NONE, "Run script faild");
      }

      if (tryCatch.HasCaught()) {
        v8::Local<v8::Value> exception = tryCatch.Exception();
        v8::String::Utf8Value message(isolate, exception);
        node::Debug(env, node::DebugCategory::NONE, *message);
      }
    }
  }


  if (::uv_is_closing(reinterpret_cast<uv_handle_t*>(handle)) == 0) {
    const auto cb = [](uv_handle_t* handle) {
      JSAsyncInfo* asyncInfo = ContainerOf(&JSAsyncInfo::async_handle, reinterpret_cast<uv_async_t*>(handle));
      if (asyncInfo != nullptr) {
          asyncInfo->Dispose();
      }
    };
    ::uv_close(reinterpret_cast<uv_handle_t*>(handle), cb);
  }
}


namespace {


void createCallbacks(v8::Local<v8::Context> context, const JSCallbackInfo& callbackInfo) {
  v8::Isolate* isolate = context->GetIsolate();

  v8::Local<v8::String> name = v8::String::NewFromUtf8(isolate,
                                                       callbackInfo.name.c_str(),
                                                       v8::NewStringType::kInternalized).ToLocalChecked();

  v8::Local<v8::External> external;
  if (callbackInfo.external) {
    external = v8::External::New(isolate, callbackInfo.external);
  }

  v8::Local<v8::FunctionTemplate> functionTemplate = v8::FunctionTemplate::New(isolate,
                                                                               callbackInfo.function,
                                                                               external);

  v8::Local<v8::Function> function = functionTemplate->GetFunction(context).ToLocalChecked();
  function->SetName(name);

  v8::Local<v8::Object> global = context->Global();
  global->Set(context, name, function).Check();
}


} // Anonymous namespace


JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance,
                                      const std::string& script) {
    const std::vector<JSCallbackInfo> dummy{};
    return RunScriptText(instance, script, dummy);
}

JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance,
                                      const std::string& script,
                                      const std::vector<JSCallbackInfo>& callbacks) {
  if (instance == nullptr) return JS_ERROR;
  if (script.empty()) return JS_ERROR;

  std::unique_ptr<JSAsyncInfo> info{JSAsyncInfo::create()};

  info->script = script;

  const auto pred = [](const JSCallbackInfo& cbInfo) -> bool {
      return (!cbInfo.name.empty()) && (cbInfo.function != nullptr);
  };
  std::copy_if(std::cbegin(callbacks), std::cend(callbacks), std::back_inserter(info->callbacks), pred);

  info->instance.reset(static_cast<JSInstanceImpl*>(instance));
  if (info->instance->isRun()) {
    const int resInit = ::uv_async_init(info->instance->event_loop(),
                                        &info->async_handle,
                                        _async_execute_script);
    CHECK_EQ(resInit, 0);

    ::uv_unref(reinterpret_cast<uv_handle_t*>(&info->async_handle));
    const int resSend = ::uv_async_send(&info->async_handle);

    CHECK_EQ(resSend, 0);
    info.release();

    return JS_SUCCESS;
  }

  return JS_ERROR;
}


}  // namespace jscript
}  // namespace node
