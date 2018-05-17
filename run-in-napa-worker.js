"use strict";

let path=require('path');
let napa=require('./lib/index');
let debug=require('debug')('napa:testInWorker');

if (process.argv.length <= 2) {
  throw new error("Usage: node thisWrapperScript originalTestScripts [args]");
}

process.on('unhandledRejection', (reason, p) => {
  console.error(reason, 'Unhandled Rejection at Promise', p);
  process.exit(1);
});

process.on('uncaughtException', err => {
  console.error(err, 'Uncaught Exception thrown');
  process.exit(1);
});


debug("------Wrapper ARGV:" + process.argv);
let originalScriptPath=path.resolve(process.argv[2]);
let scriptArgs=process.argv.slice(3);

let workerCount = Number(process.env.NODE_WORKER_TEST_WORKER_COUNT || 1);
debug("Using NODE_WORKER_TEST_WORKER_COUNT=", workerCount);
let testZone = napa.zone.create("testZone", {workers : workerCount});
testZone.on("terminated", (exit_code) => {
    process.exit(exit_code);
});

//Setting argv in worker(s)
process.argv = process.argv.slice(0,0).concat(process.argv.slice(2));
let args = JSON.stringify(process.argv);
let set_args_command = 
  "process.argv=" + args + ";" +
  'console.log("------WORKER ARGV:", process.argv);' +
  "0;";
testZone.broadcast(set_args_command);

//Setting process.exit in worker(s)
let command_process_exit='process.exit=function(code){ if (code || code === 0) process.exitCode = code; process.exitCode = process.exitCode || 0; let nodezone = napa.zone.node; nodezone.broadcast(`console.log("----Receiving worker exit(${process.exitCode})"); process.exit(${process.exitCode});`); };';
testZone.broadcast(command_process_exit);

testZone.broadcast(`require("${originalScriptPath}"); 0;`);
testZone.recycle();

0;

