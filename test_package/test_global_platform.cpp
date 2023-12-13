// Test for jscript Conan package manager
// node::per_process::v8_platform
// Dmitriy Vetutnev, ODANT, 2020-2021


#ifdef NDEBUG
#undef NDEBUG
#endif


#include <jscript.h>

#include <iostream>
#include <cassert>
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
    const std::string coreFolder = binFolder.string();
    const std::vector<std::string> nodeFolders = {
        coreFolder + "/node_modules",
    };

    node::jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, nodeFolders);

    node::MultiIsolatePlatform* p = node::jscript::GetGlobalPlatform();
    assert(p != nullptr);

    node::jscript::Uninitilize();

    return EXIT_SUCCESS;
}
