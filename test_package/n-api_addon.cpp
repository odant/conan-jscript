// Test for jscript Conan package manager
// Simple addon for Node.js
// Dmitriy Vetutnev, Odant, 2020


#include <node_version.h> // Define NAPI_VERSION

#if !defined(NAPI_VERSION)
#error NAPI_VERSION not defined
#endif

#include <node_api.h>

