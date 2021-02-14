#pragma once

#include "jscript.h"
#include "executer_counter.h"
#include "ref_counter.h"

#include "node_errors.h"
#include "node_internals.h"
#include "node_v8_platform-inl.h"
#include "node_crypto.h"
#include "large_pages/node_large_page.h"

#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <cstdio>
#include <algorithm>
#include <sstream>

#include <mutex>
#include <condition_variable>
#include <chrono>


namespace node {
namespace jscript {
using namespace ::node;


std::atomic<bool> is_initilized{false};

std::vector<std::string> args;
std::vector<std::string> exec_args;


class NodeInstanceData
{
public:
  NodeInstanceData();
  ~NodeInstanceData();

  uv_loop_t* event_loop();

  void print_handles();

  void close_loop();

  int exit_code();
  void set_exit_code(int);

protected:
  std::unique_ptr<IsolateData> isolate_data_;
  const bool deserialize_mode_ = false;

private:
  int exit_code_;

  bool isClosedEventLoop;
  uv_loop_t event_loop_;
};


NodeInstanceData::NodeInstanceData()
  :
    exit_code_{1},
    isClosedEventLoop{false}
{
  CHECK_EQ(::uv_loop_init(event_loop()), 0);
  CHECK_EQ(::uv_loop_configure(event_loop(), UV_METRICS_IDLE_TIME), 0);
}

NodeInstanceData::~NodeInstanceData() {
  close_loop();
}

uv_loop_t* NodeInstanceData::event_loop() {
  return &event_loop_;
}

void NodeInstanceData::print_handles() {
  ::fprintf(stderr, "\r\n%p\r\n", event_loop());
  ::uv_print_all_handles(event_loop(), stderr);
  ::fprintf(stderr, "\r\n");
}

void NodeInstanceData::close_loop() {
  if (isClosedEventLoop) {
      return;
  }
  isClosedEventLoop = true;

  while (::uv_loop_close(event_loop()) == UV_EBUSY) {
    auto cb = [](uv_handle_t* handle, void* arg) {
      if (::uv_is_closing(handle) == 0) {
        ::uv_close(handle, nullptr);
      }
    };
    ::uv_walk(event_loop(), cb, nullptr);

    ::uv_run(event_loop(), UV_RUN_DEFAULT);
  }
}

int NodeInstanceData::exit_code() {
  return exit_code_;
}

void NodeInstanceData::set_exit_code(int exit_code) {
  exit_code_ = exit_code;
}


class JSInstanceImpl : public JSInstance, public RefCounter, public NodeInstanceData
{
private:
  struct CtorTag {};

public:
  using Ptr = RefCounter::Ptr<JSInstanceImpl>;

  JSInstanceImpl(CtorTag);

  static JSInstanceImpl::Ptr create();

#ifdef ERROR
  #undef ERROR
#endif
  enum state_t { CREATE, RUN, STOPPING, STOP, TIMEOUT, ERROR };
  using AutoResetState = std::unique_ptr<void, std::function<void(void*)>>;
  AutoResetState createAutoReset(state_t);

  void StartNodeInstance();

  bool isStopping() const;
  bool isRun() const;
  bool isInitialize() const;

  void setState(state_t state);

  void SetLogCallback(JSLogCallback cb);
  JSLogCallback& logCallback();

  static const std::string defaultOrigin;
  static const std::string externalOrigin;

  Mutex _isolate_mutex;
  v8::Isolate* _isolate{nullptr};
  Environment* _env{nullptr};
  std::thread _thread;

  std::mutex _state_mutex;
  std::condition_variable _state_cv;

private:
  DeleteFnPtr<Environment, FreeEnvironment> CreateEnvironment(int*);

  void addSetStates(v8::Local<v8::Context>);
  void addSetState(v8::Local<v8::Context> context, const char* name, const state_t state);

  void addGlobalStringValue(v8::Local<v8::Context> context, const std::string& name, const std::string& value);

  void overrideConsole(v8::Local<v8::Context>);
  void overrideConsole(v8::Local<v8::Context>, const char* name, const JSLogType type);

  std::atomic<state_t> _state = ATOMIC_VAR_INIT(CREATE);

