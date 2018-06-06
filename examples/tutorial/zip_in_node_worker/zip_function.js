// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

"use strict";

const fs = require('fs');
const { promisify } = require('util');
const uuidv4 = require('uuid/v4');
const zlib = require('zlib');
const debug = require('debug')('napa:sample:zip_in_node_worker');

exports.generate_zip_unzip = function(count, pathUnzipped) {
    debug(`generating ${pathUnzipped}...`);
    const unzippedStream = fs.createWriteStream(pathUnzipped);
    function write(err) {
        if (err) throw new Error(err);
        count --;
        if (count > 0) {
            const uuid = uuidv4();
            unzippedStream.write(`${uuid}\n`, 'utf8', write);
        }
        else {
            unzippedStream.end();
        }
    }

    unzippedStream.on('finish', () => {
        debug(`gzipping ${pathUnzipped}...`);
        const gzip = zlib.createGzip();
        const inp = fs.createReadStream(pathUnzipped);
        const out = fs.createWriteStream(`${pathUnzipped}.gz`);

        out.on('finish', () => {
            debug(`finished gzipping ${pathUnzipped} ...`);
        });
    
        inp.pipe(gzip).pipe(out, {end: true});
    });

    write();

    0;
}
