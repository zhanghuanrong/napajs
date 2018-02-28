// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// To Run this test, in napajs root directory after build, use:
//      node test/timer-test 

import * as napa from "../lib/index";


// To be execute in napa workers
export async function setImmediateTest(taskGroupId: number) : Promise<string> {
    const kTaskGroupSize = 5;
    const kAllowedScheduleDiffInMS = 20;

    let setImmediate = napa.timer.setImmediate;
    let clearImmediate = napa.timer.clearImmediate;

    let correctResult = "";
    let lastTaskId = 0;
    for (let taskId = 0; taskId < kTaskGroupSize; taskId++) {
        if (taskId != 1) {
            correctResult = `${correctResult}:${taskId}_OnTime`;
            lastTaskId = taskId;
        }
    }

    let promise = new Promise<string>((resolve, reject) => {
        let execResult = "";
        for (let taskId = 0; taskId < kTaskGroupSize; taskId++) {
            let startTime = Date.now();
            let immedidate = setImmediate((lastTaskId: number) => {
                let delayToRun = Date.now() - startTime;
                execResult = `${execResult}:${taskId}_OnTime`;
                if (delayToRun > kAllowedScheduleDiffInMS) {
                    execResult = `${execResult}(X)`;
                }
                if (taskId == lastTaskId) {
                    if (execResult == correctResult) {
                        resolve(`OK:${execResult}`)
                    }
                    else {
                        reject(`FAIL:${execResult} vs ${correctResult}`)
                    }
                }
            }, lastTaskId);
    
            if (taskId == 1) {
                clearImmediate(immedidate);
            }
        }
    });

    return promise;
}

export function setTimeoutTest(taskGroupId: number) : Promise<string> {
    const kTaskGroupSize = 5;
    const kAllowedScheduleDiffInMS = 20;

    let setTimeout = napa.timer.setTimeout;
    let clearTimeout = napa.timer.clearTimeout;

    let correctResult = "";
    let lastTaskId = 0;
    for (let taskId = 0; taskId < kTaskGroupSize; taskId++) {
        if (taskId != 1) {
            correctResult = `${correctResult}:${taskId}_OnTime`;
            lastTaskId = taskId;
        }
    }
    
    let promise = new Promise<string>((resolve, reject) => {
        let execResult = "";
        for (let taskId = 0; taskId < kTaskGroupSize; taskId++) {
            let wait = 200 * (taskGroupId * kTaskGroupSize + taskId + 1);
            let startTime = Date.now();
            let timeout = setTimeout((lastTaskId: number) => {
                let waitToRun = Date.now() - startTime;
                execResult = `${execResult}:${taskId}_OnTime`;
                if (Math.abs(waitToRun - wait) > kAllowedScheduleDiffInMS) {
                    execResult = `${execResult}(X)`;
                }

                if (taskId == lastTaskId) {
                    if (execResult == correctResult) {
                        resolve(`OK:${execResult}`)
                    }
                    else {
                        reject(`FAIL:${execResult} .vs. ${correctResult}`)
                    }
                }
            }, wait, lastTaskId);

            if (taskId == 1) {
                clearTimeout(timeout);
            }
        }
    });

    return promise;
}


export function setIntervalTest(taskGroupId: number, duration: number, count: number) : Promise<string> {
    const kAllowedScheduleDiffInMS = 20;

    let setInterval = napa.timer.setInterval;
    let clearInterval = napa.timer.clearInterval;
    let setTimeout = napa.timer.setTimeout;
    let clearTimeout = napa.timer.clearTimeout;

    let correctResult = "";
    for (let i = 0; i < count; ++i) {
        correctResult = `${correctResult}:${i}_OnTime`
    }

    let repeatCount = 0;
    let execResult = "";
    let startTime = Date.now();
    let interval = setInterval((taskGroupId: number, duration: number) => {
        let wait = Date.now() - startTime;
        execResult = `${execResult}:${repeatCount}_OnTime`;
        let avgScheduleDiff = Math.abs(wait - (1 + repeatCount) * duration) / (1 + repeatCount);
        if (avgScheduleDiff > kAllowedScheduleDiffInMS) {
            execResult = `${execResult}(X)`;
        }
        repeatCount = repeatCount + 1;
    }, duration, taskGroupId, duration)

    setTimeout(()=> { 
        clearInterval(interval);
    }, duration * (count + 0.5));

    let promise = new Promise<string>((resolve, reject) => {
        setTimeout(() => {
            if (execResult == correctResult) {
                resolve(`OK:${execResult}`)
            }
            else {
                reject(`FAIL:${execResult} .vs. ${correctResult}`)
            }
        }, duration * (count + 5));
    });
    return promise;
}

var zone: napa.zone.Zone;

async function initZone(workerCount: number) {
    zone = napa.zone.create('zone', { workers: workerCount });
    await zone.broadcast('');
}

declare var __in_napa: boolean;
if (typeof __in_napa === 'undefined') {
    let assert = require('assert');

    const NUMBER_OF_WORKERS = 3;
    initZone(NUMBER_OF_WORKERS);

    const kTaskGroupCount = 3;

    describe("SetImmediate/clearImmediate", function() {
        let promises: Promise<napa.zone.Result>[] = [];
        for (let groupId = 0; groupId < kTaskGroupCount; groupId++) {
            let res = zone.execute('./timer-test', 'setImmediateTest', [groupId]);
            promises.push(res);
        }

        for (let groupId = 0; groupId < kTaskGroupCount; groupId++) {
            it(`Immediate test group:${groupId} should return string prefixed with OK`, 
                async function() {
                    let result = (await promises[groupId]).value;
                    assert(result.startsWith('OK'), `${result}`);
                }
            );
        }
    });


    describe("SetTimeout/clearTimeout", async function() {
        let promises: Promise<napa.zone.Result>[] = [];
        for (let groupId = 0; groupId < kTaskGroupCount; groupId++) {
            let res = zone.execute('./timer-test', 'setTimeoutTest', [groupId]);
            promises.push(res);
        }

        for (let groupId = 0; groupId < kTaskGroupCount; groupId++) {
            it(`Timeout test group:${groupId} should return string prefixed with OK`, 
                async function() {
                    let result = (await promises[groupId]).value;
                    assert(result.startsWith('OK'), `${result}`);
                }
            );
        }
    });


    describe("setInterval/clearInterval", function() {
        it(`Interval test should return string prefixed with OK`, 
            async function() {
                let promise = zone.execute('./timer-test', 'setIntervalTest', ["0", 200, 6]);
                let result = (await promise).value;
                assert(result.startsWith('OK'), `${result}`);
            }
        ).timeout(3000);
    });
}