  JSLogCallback _logCallback;
};


const std::string JSInstanceImpl::defaultOrigin;
const std::string JSInstanceImpl::externalOrigin;


inline JSInstanceImpl::JSInstanceImpl(JSInstanceImpl::CtorTag)
{}

inline JSInstanceImpl::Ptr JSInstanceImpl::create() {
  return JSInstanceImpl::Ptr{
    new JSInstanceImpl{CtorTag{}}
  };
}

bool JSInstanceImpl::isStopping() const {
  const bool envStopped = !(_env && _env->is_stopping());
  return _state == STOPPING || envStopped;
}

bool JSInstanceImpl::isRun() const {
  const bool envNotStopped = _env && !(_env->is_stopping());
  return _state == RUN && envNotStopped;
}

bool JSInstanceImpl::isInitialize() const {
  return _state != CREATE;
}

void JSInstanceImpl::setState(state_t state) {
  _state = state;
  _state_cv.notify_all();
}

JSLogCallback& JSInstanceImpl::logCallback() {
  return _logCallback;
}

void JSInstanceImpl::SetLogCallback(JSLogCallback cb) {
  _logCallback = std::move(cb);
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
  _isolate = v8::Isolate::Allocate();
  CHECK_NOT_NULL(_isolate);
  // Register the isolate on the platform before the isolate gets initialized,
  // so that the isolate can access the platform during initialization.
  platform->RegisterIsolate(_isolate, event_loop());
  SetIsolateCreateParamsForNode(&params);
  v8::Isolate::Initialize(_isolate, params);

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
    v8::Locker locker{_isolate};
    v8::Isolate::Scope isolateScope{_isolate};
    v8::HandleScope handleScope{_isolate};

    auto env = this->CreateEnvironment(&exit_code);
    CHECK(env);
    _env = env.get();

    v8::Local<v8::Context> context = env->context();
    v8::Context::Scope contextScope{context};
    if (exit_code == 0) {
      LoadEnvironment(env.get());
      this->overrideConsole(context);

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

    ResetStdio();

#if defined(LEAK_SANITIZER)
    __lsan_do_leak_check();
#endif
  }
  set_exit_code(exit_code);
  _env = nullptr;

  platform->DrainTasks(_isolate);
  platform->UnregisterIsolate(_isolate);
  _isolate->Dispose();

  _isolate = nullptr;
  close_loop();
}

DeleteFnPtr<Environment, FreeEnvironment> JSInstanceImpl::CreateEnvironment(
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

    DeleteFnPtr<Environment, FreeEnvironment> env{ new Environment(
        isolate_data_.get(),
        context,
        args,
        exec_args,
        static_cast<EnvironmentFlags::Flags>(/* EnvironmentFlags::kIsMainThread | */
                                             EnvironmentFlags::kDefaultFlags     | 
                                             EnvironmentFlags::kOwnsProcessState |
                                             EnvironmentFlags::kOwnsInspector),
        ThreadId{})
    };

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

void JSInstanceImpl::addSetState(v8::Local<v8::Context> context, const char* name, const state_t state) {
    v8::Local<v8::Object> global = context->Global();
    CHECK(!global.IsEmpty());

    v8::Local<v8::String> functionName = v8::String::NewFromUtf8(_isolate, name).ToLocalChecked();

    const auto callback = [](const v8::FunctionCallbackInfo<v8::Value>& args) {
          v8::Isolate* isolate = args.GetIsolate();
          v8::HandleScope handleScope(isolate);

          v8::Local<v8::Value> data = args.Data();
          DCHECK(!data.IsEmpty());
          DCHECK(data->IsArray());
          if (data.IsEmpty() || !data->IsArray()) {
              return;
          }

          v8::Local<v8::Array> array = data.As<v8::Array>();
          DCHECK_GE(array->Length(), 2);
          if (array->Length() < 2) {
              return;
          }

          v8::Local<v8::Context> context = array->CreationContext();

          v8::Local<v8::External> instanceExt = array->Get(context, 0).ToLocalChecked().As<v8::External>();

          JSInstanceImpl* instance = reinterpret_cast<JSInstanceImpl*>(instanceExt->Value());
          DCHECK_NOT_NULL(instance);

          v8::Local<v8::Int32> stateCode = array->Get(context, 1).ToLocalChecked().As<v8::Int32>();
          instance->setState(static_cast<JSInstanceImpl::state_t>(stateCode-> Value()));
    };  // callback

    v8::Local<v8::External> instanceExt = v8::External::New(_isolate, this);
    v8::Local<v8::Int32> stateCode = v8::Integer::New(_isolate, static_cast<int32_t>(state))->ToInt32(context).ToLocalChecked();
    v8::Local<v8::Array> array = v8::Array::New(_isolate, 2);
    array->Set(context, 0, instanceExt).Check();
    array->Set(context, 1, stateCode).Check();

    v8::Local<v8::Function> setRunStateFunction = v8::Function::New(context, callback, array).ToLocalChecked();
    global->Set(context, functionName, setRunStateFunction).Check();
}

void JSInstanceImpl::addGlobalStringValue(v8::Local<v8::Context> context, const std::string& name, const std::string& value) {
  v8::Local<v8::Object> global = context->Global();
  CHECK(!global.IsEmpty());

  v8::Local<v8::String> stringName = v8::String::NewFromUtf8(_isolate, name.c_str()).ToLocalChecked();
  v8::Local<v8::String> stringValue = v8::String::NewFromUtf8(_isolate, value.c_str()).ToLocalChecked();
  global->Set(context, stringName, stringValue).Check();
}

void JSInstanceImpl::overrideConsole(v8::Local<v8::Context> context) {
  overrideConsole(context, "log", JSLogType::LOG_TYPE);
  overrideConsole(context, "warn", JSLogType::WARN_TYPE);
  overrideConsole(context, "error", JSLogType::ERROR_TYPE);
}


namespace {

void consoleCallback(const v8::FunctionCallbackInfo<v8::Value>& args);

}


void JSInstanceImpl::overrideConsole(v8::Local<v8::Context> context, const char* name, const JSLogType type) {
  v8::HandleScope handleScope{_isolate};

  v8::Local<v8::Object> globalObj = context->Global();
  DCHECK(!globalObj.IsEmpty());

  v8::Local<v8::String> consoleName = v8::String::NewFromUtf8(_isolate, "console").ToLocalChecked();
  v8::Local<v8::String> methodName = v8::String::NewFromUtf8(_isolate, name).ToLocalChecked();

  v8::Local<v8::Object> globalConsoleObj = globalObj->Get(context, consoleName).ToLocalChecked().As<v8::Object>();

  //TODO: add check re-override control

  v8::Local<v8::Array> array = v8::Array::New(_isolate, 3);

  v8::Local<v8::Value> globalMethod = globalConsoleObj->Get(context, methodName).ToLocalChecked().As<v8::Function>();
  v8::Local<v8::External> instanceExt = v8::External::New(_isolate, this);
  v8::Local<v8::External> typeExt = v8::External::New(_isolate, reinterpret_cast<void*>(type));

  array->Set(context, 0, globalMethod).Check();
  array->Set(context, 1, instanceExt).Check();
  array->Set(context, 2, typeExt).Check();

  v8::Local<v8::Function> overrideFunction = v8::Function::New(context, consoleCallback, array).ToLocalChecked();
  globalConsoleObj->Set(context, methodName, overrideFunction).Check();

  v8::Local<v8::String> odantFrameworkName = v8::String::NewFromUtf8(_isolate, "odantFramework").ToLocalChecked();
  v8::Local<v8::Value> odantFramework = globalObj->Get(context, odantFrameworkName).ToLocalChecked();
  if (!odantFramework->IsObject()) {
    return;
  }
  v8::Local<v8::Object> odantFrameworkObj = odantFramework.As<v8::Object>();

  v8::Local<v8::Value> odantFrameworkConsole = odantFrameworkObj->Get(context, consoleName).ToLocalChecked();
  if (!odantFrameworkConsole->IsObject()) {
    odantFrameworkConsole = v8::Object::New(_isolate);
    odantFrameworkObj->Set(context, consoleName, odantFrameworkConsole).Check();
  }

  v8::Local<v8::Object> odantFrameworkConsoleObj = odantFrameworkConsole.As<v8::Object>();
  odantFrameworkConsoleObj->Set(context, methodName, overrideFunction).Check();
}


namespace {


void consoleCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handleScope(isolate);

    v8::Local<v8::Value> data = args.Data();
    DCHECK(!data.IsEmpty());
    DCHECK(data->IsArray());
    if (data.IsEmpty() || !data->IsArray()) {
        return;
    }

    v8::Local<v8::Array> array = data.As<v8::Array>();
    DCHECK_GE(array->Length(), 3);
    if (array->Length() < 3) {
        return;
    }

    v8::Local<v8::Context> context = array->CreationContext();

    v8::Local<v8::Function> globalLogFunc = array->Get(context, 0).ToLocalChecked().As<v8::Function>();

    v8::TryCatch trycatch(isolate);
    trycatch.SetVerbose(true);

    std::vector<v8::Local<v8::Value>> info;
    for (int i = 0; i < args.Length(); ++i) {
        info.push_back(args[i]);
    }

    globalLogFunc->Call(context, v8::Null(isolate), info.size(), info.data()).ToLocalChecked();

    v8::Local<v8::External> instanceExt = array->Get(context, 1).ToLocalChecked().As<v8::External>();
    JSInstanceImpl* instance = reinterpret_cast<JSInstanceImpl*>(instanceExt->Value());
    DCHECK_NOT_NULL(instance);

    auto& logCallback = instance->logCallback();
    if (logCallback) {
        v8::Local<v8::External> typeExt = array->Get(context, 2).ToLocalChecked().As<v8::External>();
        JSLogType type = static_cast<JSLogType>(reinterpret_cast<std::size_t>(typeExt->Value()));

        logCallback(args, type);
    }
}


} // Anonymouse namespace


NODE_EXTERN void Initialize(const std::vector<std::string>& argv,
                               const std::string& nodeFolder,
                               std::function<void(const std::string&)> redirectFPrintF) {

  if (is_initilized.exchange(true)) {
    return;
  }

  // Paths to node modules
  CHECK_EQ(::uv_os_setenv("NODE_PATH", nodeFolder.c_str()), 0);

  // categories is ','-separated list of C++ core debug categories that should print debug output
  // 'none' - category for oda log callback
  CHECK_EQ(::uv_os_setenv("NODE_DEBUG_NATIVE", "none"), 0);

  SetRedirectFPrintF(std::move(redirectFPrintF));

  // Initialized the enabled list for Debug() calls with system
  // environment variables.
  per_process::enabled_debug_list.Parse(nullptr);

  atexit(ResetStdio);
  PlatformInit();

#ifdef _DEBUG
  {
    std::ostringstream ss;
    ss << "jscript:" << std::endl;
    ss << "  " << "node argv:";
    for (const auto& arg : argv) {
      ss << ' ' << arg;
    }
    ss << std::endl;
    ss << "  " << "nodeFolder: " << nodeFolder << std::endl;
    per_process::Debug(DebugCategory::NONE, ss.str().c_str());
  }
#endif

  args = argv;
  std::vector<std::string> errors;

  // This needs to run *before* V8::Initialize().
  {
    const auto exit_code = InitializeNodeWithArgs(&args, &exec_args, &errors);
    for (const std::string& error : errors)
      fprintf(stderr, "%s: %s\n", args.at(0).c_str(), error.c_str());
    CHECK_EQ(exit_code, 0);
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
  v8::V8::SetEntropySource(crypto::EntropySource);
#endif  // HAVE_OPENSSL

  per_process::v8_platform.Initialize(
      per_process::cli_options->v8_thread_pool_size);
  v8::V8::Initialize();
  performance::performance_v8_start = PERFORMANCE_NOW();
  per_process::v8_initialized = true;

  SetRedirectFPrintF(std::move(redirectFPrintF));
}


namespace {

void setOrigin(const std::string& origin, const std::string& externalOrigin);
std::vector<std::string> getCommonNodeArgs(const std::string& executeFile, const std::string& coreFolder);
std::string findModule(const std::string& folder, const std::string& name);
std::string getInitScript(std::string odaFrameworkPath);
std::string convertNodeFolders(const std::vector<std::string>&);

}


NODE_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               const std::string& executeFile, const std::string& coreFolder, const std::string& nodeFolder,
                               std::function<void(const std::string&)> redirectFPrintF) {

  setOrigin(origin, externalOrigin);

  std::vector<std::string> argv = getCommonNodeArgs(executeFile, coreFolder);

  std::string moduleInit = findModule(coreFolder + "/web", "jscript-init");
  if (moduleInit.empty()) {
    argv.push_back("-e");
    std::string initScript = getInitScript(coreFolder + "/web/core/odant.js");
    argv.push_back(std::move(initScript));
  }
  else {
    argv.push_back(std::move(moduleInit));
  }

  Initialize(argv,
             nodeFolder,
             std::move(redirectFPrintF));
}

NODE_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               const std::string& executeFile, const std::string& coreFolder, const std::vector<std::string>& nodeFolders,
                               std::function<void(const std::string&)> redirectFPrintF) {

    const std::string nodeFolder = convertNodeFolders(nodeFolders);

    Initialize(origin, externalOrigin,
               executeFile, coreFolder, nodeFolder,
               std::move(redirectFPrintF));
}

NODE_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               const std::string& executeFile, const std::string& coreFolder, const std::vector<std::string>& nodeFolders,
                               const std::string& initScript,
                               std::function<void(const std::string&)> redirectFPrintF) {

  setOrigin(origin, externalOrigin);

  std::vector<std::string> argv = getCommonNodeArgs(executeFile, coreFolder);

  argv.push_back("-e");
  argv.push_back(initScript);

  const std::string nodeFolder = convertNodeFolders(nodeFolders);

  Initialize(argv,
             nodeFolder,
             std::move(redirectFPrintF));
}


