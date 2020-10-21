#pragma once

#include <string>
#include <vector>
#include <functional>

#ifdef _WIN32
# ifndef USE_JSCRIPT
#   define JSCRIPT_EXTERN __declspec(dllexport)
# else
#   define JSCRIPT_EXTERN __declspec(dllimport)
# endif
#else
# define JSCRIPT_EXTERN /* nothing */
#endif

#ifdef USE_JSCRIPT
#  include "v8.h"
#endif


namespace node {
namespace jscript {


enum JSLogType {
    LOG_TYPE,
    WARN_TYPE, 
    ERROR_TYPE,
    
    DEFAULT_TYPE = LOG_TYPE
};

typedef std::function<void(const v8::FunctionCallbackInfo<v8::Value>&, const JSLogType)>  JSLogCallback;

struct JSCallbackInfo {
    std::string          name;
    v8::FunctionCallback function = nullptr;
    void*                external = nullptr;
};


class JSInstance { };

JSCRIPT_EXTERN void Initialize(int argc, const char**);
JSCRIPT_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               std::string executeFile, std::string coreFolder, std::string nodeFolder, // copy, not reference
                               std::function<void(const std::string&)> logCallback = nullptr);
JSCRIPT_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               std::string executeFile, std::string coreFolder, // copy, not reference
                               const std::vector<std::string>& nodeFolders,
                               std::function<void(const std::string&)> logCallback = nullptr);


JSCRIPT_EXTERN void SetLogCallback(JSInstance* instance, JSLogCallback& cb);

JSCRIPT_EXTERN void Uninitilize();

typedef enum {
    JS_SUCCESS = 0,
    JS_ERROR
} result_t;

JSCRIPT_EXTERN result_t CreateInstance(JSInstance** outNewInstance);
JSCRIPT_EXTERN result_t StopInstance(JSInstance* instance);

JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance, const std::string& script);
JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance, const std::string& script, const std::vector<JSCallbackInfo>& callbacks);


} // namespace jscript
} // namespace node


#ifdef USE_JSCRIPT
namespace jscript = node::jscript;
#endif
