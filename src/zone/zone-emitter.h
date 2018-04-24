// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <string>
#include <stdexcept>

#include <v8.h>
#include <uv.h>

#include "any.hpp"

namespace napa {
namespace zone {

using ::linb::any;
using ::linb::any_cast;

struct ZoneEmitContext {
    uv_async_t _h;

    std::string _event;
    
    // This callback will be executed after event is triggered, and notified to the caller's isolation.
    std::function<void (ZoneEmitContext*)> _cb;

    // Real args when event is emitted.
    std::vector<any> _args;

    ZoneEmitContext(const std::string& event, std::function<void (ZoneEmitContext*)> cb)
        : _h(), _event(event), _cb(cb), _args() {
    }

    inline std::vector<v8::Local<v8::Value>> getCallingParameters(v8::Isolate* isolate) {
        std::vector<v8::Local<v8::Value>> parameters;
        parameters.reserve(_args.size());
        for (size_t i = 0; i < _args.size(); ++i) {
            any& v = _args[i];
            if (v.type() == typeid(int)) {
                parameters.emplace_back(v8::Integer::New(isolate, any_cast<int>(v)));
            }
            else if (v.type() == typeid(double)) {
                parameters.emplace_back(v8::Number::New(isolate, any_cast<double>(v)));
            }
            else if (v.type() == typeid(std::string)) {
                parameters.emplace_back(v8::String::NewFromUtf8(
                    isolate, any_cast<std::string>(v).c_str(), v8::NewStringType::kNormal).ToLocalChecked());
            }
            else {
                std::string errorMessage("Currently unsupported type in zone event emitter: ");
                errorMessage += v.type().name();
                throw std::runtime_error(errorMessage);
            }
        }
        return parameters;
    }
};

};
};

