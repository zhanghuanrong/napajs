// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "zone-wrap.h"
#include "zone/zone-emitter.h"

#include "transport-context-wrap-impl.h"

#include <napa/zone.h>
#include <napa/assert.h>
#include <napa/async.h>
#include <napa/v8-helpers.h>

#include <sstream>
#include <vector>

using namespace napa::module;
using namespace napa::v8_helpers;

NAPA_DEFINE_PERSISTENT_CONSTRUCTOR(ZoneWrap);

// Forward declaration.
static v8::Local<v8::Object> CreateResponseObject(const napa::Result& result);
template <typename Func>
static void CreateRequestAndExecute(v8::Local<v8::Object> obj, Func&& func);

void ZoneWrap::Init() {
    auto isolate = v8::Isolate::GetCurrent();

    // Prepare constructor template.
    auto functionTemplate = v8::FunctionTemplate::New(isolate, DefaultConstructorCallback<ZoneWrap>);
    functionTemplate->SetClassName(MakeV8String(isolate, exportName));
    functionTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    // Prototypes.
    NAPA_SET_PROTOTYPE_METHOD(functionTemplate, "getId", GetId);
    NAPA_SET_PROTOTYPE_METHOD(functionTemplate, "broadcast", Broadcast);
    NAPA_SET_PROTOTYPE_METHOD(functionTemplate, "broadcastSync", BroadcastSync);
    NODE_SET_PROTOTYPE_METHOD(functionTemplate, "execute", Execute);
    NODE_SET_PROTOTYPE_METHOD(functionTemplate, "executeSync", ExecuteSync);
    NODE_SET_PROTOTYPE_METHOD(functionTemplate, "recycle", Recycle);
    NODE_SET_PROTOTYPE_METHOD(functionTemplate, "on", On);

    // Set persistent constructor into V8.
    NAPA_SET_PERSISTENT_CONSTRUCTOR(exportName, functionTemplate->GetFunction());
}

v8::Local<v8::Object> ZoneWrap::NewInstance(std::unique_ptr<napa::Zone> zoneProxy) {
    auto isolate = v8::Isolate::GetCurrent();
    auto context = isolate->GetCurrentContext();

    auto constructor = NAPA_GET_PERSISTENT_CONSTRUCTOR(exportName, ZoneWrap);
    auto object = constructor->NewInstance(context).ToLocalChecked();
    auto wrap = NAPA_OBJECTWRAP::Unwrap<ZoneWrap>(object);

    wrap->_zoneProxy = std::move(zoneProxy);
    return object;
}

void ZoneWrap::GetId(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();

    auto wrap = ObjectWrap::Unwrap<ZoneWrap>(args.Holder());

    args.GetReturnValue().Set(MakeV8String(isolate, wrap->_zoneProxy->GetId()));
}

void ZoneWrap::Broadcast(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();

    CHECK_ARG(isolate, args[0]->IsObject(), "first argument to zone.broadcast must be the function spec object");
    CHECK_ARG(isolate, args[1]->IsFunction(), "second argument to zone.broadcast must be the callback");
    
    v8::String::Utf8Value source(args[0]->ToString());

    napa::zone::DoAsyncWork(v8::Local<v8::Function>::Cast(args[1]),
        [&args](std::function<void(void*)> complete) {
            CreateRequestAndExecute(args[0]->ToObject(), [&args, &complete](const napa::FunctionSpec& spec) {
                auto wrap = ObjectWrap::Unwrap<ZoneWrap>(args.Holder());

                wrap->_zoneProxy->Broadcast(spec, [complete = std::move(complete)](napa::Result result) {
                    complete(new napa::Result(std::move(result)));
                });
            });
        },
        [](auto jsCallback, void* res) {
            auto isolate = v8::Isolate::GetCurrent();
            auto context = isolate->GetCurrentContext();

            auto result = static_cast<napa::Result*>(res);

            v8::HandleScope scope(isolate);

            std::vector<v8::Local<v8::Value>> argv;
            argv.emplace_back(CreateResponseObject(*result));

            (void)jsCallback->Call(context, context->Global(), static_cast<int>(argv.size()), argv.data());

            delete result;
        }
    );
}

void ZoneWrap::BroadcastSync(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();

    CHECK_ARG(isolate, args[0]->IsObject(), "first argument to zone.broadcastSync must be the function spec object");

    CreateRequestAndExecute(args[0]->ToObject(), [&args](const napa::FunctionSpec& spec) {
        auto wrap = ObjectWrap::Unwrap<ZoneWrap>(args.Holder());

        napa::Result result = wrap->_zoneProxy->BroadcastSync(spec);
        args.GetReturnValue().Set(CreateResponseObject(result));
    });
}

