#pragma once


#include <v8.h>

#ifdef _MSC_VER
#    pragma warning (push)
#    pragma warning (disable:4251 4275)
#endif
#include <node.h>
#ifdef _MSC_VER
#    pragma warning (pop)
#endif


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


enum class ConsoleType {
    Log   = 1,
    Warn  = 2,
    Error = 3,

    Default = Log
};

using ConsoleCallback = std::function<void(const v8::FunctionCallbackInfo<v8::Value>&, ConsoleType)>;

NODE_EXTERN void SetConsoleCallback(JSInstance* instance, ConsoleCallback cb);

inline std::ostream& operator<< (std::ostream& os, const ConsoleType type) {
    os << "ConsoleType: ";

    switch (type) {

        case ConsoleType::Log: {
            os << "Log";
        } break;

        case ConsoleType::Warn: {
            os << "Warn";
        } break;

        case ConsoleType::Error: {
            os << "Error";
        } break;

        default: {
            os << "Default";
        } break;
    }

    return os;
}


NODE_EXTERN MultiIsolatePlatform* GetGlobalPlatform();


} // namespace jscript
} // namespace node
