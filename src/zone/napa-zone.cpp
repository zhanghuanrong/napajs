// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "napa-zone.h"

#include <module/loader/module-loader.h>
#include <platform/dll.h>
#include <platform/filesystem.h>
#include <utils/string.h>
#include <zone/eval-task.h>
#include <zone/call-task.h>
#include <zone/call-context.h>
#include <zone/task-decorators.h>
#include <zone/worker-context.h>
#include <zone/event-emitter.h>

#include <napa/log.h>

#include <future>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace napa;
using namespace napa::zone;

// A NapaZone has the following stage in its lifecycle:
//  - Creating
//      AFTER `zone.create()` is called and BEFORE all the workers are ready.
//      * initialization stage *
//      * zone JSObject is available to use *
//      - `zone.broadcast()` and `zone.execute()` can be called but the tasks
//        will not run until the next stage
//
//  - Created
//      AFTER all the workers are ready and BEFORE either `zone.recycle()` is
//      called or zone destructed triggered by GC.
//      * the active stage *
//
//  - Recycling
//      AFTER `zone.recycle()` is called and BEFORE zone destructed triggered by GC.
//      * stop processing new request *
//      * call to `zone.broadcast()` and `zone.execute()` should return a rejected
//        Promise with error *
//      - a zone may not enter this stage. If a zone is never called `zone.recycle()`
//        manually, when it's destructed stage goes directly from 'Created' to
//        'Recycled'.
//
//  - Recycled
//      AFTER zone destructed triggered by GC and BEFORE all workers complete their
//      remaining tasks
//      * no active JSObject reference to this zone exists *
//      * zone.get() returns null *
//      * zone.current returns a recycled zone *
//      - a zone may stay no time or infinite time in this stage, depending on what
//        the rest tasks are in the workers' queue.
//
//  - Terminating
//      AFTER all workers complete their remaining tasks and BEFORE all resources
//      released
//      - this stage should not wait for workers. Instead, it should be triggered by
//        the signal that all workers complete their tasks.
//
//  - Terminated
//      All resources should be released by this time point, including all C++/JS
//      objects, all references of those and corresponding worker threads
//
// destructer may be triggered by napa_zone_release() via CAPI or javascript GC

namespace {
    // NapaZoneEventEmitter helps to notify the zone status change
    struct NapaZoneEventEmitter;

    // NapaZoneImpl is the unique instance that lives through the whole lifecycle of a zone
    struct NapaZoneImpl;

    // RecycledZone is used to enable getting a recycled zone from zone APIs
    struct RecycledZone;

    // ZoneData represents all references of a zone instance
    struct ZoneData;


    // Emitter should run very fast (no any blocking operation) when emit event.
    struct NapaZoneImpl { // address of 'this' will be stored to TLS (WorkerContextItem::ZONE)
        Zone::State _state;
        settings::ZoneSettings _settings; // address of '_settings.id' will be stored to TLS (WorkerContextItem::ZONE_ID)
        std::unique_ptr<Scheduler> _scheduler;
        std::shared_ptr<ZoneData> _zoneData;
        EventEmitter _events;

        ~NapaZoneImpl() {
            NAPA_DEBUG("Zone", "Destructor NapaZoneImpl");
        }
    };

    struct RecycledZone : Zone {
        RecycledZone(std::shared_ptr<NapaZoneImpl> impl) : _impl(impl) {}
        ~RecycledZone() { NAPA_DEBUG("Zone", "Destructor RecycledZone"); }
        std::shared_ptr<NapaZoneImpl> _impl;

        virtual const std::string& GetId() const override {
            return _impl->_settings.id;
        }

        virtual State GetState() const override {
            return _impl->_state;
        }

        virtual void Broadcast(const FunctionSpec& spec, BroadcastCallback callback) override {
            callback({
                NAPA_RESULT_ZONE_IS_RECYCLING,
                "Failed to broadcast on a recycling zone.",
                "",
                std::move(spec.transportContext)
            });
        }

        virtual void Execute(const FunctionSpec& spec, ExecuteCallback callback) override {
            callback({
                NAPA_RESULT_ZONE_IS_RECYCLING,
                "Failed to execute on a recycling zone.",
                "",
                std::move(spec.transportContext)
            });
        }

        virtual void Recycle() override {
        }
    };

