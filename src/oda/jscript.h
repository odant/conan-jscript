#pragma once


#include <v8.h>

#include <string>
#include <vector>
#include <functional>
#include <ostream>


#ifdef _WIN32
# ifndef USE_JSCRIPT
#   define JSCRIPT_EXTERN __declspec(dllexport)
# else
#   define JSCRIPT_EXTERN __declspec(dllimport)
# endif
#else
# define JSCRIPT_EXTERN /* nothing */
#endif


namespace node {
namespace jscript {


JSCRIPT_EXTERN void Initialize(std::vector<std::string> argv);  // copy, not reference


JSCRIPT_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               std::string executeFile, std::string coreFolder, std::string nodeFolder, // copy, not reference
                               std::function<void(const std::string&)> redirectFPrintF = nullptr);
JSCRIPT_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               std::string executeFile, std::string coreFolder, // copy, not reference
                               const std::vector<std::string>& nodeFolders,
                               std::function<void(const std::string&)> redirectFPrintF = nullptr);

JSCRIPT_EXTERN void Uninitilize();


typedef enum {
    JS_SUCCESS = 0,
    JS_ERROR
} result_t;

class JSInstance { };

JSCRIPT_EXTERN result_t CreateInstance(JSInstance** outNewInstance);
JSCRIPT_EXTERN result_t StopInstance(JSInstance* instance);


struct JSCallbackInfo {
    std::string          name;
    v8::FunctionCallback function = nullptr;
    void*                external = nullptr;
};

JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance, const std::string& script);
JSCRIPT_EXTERN result_t RunScriptText(JSInstance* instance, const std::string& script, const std::vector<JSCallbackInfo>& callbacks);


enum JSLogType {
    LOG_TYPE,
    WARN_TYPE,
    ERROR_TYPE,

    DEFAULT_TYPE = LOG_TYPE
};

using JSLogCallback = std::function<void(const v8::FunctionCallbackInfo<v8::Value>&, const JSLogType)>;

JSCRIPT_EXTERN void SetLogCallback(JSInstance* instance, JSLogCallback cb);

inline std::ostream& operator<< (std::ostream& os, const JSLogType type) {
    os << "JSLogType: ";

    switch (type) {

        case JSLogType::LOG_TYPE: {
            os << "LOG_TYPE";
        } break;

        case JSLogType::WARN_TYPE: {
                os << "WARN_TYPE";
        } break;

        case JSLogType::ERROR_TYPE: {
                os << "ERROR_TYPE";
        } break;

        default: {
                os << "DEFAULT_TYPE";
        } break;
    }

    return os;
}


} // namespace jscript
} // namespace node


#ifdef USE_JSCRIPT
namespace jscript = node::jscript;
#endif
