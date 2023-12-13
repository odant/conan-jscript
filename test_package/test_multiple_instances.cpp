// Test for jscript Conan package manager
// Dmitriy Vetutnev, Odant, 2018 - 2020


#include <jscript.h>

#include <iostream>
#include <cstdlib>
#include <filesystem>


int main(int argc, char** argv) {
    const std::filesystem::path binFile{ argv[0] };
    assert(!binFile.empty());
    const std::filesystem::path binFolder{ binFile.parent_path() };
    assert(!binFolder.empty());

    std::cout << "Binary file: " << binFile.string() << std::endl << "Binary directory: " << binFolder.string() << std::endl;

    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = binFile.string();
    const auto coreFolder = binFolder;
    const auto nodeFolder = coreFolder / "node_modules";

    node::jscript::Initialize(origin, externalOrigin, executeFile, coreFolder.string(), nodeFolder.string());
    std::cout << "node::jscript::Initialize() done" << std::endl;

    node::jscript::result_t res;

    // Create instances

    node::jscript::JSInstance* instance1{nullptr};
    res = node::jscript::CreateInstance(&instance1);
    if (res != node::jscript::JS_SUCCESS || !instance1) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "First instance created" << std::endl;

    node::jscript::JSInstance* instance2{nullptr};
    res = node::jscript::CreateInstance(&instance2);
    if (res != node::jscript::JS_SUCCESS || !instance2) {
        std::cout << "Failed instance create" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Second instance created" << std::endl;

    // Shutdown

    res = node::jscript::StopInstance(instance1);
    if (res != node::jscript::JS_SUCCESS) {
        std::cout << "Failed instance stop" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "First instance stopped" << std::endl;

    res = node::jscript::StopInstance(instance2);
    if (res != node::jscript::JS_SUCCESS) {
        std::cout << "Failed instance stop" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "Second instance stopped" << std::endl;

    node::jscript::Uninitilize();
    std::cout << "node::jscript::Uninitilize() done" << std::endl;

    return EXIT_SUCCESS;
}