    struct ZoneData : std::enable_shared_from_this<ZoneData> {
        std::weak_ptr<NapaZone> _zone;
        std::shared_ptr<NapaZone> _persistent;
        std::shared_ptr<RecycledZone> _recyclePlaceHolder;

        ~ZoneData() {
            NAPA_DEBUG("Zone", "Destructor ZoneData");
        }
    };

    std::mutex _mutex;

    std::unordered_map<std::string, std::weak_ptr<NapaZone>> _activeZones; // ID to zone instance, remove when GC
    std::unordered_set<std::shared_ptr<ZoneData>> _allZones; // remove when GC

    uv_async_t _activePlaceHolder;
} // namespace

class napa::zone::NapaZone::Impl : public NapaZoneImpl {};

/// <summary> Load 'napajs' module during bootstrap. We use relative path to decouple from how module will be published.  </summary>
static const std::string NAPAJS_MODULE_PATH = filesystem::Path(dll::ThisLineLocation()).Parent().Parent().Normalize().String();
static const std::string BOOTSTRAP_SOURCE = "require('" + utils::string::ReplaceAllCopy(NAPAJS_MODULE_PATH, "\\", "\\\\") + "');";
static const std::string WORKER_RECYCLE_FUNCTION = "__recycle";

std::shared_ptr<NapaZone> NapaZone::Create(const settings::ZoneSettings& settings) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto iter = _activeZones.find(settings.id);
    if (iter != _activeZones.end() && !iter->second.expired()) {
        NAPA_DEBUG("Zone", "Failed to create zone '%s': a zone with this name already exists.", settings.id.c_str());
        return nullptr;
    }

    // An helper class to enable make_shared of NapaZone
    struct MakeSharedEnabler : public NapaZone {
        MakeSharedEnabler(const settings::ZoneSettings& settings) : NapaZone(settings) {}
    };

    // Fail to create Napa zone is not expected, will always trigger crash.
    auto zone = std::make_shared<MakeSharedEnabler>(settings);
    _activeZones[settings.id] = zone;

    // Create the corresponding ZoneData instance 
    auto zoneData = std::make_shared<ZoneData>();
    zoneData->_zone = zone;
    zoneData->_persistent = (settings.recycle == settings::ZoneSettings::RecycleMode::Manual) ? zone : nullptr;
    zoneData->_recyclePlaceHolder = std::make_shared<RecycledZone>(zone->_impl);

    // Register the zone data
    if (_allZones.empty()) {
        auto loop = reinterpret_cast<uv_loop_t*>(WorkerContext::Get(WorkerContextItem::EVENT_LOOP));
        NAPA_ASSERT(loop == uv_default_loop(),
            "The first napa zone creation should always happen in node main thread.");
        auto errorCode = uv_async_init(loop, &_activePlaceHolder, [](uv_async_t* task) {
            uv_close(reinterpret_cast<uv_handle_t*>(task), nullptr);
            NAPA_DEBUG("Zone", "Unregistered active place holder.");
        });
        NAPA_ASSERT(0 == errorCode, "Failed to register active place holder");
        NAPA_DEBUG("Zone", "Registered active place holder.");
    }
    auto result = _allZones.insert(zoneData);
    NAPA_ASSERT(result.second, "Failed to insert zone '%s' into cache.", settings.id.c_str());

    zone->_impl->_zoneData = zoneData;

    NAPA_DEBUG("Zone", "Napa zone \"%s\" created.", settings.id.c_str());

    return zone;
}

std::shared_ptr<Zone> NapaZone::Get(const std::string& id) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto iter = _activeZones.find(id);
    if (iter == _activeZones.end()) {
        NAPA_DEBUG("Zone", "Get zone \"%s\" failed due to not found.", id.c_str());
        return nullptr;
    }

    auto zone = iter->second.lock();
    if (zone == nullptr) {
        LOG_WARNING("Zone", "Zone '%s' was already recycled.", id.c_str());

        // Use this chance to clean up the map
        _activeZones.erase(id);
    }
    
    NAPA_DEBUG("Zone", "Get zone \"%s\" succeeded.", id.c_str());
    return zone;
}

std::shared_ptr<Zone> NapaZone::GetCurrent() {
    auto zoneImplPtr = reinterpret_cast<NapaZoneImpl *>(zone::WorkerContext::Get(zone::WorkerContextItem::ZONE));
    if (zoneImplPtr == nullptr) {
        LOG_WARNING("Zone", "Trying to get current zone from a thread that is not associated with a zone");
        return nullptr;
    }

    auto zone = zoneImplPtr->_zoneData->_zone.lock();
    if (zone == nullptr) {
        LOG_WARNING("Zone", "Current zone '%s' was already recycled.", zoneImplPtr->_settings.id.c_str());
        return zoneImplPtr->_zoneData->_recyclePlaceHolder;
    }

    return zone;
}

