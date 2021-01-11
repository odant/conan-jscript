# Test for jscript Conan package manager
# Dmitriy Vetutnev, Odant, 2018


project(PackageTest CXX)
cmake_minimum_required(VERSION 3.0)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FilesystemLibrary)
if(CMAKE_COMPILER_IS_GNUCXX)
    set(FilesystemLibrary stdc++fs)
endif()

include(${CMAKE_CURRENT_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)


if(UNIX)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
else()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
endif()


# Copy fake Odant framework and node modules
execute_process(COMMAND ${CMAKE_COMMAND} "-E" "copy_directory" ${CMAKE_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/bin/web)
execute_process(COMMAND ${CMAKE_COMMAND} "-E" "copy_directory" ${CMAKE_SOURCE_DIR}/node_modules ${CMAKE_BINARY_DIR}/bin/node_modules)
execute_process(COMMAND ${CMAKE_COMMAND} "-E" "copy_directory" ${CMAKE_SOURCE_DIR}/node_modules2 ${CMAKE_BINARY_DIR}/bin/node_modules2)


enable_testing()


# CONAN_PKG imported targets
add_executable(test_jscript_CONAN_PKG test_jscript.cpp)
target_link_libraries(test_jscript_CONAN_PKG CONAN_PKG::jscript ${FilesystemLibrary})
set_target_properties(test_jscript_CONAN_PKG
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_jscript_CONAN_PKG
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_jscript_CONAN_PKG
)


# Normal imported targets
find_package(JScript REQUIRED)

add_executable(test_jscript test_jscript.cpp)
target_link_libraries(test_jscript JScript::JScript ${FilesystemLibrary})
set_target_properties(test_jscript
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_jscript
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_jscript
)

add_executable(test_multiple_instances test_multiple_instances.cpp)
target_link_libraries(test_multiple_instances JScript::JScript ${FilesystemLibrary})
set_target_properties(test_multiple_instances
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_multiple_instances
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_multiple_instances
)

add_executable(test_headers test_headers.cpp)
target_link_libraries(test_headers JScript::JScript ${FilesystemLibrary})
set_target_properties(test_headers
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_headers
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_headers
)


add_library(napi_addon SHARED
    napi_addon.cpp
    #${JScript_INCLUDE_DIRS}/win_delay_load_hook.cc
)
set_target_properties(napi_addon PROPERTIES PREFIX "" SUFFIX ".node")
target_compile_definitions(napi_addon PRIVATE BUILDING_NODE_EXTENSION)
target_link_libraries(napi_addon JScript::JScript)

add_executable(test_napi_addon test_napi_addon.cpp)
target_link_libraries(test_napi_addon JScript::JScript ${FilesystemLibrary})
set_target_properties(test_napi_addon
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_napi_addon
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_napi_addon
)
add_dependencies(test_napi_addon napi_addon)


find_program(JSCRIPT_INTERPRETER
    NAMES jscript jscriptd
    PATHS ${CMAKE_CURRENT_BINARY_DIR}/bin
    NO_DEFAULT_PATH
)

add_test(
    NAME test_compile_failed_interpreter
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND ${JSCRIPT_INTERPRETER} -e "compile failed zzzzzzzzz"
)
set_property(TEST test_compile_failed_interpreter PROPERTY WILL_FAIL True)
if(UNIX)
    set_property(TEST test_compile_failed_interpreter PROPERTY ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/bin)
endif()

add_executable(test_compile_failed test_compile_failed.cpp)
target_link_libraries(test_compile_failed JScript::JScript ${FilesystemLibrary})
set_target_properties(test_compile_failed
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_compile_failed
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_compile_failed
)

add_test(
    NAME test_throw_exception_interpreter
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND ${JSCRIPT_INTERPRETER} -e "throw 'Error42'" --trace-uncaught
)
set_property(TEST test_throw_exception_interpreter PROPERTY WILL_FAIL True)
if(UNIX)
    set_property(TEST test_throw_exception_interpreter PROPERTY ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/bin)
endif()

add_executable(test_throw_exception test_throw_exception.cpp)
target_link_libraries(test_throw_exception JScript::JScript ${FilesystemLibrary})
set_target_properties(test_throw_exception
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_throw_exception
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_throw_exception
)

add_executable(test_multiple_paths_node_modules test_multiple_paths_node_modules.cpp)
target_link_libraries(test_multiple_paths_node_modules JScript::JScript ${FilesystemLibrary})
set_target_properties(test_multiple_paths_node_modules
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_multiple_paths_node_modules
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_multiple_paths_node_modules
)

add_executable(test_external_init_script test_external_init_script.cpp)
target_link_libraries(test_external_init_script JScript::JScript ${FilesystemLibrary})
set_target_properties(test_external_init_script
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_external_init_script
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_external_init_script
)

add_executable(test_redirect_fprintf test_redirect_fprintf.cpp)
target_link_libraries(test_redirect_fprintf JScript::JScript ${FilesystemLibrary})
set_target_properties(test_redirect_fprintf
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_redirect_fprintf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_redirect_fprintf
)

add_executable(test_redirect_fprintf2 test_redirect_fprintf2.cpp)
target_link_libraries(test_redirect_fprintf2 JScript::JScript ${FilesystemLibrary})
set_target_properties(test_redirect_fprintf2
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_redirect_fprintf2
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_redirect_fprintf2
)

add_executable(test_log_cb test_log_cb.cpp)
target_link_libraries(test_log_cb JScript::JScript ${FilesystemLibrary})
set_target_properties(test_log_cb
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_log_cb
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_log_cb
)

add_executable(test_init_script test_init_script.cpp)
target_link_libraries(test_init_script JScript::JScript ${FilesystemLibrary})
set_target_properties(test_init_script
    PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH True
)
add_test(
    NAME test_init_script
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND test_init_script
)
