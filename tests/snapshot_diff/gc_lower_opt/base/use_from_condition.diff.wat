;; ====================
(func $tests/snapshot_diff/gc_lower_opt/base/use_from_condition/_start (type $func.0 (func (result i32)))
  (local i32)
;; =========add========
  (local i32)
;; ====================
  block ;;i32
;; =========add========
      i32.const 8
    call $~lib/rt/__decrease_sp
      block ;;unreachable
;; ====================
        if ;;none
          global.get $tests/snapshot_diff/gc_lower_opt/base/use_from_condition/v
                i32.const 0
              call $tests/snapshot_diff/common_lib/normal/Normal#constructor
;; ======remove=======
;;      call $~lib/rt/__localtostack
;; =========add========
            call $~lib/rt/__tostack<0>
;; ====================
          local.set $0
                i32.const 0
              call $tests/snapshot_diff/common_lib/normal/Normal#constructor
;; ======remove=======
;;      call $~lib/rt/__localtostack
;; =========add========
            call $~lib/rt/__tostack<4>
;; ====================
          local.set $0
        end
;; =========add========
        block ;;unreachable
;; ====================
            local.get $0
;; =========add========
          local.set $1
            i32.const 8
          call $~lib/rt/__increase_sp
            local.get $1
;; ====================
          return
        end
;; =========add========
      end
    local.tee $1
      i32.const 8
    call $~lib/rt/__increase_sp
    local.get $1
  end
;; ====================
)