NapaZone::NapaZone(const settings::ZoneSettings& settings) :
    _impl(std::make_shared<NapaZone::Impl>()), _recycling(false) {


    _impl->_state = Zone::State::Initializing;
    _impl->_settings = settings;
    _impl->_scheduler = std::make_unique<Scheduler>(
        settings,
        [this](WorkerId id, uv_loop_t* event_loop) {

            // Initialize the worker context TLS data
            INIT_WORKER_CONTEXT();

            // Zone instance into TLS.
            WorkerContext::Set(WorkerContextItem::ZONE, reinterpret_cast<void*>(this->_impl.get()));

            // Zone Id into TLS.
            WorkerContext::Set(WorkerContextItem::ZONE_ID, const_cast<void*>(reinterpret_cast<const void*>(&this->GetId())));

            // Worker Id into TLS.
            WorkerContext::Set(WorkerContextItem::WORKER_ID, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));

            // Set event loop into TLS. </summary>
            WorkerContext::Set(WorkerContextItem::EVENT_LOOP, reinterpret_cast<void*>(event_loop));
        },
        [impl = _impl](int exit_code) {
            // destruct the scheduler, which also destruct the workers.
            impl->_scheduler.reset();

            impl->_events.Emit("Terminated", exit_code);
            impl->_zoneData->_recyclePlaceHolder.reset();

            {
                std::lock_guard<std::mutex> lock(_mutex);
                _allZones.erase(impl->_zoneData);
                if (_allZones.empty()) {
                    uv_async_send(&_activePlaceHolder);
                }            
            }
        });
}

const std::string& NapaZone::GetId() const {
    return _impl->_settings.id;
}

Zone::State NapaZone::GetState() const {
    return _impl->_state;
}

void NapaZone::Broadcast(const FunctionSpec& spec, BroadcastCallback callback) {
    if (_recycling) {
        callback({
            NAPA_RESULT_ZONE_IS_RECYCLING,
            "Failed to broadcast on a recycling zone.",
            "",
            std::move(spec.transportContext)
        });
        return;
    }

    // Makes sure the callback is only called once, after all workers finished running the broadcast task.
    auto workersCount = _impl->_settings.workers;
    auto counter = std::make_shared<std::atomic<uint32_t>>(workersCount);
    auto callOnce = [this, callback = std::move(callback), counter](Result result) {
        if (--(*counter) == 0) {
            callback(std::move(result));
        }
    };

    for (WorkerId id = 0; id < workersCount; id++) {
        std::shared_ptr<Task> task;

        if (spec.options.timeout > 0) {
            task = std::make_shared<TimeoutTaskDecorator<CallTask>>(
                std::chrono::milliseconds(spec.options.timeout),
                std::make_shared<CallContext>(spec, callOnce));
        } else {
            task = std::make_shared<CallTask>(std::make_shared<CallContext>(spec, callOnce));
        }

        _impl->_scheduler->ScheduleOnWorker(id, std::move(task));
    }

    NAPA_DEBUG("Zone", "Broadcast function \"%s.%s\" on zone \"%s\"", spec.module.data, spec.function.data, GetId().c_str());
}

void NapaZone::Execute(const FunctionSpec& spec, ExecuteCallback callback) {
    if (_recycling) {
        callback({
            NAPA_RESULT_ZONE_IS_RECYCLING,
            "Failed to execute on a recycling zone.",
            "",
            std::move(spec.transportContext)
        });
        return;
    }

    std::shared_ptr<Task> task;

    if (spec.options.timeout > 0) {
        task = std::make_shared<TimeoutTaskDecorator<CallTask>>(
            std::chrono::milliseconds(spec.options.timeout),
            std::make_shared<CallContext>(spec, std::move(callback)));
    } else {
        task = std::make_shared<CallTask>(std::make_shared<CallContext>(spec, std::move(callback)));
    }
    
    NAPA_DEBUG("Zone", "Execute function \"%s.%s\" on zone \"%s\"", spec.module.data, spec.function.data, GetId().c_str());
    _impl->_scheduler->Schedule(std::move(task));
}

