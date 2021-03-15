// Test for node::jscript:: Conan package manager
// SetLogCallback
// Dmitriy Vetutnev, ODANT, 2020-2021


#ifdef NDEBUG
#undef NDEBUG
#endif

#include <jscript.h>

#include <iostream>
#include <cstdlib>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <sstream>
#include <cassert>


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
    (void) argc;

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

        "global.odantFramework = new Object();\n"

        "console.log('initScript done');\n"
        "";


    auto cbFPrintF = [](const std::string& msg) {
        std::cout << "cbFPrintF: " << msg;
    };

    node::jscript::Initialize(origin, externalOrigin,
                              executeFile, coreFolder, {},
                              initScript,
                              cbFPrintF);

    std::cout << "node::jscript::Initialize() done" << std::endl;

    node::jscript::result_t res;
    node::jscript::JSInstance* instance{nullptr};
    res = node::jscript::CreateInstance(&instance);
    assert(instance != nullptr);
    assert(res == node::jscript::JS_SUCCESS);

    bool isLogCbCalled= false;
    node::jscript::ConsoleCallback cb = [&isLogCbCalled](const v8::FunctionCallbackInfo<v8::Value>& args, node::jscript::ConsoleType type) {
        isLogCbCalled = true;

        v8::Isolate* isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        std::ostringstream ss;
        ss << "logCb: type " << type << std::endl;
        ss << " " << "length: " << args.Length() << std::endl;

        ss << " " << "args: ";
        for (int i = 0; i < args.Length(); ++i) {
            auto arg = args[i];
            if (arg.IsEmpty()) {
                continue;
            }
            auto strObj = arg->ToString(context);
            if (strObj.IsEmpty()) {
                continue;
            }

            v8::String::Utf8Value v8Utf8String(isolate, strObj.ToLocalChecked());
            ss << " " << *v8Utf8String;
        }
        ss << std::endl;

        std::cout << ss.str();
    };
    node::jscript::SetConsoleCallback(instance, cb);

    const std::string script = ""
        "console.log('console.log');\n"
        "console.warn('console.warn', 'console.warn');\n"
        "console.error('console.error', 'console.error', 'console.error');\n"

        "odantFramework.console.log('odantFramework.console.log');\n"
        "odantFramework.console.warn('odantFramework.console.warn');\n"
        "odantFramework.console.error('odantFramework.console.error');\n"

        "scriptDone();\n"
        "";

    node::jscript::JSCallbackInfo callbackInfo;
    callbackInfo.name = "scriptDone";
    callbackInfo.function = cbScriptDone;

    const std::vector<node::jscript::JSCallbackInfo> callbacks{
        std::move(callbackInfo)
    };

    res = node::jscript::RunScriptText(instance, script, callbacks);
    assert(res == node::jscript::JS_SUCCESS);

    std::cout << "Script running, waiting..." << std::endl;    
    std::unique_lock<std::mutex> lock{mtxIsScriptDone};
    cvIsScriptDone.wait(lock, [] { return isScriptDone; });
        
    std::cout << "Script done" << std::endl;
    
    res = node::jscript::StopInstance(instance);
    assert(res == node::jscript::JS_SUCCESS);
    std::cout << "Instance stopped" << std::endl;

    assert(isLogCbCalled);

    node::jscript::Uninitilize();
    std::cout << "node::jscript::Uninitilize() done" << std::endl;

    return EXIT_SUCCESS;
}
