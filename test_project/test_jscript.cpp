// Test project for jscript
// Dmitriy Vetutnev, Odant, 2019


#include <oda/jscript.h>


#include <iostream>
#include <cstdlib>
#include <string>
#include <memory>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <condition_variable>


// For GetCwd
#include <cstdio>
#ifdef _WIN32
    #include <direct.h>
    #define _GetCwd _getcwd
    #include "Windows.h"
#else
    #include <unistd.h>
    #define _GetCwd getcwd
#endif


std::string GetCwd();


static std::atomic_bool script_done{false};
static bool is_script_done() {
    return script_done;
}
static std::mutex script_mutex;
static std::condition_variable script_cv;
static void script_cb(const v8::FunctionCallbackInfo<v8::Value>&) {
    std::cout << "Call script_cb(const v8::FunctionCallbackInfo<v8::Value>&)" << std::endl;
    script_done = true;
    script_cv.notify_all();
}


int main(int argc, char** argv) {

    const std::string cwd = GetCwd();
    std::cout << "Current directory: " << cwd << std::endl;

    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = argv[0];
    const std::string coreFolder = cwd;
    const std::string nodeFolder = coreFolder + "/node_modules";

    jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, nodeFolder);
    std::cout << "jscript::Initialize() done" << std::endl;
    {
#ifndef _WIN32
        const char* node_path = std::getenv("NODE_PATH");
        if (node_path != nullptr) {
            std::cout << "NODE_PATH=" << node_path << std::endl;
        } else {
            std::cout << "NODE_PATH not set" << std::endl;
        }
#else
        auto node_path = std::make_unique<wchar_t[]>(32768);
        auto res = ::GetEnvironmentVariableW(L"NODE_PATH", node_path.get(), 32768);
        std::wcout << L"NODE_PATH=" << node_path.get() << std::endl;
#endif
    }

    jscript::result_t res;
    jscript::JSInstance* instance{nullptr};
    res = jscript::CreateInstance(&instance);
    if (res != jscript::JS_SUCCESS || !instance) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Instance created" << std::endl;

    const std::string script = ""
        "var promise_cb = new Promise((resolve, reject) => {\n"
        "   setTimeout(() => {\n"
        "       console.log('JS: Test callback...');\n"
        "       resolve(42);\n"
        "   }, 200);\n"
        "});\n"
        "promise_cb.then(resolve).catch(reject);\n"
        "";

    jscript::JSCallbackInfo resolveInfo;
    resolveInfo.external = reinterpret_cast<void*>(1);
    resolveInfo.name = "resolve";
    resolveInfo.function = script_cb;

    jscript::JSCallbackInfo rejectInfo;
    rejectInfo.external = reinterpret_cast<void*>(1);
    rejectInfo.name = "reject";
    rejectInfo.function = script_cb;

    jscript::JSCallbackInfo* callbacks[] = { &resolveInfo, &rejectInfo, nullptr };

    res = jscript::RunScriptText(instance, script, callbacks);
    if (res != jscript::JS_SUCCESS) {
        std::cout << "Failed running script" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::cout << "Script running, waiting..." << std::endl;    
    std::unique_lock<std::mutex> script_lock{script_mutex};
    script_cv.wait(script_lock, [] { return is_script_done(); });
        
    std::cout << "Script done" << std::endl;
    
    res = jscript::StopInstance(instance);
    if (res != jscript::JS_SUCCESS) {
        std::cout << "Failed instance stop" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Instance stopped" << std::endl;
        
    jscript::Uninitilize();
    std::cout << "jscript::Uninitilize() done" << std::endl;

    return EXIT_SUCCESS;
}


std::string GetCwd() {

    auto buffer = std::make_unique<char[]>(FILENAME_MAX);

    if ( !_GetCwd(buffer.get(), FILENAME_MAX) ) {
        std::cout << "Error. Can`t get current directory" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    
    std::string ret{buffer.get()};
    std::replace(std::begin(ret), std::end(ret), '\\', '/');

    return std::move(ret);
}