void NapaZone::Recycle() {
    if (!_recycling) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_recycling) {
            // broadcast exit script
            FunctionSpec exitSpec;
            exitSpec.function = STD_STRING_TO_NAPA_STRING_REF(WORKER_RECYCLE_FUNCTION);
            Broadcast(exitSpec, [](Result){});

            _recycling = true;
            if (_impl->_settings.recycle == settings::ZoneSettings::RecycleMode::Manual) {
                _impl->_zoneData->_persistent.reset();
            }

            _impl->_events.Emit("Recycling");
        }
    }
}

struct ZoneEmitContext {
    uv_async_t _h;
    
    std::function<void (uv_async_t*)> _cb;

    // std::tuple where size and types are interpreted and understand by the _cb function
    void* _args; 

    int _listener_id;

    ZoneEmitContext(std::function<void (uv_async_t*)> cb)
        : _h(), _args(nullptr), _cb(cb), _listener_id(0) {
    }

    std::vector<v8::Local<v8::Value>> 
    getCallingParameters(const std::string& event, v8::Isolate* isolate) {
        std::vector<v8::Local<v8::Value>> parameters;
        if (_args != nullptr) {
            if (event.compare("Terminated") == 0) {
                parameters.reserve(1);
                std::tuple<int>* t = reinterpret_cast<std::tuple<int> *>(_args);
                int exit_code = std::get<0>(*t);
                v8::Local<v8::Integer> lv_exit_code = v8::Integer::New(isolate, exit_code);
                parameters.push_back(lv_exit_code);
            }
        }
        return parameters;
    }
};

// When call some zone's On() in specific worker (highly possible in node's main loop), 
//     *) new ZoneEmitContext will be created, where
//          +) uv_async_t handle will be created and added to current worker's loop, 
//             it will be activated when the event is emitted from the zone.
//          *) wrapper call back logic will be created to call the jsFunc when
//             above uv_async_t handle is activated and executed later in current worker's 
//             loop. at that time, real call back parameter is known and should be
//             set in the ZoneEmitContext's _cbParamters.
void NapaZone::On(const std::string& event, v8::Local<v8::Function> jsFunc) {
    auto isolate = v8::Isolate::GetCurrent();
    auto persistFunc = std::make_shared<v8::Persistent<v8::Function>>(isolate, jsFunc);

    // TODO: save context here to the onEvent capture
    auto onEvent = [event, persistFunc](uv_async_t* h) {
        ZoneEmitContext* emitContext = reinterpret_cast<ZoneEmitContext*>(h->data);

        {
            auto isolate = v8::Isolate::GetCurrent();
            v8::HandleScope handle_scope(isolate);
            v8::Local<v8::Context> local_context = v8::Context::New(isolate);
            auto jsCallback = v8::Local<v8::Function>::New(isolate, *persistFunc);
            std::vector<v8::Local<v8::Value>> parameters = emitContext->getCallingParameters(event, isolate);
            jsCallback->Call(isolate->GetCurrentContext()->Global(), parameters.size(), parameters.data());
        }
        
        persistFunc->Reset();
    };

    ZoneEmitContext* emitContext = new ZoneEmitContext(onEvent);
    
    uv_loop_t* loop = reinterpret_cast<uv_loop_t*>(zone::WorkerContext::Get(zone::WorkerContextItem::EVENT_LOOP));
    uv_async_init(loop, &emitContext->_h, [](uv_async_t* h) {
            auto emitContext = reinterpret_cast<ZoneEmitContext*>(h->data);
            emitContext->_cb(h);
            delete emitContext;
        });
    emitContext->_h.data = emitContext;

    int listener_id = 0;
    if (event.compare("Terminated") == 0) {
        // call back parameter is (int exit_code)
        std::function<void(int)> cb = [emitContext](int exit_code) -> void {
            emitContext->_args = static_cast<void*>(new std::tuple<int>{exit_code});
            uv_async_send(&(emitContext->_h));
        };
        listener_id = _impl->_events.On(event, cb);
    }
    else { //default, no call back parameter
        std::function<void()> cb = [emitContext]() {
            uv_async_send(&(emitContext->_h));
        };
        listener_id = _impl->_events.On(event, cb);
    }
    emitContext->_listener_id = listener_id;
}


NapaZone::~NapaZone() {
    // _activeZones[this_zone_id] is expired by now
    Recycle();
    _impl->_events.Emit("Recycled");
}