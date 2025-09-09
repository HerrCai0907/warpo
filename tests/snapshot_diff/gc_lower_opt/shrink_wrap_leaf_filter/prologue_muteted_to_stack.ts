//! { "func": "^.*_start$" }

import { Normal } from "../../common_lib/normal";

export function foo(v: Normal | null): void {}

let v0: Normal | null = null;
let v1: Normal | null = null;
let a = 0;

export function _start(): Normal | null {
  foo(a ? v0 : v1);
  let v = new Normal();
  new Normal();
  return v;
}