void ZoneWrap::Execute(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();

    CHECK_ARG(isolate, args[0]->IsObject(), "first argument to zone.execute must be the function spec object");
    CHECK_ARG(isolate, args[1]->IsFunction(), "second argument to zone.execute must be the callback");

    napa::zone::DoAsyncWork(v8::Local<v8::Function>::Cast(args[1]),
        [&args](std::function<void(void*)> complete) {
            CreateRequestAndExecute(args[0]->ToObject(), [&args, &complete](const napa::FunctionSpec& spec) {
                auto wrap = ObjectWrap::Unwrap<ZoneWrap>(args.Holder());

                wrap->_zoneProxy->Execute(spec, [complete = std::move(complete)](napa::Result result) {
                    complete(new napa::Result(std::move(result)));
                });
            });
        },
        [](auto jsCallback, void* res) {
            auto isolate = v8::Isolate::GetCurrent();
            auto context = isolate->GetCurrentContext();

            auto result = static_cast<napa::Result*>(res);

            v8::HandleScope scope(isolate);

            std::vector<v8::Local<v8::Value>> argv;
            argv.emplace_back(CreateResponseObject(*result));

            (void)jsCallback->Call(context, context->Global(), static_cast<int>(argv.size()), argv.data());

            delete result;
        }
    );
}

void ZoneWrap::ExecuteSync(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();

    CHECK_ARG(isolate, args[0]->IsObject(), "first argument to zone.execute must be the function spec object");

    CreateRequestAndExecute(args[0]->ToObject(), [&args](const napa::FunctionSpec& spec) {
        auto wrap = ObjectWrap::Unwrap<ZoneWrap>(args.Holder());

        napa::Result result = wrap->_zoneProxy->ExecuteSync(spec);
        args.GetReturnValue().Set(CreateResponseObject(result));
    });
}

static v8::Local<v8::Object> CreateResponseObject(const napa::Result& result) {
    auto isolate = v8::Isolate::GetCurrent();
    auto context = isolate->GetCurrentContext();

    auto responseObject = v8::Object::New(isolate);

    (void)responseObject->CreateDataProperty(
        context,
        MakeV8String(isolate, "code"),
        v8::Uint32::NewFromUnsigned(isolate, result.code));

    (void)responseObject->CreateDataProperty(
        context,
        MakeV8String(isolate, "errorMessage"),
        MakeV8String(isolate, result.errorMessage));

    (void)responseObject->CreateDataProperty(
        context,
        MakeV8String(isolate, "returnValue"),
        MakeV8String(isolate, result.returnValue));

    // Transport context handle
    v8::Local<v8::Value> contextHandleValue;
    auto transportContext = result.transportContext.release();
    if (transportContext == nullptr) {
        contextHandleValue = v8::Null(isolate);
    } else {
        contextHandleValue = PtrToV8Uint32Array(isolate, transportContext);
    }
    (void)responseObject->CreateDataProperty(
        context,
        MakeV8String(isolate, "contextHandle"),
        contextHandleValue);

    return responseObject;
}

template <typename Func>
static void CreateRequestAndExecute(v8::Local<v8::Object> obj, Func&& func) {
    auto isolate = v8::Isolate::GetCurrent();
    auto context = isolate->GetCurrentContext();

    napa::FunctionSpec spec;
    
    // module property is optional in a spec
    Utf8String module;
    auto maybe = obj->Get(context, MakeV8String(isolate, "module"));
    if (!maybe.IsEmpty()) {
        module = Utf8String(maybe.ToLocalChecked());
        spec.module = NAPA_STRING_REF_WITH_SIZE(module.Data(), module.Length());
    }

    // function property is mandatory in a spec
    maybe = obj->Get(context, MakeV8String(isolate, "function"));
    CHECK_ARG(isolate, !maybe.IsEmpty(), "function property is missing in function spec object");

    auto functionValue = maybe.ToLocalChecked();
    CHECK_ARG(isolate, functionValue->IsString(), "function property in function spec object must be a string");

    v8::String::Utf8Value function(functionValue->ToString());
    spec.function = NAPA_STRING_REF_WITH_SIZE(*function, static_cast<size_t>(function.length()));

    // arguments are optional in a spec
    maybe = obj->Get(context, MakeV8String(isolate, "arguments"));
    std::vector<Utf8String> arguments;
    if (!maybe.IsEmpty()) {
        arguments = V8ArrayToVector<Utf8String>(isolate, v8::Local<v8::Array>::Cast(maybe.ToLocalChecked()));

        spec.arguments.reserve(arguments.size());
        for (const auto& arg : arguments) {
            spec.arguments.emplace_back(NAPA_STRING_REF_WITH_SIZE(arg.Data(), arg.Length()));
        }
    }

    // options argument is optional.
    maybe = obj->Get(context, MakeV8String(isolate, "options"));
    if (!maybe.IsEmpty()) {
        auto optionsValue = maybe.ToLocalChecked();
        JS_ENSURE(isolate, optionsValue->IsObject(), "argument 'options' must be an object.");
        auto options = v8::Local<v8::Object>::Cast(optionsValue);

        // timeout is optional.
        maybe = options->Get(context, MakeV8String(isolate, "timeout"));
        if (!maybe.IsEmpty()) {
            spec.options.timeout = maybe.ToLocalChecked()->Uint32Value(context).FromJust();
        }

        // transport option is optional.
        maybe = options->Get(context, MakeV8String(isolate, "transport"));
        if (!maybe.IsEmpty()) {
            spec.options.transport = static_cast<napa::TransportOption>(maybe.ToLocalChecked()->Uint32Value(context).FromJust());
        }
    }

    // transportContext property is mandatory in a spec
    maybe = obj->Get(context, MakeV8String(isolate, "transportContext"));
    CHECK_ARG(isolate, !maybe.IsEmpty(), "transportContext property is missing in function spec object");

    // for broadcast(), we are not creating transportContext. The value of the property is set to null
    // otherwise it should be an object.
    auto transportContextValue = maybe.ToLocalChecked();
    if (!transportContextValue->IsNull()) {
        CHECK_ARG(isolate, transportContextValue->IsObject(), "transportContext must be null or an object.");
        auto transportContextWrap = NAPA_OBJECTWRAP::Unwrap<TransportContextWrapImpl>(transportContextValue->ToObject());
        spec.transportContext.reset(transportContextWrap->Get());
    }

    // Execute
    func(spec);
}

