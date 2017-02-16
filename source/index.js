// this script is run when require() is called and should return the module
// TODO
'use strict';
var rpio = require("../../build/Release/rpio.node")
module.exports = rpio; // <=> {cleanup: rpio.cleanup, ...};?
