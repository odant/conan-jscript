#pragma once


#include <v8.h>
#include <node.h>

#include <string>
#include <vector>
#include <functional>
#include <ostream>


namespace node {
namespace jscript {


NODE_EXTERN void Initialize(const std::vector<std::string>& argv,
                               const std::string& nodeFolder,   // set NODE_PATH environment variable (node modules paths)
                               std::function<void(const std::string&)> redirectFPrintF = nullptr);


NODE_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               const std::string& executeFile, const std::string& coreFolder, const std::string& nodeFolder,
                               std::function<void(const std::string&)> redirectFPrintF = nullptr);

NODE_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               const std::string& executeFile, const std::string& coreFolder, const std::vector<std::string>& nodeFolders,
                               std::function<void(const std::string&)> redirectFPrintF = nullptr);

NODE_EXTERN void Initialize(const std::string& origin, const std::string& externalOrigin,
                               const std::string& executeFile, const std::string& coreFolder, const std::vector<std::string>& nodeFolders,
                               const std::string& initScript,
                               std::function<void(const std::string&)> redirectFPrintF = nullptr);


NODE_EXTERN void Uninitilize();


typedef enum {
    JS_SUCCESS = 0,
    JS_ERROR
} result_t;

class JSInstance { };

NODE_EXTERN result_t CreateInstance(JSInstance** outNewInstance);
NODE_EXTERN result_t StopInstance(JSInstance* instance);


struct JSCallbackInfo {
    std::string          name;
    v8::FunctionCallback function = nullptr;
    void*                external = nullptr;
};

NODE_EXTERN result_t RunScriptText(JSInstance* instance, const std::string& script);
NODE_EXTERN result_t RunScriptText(JSInstance* instance, const std::string& script, const std::vector<JSCallbackInfo>& callbacks);


enum JSLogType {
    LOG_TYPE,
    WARN_TYPE,
    ERROR_TYPE,

    DEFAULT_TYPE = LOG_TYPE
};

using JSLogCallback = std::function<void(const v8::FunctionCallbackInfo<v8::Value>&, const JSLogType)>;

NODE_EXTERN void SetLogCallback(JSInstance* instance, JSLogCallback cb);

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


NODE_EXTERN MultiIsolatePlatform* GetGlobalPlatform();


} // namespace jscript
} // namespace node


#ifdef USE_JSCRIPT
namespace jscript = node::jscript;
#endif
