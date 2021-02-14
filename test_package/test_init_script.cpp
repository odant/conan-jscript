// Test for jscript Conan package manager
// Explicit init script
// Dmitriy Vetutnev, ODANT, 2021


#include <jscript.h>

#include <iostream>
#include <cstdlib>
#include <mutex>
#include <condition_variable>
#include <filesystem>


bool isScriptDone = false;
std::mutex mtxIsScriptDone;
std::condition_variable cvIsScriptDone;

void cbScriptDone(const v8::FunctionCallbackInfo<v8::Value>&) {
    std::cout << "Call cbScriptDone(const v8::FunctionCallbackInfo<v8::Value>&)" << std::endl;

    std::unique_lock<std::mutex> lock{mtxIsScriptDone};
    isScriptDone = true;
    cvIsScriptDone.notify_all();
}


int main(int argc, char** argv) {

    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = argv[0];
    const std::string coreFolder = std::filesystem::current_path().string();

    const std::string initScript = ""
        "process.stdout.write = (msg) => {\n"
        "   process._rawDebug(msg);\n"
        "};\n"

        "process.stderr.write = (msg) => {\n"
        "   process._rawDebug(msg);\n"
        "};\n"

        "process.on('uncaughtException', (err) => {\n"
        "    console.log(err);\n"
        "});\n"

        "process.on('unhandledRejection', (err) => {\n"
        "    console.log(err);\n"
        "});\n"

        "console.log('I`m initScript!');\n"

        "var infiniteFunction = () => {\n"
        "    setTimeout(() => {\n"
        "        infiniteFunction();\n"
        "    }, 1000);\n"
        "};\n"
        "infiniteFunction();\n"

        "global.__oda_setRunState();\n"

        "console.log('initScript done');\n"
        "";


    node::jscript::Initialize(origin, externalOrigin,
                              executeFile, coreFolder, {},
                              initScript);
    std::cout << "node::jscript::Initialize() done" << std::endl;

    node::jscript::result_t res;
    node::jscript::JSInstance* instance{nullptr};
    res = node::jscript::CreateInstance(&instance);
    if (res != node::jscript::JS_SUCCESS || !instance) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Instance created" << std::endl;

    const std::string script = ""
        "console.log('Hello world');\n"
        "scriptDone();\n"
        "";

    node::jscript::JSCallbackInfo callbackInfo;
    callbackInfo.name = "scriptDone";
    callbackInfo.function = cbScriptDone;

    const std::vector<node::jscript::JSCallbackInfo> callbacks{
        std::move(callbackInfo)
    };

    res = node::jscript::RunScriptText(instance, script, callbacks);
    if (res != node::jscript::JS_SUCCESS) {
        std::cout << "Failed running script" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::cout << "Script running, waiting..." << std::endl;    
    std::unique_lock<std::mutex> lock{mtxIsScriptDone};
    cvIsScriptDone.wait(lock, [] { return isScriptDone; });
        
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
