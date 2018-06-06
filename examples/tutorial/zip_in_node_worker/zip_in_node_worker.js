// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

"use strict";

const napa = require('../../../lib');
const debug = require('debug')('napa:sample:zip_in_node_worker');

const {generate_zip_unzip} = require('./zip_function');

const numberOfFiles = 4;
const sizeInUuid = 256 * 1024;

if (process.argv.length > 2) {
    // Just run in node main if any other parameter is given
    console.log("====Running in node main only...");
    for (let i=0; i < numberOfFiles; ++i) {
        const filename = `temp_content_000${i}.txt`;
        generate_zip_unzip(sizeInUuid, filename);
    }
}
else {
    const sampleZipZone = napa.zone.create('sampleZipZone', {workers : 1});
    for (let i=0; i < numberOfFiles; ++i) {
        const filename = `temp_content_000${i}.txt`;
        sampleZipZone.execute('./zip_function.js', 'generate_zip_unzip', [sizeInUuid, filename]);
    };
    let zoneFinished = false;
    sampleZipZone.on('terminated', () => {zoneFinished = true;})
    setTimeout(() => {
        sampleZipZone.recycle();
    }, 3000);
}

0;