namespace {


void setOrigin(const std::string& origin, const std::string& externalOrigin) {
    const_cast<std::string&>(JSInstanceImpl::defaultOrigin) = origin;
    const_cast<std::string&>(JSInstanceImpl::externalOrigin) = externalOrigin;
}

std::vector<std::string> getCommonNodeArgs(const std::string& executeFile, const std::string& coreFolder) {
    std::vector<std::string> argv{
        std::move(executeFile),
        "--experimental-vm-modules"
    };

    std::string modulesLoader = findModule(coreFolder + "/web", "modules-loader");
    if (!modulesLoader.empty()) {
        argv.push_back("--experimental-loader");
  #ifdef _WIN32
            static const std::string fileScheme{ "file:///" };
  #else
            static const std::string fileScheme{ "file://" };
  #endif
        modulesLoader = fileScheme + modulesLoader;
        argv.push_back(std::move(modulesLoader));
    }

    return argv;
}

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

std::string getInitScript(std::string odaFrameworkPath) {
  std::replace(std::begin(odaFrameworkPath), std::end(odaFrameworkPath), '\\', '/');

  std::string script =  ""
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

  return script;
}

std::string convertNodeFolders(const std::vector<std::string>& nodeFolders) {
#ifdef _WIN32
    const char delimiter = ';';
#else
    const char delimiter = ':';
#endif

    std::string nodeFolder;
    for (const std::string& folder : nodeFolders) {
        if (!nodeFolder.empty()) {
            nodeFolder += delimiter;
        }
        nodeFolder += folder;
    }

    return nodeFolder;
}


} // Anonymouse namespace


