'use strict';

import { strict as assert } from "assert";

import { answer } from "esm_nodepath";
import { answer2 } from "esm_nodepath2";

assert.strictEqual(answer(), 42);
assert.strictEqual(answer2(), 37);

console.log(answer() + " & " + answer2());
