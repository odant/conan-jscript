// Test for jscript Conan package manager
// node::per_process::v8_platform
// Dmitriy Vetutnev, ODANT, 2020-2021


#ifdef NDEBUG
#undef NDEBUG
#endif


#include <jscript.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>


int main(int argc, char** argv) {
    const std::string cwd = std::filesystem::current_path().string();

    const std::string origin = "http://127.0.0.1:8080";
    const std::string externalOrigin = "http://127.0.0.1:8080";
    const std::string executeFile = argv[0];
    const std::string coreFolder = cwd;
    const std::vector<std::string> nodeFolders = {
        coreFolder + "/node_modules",
    };

    node::jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, nodeFolders);

    node::MultiIsolatePlatform* p = node::jscript::GetGlobalPlatform();
    assert(p != nullptr);

    node::jscript::Uninitilize();

    return EXIT_SUCCESS;
}
