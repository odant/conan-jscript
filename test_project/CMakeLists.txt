# Test project for debug jscript
# Dmitriy Vetutnev, Odant, 2019


project(PackageTest CXX)
cmake_minimum_required(VERSION 3.5)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

execute_process(COMMAND ${CMAKE_COMMAND} "-E" "copy_directory" ${CMAKE_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/bin/web)

set(BUILD_TYPE_FOR_CONAN ${CMAKE_BUILD_TYPE})
if((${BUILD_TYPE_FOR_CONAN} STREQUAL "RelWithDebInfo") OR (${BUILD_TYPE_FOR_CONAN} STREQUAL "MinSizeRel"))
    set(BUILD_TYPE_FOR_CONAN "Release")
endif()

include(conan.cmake)
conan_cmake_run(
    REQUIRES jscript/11.12.0.0@odant/editable
    BUILD_TYPE ${BUILD_TYPE_FOR_CONAN}
    IMPORTS "bin, *.dll -> ./bin" "bin, *.pdb -> ./bin" "lib, *.so* -> ./bin"
    BASIC_SETUP
    NO_OUTPUT_DIRS
)

find_package(JScript REQUIRED)

add_executable(test_jscript test_jscript.cpp)
target_link_libraries(test_jscript JScript::JScript)
set_target_properties(test_jscript
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
