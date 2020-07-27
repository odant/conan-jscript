// Test for jscript Conan package manager
// Dmitriy Vetutnev, Odant, 2020


#include <jscript.h>
#include "get_cwd.h"

#include <iostream>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>


int main(int argc, char** argv) {

    const std::string cwd = GetCwd();
    std::cout << "Current directory: " << cwd << std::endl;

    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = argv[0];
    const std::string coreFolder = cwd;
    const std::string nodeFolder = coreFolder + "/node_modules";

    auto logCb = [](const std::string& msg) {
        std::cout << "logCb: " << msg;
    };

    jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, nodeFolder, logCb);
    std::cout << "jscript::Initialize() done" << std::endl;

    jscript::result_t res;
    jscript::JSInstance* instance{nullptr};
    res = jscript::CreateInstance(&instance);
    if (res != jscript::JS_SUCCESS || !instance) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Instance created" << std::endl;

    res = jscript::RunScriptText(instance, "compile failed zzzzzzzz");
    if (res != jscript::JS_SUCCESS) {
        std::cout << "Failed running script" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::cout << "Script running, waiting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds{1});
        
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