void ZoneWrap::Recycle(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();

    auto wrap = ObjectWrap::Unwrap<ZoneWrap>(args.Holder());

    wrap->_zoneProxy->Recycle();
}


// When call some zone's On() in specific worker (highly possible in node's main loop), 
//     *) new ZoneEmitContext will be created, where
//          +) uv_async_t handle will be created and added to current worker's loop, 
//             it will be activated when the event is emitted from the target zone.
//          *) wrapper call back logic will be created to call the jsFunc when
//             above uv_async_t handle is activated and executed later in current worker's 
//             loop. at that time, real call back args are known and should be
//             set in the ZoneEmitContext's _args.
void ZoneWrap::On(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();
    auto wrap = ObjectWrap::Unwrap<ZoneWrap>(args.Holder());

    CHECK_ARG(isolate, args[0]->IsString(), "first argument to zone.On must be the string");
    CHECK_ARG(isolate, args[1]->IsFunction(), "second argument to zone.On must be function");
    
    v8::String::Utf8Value event(args[0]->ToString());
    std::string eventName(*event, event.length());
    v8::Local<v8::Function> jsFunction = v8::Local<v8::Function>::Cast(args[1]);

    auto persistContext = std::make_shared<v8::Persistent<v8::Context>>(isolate, isolate->GetCurrentContext());
    auto persistFunc = std::make_shared<v8::Persistent<v8::Function>>(isolate, jsFunction);

    auto emitContext = new napa::zone::ZoneEmitContext(
        eventName,
        [persistFunc, persistContext](napa::zone::ZoneEmitContext* emitContext) {
            auto isolate = v8::Isolate::GetCurrent();
            v8::HandleScope handle_scope(isolate);
            auto context = v8::Local<v8::Context>::New(isolate, *persistContext);
            v8::Context::Scope contextScope(context);
            v8::Local<v8::Context> local_context = v8::Context::New(isolate);

            auto jsCallback = v8::Local<v8::Function>::New(isolate, *persistFunc);
            std::vector<v8::Local<v8::Value>> parameters = emitContext->getCallingParameters(isolate);
            jsCallback->Call(context, context->Global(), static_cast<int>(parameters.size()), parameters.data());
            persistFunc->Reset();
            persistContext->Reset();
        }
    );
    
    // Hook helper to trigger and execute the event callback in caller's isolation.
    uv_loop_t* loop = reinterpret_cast<uv_loop_t*>(zone::WorkerContext::Get(zone::WorkerContextItem::EVENT_LOOP));
    uv_async_init(loop, &emitContext->_h, [](uv_async_t* h) {
            auto emitContext = reinterpret_cast<napa::zone::ZoneEmitContext*>(h->data);
            emitContext->_cb(emitContext);
            uv_close(reinterpret_cast<uv_handle_t*>(h), [](uv_handle_t* h) { 
                delete reinterpret_cast<napa::zone::ZoneEmitContext*>(h->data);
            });
        });
    emitContext->_h.data = emitContext;

    wrap->_zoneProxy->On(emitContext);
}
