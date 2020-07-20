// Test for jscript Conan package manager
// Simple addon for Node.js
// Dmitriy Vetutnev, Odant, 2020


#include <node_version.h> // Define NAPI_VERSION

#if !defined(NAPI_VERSION)
#error NAPI_VERSION not defined
#endif

#include <node_api.h>

#include <cassert>

napi_value Method(napi_env env, napi_callback_info) {
  napi_status status;
  napi_value world;
  const char str[] = "world, I`m napi_addon!";
  status = napi_create_string_utf8(env, str, sizeof(str), &world);
  assert(status == napi_ok);
  return world;
}

#define DECLARE_NAPI_METHOD(name, func)                                        \
  { name, 0, func, 0, 0, 0, napi_default, 0 }

napi_value Init(napi_env env, napi_value exports) {
  napi_status status;
  napi_property_descriptor desc = DECLARE_NAPI_METHOD("hello", Method);
  status = napi_define_properties(env, exports, 1, &desc);
  assert(status == napi_ok);
  return exports;
}

//#define NODE_GYP_MODULE_NAME napi_addon

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
