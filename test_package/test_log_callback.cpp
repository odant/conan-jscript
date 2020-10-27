// Test for jscript Conan package manager
// Load external init script
// Dmitriy Vetutnev, ODANT, 2020


#include <jscript.h>
#include "get_cwd.h"

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>


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

    std::ofstream externalScript{cwd + "/web/jscript-init.js"};
    externalScript
        << "console.log('I`m external script');" << std::endl
        << "var infiniteFunction = function() {" << std::endl
        << "    setTimeout(function() {" << std::endl
        << "        infiniteFunction();" << std::endl
        << "    }, 1000);" << std::endl
        << "};" << std::endl
        << "infiniteFunction()" << std::endl
        << "global.__oda_setRunState();" << std::endl
        << "console.log('External script done');" << std::endl
    ;
    externalScript.close();

    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = argv[0];
    const std::string coreFolder = cwd;

    bool isLogCbCalled = false;
    auto logCb = [&isLogCbCalled](const std::string& msg) {
        isLogCbCalled = true;
        std::cout << "logCb: " << msg;
    };

    jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, std::string{}, logCb);
    std::cout << "jscript::Initialize() done" << std::endl;

    jscript::result_t res;
    jscript::JSInstance* instance{nullptr};
    res = jscript::CreateInstance(&instance);
    if (res != jscript::JS_SUCCESS || !instance) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Instance created" << std::endl;

    const std::string script = ""
        "console.log('Script from cpp-file');\n"
        "scriptDone();\n"
        "";

    jscript::JSCallbackInfo callbackInfo;
    callbackInfo.name = "scriptDone";
    callbackInfo.function = script_cb;

    const std::vector<jscript::JSCallbackInfo> callbacks{
        std::move(callbackInfo)
    };

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

    if (!isLogCbCalled) {
        std::cout << "Error, logCb not called!" << std::endl;
        return EXIT_FAILURE;
    }

    jscript::Uninitilize();
    std::cout << "jscript::Uninitilize() done" << std::endl;

    return EXIT_SUCCESS;
}

