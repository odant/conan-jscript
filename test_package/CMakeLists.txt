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


function(library_test name)
    add_executable(${name} "${name}.cpp")
    target_link_libraries(${name} CONAN_PKG::jscript ${FilesystemLibrary})
    set_target_properties(${name}
        PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH True
    )
    add_test(
        NAME ${name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        COMMAND ${name}
    )
endfunction()

find_program(JSCRIPT_INTERPRETER
    NAMES jscript jscriptd
    PATHS ${CMAKE_CURRENT_BINARY_DIR}/bin
    NO_DEFAULT_PATH
)

function(interpreter_test name)
    add_test(
        NAME ${name}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
        COMMAND ${JSCRIPT_INTERPRETER} $ARGN
    )
    set_property(TEST ${name} PROPERTY WILL_FAIL True)
    if(UNIX)
        set_property(TEST ${name} PROPERTY ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/bin)
    endif()
endfunction()


library_test(test_jscript)
library_test(test_multiple_instances)
library_test(test_headers)
interpreter_test(test_compile_failed_interpreter -e "compile failed zzzzzzzzz")
library_test(test_compile_failed)
interpreter_test(test_throw_exception_interpreter -e "throw 'Error42'" --trace-uncaught)
library_test(test_throw_exception)
library_test(test_multiple_paths_node_modules)
library_test(test_external_init_script)
library_test(test_redirect_fprintf)
library_test(test_redirect_fprintf2)
library_test(test_log_cb)
library_test(test_init_script)

library_test(test_napi_addon)
add_library(napi_addon SHARED
    napi_addon.cpp
    #${JScript_INCLUDE_DIRS}/win_delay_load_hook.cc
)
set_target_properties(napi_addon PROPERTIES PREFIX "" SUFFIX ".node")
target_compile_definitions(napi_addon PRIVATE BUILDING_NODE_EXTENSION)
target_link_libraries(napi_addon CONAN_PKG::jscript)
add_dependencies(test_napi_addon napi_addon)
