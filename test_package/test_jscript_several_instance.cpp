// Test for jscript Conan package manager
// Dmitriy Vetutnev, Odant, 2018 - 2020


#include <oda/jscript.h>
#include "get_cwd.h"

#include <iostream>
#include <cstdlib>
#include <string>


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

    // Create instances

    jscript::JSInstance* instance1{nullptr};
    res = jscript::CreateInstance(&instance1);
    if (res != jscript::JS_SUCCESS || !instance1) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "First instance created" << std::endl;

    jscript::JSInstance* instance2{nullptr};
    res = jscript::CreateInstance(&instance2);
    if (res != jscript::JS_SUCCESS || !instance2) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Second instance created" << std::endl;

    // Shutdown

    res = jscript::StopInstance(instance1);
    if (res != jscript::JS_SUCCESS) {
        std::cout << "Failed instance stop" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "First instance stopped" << std::endl;

    res = jscript::StopInstance(instance2);
    if (res != jscript::JS_SUCCESS) {
        std::cout << "Failed instance stop" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Second instance stopped" << std::endl;

    jscript::Uninitilize();
    std::cout << "jscript::Uninitilize() done" << std::endl;

    return EXIT_SUCCESS;
}

