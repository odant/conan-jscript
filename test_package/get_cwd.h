// Test for jscript Conan package manager
// Get current directory
// Dmitriy Vetutnev, Odant, 2018 - 2020


#pragma once


#include <memory>
#include <string>
#include <stdexcept>

// For GetCwd
#include <cstdio>
#ifdef _WIN32
    #include <direct.h>
    #define _GetCwd _getcwd
    #include "Windows.h"
#else
    #include <unistd.h>
    #define _GetCwd getcwd
#endif


inline std::string GetCwd() {
    auto buffer = std::make_unique<char[]>(FILENAME_MAX);
    if ( !_GetCwd(buffer.get(), FILENAME_MAX) ) {
        throw std::runtime_error{"Error. Can`t get current directory"};
    }
    return std::string{buffer.get()};
}
