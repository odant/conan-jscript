// Test for jscript Conan package manager
// Load addon
// Dmitriy Vetutnev, Odant, 2020


#include <jscript.h>

#include <iostream>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <filesystem>


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

    const std::string cwd = std::filesystem::current_path().string();

    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = argv[0];
    const std::string coreFolder = cwd;
    const std::string nodeFolder = coreFolder; // + "/node_modules";

    node::jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, nodeFolder);
    std::cout << "node::jscript::Initialize() done" << std::endl;

    node::jscript::result_t res;
    node::jscript::JSInstance* instance{nullptr};
    res = node::jscript::CreateInstance(&instance);
    if (res != node::jscript::JS_SUCCESS || !instance) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Instance created" << std::endl;

    const char* script = ""
        "try {\n"
        "var addon = require('napi_addon');\n"
        "console.log(addon.hello());\n"
        "} catch (e) {\n"
        "console.log(e); }\n"
        "\n"
        "scriptDone();\n"
        "";

    node::jscript::JSCallbackInfo callbackInfo;
    callbackInfo.name = "scriptDone";
    callbackInfo.function = script_cb;

    res = node::jscript::RunScriptText(instance, script, {std::move(callbackInfo)});
    if (res != node::jscript::JS_SUCCESS) {
        std::cout << "Failed running script" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::cout << "Script running, waiting..." << std::endl;
    std::unique_lock<std::mutex> script_lock{script_mutex};
    script_cv.wait(script_lock, [] { return is_script_done(); });

    std::cout << "Script done" << std::endl;

    res = node::jscript::StopInstance(instance);
    if (res != node::jscript::JS_SUCCESS) {
        std::cout << "Failed instance stop" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Instance stopped" << std::endl;

    node::jscript::Uninitilize();
    std::cout << "node::jscript::Uninitilize() done" << std::endl;

    return EXIT_SUCCESS;
}

