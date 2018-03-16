#include <jscript.h>

#include <iostream>
#include <cstdlib>
#include <string>
#include <chrono>
#include <thread>
#include <memory>
#include <algorithm>

// For GetCwd
#include <cstdio>
#ifdef _WIN32
    #include <direct.h>
    #define _GetCwd _getcwd
#else
    #include <unistd.h>
    #define _GetCwd getcwd
#endif


std::string GetCwd();

int main(int argc, char** argv) {
    std::cout << "--------Running--------" << std::endl;
/*    
    const wchar_t* argv[] = {
        L"test_jscript",
        L"--version"
    };
    jscript::Initialize(2, argv);
    jscript::Uninitilize();
*/    
    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = argv[0];
    const std::string coreFolder = GetCwd();
    const std::string nodeFolder = coreFolder + "/web/node_modules";
    
    jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, nodeFolder);
    std::cout << "jscript::Initialize() done" << std::endl;
    
    jscript::result_t res;
    jscript::JSInstance* instance{nullptr};
    res = CreateInstance(&instance);
    if (res != jscript::JS_SUCCESS || !instance) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Instance created" << std::endl;
    
    const char* script = "console.log(\"Is work?\");";
    res = RunScriptText(instance, script);
    if (res != jscript::JS_SUCCESS) {
        std::cout << "Failed running script" << std::endl;
        std::exit(EXIT_FAILURE);
    }
        
    std::cout << "Script running, waiting..." << std::endl;
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(20s);
    std::cout << "End waiting" << std::endl;
    
    res = StopInstance(instance);
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
    std:replace(std::begin(ret), std::end(ret), '\\', '/');

    return std::move(ret);
}