NODE_EXTERN void SetLogCallback(JSInstance* instance, JSLogCallback cb) {
  DCHECK(is_initilized);
  DCHECK_NOT_NULL(instance);

  JSInstanceImpl* instanceImpl = static_cast<JSInstanceImpl*>(instance);

  instanceImpl->SetLogCallback(std::move(cb));
}

NODE_EXTERN void Uninitilize() {
  if (!is_initilized.exchange(false)) {
    return;
  }

  ExecutorCounter::global().waitAllStop();

  TearDownOncePerProcess();
}

NODE_EXTERN result_t CreateInstance(JSInstance** outNewInstance) {
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


NODE_EXTERN result_t StopInstance(JSInstance* instance_) {
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


void compileAndRun(node::Environment& env, const std::string&, const std::vector<JSCallbackInfo>&);


} // Anonymous namespace


void _async_execute_script(uv_async_t* handle) {
  JSAsyncInfo* asyncInfo = ContainerOf(&JSAsyncInfo::async_handle, handle);
  CHECK_NOT_NULL(asyncInfo);

  JSInstanceImpl::Ptr instance = asyncInfo->instance;
  CHECK(instance);


  node::Mutex::ScopedLock scopedLock{instance->_isolate_mutex};


  node::Environment* env = instance->_env;
  CHECK_NOT_NULL(env);

  const std::string& text = asyncInfo->script;
  const auto& callbacks = asyncInfo->callbacks;

  compileAndRun(*env, text, callbacks);

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


void insertCallbacks(v8::Local<v8::Context>, const JSCallbackInfo&);
void processTryCatch(node::Environment&, const v8::TryCatch&);

void compileAndRun(node::Environment& env, const std::string& text, const std::vector<JSCallbackInfo>& callbacks) {
  v8::Local<v8::Context> context = env.context();
  CHECK(!context.IsEmpty());

  v8::Isolate* isolate = context->GetIsolate();
  CHECK_NOT_NULL(isolate);

  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope scope(isolate);
  v8::Context::Scope contextScope(context);

  for (const auto& cbInfo : callbacks) {
    insertCallbacks(context, cbInfo);
  }

  v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, text.c_str()).ToLocalChecked();

  v8::TryCatch tryCatch{isolate};
#ifdef _DEBUG
  tryCatch.SetVerbose(true);
#else
  tryCatch.SetVerbose(false);
#endif

  v8::MaybeLocal<v8::Script> compileResult = v8::Script::Compile(context, source);

  if (tryCatch.HasCaught()) {
    processTryCatch(env, tryCatch);
  }
  else if (!compileResult.IsEmpty()) {
    v8::Local<v8::Script> script = compileResult.ToLocalChecked();

    v8::MaybeLocal<v8::Value> result = script->Run(context);
    if (result.IsEmpty()) {
      node::Debug(&env, node::DebugCategory::NONE, "Run script faild");
    }

    if (tryCatch.HasCaught()) {
      processTryCatch(env, tryCatch);
    }
  }
}

void insertCallbacks(v8::Local<v8::Context> context, const JSCallbackInfo& callbackInfo) {
  v8::Isolate* isolate = context->GetIsolate();

  v8::Local<v8::String> name = v8::String::NewFromUtf8(isolate, callbackInfo.name.c_str(),
                                                       v8::NewStringType::kInternalized, callbackInfo.name.length()).ToLocalChecked();

  v8::Local<v8::External> external;
  if (callbackInfo.external) {
    external = v8::External::New(isolate, callbackInfo.external);
  }

  v8::Local<v8::Function> function = v8::Function::New(context, callbackInfo.function, external).ToLocalChecked();
  function->SetName(name);

  v8::Local<v8::Object> global = context->Global();
  global->Set(context, name, function).Check();
}

void processTryCatch(node::Environment& env, const v8::TryCatch& tryCatch) {
  v8::Local<v8::Context> context = env.context();
  DCHECK(!context.IsEmpty());

  v8::Isolate* isolate = context->GetIsolate();
  DCHECK_NOT_NULL(isolate);

  v8::Local<v8::Value> exception = tryCatch.Exception();
  v8::String::Utf8Value message{isolate, exception};
  node::Debug(&env, node::DebugCategory::NONE, *message);

#ifdef _DEBUG
    v8::MaybeLocal<v8::Value> maybeStackTrace = tryCatch.StackTrace(context);
    if (!maybeStackTrace.IsEmpty()) {
      v8::Local<v8::Value> stackTrace;
      DCHECK(maybeStackTrace.ToLocal(&stackTrace));
      v8::String::Utf8Value messageStackTrace{isolate, stackTrace};
      node::Debug(&env, node::DebugCategory::NONE, *messageStackTrace);
    }
    else {
        node::Debug(&env, node::DebugCategory::NONE, "tryCatch.StackTrace(context) is empty");
    }
#endif

}


} // Anonymous namespace


