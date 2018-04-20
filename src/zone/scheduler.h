// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "simple-thread-pool.h"
#include "task.h"
#include "worker.h"

#include <settings/settings.h>

#include <napa/log.h>

#include <atomic>
#include <list>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

namespace napa {
namespace zone {

    /// <summary> The scheduler is responsible for assigning tasks to workers. </summary>
    template <typename WorkerType>
    class SchedulerImpl {
    public:

        using SequenceType = uint64_t;
        using SequencedTask = std::pair<SequenceType, std::shared_ptr<Task>>;

        /// <summary> Constructor. </summary>
        /// <param name="settings"> A settings object. </param>
        /// <param name="workerSetupCallback"> Callback to setup the isolate after worker created its isolate. </param>
        /// <param name="exitCallback"> Callback to all workers exit. </param>
        SchedulerImpl(
            const settings::ZoneSettings& settings,
            std::function<void(WorkerId, uv_loop_t*)> workerSetupCallback,
            std::function<void(int)> zoneExitCallback);

        /// <summary> Destructor. Waits for all tasks to finish. </summary>
        ~SchedulerImpl();

        /// <summary> Schedules the task on a single worker. </summary>
        /// <param name="task"> Task to schedule. </param>
        void Schedule(std::shared_ptr<Task> task);

        /// <summary> Schedules the task on a specific worker. </summary>
        /// <param name="workerId"> The id of the worker. </param>
        /// <param name="task"> Task to schedule. </param>
        /// <remarks>
        /// By design, it enqueues a task immediately,
        /// so the task will have higher priority than ones called by Schedule().
        /// </remarks>
        void ScheduleOnWorker(WorkerId workerId, std::shared_ptr<Task> task);

        /// <summary> Schedules the task on all workers. </summary>
        /// <param name="task"> Task to schedule. </param>
        /// <remarks>
        /// By design, it enqueues a task immediately,
        /// so the task will have higher priority than ones called by Schedule().
        /// </remarks>
        void ScheduleOnAllWorkers(std::shared_ptr<Task> task);

    private:

        /// <summary> The logic invoked when a worker is idle. </summary>
        void IdleWorkerNotificationCallback(WorkerId workerId);

        /// <summary> The workers that are used for running the tasks. </summary>
        std::vector<WorkerType> _workers;

        std::vector<int> _worker_exit_codes;

        /// <summary> New tasks that weren't assigned to a specific worker. </summary>
        std::queue<SequencedTask> _nonScheduledTasks;

        /// <summary> New tasks that targets a specific worker. </summary>
        std::vector<std::queue<SequencedTask>> _perWorkerNonScheduledTasks;

        /// <summary> The sequence number assigned to a task When it is being scheduled.
        /// It increases monotonically to indicate the order in which tasks are scheduled by the scheduler.
        /// </summary>
        SequenceType _currentTaskSequence;

        /// <summary> List of idle workers, used when assigning non scheduled tasks. </summary>
        std::list<WorkerId> _idleWorkers;

        /// <summary> Flags to indicate that a worker is in the idle list. </summary>
        std::vector<std::list<WorkerId>::iterator> _idleWorkersFlags;

        /// <summary> Tasks being scheduled but not yet dispatched to worker or put into (per-worker) non-scheduled queue by syncronizer. </summary>
        std::atomic<size_t> _beingScheduled;

        /// <summary> Uses a single thread to synchronize task queuing and posting. </summary>
        static std::unique_ptr<SimpleThreadPool> _synchronizer;
        static std::once_flag _synchronizerCreateOnceFlag;
    };

    template <typename WorkerType> std::unique_ptr<SimpleThreadPool> SchedulerImpl<WorkerType>::_synchronizer;
    template <typename WorkerType> std::once_flag SchedulerImpl<WorkerType>::_synchronizerCreateOnceFlag;

    typedef SchedulerImpl<Worker> Scheduler;

