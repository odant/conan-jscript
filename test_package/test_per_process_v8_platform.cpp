// Test for jscript Conan package manager
// node::per_process::v8_platform
// Dmitriy Vetutnev, ODANT, 2020-2021


#ifdef NDEBUG
#undef NDEBUG
#endif


#include <jscript.h>

#define NODE_WANT_INTERNALS 1
#include <node_internals.h>
#include <node_v8_platform-inl.h>

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

    jscript::Initialize(origin, externalOrigin, executeFile, coreFolder, nodeFolders);

    node::MultiIsolatePlatform* p = node::per_process::v8_platform.Platform();
    assert(p != nullptr);

    jscript::Uninitilize();

    return EXIT_SUCCESS;
}
