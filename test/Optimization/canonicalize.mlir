// --smr-canonicalize runs CIR's own cleanups: cir-canonicalize, then
// cir-simplify. They do not touch loops.
//
// RUN: smr-opt --smr-canonicalize %s | FileCheck %s

!s32i = !cir.int<s, 32>

module {
  // CHECK-LABEL: cir.func @empty_scope(
  // CHECK-NOT:     cir.scope
  // CHECK:         cir.return
  cir.func @empty_scope() {
    cir.scope {
    }
    cir.return
  }

  // CHECK-LABEL: cir.func @ternary(
  // CHECK:         %[[ONE:.*]] = cir.const #cir.int<1> : !s32i
  // CHECK:         %[[TWO:.*]] = cir.const #cir.int<2> : !s32i
  // CHECK:         %[[RES:.*]] = cir.select if %arg0 then %[[ONE]] else %[[TWO]]
  // CHECK:         cir.return %[[RES]]
  cir.func @ternary(%arg0: !cir.bool) -> !s32i {
    %0 = cir.ternary(%arg0, true {
      %1 = cir.const #cir.int<1> : !s32i
      cir.yield %1 : !s32i
    }, false {
      %1 = cir.const #cir.int<2> : !s32i
      cir.yield %1 : !s32i
    }) : (!cir.bool) -> !s32i
    cir.return %0 : !s32i
  }

  // CHECK-LABEL: cir.func @select_const(
  // CHECK-NOT:     cir.select
  // CHECK:         cir.return %arg0
  cir.func @select_const(%arg0: !s32i, %arg1: !s32i) -> !s32i {
    %0 = cir.const #cir.bool<true> : !cir.bool
    %1 = cir.select if %0 then %arg0 else %arg1 : (!cir.bool, !s32i, !s32i) -> !s32i
    cir.return %1 : !s32i
  }

  // CHECK-LABEL: cir.func @loop(
  // CHECK:         cir.for : cond {
  // CHECK:         } body {
  // CHECK:         } step {
  cir.func @loop(%arg0: !s32i) {
    %0 = cir.alloca "i" align(4) init : !cir.ptr<!s32i>
    %1 = cir.const #cir.int<0> : !s32i
    cir.store %1, %0 : !s32i, !cir.ptr<!s32i>
    cir.for : cond {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.cmp lt %2, %arg0 : !s32i
      cir.condition(%3)
    } body {
      cir.yield
    } step {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.inc nsw %2 : !s32i
      cir.store %3, %0 : !s32i, !cir.ptr<!s32i>
      cir.yield
    }
    cir.return
  }
}
