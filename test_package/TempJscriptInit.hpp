#pragma once

#include <iostream>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cassert>

class TempJscriptInit {
public:
    TempJscriptInit(const std::filesystem::path& path, const std::string& script)
        : _path(path / "web" / "jscript-init.js") { 
        create(script);
    }
    ~TempJscriptInit() {
        std::error_code ec;
        const auto removeSuccess = std::filesystem::remove(_path, ec);
        assert(removeSuccess && !ec);
    }

private:
    void create(const std::string& script) {
        std::error_code ec;
        const auto parent = _path.parent_path();
        const bool directoriesCreated = std::filesystem::create_directories(parent, ec);
        assert(!ec);

        std::ofstream stream;
        stream.open(_path, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
        assert(stream.is_open());
        stream << script;
        stream.close();
    }

    const std::filesystem::path _path;
};
