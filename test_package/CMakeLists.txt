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

execute_process(COMMAND ${CMAKE_COMMAND} "-E" "copy_directory" ${CMAKE_SOURCE_DIR}/_esm_nodepath ${CMAKE_BINARY_DIR}/bin/node_modules/esm_nodepath)
execute_process(COMMAND ${CMAKE_COMMAND} "-E" "copy_directory" ${CMAKE_SOURCE_DIR}/_esm_nodepath2 ${CMAKE_BINARY_DIR}/bin/node_modules2/esm_nodepath2)


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
        COMMAND ${JSCRIPT_INTERPRETER} ${ARGN}
    )

    if(UNIX)
        set_property(TEST ${name} PROPERTY ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/bin)
    endif()
endfunction()


library_test(test_jscript)
library_test(test_multiple_instances)
library_test(test_headers)

interpreter_test(test_compile_failed_interpreter --eval "compile failed zzzzzzzzz")
set_property(TEST test_compile_failed_interpreter PROPERTY WILL_FAIL True)

library_test(test_compile_failed)

interpreter_test(test_throw_exception_interpreter --eval "throw 'Error42'" --trace-uncaught)
set_property(TEST test_throw_exception_interpreter PROPERTY WILL_FAIL True)

library_test(test_throw_exception)
library_test(test_multiple_paths_node_modules)
library_test(test_external_init_script)
library_test(test_redirect_fprintf)
library_test(test_redirect_fprintf2)
library_test(test_console_callback)
library_test(test_init_script)
library_test(test_global_platform)

interpreter_test(test_esm_nodepath_interpreter ${CMAKE_SOURCE_DIR}/test_esm_nodepath.mjs)
if(WIN32)
    set(NODE_ENV
        "NODE_PATH=${CMAKE_BINARY_DIR}/bin/node_modules\\\;${CMAKE_BINARY_DIR}/bin/node_modules2"
        "NODE_SKIP_PLATFORM_CHECK=1"
    )
else()
    set(NODE_ENV
        "NODE_PATH=${CMAKE_BINARY_DIR}/bin/node_modules:${CMAKE_BINARY_DIR}/bin/node_modules2"
    )
endif()
set_property(TEST test_esm_nodepath_interpreter PROPERTY ENVIRONMENT ${NODE_ENV})

library_test(test_napi_addon)
add_library(napi_addon SHARED
    napi_addon.cpp
    #${JScript_INCLUDE_DIRS}/win_delay_load_hook.cc
)
set_target_properties(napi_addon PROPERTIES PREFIX "" SUFFIX ".node")
target_compile_definitions(napi_addon PRIVATE BUILDING_NODE_EXTENSION)
target_link_libraries(napi_addon CONAN_PKG::jscript)
add_dependencies(test_napi_addon napi_addon)
