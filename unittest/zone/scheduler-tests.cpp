// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <catch/catch.hpp>
#include <zone/scheduler.h>

#include <uv.h>

#include <cstddef>
#include <atomic>
#include <future>

using namespace napa;
using namespace napa::zone;
using namespace napa::settings;

class TestTask : public Task {
public:
    TestTask(std::function<void()> callback = []() {}) : 
        numberOfExecutions(0),
        lastExecutedWorkerId(99),
        _callback(std::move(callback)) {}

    void SetCurrentWorkerId(WorkerId id) {
        lastExecutedWorkerId = id;
    }

    virtual void Execute() override
    {
        numberOfExecutions++;
        _callback();
    }

    std::atomic<uint32_t> numberOfExecutions;
    std::atomic<WorkerId> lastExecutedWorkerId;

private:
    std::function<void()> _callback;
};


template <uint32_t I>
class TestWorker {
public:

    TestWorker(WorkerId id,
               const ZoneSettings &settings,
               std::function<void(WorkerId, uv_loop_t*)> setupCompleteCallback,
               std::function<void(WorkerId)> idleCallback,
               std::function<void(WorkerId)> exitCallback) : _id(id) {
        
        numberOfWorkers++;
        _idleNotificationCallback = idleCallback;
        setupCompleteCallback(id, nullptr);
    }

    ~TestWorker() {
        for (auto& fut : _futures) {
            fut.get();
        }
    }

    void Start() {
        _idleNotificationCallback(_id);
    }

    void Schedule(std::shared_ptr<Task> task) {
        auto testTask = std::dynamic_pointer_cast<TestTask>(task);
        testTask->SetCurrentWorkerId(_id);

        _futures.emplace_back(std::async(std::launch::async, [this, task]() {
            task->Execute();
            _idleNotificationCallback(_id);
        }));
    }

    WorkerId GetWorkerId() const {
        return _id;
    }

    static uint32_t numberOfWorkers;

private:
    WorkerId _id;
    std::vector<std::shared_future<void>> _futures;
    std::function<void(WorkerId)> _idleNotificationCallback;
};

template <uint32_t I>
uint32_t TestWorker<I>::numberOfWorkers = 0;


TEST_CASE("scheduler creates correct number of worker", "[scheduler]") {
    ZoneSettings settings;
    settings.workers = 3;

    auto scheduler = std::make_unique<SchedulerImpl<TestWorker<1>>>(
        settings,
        [](WorkerId, uv_loop_t*) {},
        []() {});

    REQUIRE(TestWorker<1>::numberOfWorkers == settings.workers);
}

TEST_CASE("scheduler dispatches worker setup complete callback correctly", "[scheduler]") {
    ZoneSettings settings;
    settings.workers = 3;
    WorkerId idSum = 0;
    auto scheduler = std::make_unique<SchedulerImpl<TestWorker<1>>>(
        settings,
        [&idSum](WorkerId id, uv_loop_t*) { idSum += id;},
        []() {});

    REQUIRE(idSum == settings.workers * (settings.workers - 1) / 2);
}

TEST_CASE("scheduler assigns tasks correctly", "[scheduler]") {
    ZoneSettings settings;
    settings.workers = 3;

    auto scheduler = std::make_unique<SchedulerImpl<TestWorker<2>>>(
        settings,
        [](WorkerId, uv_loop_t*) {},
        []() {});
    auto task = std::make_shared<TestTask>();

    SECTION("schedules on exactly one worker") {
        scheduler->Schedule(task);
        scheduler = nullptr; // force draining all scheduled tasks

        REQUIRE(task->numberOfExecutions == 1);
    }

    SECTION("schedule on a specific worker") {
        scheduler->ScheduleOnWorker(2, task);
        scheduler = nullptr; // force draining all scheduled tasks

        REQUIRE(task->numberOfExecutions == 1);
        REQUIRE(task->lastExecutedWorkerId == 2);
    }

    SECTION("schedule on all workers") {
        scheduler->ScheduleOnAllWorkers(task);
        scheduler = nullptr; // force draining all scheduled tasks

        REQUIRE(task->numberOfExecutions == settings.workers);
    }
}

TEST_CASE("scheduler distributes and schedules all tasks", "[scheduler]") {
    ZoneSettings settings;
    settings.workers = 4;

    auto scheduler = std::make_unique<SchedulerImpl<TestWorker<3>>>(
        settings,
        [](WorkerId, uv_loop_t*) {},
        []() {});

    std::vector<std::shared_ptr<TestTask>> tasks;
    for (size_t i = 0; i < 1000; i++) {
        auto task = std::make_shared<TestTask>();
        tasks.push_back(task);
        scheduler->Schedule(task);
    }

    scheduler = nullptr; // force draining all scheduled tasks

    std::vector<bool> scheduledWorkersFlags = { false, false, false, false };
    size_t notRun = 0;
    for (size_t i = 0; i < 1000; i++) {
        // Make sure that each task was executed once
        if (tasks[i]->numberOfExecutions == 0) {
            ++notRun;
        }
        //REQUIRE(tasks[i]->numberOfExecutions == 1);
        scheduledWorkersFlags[tasks[i]->lastExecutedWorkerId] = true;
    }
    REQUIRE(notRun == 0);

    // Make sure that all workers were participating
    for (auto flag: scheduledWorkersFlags) {
        REQUIRE(flag);
    }
}
