// Test for jscript Conan package manager
// Redirect FPrintF
// Dmitriy Vetutnev, ODANT, 2020


#ifdef NDEBUG
#undef NDEBUG
#endif

#include "TempJscriptInit.hpp"

#include <jscript.h>

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <cassert>


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
    const std::filesystem::path binFile{ argv[0] };
    assert(!binFile.empty());
    const std::filesystem::path binFolder{ binFile.parent_path() };
    assert(!binFolder.empty());

    std::cout << "Binary file: " << binFile.string() << std::endl << "Binary directory: " << binFolder.string() << std::endl;

    const std::string externalScript = ""
        "process.stdout.write = (msg) => {\n"
        "    process._rawDebug(msg);\n"
        "};\n"
        "process.stderr.write = (msg) => {\n"
        "    process._rawDebug(msg);\n"
        "};\n"
        "process.on('uncaughtException', (err) => {\n"
        "    console.log(err);\n"
        "});\n"
        "process.on('unhandledRejection', (err) => {\n"
        "    console.log(err);\n"
        "});\n"

        "console.log('I`m external init script');\n"

        "const infiniteFunction = () => {\n"
        "    setTimeout(() => {\n"
        "        infiniteFunction();\n"
        "    }, 1000);\n"
        "};\n"
        "infiniteFunction();\n"

        "global.__oda_setRunState();\n"

        "console.log('External script done');\n"
        ;

    TempJscriptInit jscriptInit(binFolder, externalScript);

    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = binFile.string();
    const std::string coreFolder = binFolder.string();

    bool isRedirectCbCalled = false;
    std::string bufferRedirectFPrintF;
    auto redirectFPrintF = [&isRedirectCbCalled, &bufferRedirectFPrintF](const std::string& msg) {
        isRedirectCbCalled = true;
        std::cout << "redirectFPrintF cb: " << msg;
        bufferRedirectFPrintF += msg;
    };

    node::jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, std::string{}, redirectFPrintF);
    std::cout << "node::jscript::Initialize() done" << std::endl;

    node::jscript::result_t res;
    node::jscript::JSInstance* instance = nullptr;
    res = node::jscript::CreateInstance(&instance);
    assert(res == node::jscript::JS_SUCCESS);
    assert(instance != nullptr);
    std::cout << "Instance created" << std::endl;

    const std::string script = ""
        "console.log('Script from cpp-file');\n"
        "scriptDone();\n"
        "";

    node::jscript::JSCallbackInfo callbackInfo;
    callbackInfo.name = "scriptDone";
    callbackInfo.function = script_cb;

    const std::vector<node::jscript::JSCallbackInfo> callbacks{
        std::move(callbackInfo)
    };

    res = node::jscript::RunScriptText(instance, script, callbacks);
    assert(res == node::jscript::JS_SUCCESS);

    std::cout << "Script running, waiting..." << std::endl;    
    std::unique_lock<std::mutex> script_lock{script_mutex};
    script_cv.wait(script_lock, [] { return is_script_done(); });
        
    std::cout << "Script done" << std::endl;
    
    res = node::jscript::StopInstance(instance);
    assert(res == node::jscript::JS_SUCCESS);
    std::cout << "Instance stopped" << std::endl;

    assert(isRedirectCbCalled);
    auto pos = bufferRedirectFPrintF.find("Script from cpp-file");
    assert(pos != std::string::npos);

    node::jscript::Uninitilize();
    std::cout << "node::jscript::Uninitilize() done" << std::endl;

    return EXIT_SUCCESS;
}