    template <typename WorkerType>
    SchedulerImpl<WorkerType>::SchedulerImpl(
        const settings::ZoneSettings& settings,
        std::function<void(WorkerId, uv_loop_t*)> workerSetupCallback,
        std::function<void(int)> zoneExitCallback) :
        _idleWorkersFlags(settings.workers, _idleWorkers.end()),
        _perWorkerNonScheduledTasks(settings.workers),
        _worker_exit_codes(settings.workers, 0),
        _currentTaskSequence(0),
        _beingScheduled(0) {

        // Create the synchronizer if it's not ready.
        std::call_once(_synchronizerCreateOnceFlag, [](){ _synchronizer = std::make_unique<SimpleThreadPool>(1); });

        auto counter = std::make_shared<std::atomic<uint32_t>>(settings.workers);
        auto workerExitCallback = [zoneExitCallback = std::move(zoneExitCallback), counter, this](WorkerId workerId, int exit_code) {
            _worker_exit_codes[workerId] = exit_code;
            if (--(*counter) == 0) {
                // use synchronizer to destruct scheduler and workers
                _synchronizer->Execute([this](std::function<void(int)> zoneExitCallback) {
                    auto iter = std::find_if(_worker_exit_codes.begin(), _worker_exit_codes.end(), [](const int& v) -> bool { return v != 0; });
                    int zoneExitCode = (iter == _worker_exit_codes.end()) ? 0 : *iter;
                    zoneExitCallback(zoneExitCode);
                }, std::move(zoneExitCallback));
            }
        };
        _workers.reserve(settings.workers);

        for (WorkerId i = 0; i < settings.workers; i++) {
            _workers.emplace_back(
                i, settings, workerSetupCallback, 
                [this](WorkerId workerId) {
                        // idle callback
                        IdleWorkerNotificationCallback(workerId);
                    }, 
                workerExitCallback);
            _workers[i].Start();
        }
    }

    template <typename WorkerType>
    SchedulerImpl<WorkerType>::~SchedulerImpl() {
        NAPA_DEBUG("Scheduler", "Shutting down: Start draining unscheduled tasks...");

        // We assume Scheduler should not be destructed when there are non scheduled tasks.
        NAPA_ASSERT(_beingScheduled == 0
                    && _nonScheduledTasks.empty()
                    && std::find_if_not(_perWorkerNonScheduledTasks.begin(),
                                        _perWorkerNonScheduledTasks.end(),
                                        [](auto& pq) { return pq.empty(); }
                                        ) == _perWorkerNonScheduledTasks.end(),
                        "Scheduler is destructed with non scheduled tasks");

        // Wait for all workers to finish processing remaining tasks.
        _workers.clear();

        NAPA_DEBUG("Scheduler", "Shutdown completed");
    }

    template <typename WorkerType>
    void SchedulerImpl<WorkerType>::Schedule(std::shared_ptr<Task> task) {
        NAPA_ASSERT(task, "task is null");
        _beingScheduled++;
        _synchronizer->Execute([this, task]() {
            if (_idleWorkers.empty()) {
                NAPA_DEBUG("Scheduler", "All workers are busy, putting task to non-scheduled queue.");

                // If there is no idle worker, put the task into the non-scheduled queue.
                _nonScheduledTasks.emplace(_currentTaskSequence++, task);
            } else {
                // Pop the worker id from the idle workers list.
                auto workerId = _idleWorkers.front();
                _idleWorkers.pop_front();
                _idleWorkersFlags[workerId] = _idleWorkers.end();

                // Schedule task on worker
                _workers[workerId].Schedule(std::move(task));

                NAPA_DEBUG("Scheduler", "Scheduled task on worker %u.", workerId);
            }
            _beingScheduled--;
        });
        
    }

    template <typename WorkerType>
    void SchedulerImpl<WorkerType>::ScheduleOnWorker(WorkerId workerId, std::shared_ptr<Task> task) {
        NAPA_ASSERT(workerId < _workers.size(), "worker id out of range");

        _beingScheduled++;
        _synchronizer->Execute([workerId, this, task]() {
            // If the worker is idle, change it's status.
            if (_idleWorkersFlags[workerId] != _idleWorkers.end()) {
                _idleWorkers.erase(_idleWorkersFlags[workerId]);
                _idleWorkersFlags[workerId] = _idleWorkers.end();

                // Schedule task on worker
                _workers[workerId].Schedule(std::move(task));

                NAPA_DEBUG("Scheduler", "Explicitly scheduled a task on worker %u.", workerId);
            }
            else {
                _perWorkerNonScheduledTasks[workerId].emplace(_currentTaskSequence++, task);
                NAPA_DEBUG("Scheduler", "Given worker %u is busy, explicitly put a task to its non-scheduled queue.", workerId);
            }
            _beingScheduled--;
        });
    }

