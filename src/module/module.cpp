// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <napa/module/module-internal.h>
#include <napa/v8-helpers.h>

#include <zone/worker-context.h>

#include <iostream>

using namespace napa;

/// <summary>
/// Map from module's class name to persistent constructor object.
/// To suppport multiple isolates, let each isolate has its own persistent constructor at thread local storage.
/// </summary>
typedef v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function>> PersistentConstructor;
struct ConstructorInfo {
    std::unordered_map<std::string, PersistentConstructor> constructorMap;
};

/// <summary> It sets the persistent constructor at the current V8 isolate. </summary>
/// <param name="name"> Unique constructor name. It's recommended to use the same name as module. </param>
/// <param name="constructor"> V8 persistent function to constructor V8 object. </param>
void napa::module::SetPersistentConstructor(const char* name,
                                     v8::Local<v8::Function> constructor) {
    std::cout << "SetPersistentConstructor of " << name << std::endl;
    std::cout << "CC display name" << *(v8::String::Utf8Value(constructor->GetDisplayName()->ToString())) << std::endl;
    std::cout << "CC inferred name" << *(v8::String::Utf8Value(constructor->GetInferredName()->ToString())) << std::endl;
    std::cout << "CC name" << *(v8::String::Utf8Value(constructor->GetName()->ToString())) << std::endl;
    std::cout << "CC GetScriptLineNumber" << constructor->GetScriptLineNumber() << std::endl;
    std::cout << "CC GetScriptColumnNumber" << constructor->GetScriptColumnNumber() << std::endl;
    auto isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);

    auto constructorInfo =
        static_cast<ConstructorInfo*>(zone::WorkerContext::Get(zone::WorkerContextItem::CONSTRUCTOR));
    if (constructorInfo == nullptr) {
        constructorInfo = new ConstructorInfo();
        zone::WorkerContext::Set(zone::WorkerContextItem::CONSTRUCTOR, constructorInfo);
    }

    constructorInfo->constructorMap.emplace(std::piecewise_construct,
                                            std::forward_as_tuple(name),
                                            std::forward_as_tuple(isolate, constructor));
}

/// <summary> It gets the given persistent constructor from the current V8 isolate. </summary>
/// <param name="name"> Unique constructor name given at SetPersistentConstructor() call. </param>
/// <returns> V8 local function object. </returns>
v8::Local<v8::Function> napa::module::GetPersistentConstructor(const char* name) {
    std::cout << "~~~~~~~~~~~~~~~GetPersistentConstructor of " << name << std::endl;
    auto isolate = v8::Isolate::GetCurrent();
    v8::EscapableHandleScope scope(isolate);

    auto constructorInfo =
        static_cast<ConstructorInfo*>(zone::WorkerContext::Get(zone::WorkerContextItem::CONSTRUCTOR));
    if (constructorInfo == nullptr) {
    std::cout << "~~~~~~~~~~~~~~zone::WorkerContext::Get(zone::WorkerContextItem::CONSTRUCTOR) == nullptr " << name << std::endl;
        return scope.Escape(v8::Local<v8::Function>());
    }

    auto iter = constructorInfo->constructorMap.find(name);
    if (iter != constructorInfo->constructorMap.end()) {
    std::cout << "~~~~~~~~~~~~~~~Found constructor of " << name << std::endl;
        auto constructor = v8::Local<v8::Function>::New(isolate, iter->second);
        return scope.Escape(constructor);
    } else {
    std::cout << "~~~~~~~~~~~~~~NOT Found constructor of " << name << std::endl;
        return scope.Escape(v8::Local<v8::Function>());
    }
}
