// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "transport-context-wrap-impl.h"

#include <napa/module/shareable-wrap.h>
#include <napa/module/binding/wraps.h>

using namespace napa;
using namespace napa::transport;
using namespace napa::module;


NAPA_DEFINE_PERSISTENT_CONSTRUCTOR(TransportContextWrapImpl);

TransportContextWrapImpl::TransportContextWrapImpl(TransportContext* context, bool owning) : 
    _context(context), _owning(owning) {
}

v8::Local<v8::Object> TransportContextWrapImpl::NewInstance(bool owning, napa::transport::TransportContext* context) {
    auto isolate = v8::Isolate::GetCurrent();
    v8::EscapableHandleScope scope(isolate);

    v8::Local<v8::Value> argv[] = { v8::Boolean::New(isolate, owning), v8_helpers::PtrToV8Uint32Array(isolate, context) };
    auto object = napa::module::NewInstance<TransportContextWrapImpl>(sizeof(argv) / sizeof(v8::Local<v8::Value>), argv);
    RETURN_VALUE_ON_PENDING_EXCEPTION(object, v8::Local<v8::Object>());
    
    return scope.Escape(object.ToLocalChecked());
}

TransportContextWrapImpl::~TransportContextWrapImpl() {
    if (_owning) {
        delete _context;
    }
}

TransportContext* TransportContextWrapImpl::Get() {
    return _context;
}

void TransportContextWrapImpl::Init() {
    auto isolate = v8::Isolate::GetCurrent();
    auto constructorTemplate = v8::FunctionTemplate::New(isolate, TransportContextWrapImpl::ConstructorCallback);
    constructorTemplate->SetClassName(v8_helpers::MakeV8String(isolate, exportName));
    constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "saveShared", SaveSharedCallback);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "loadShared", LoadSharedCallback);
    NAPA_SET_ACCESSOR(constructorTemplate, "sharedCount", GetSharedCountCallback, nullptr);

    NAPA_SET_PERSISTENT_CONSTRUCTOR(exportName, constructorTemplate->GetFunction());
}

void TransportContextWrapImpl::GetSharedCountCallback(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& args){
    auto isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    auto thisObject = node::ObjectWrap::Unwrap<TransportContextWrapImpl>(args.Holder());
    JS_ENSURE(isolate, thisObject != nullptr, "Invalid object to get property \"sharedCount\".");

    args.GetReturnValue().Set(thisObject->_context->GetSharedCount());
}

void TransportContextWrapImpl::ConstructorCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);

    JS_ENSURE(isolate, args.IsConstructCall(), 
        "class \"TransportContextWrap\" allows constructor call only.");

    CHECK_ARG(isolate, args.Length() == 1 || args.Length() == 2, 
        "class \"TransportContextWrap\" accept a required boolean argument 'owning' and an optional argument 'handle' of Handle type.");

    CHECK_ARG(isolate, args[0]->IsBoolean(), "Argument \"owning\" must be boolean.");
    bool owning = args[0]->BooleanValue();

    // It's deleted when its Javascript object is garbage collected by V8's GC.
    TransportContext* context = nullptr;
    
    if (args.Length() == 1 || args[1]->IsUndefined()) {
        context = new TransportContext();
    } else {
        auto result = v8_helpers::V8ValueToPtr<TransportContext>(isolate, args[1]);
        JS_ENSURE(isolate, result.second, 
            "argument 'handle' must be of type [number, number].");

        context = result.first;
    }
    auto wrap = new TransportContextWrapImpl(context, owning);
    wrap->Wrap(args.This());
    args.GetReturnValue().Set(args.This());
}

void TransportContextWrapImpl::SaveSharedCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);

    CHECK_ARG(isolate, args.Length() == 1, "1 arguments are required for \"saveShared\".");
    CHECK_ARG(isolate, args[0]->IsObject(), "Argument \"object\" shall be 'ShareableWrap' type.");

    auto thisObject = node::ObjectWrap::Unwrap<TransportContextWrap>(args.Holder());
    auto sharedWrap = node::ObjectWrap::Unwrap<ShareableWrap>(v8::Local<v8::Object>::Cast(args[0]));
    thisObject->Get()->SaveShared(sharedWrap->Get<void>());
}

void TransportContextWrapImpl::LoadSharedCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);

    CHECK_ARG(isolate, args.Length() == 1, "1 arguments are required for \"saveShared\".");

    auto result = v8_helpers::V8ValueToUintptr(isolate, args[0]);
    JS_ENSURE(isolate, result.second, "Unable to cast \"handle\" to pointer. Please check if it's in valid format.");

    auto thisObject = node::ObjectWrap::Unwrap<TransportContextWrap>(args.Holder());
    auto object = thisObject->Get()->LoadShared<void>(result.first);

    args.GetReturnValue().Set(binding::CreateShareableWrap(object));
}