    template <typename WorkerType>
    void SchedulerImpl<WorkerType>::ScheduleOnAllWorkers(std::shared_ptr<Task> task) {
        NAPA_ASSERT(task, "task is null");

        _beingScheduled++;
        _synchronizer->Execute([this, task]() {
            // Clear all idle workers.
            _idleWorkers.clear();
            for (auto& worker : _workers) {
                auto workerId = worker.GetWorkerId();
                if (_idleWorkersFlags[workerId] != _idleWorkers.end()) {
                    // If the worker is idle, schedule the task on it. 
                    _idleWorkersFlags[workerId] = _idleWorkers.end();
                    worker.Schedule(std::move(task));

                    NAPA_DEBUG("Scheduler", "Scheduled a broadcast task on worker %u.", workerId);
                } else {
                    // If the worker is not idle, put the task to its non-scheduled queuqe
                    _perWorkerNonScheduledTasks[workerId].emplace(_currentTaskSequence++, task);
                    NAPA_DEBUG("Scheduler", "Given worker %u is busy, put a broadcast task to its non-scheduled queue.", workerId);
                }
            }
            _beingScheduled--;
        });
    }

    template <typename WorkerType>
    void SchedulerImpl<WorkerType>::IdleWorkerNotificationCallback(WorkerId workerId) {
        NAPA_ASSERT(workerId < _workers.size(), "worker id (id=%u) out of range", workerId);

        _synchronizer->Execute([this, workerId]() {
            // Pick the earliest task and schedule it on the worker when there are non-scedule tasks for the worker.
            if (!_nonScheduledTasks.empty() && !_perWorkerNonScheduledTasks[workerId].empty()) {
                if (_nonScheduledTasks.front().first < _perWorkerNonScheduledTasks[workerId].front().first) {
                    auto priorityTask = _nonScheduledTasks.front();
                    _nonScheduledTasks.pop();
                    _workers[workerId].Schedule(std::move(priorityTask.second));

                    NAPA_DEBUG("Scheduler", "Worker %u fetched a task from the zone non-scheduled queue", workerId);
                } else {
                    auto priorityTask = _perWorkerNonScheduledTasks[workerId].front();
                    _perWorkerNonScheduledTasks[workerId].pop();
                    _workers[workerId].Schedule(std::move(priorityTask.second));

                    NAPA_DEBUG("Scheduler", "Worker %u fetched a task from its non-scheduled queue", workerId);
                }
            } else if (!_nonScheduledTasks.empty()) {
                auto priorityTask = _nonScheduledTasks.front();
                _nonScheduledTasks.pop();
                _workers[workerId].Schedule(std::move(priorityTask.second));

                NAPA_DEBUG("Scheduler", "Worker %u fetched a task from the zone non-scheduled queue", workerId);
            } else if (!_perWorkerNonScheduledTasks[workerId].empty()) {
                auto priorityTask = _perWorkerNonScheduledTasks[workerId].front();
                _perWorkerNonScheduledTasks[workerId].pop();
                _workers[workerId].Schedule(std::move(priorityTask.second));

                NAPA_DEBUG("Scheduler", "Worker %u fetched a task from its non-scheduled queue", workerId);
            }
            else {
                // Put worker in idle list.
                if (_idleWorkersFlags[workerId] == _idleWorkers.end()) {
                    auto iter = _idleWorkers.emplace(_idleWorkers.end(), workerId);
                    _idleWorkersFlags[workerId] = iter;

                    NAPA_DEBUG("Scheduler", "Worker %u becomes idle", workerId);
                }
            }
        });
    }
}
}
