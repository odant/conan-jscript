// Test for jscript Conan package manager
// Dmitriy Vetutnev, Odant, 2018 - 2020


#include <jscript.h>

#include <iostream>
#include <cstdlib>
#include <filesystem>


int main(int argc, char** argv) {
    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = argv[0];
    const auto coreFolder = std::filesystem::current_path();
    const auto nodeFolder = coreFolder / "node_modules";

    jscript::Initialize(origin, externalOrigin, executeFile, coreFolder.string(), nodeFolder.string());
    std::cout << "jscript::Initialize() done" << std::endl;

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

