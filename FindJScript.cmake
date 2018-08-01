# FindJScript.cmake for Conan jscript package (ODANT Node.JS version)
# Dmitriy Vetutnev, ODANT, 2018


# Find include path
find_path(JScript_INCLUDE_DIR
    NAMES jscript.h
    PATHS ${CONAN_INCLUDE_DIRS_JSCRIPT}
    NO_DEFAULT_PATH
)

# Find library
find_library(JScript_LIBRARY
    NAMES jscript jscript.dll jscriptd.dll jscript64.dll jscript64d.dll
    PATHS ${CONAN_LIB_DIRS_JSCRIPT}
    NO_DEFAULT_PATH
)

# Parse version
if(JScript_INCLUDE_DIR AND EXISTS ${JScript_INCLUDE_DIR}/node_version.h)

    file(STRINGS ${JScript_INCLUDE_DIR}/node_version.h DEFINE_NODE_MAJOR_VERSION REGEX "^#define NODE_MAJOR_VERSION")
    string(REGEX REPLACE "^.*NODE_MAJOR_VERSION +([0-9]+).*$" "\\1" JScript_VERSION_MAJOR "${DEFINE_NODE_MAJOR_VERSION}")

    file(STRINGS ${JScript_INCLUDE_DIR}/node_version.h DEFINE_NODE_MINOR_VERSION REGEX "^#define NODE_MINOR_VERSION")
    string(REGEX REPLACE "^.*NODE_MINOR_VERSION +([0-9]+).*$" "\\1" JScript_VERSION_MINOR "${DEFINE_NODE_MINOR_VERSION}")

    file(STRINGS ${JScript_INCLUDE_DIR}/node_version.h DEFINE_NODE_PATCH_VERSION REGEX "^#define NODE_PATCH_VERSION")
    string(REGEX REPLACE "^.*NODE_PATCH_VERSION +([0-9]+).*$" "\\1" JScript_VERSION_PATCH "${DEFINE_NODE_PATCH_VERSION}")

    file(STRINGS ${JScript_INCLUDE_DIR}/node_version.h DEFINE_NODE_BUILD_VERSION REGEX "^#define NODE_BUILD_VERSION")
    string(REGEX REPLACE "^.*NODE_BUILD_VERSION +([0-9]+).*$" "\\1" JScript_VERSION_BUILD "${DEFINE_NODE_BUILD_VERSION}")
    
    set(JScript_VERSION_STRING "${JScript_VERSION_MAJOR}.${JScript_VERSION_MINOR}.${JScript_VERSION_PATCH}.${JScript_VERSION_BUILD}")
    set(JScript_VERSION ${JScript_VERSION_STRING})
    mark_as_advanced(JScript_VERSION_STRING)
    set(JScript_VERSION_COUNT 4)

endif()

# Check variables
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JScript
    REQUIRED_VARS JScript_INCLUDE_DIR JScript_LIBRARY
    VERSION_VAR JScript_VERSION
)

# Add imported target
if(JScript_FOUND AND NOT TARGET JScript::JScript)

    include(CMakeFindDependencyMacro)
    find_dependency(OpenSSL)

    add_library(JScript::JScript UNKNOWN IMPORTED)
    set_target_properties(JScript::JScript PROPERTIES
        IMPORTED_LOCATION ${JScript_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${JScript_INCLUDE_DIR}
        INTERFACE_LINK_LIBRARIES OpenSSL::SSL
        INTERFACE_COMPILE_DEFINITIONS "${CONAN_COMPILE_DEFINITIONS_JSCRIPT}"
    )
    
    set(JScript_INCLUDE_DIRS ${JScript_INCLUDE_DIR})
    set(JScript_LIBRARIES ${JScript_LIBRARY})
    mark_as_advanced(JScript_INCLUDE_DIR JScript_LIBRARY)
    set(PCRE_DEFINITIONS "${CONAN_COMPILE_DEFINITIONS_JSCRIPT}")

endif()
