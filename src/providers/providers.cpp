// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "providers.h"

#include "console-logging-provider.h"
#include "nop-logging-provider.h"
#include "nop-metric-provider.h"

#include <memory>
#include <platform/dll.h>
#include <platform/filesystem.h>

#include <napa/log.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>

#include <fstream>
#include <string>

using namespace napa;
using namespace napa::providers;

// Forward declarations.
static LoggingProvider* LoadLoggingProvider(const std::string& providerName);
static MetricProvider* LoadMetricProvider(const std::string& providerName);

// Providers - Initially assigned to defaults.
static LoggingProvider* _loggingProvider = LoadLoggingProvider("");
static MetricProvider* _metricProvider = LoadMetricProvider("");


bool napa::providers::Initialize(const settings::PlatformSettings& settings) {
    _loggingProvider = LoadLoggingProvider(settings.loggingProvider);
    _metricProvider = LoadMetricProvider(settings.metricProvider);

    return true;
}

void napa::providers::Shutdown() {
    if (_loggingProvider != nullptr) {
        _loggingProvider->Destroy();
    }
    
    if (_metricProvider != nullptr) {
        _metricProvider->Destroy();
    }
}

LoggingProvider& napa::providers::GetLoggingProvider() {
    return *_loggingProvider;
}

MetricProvider& napa::providers::GetMetricProvider() {
    return *_metricProvider;
}

template <typename ProviderType>
static ProviderType* LoadProvider(
    const std::string& providerName,
    const std::string& jsonPropertyPath,
    const std::string& functionName) {

    //
    // Disable custom provider temporarily.
    // We are doing this because this implementation uses napa::module::ModuleResolver::Resolve(),
    // which is not avilable in current status
    //
    return nullptr;


    // napa::module::ModuleResolver moduleResolver;

    // // Resolve the provider module information
    // auto moduleInfo = moduleResolver.Resolve(providerName.c_str());
    // NAPA_ASSERT(!moduleInfo.packageJsonPath.empty(), "missing package.json in provider '%s'", providerName.c_str());

    // // Full path to the root of the provider module
    // auto modulePath = filesystem::Path(moduleInfo.packageJsonPath).Parent().Normalize();

    // // Extract relative path to provider dll from package.json
    // rapidjson::Document package;
    // std::ifstream ifs(moduleInfo.packageJsonPath);
    // rapidjson::IStreamWrapper isw(ifs);
    // NAPA_ASSERT(!package.ParseStream(isw).HasParseError(), rapidjson::GetParseError_En(package.GetParseError()));

    // NAPA_ASSERT(package.HasMember(jsonPropertyPath.c_str()),
    //             "missing property '%s' in '%s'",
    //             jsonPropertyPath.c_str(),
    //             moduleInfo.packageJsonPath.c_str());

    // auto providerRelativePath = package[jsonPropertyPath.c_str()].GetString();

    // // Full path to provider dll
    // auto providerPath = (modulePath / providerRelativePath).Normalize();

    // // Keep a static instance for each provider type (each template type will have its own static variable).
    // static dll::SharedLibrary library(providerPath.String());
    // auto createProviderFunc = library.Import<ProviderType*()>(functionName);
    // return createProviderFunc();
}

static LoggingProvider* LoadLoggingProvider(const std::string& providerName) {
    if (providerName.empty() || providerName == "console") {
        static auto consoleLoggingProvider = std::make_unique<ConsoleLoggingProvider>();
        return consoleLoggingProvider.get();
    }

    if (providerName == "nop") {
        static auto nopLoggingProvider = std::make_unique<NopLoggingProvider>();
        return nopLoggingProvider.get();
    }

    return LoadProvider<LoggingProvider>(providerName, "providers.logging", "CreateLoggingProvider");
}

static MetricProvider* LoadMetricProvider(const std::string& providerName) {
    if (providerName.empty()) {
        static auto nopMetricProvider = std::make_unique<NopMetricProvider>();
        return nopMetricProvider.get();
    }

    return LoadProvider<MetricProvider>(providerName, "providers.metric", "CreateMetricProvider");;
}
