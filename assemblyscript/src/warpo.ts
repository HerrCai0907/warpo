declare function _WarpoMarkDataElementImmutable(begin: u32, size: u32): void;

export function markDataElementImmutable(begin: i64, size: i32): void {
  _WarpoMarkDataElementImmutable(i64_low(begin), <u32>size);
}
