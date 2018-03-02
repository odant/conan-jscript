// jscript-dll-test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <Windows.h>

#define USE_JSCRIPT
#include "../jscript.h"

#include "boost/thread/mutex.hpp"
#include "boost/thread/condition_variable.hpp"

v8::Local<v8::Value> convert(v8::Isolate* isolate, const char* str) {
    return v8::String::NewFromOneByte(isolate,
                                      reinterpret_cast<const uint8_t*>(str),
                                      v8::NewStringType::kNormal).ToLocalChecked();
}

boost::mutex mutex;
boost::condition_variable cond;
int wait_count = 2;


int wmain(int argc, wchar_t *wargv[])
{
    int exit_code = 0;

    jscript::Initialize();

    //auto res1 = std::async(
    //    [=] () { 

            node::jscript::JSInstance* instance = nullptr;
            node::jscript::CreateInstance(&instance);

           ::Sleep(2000);

           //jscript::JSCallback callbackFunction = [] (const v8::FunctionCallbackInfo<v8::Value>& args) { 
           //};

           {
               jscript::JSCallbackInfo callbackInfo_1;

               callbackInfo_1.name = L"callback_1";
               callbackInfo_1.function = [] (const v8::FunctionCallbackInfo<v8::Value>& args) {
                   v8::Isolate* isolate = args.GetIsolate();
                   
                   v8::Local<v8::Context> context = isolate->GetCurrentContext();
                   v8::Context::Scope context_scope(context);

                   v8::Local<v8::Object> global = context->Global();

                   if (!global.IsEmpty()) {
                       v8::Local<v8::String> consoleName = v8::String::NewFromOneByte(isolate,
                                                                                      reinterpret_cast<const uint8_t*>("console"),
                                                                                      v8::NewStringType::kNormal,
                                                                                      7).ToLocalChecked();
                       v8::Local<v8::Value> find_result = global->Get(context, consoleName).ToLocalChecked();
                       if (!find_result.IsEmpty() && find_result->IsObject()) {
                           v8::Local<v8::String> logName = v8::String::NewFromOneByte(isolate,
                                                                                          reinterpret_cast<const uint8_t*>("log"),
                                                                                          v8::NewStringType::kNormal,
                                                                                          3).ToLocalChecked();
                           v8::Local<v8::Value> log_result = find_result.As<v8::Object>()->Get(context, logName).ToLocalChecked();

                           if (!log_result.IsEmpty() && log_result->IsFunction()) {
                               v8::Local<v8::Function> f = log_result.As<v8::Function>();
                               if (!f.IsEmpty()) {
                                   v8::Local<v8::Value> str = v8::String::NewFromOneByte(isolate,
                                                                                         reinterpret_cast<const uint8_t*>("1111111 - callback_1"),
                                                                                         v8::NewStringType::kNormal,
                                                                                         20).ToLocalChecked();

                                   v8::TryCatch trycatch(isolate);
                                   trycatch.SetVerbose(false);

                                   v8::Local<v8::Value> argv[] = { str };

                                   f->Call(v8::Null(isolate), 1, &str);

                                   if (trycatch.HasCaught()) {
                                       v8::Handle<v8::Value> exception = trycatch.Exception();
                                       v8::String::Utf8Value message(exception);
                                       std::cout << "exception: " << *message << std::endl;
                                   }
                               }
                           }
                       }
                   }

                   args.GetReturnValue().Set(42);
                   
                   {
                       boost::lock_guard<boost::mutex> lock(mutex);
                       --wait_count;
                   }
                   cond.notify_one();
               };

               jscript::JSCallbackInfo callbackInfo_2;

               callbackInfo_2.name = L"callback_2";
               callbackInfo_2.function = [] (const v8::FunctionCallbackInfo<v8::Value>& args) {
                   v8::Isolate* isolate = args.GetIsolate();

                   v8::Local<v8::Context> context = isolate->GetCurrentContext();
                   v8::Context::Scope context_scope(context);

                   v8::Local<v8::Object> global = context->Global();
                   if (!global.IsEmpty()) {
                       v8::Local<v8::String> consoleName = v8::String::NewFromOneByte(isolate,
                                                                                      reinterpret_cast<const uint8_t*>("console"),
                                                                                      v8::NewStringType::kNormal,
                                                                                      7).ToLocalChecked();
                       v8::Local<v8::Value> find_result = global->Get(context, consoleName).ToLocalChecked();
                       if (!find_result.IsEmpty() && find_result->IsObject()) {
                           v8::Local<v8::String> logName = v8::String::NewFromOneByte(isolate,
                                                                                      reinterpret_cast<const uint8_t*>("log"),
                                                                                      v8::NewStringType::kNormal,
                                                                                      3).ToLocalChecked();
                           v8::Local<v8::Value> log_result = find_result.As<v8::Object>()->Get(context, logName).ToLocalChecked();

                           if (!log_result.IsEmpty() && log_result->IsFunction()) {
                               v8::Local<v8::Function> f = log_result.As<v8::Function>();
                               if (!f.IsEmpty()) {
                                   v8::Local<v8::Value> str = v8::String::NewFromOneByte(isolate,
                                                                                         reinterpret_cast<const uint8_t*>("1111111 - callback_2"),
                                                                                         v8::NewStringType::kNormal,
                                                                                         20).ToLocalChecked();

                                   v8::TryCatch trycatch(isolate);
                                   trycatch.SetVerbose(false);

                                   v8::Local<v8::Value> argv[] = { str };

                                   f->Call(v8::Null(isolate), 1, &str);

                                   if (trycatch.HasCaught()) {
                                       v8::Handle<v8::Value> exception = trycatch.Exception();
                                       v8::String::Utf8Value message(exception);
                                       std::cout << "exception: " << *message << std::endl;
                                   }
                               }
                           }
                       }
                   }

                   args.GetReturnValue().Set(true);

                   {
                       boost::lock_guard<boost::mutex> lock(mutex);
                       --wait_count;
                   }
                   cond.notify_one();
               };

               jscript::JSCallbackInfo* callbacks[] = { &callbackInfo_1, &callbackInfo_2, nullptr };
               node::jscript::RunScriptText(instance,
                                            L"console.log('Start method!');\r\n"
                                            L"setTimeout(()=>{console.log(callback_1());},5000);\r\n"
                                            L"setTimeout(()=>{console.log(callback_2());},2500);\r\n"
                                            , callbacks);

               {
                   boost::unique_lock<boost::mutex> lock(mutex);
                   while (wait_count != 0)
                       cond.wait(lock);
               }

           }

           {
               jscript::JSCallbackInfo callbackInfo_1;

               callbackInfo_1.name = L"callback_1";
               callbackInfo_1.function = [] (const v8::FunctionCallbackInfo<v8::Value>& args) {
                   v8::Isolate* isolate = args.GetIsolate();

                   v8::Local<v8::Context> context = isolate->GetCurrentContext();
                   v8::Context::Scope context_scope(context);

                   v8::Local<v8::Object> global = context->Global();
                   if (!global.IsEmpty()) {
                       v8::Local<v8::String> consoleName = v8::String::NewFromOneByte(isolate,
                                                                                      reinterpret_cast<const uint8_t*>("console"),
                                                                                      v8::NewStringType::kNormal,
                                                                                      7).ToLocalChecked();
                       v8::Local<v8::Value> find_result = global->Get(context, consoleName).ToLocalChecked();
                       if (!find_result.IsEmpty() && find_result->IsObject()) {
                           v8::Local<v8::String> logName = v8::String::NewFromOneByte(isolate,
                                                                                      reinterpret_cast<const uint8_t*>("log"),
                                                                                      v8::NewStringType::kNormal,
                                                                                      3).ToLocalChecked();
                           v8::Local<v8::Value> log_result = find_result.As<v8::Object>()->Get(context, logName).ToLocalChecked();

                           if (!log_result.IsEmpty() && log_result->IsFunction()) {
                               v8::Local<v8::Function> f = log_result.As<v8::Function>();
                               if (!f.IsEmpty()) {
                                   v8::Local<v8::Value> str = v8::String::NewFromOneByte(isolate,
                                                                                         reinterpret_cast<const uint8_t*>("2222222 - callback_1"),
                                                                                         v8::NewStringType::kNormal,
                                                                                         20).ToLocalChecked();

                                   v8::TryCatch trycatch(isolate);
                                   trycatch.SetVerbose(false);

                                   v8::Local<v8::Value> argv[] = { str };

                                   f->Call(v8::Null(isolate), 1, &str);

                                   if (trycatch.HasCaught()) {
                                       v8::Handle<v8::Value> exception = trycatch.Exception();
                                       v8::String::Utf8Value message(exception);
                                       std::cout << "exception: " << *message << std::endl;
                                   }
                               }
                           }
                       }
                   }

                   v8::Local<v8::Object> obj = v8::Object::New(isolate);
                   obj->Set(convert(isolate, "key1"), convert(isolate, "val1"));
                   obj->Set(convert(isolate, "key2"), convert(isolate, "val2"));
                   obj->Set(convert(isolate, "key3"), convert(isolate, "val3"));

                   args.GetReturnValue().Set(obj);
               };

               jscript::JSCallbackInfo callbackInfo_2;

               callbackInfo_2.name = L"callback_2";
               callbackInfo_2.function = [] (const v8::FunctionCallbackInfo<v8::Value>& args) {
                   v8::Isolate* isolate = args.GetIsolate();

                   v8::Local<v8::Context> context = isolate->GetCurrentContext();
                   v8::Context::Scope context_scope(context);

                   v8::Local<v8::Object> global = context->Global();
                   if (!global.IsEmpty()) {
                       v8::Local<v8::String> consoleName = v8::String::NewFromOneByte(isolate,
                                                                                      reinterpret_cast<const uint8_t*>("console"),
                                                                                      v8::NewStringType::kNormal,
                                                                                      7).ToLocalChecked();
                       v8::Local<v8::Value> find_result = global->Get(context, consoleName).ToLocalChecked();
                       if (!find_result.IsEmpty() && find_result->IsObject()) {
                           v8::Local<v8::String> logName = v8::String::NewFromOneByte(isolate,
                                                                                      reinterpret_cast<const uint8_t*>("log"),
                                                                                      v8::NewStringType::kNormal,
                                                                                      3).ToLocalChecked();
                           v8::Local<v8::Value> log_result = find_result.As<v8::Object>()->Get(context, logName).ToLocalChecked();

                           if (!log_result.IsEmpty() && log_result->IsFunction()) {
                               v8::Local<v8::Function> f = log_result.As<v8::Function>();
                               if (!f.IsEmpty()) {
                                   v8::Local<v8::Value> str = v8::String::NewFromOneByte(isolate,
                                                                                         reinterpret_cast<const uint8_t*>("2222222 - callback_2"),
                                                                                         v8::NewStringType::kNormal,
                                                                                         20).ToLocalChecked();

                                   v8::TryCatch trycatch(isolate);
                                   trycatch.SetVerbose(false);

                                   v8::Local<v8::Value> argv[] = { str };

                                   f->Call(v8::Null(isolate), 1, &str);

                                   if (trycatch.HasCaught()) {
                                       v8::Handle<v8::Value> exception = trycatch.Exception();
                                       v8::String::Utf8Value message(exception);
                                       std::cout << "exception: " << *message << std::endl;
                                   }
                               }
                           }
                       }
                   }

                   args.GetReturnValue().Set(42);
               };

               jscript::JSCallbackInfo* callbacks[] = { &callbackInfo_1, &callbackInfo_2, nullptr };
               node::jscript::RunScriptText(instance,
                                            L"console.log('Start method!');\r\n"
                                            L"setTimeout(()=>{console.log(callback_1());},300);\r\n"
                                            L"setTimeout(()=>{console.log(callback_2());},250);\r\n"
                                            , callbacks);
           }

            //::Sleep(10000);

            //node::jscript::RunScriptText(instance,
            //                             L"process._getActiveHandles().forEach(item=>{ if (item.close) item.close(); });\r\n"
            //);

            ::Sleep(100000);

            node::jscript::StopInstance(instance);
    //    }
    //);

    ::Sleep(500);

    auto res2 = std::async([=] () {
        node::jscript::JSInstance* instance = nullptr;
        node::jscript::CreateInstance(&instance);

        //::Sleep(2000);

        node::jscript::RunScriptText(instance,
                                     L"'use strict';\r\n"
                                     L"console.log('--------------------------------');\r\n"
                                     L"console.log('           RunMethod            ');\r\n"
                                     L"console.log('--------------------------------');\r\n"
                                     L"global.odantFramework.then(odant => {\r\n"
                                     L"  odant.executeMethod('/H:1D03A3F3B5863BB/P:WORK/B:1D03A40BBA30B8C/C:1D1BD7DB1E0A50A', 'test', { a:3333, b : 7777 }).then((result) =>{\r\n"
                                     L"      console.log(result)\r\n"
                                     L"  }).catch ((error) =>{\r\n"
                                     L"      console.log(error);\r\n"
                                     L"  });\r\n"
                                     L"});\r\n"
        );

        ::Sleep(5000);

        node::jscript::StopInstance(instance);
    });

    auto res3 = std::async([=] () {
        node::jscript::JSInstance* instance = nullptr;
        node::jscript::CreateInstance(&instance);

        //::Sleep(2000);

        node::jscript::RunScriptText(instance,
                                     L"'use strict';\r\n"
                                     L"console.log('--------------------------------');\r\n"
                                     L"console.log('           RunMethod            ');\r\n"
                                     L"console.log('--------------------------------');\r\n"
                                     L"global.odantFramework.then(odant => {\r\n"
                                     L"  odant.executeMethod('/H:1D03A3F3B5863BB/P:WORK/B:1D03A40BBA30B8C/C:1D1BD7DB1E0A50A', 'test', { a:12345, b : 54321 }).then((result) =>{\r\n"
                                     L"      console.log(result)\r\n"
                                     L"  }).catch ((error) =>{\r\n"
                                     L"      console.log(error);\r\n"
                                     L"  });\r\n"
                                     L"});\r\n"
        );

        ::Sleep(5000);

        node::jscript::StopInstance(instance);
    });

    //res1.wait();
    res2.wait();
    res3.wait();

    ::Sleep(200000);

    jscript::Uninitilize();

    std::cout << std::endl << std::endl << "Finished. Pres any key to exit...";
    getchar();
    
    return exit_code;
}