NODE_EXTERN result_t RunScriptText(JSInstance* instance,
                                      const std::string& script) {
    const std::vector<JSCallbackInfo> dummy{};
    return RunScriptText(instance, script, dummy);
}

NODE_EXTERN result_t RunScriptText(JSInstance* instance_,
                                      const std::string& script,
                                      const std::vector<JSCallbackInfo>& callbacks) {
  if (instance_ == nullptr) {
    return JS_ERROR;
  }

  JSInstanceImpl* instance  = static_cast<JSInstanceImpl*>(instance_);
  if (!instance->isRun()) {
    return JS_ERROR;
  }

  if (script.empty()) {
    return JS_ERROR;
  }

  std::unique_ptr<JSAsyncInfo> info{JSAsyncInfo::create()};

  info->instance.reset(instance);
  info->script = script;

  const auto pred = [](const JSCallbackInfo& cbInfo) -> bool {
      return (!cbInfo.name.empty()) && (cbInfo.function != nullptr);
  };
  std::copy_if(std::cbegin(callbacks), std::cend(callbacks), std::back_inserter(info->callbacks), pred);

  const int resInit = ::uv_async_init(instance->event_loop(),
                                      &info->async_handle,
                                      _async_execute_script);
  CHECK_EQ(resInit, 0);

  ::uv_unref(reinterpret_cast<uv_handle_t*>(&info->async_handle));
  const int resSend = ::uv_async_send(&info->async_handle);

  CHECK_EQ(resSend, 0);
  info.release();

  return JS_SUCCESS;
}

MultiIsolatePlatform* GetGlobalPlatform() {
    return per_process::v8_platform.Platform();
}


}  // namespace jscript
}  // namespace node
