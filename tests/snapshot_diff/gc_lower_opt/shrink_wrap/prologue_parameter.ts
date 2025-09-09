//! { "func": "^.*_start$" }

import { Normal } from "../../common_lib/normal";

export function _start(v: Normal | null): Normal | null {
  if (v == null) return null;
  v = new Normal();
  return v;
}
