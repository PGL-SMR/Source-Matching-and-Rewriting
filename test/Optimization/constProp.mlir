// --smr-const-prop replaces cir.load operations with constants when the target
// memory address holds a known constant value.
//
// RUN: smr-opt --smr-const-prop %s | FileCheck %s

!s32i = !cir.int<s, 32>

module {
  // Test basic constant propagation from store to load.
  //
  // CHECK-LABEL: cir.func @propagate_const(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i) -> !s32i
  // CHECK:         [[ADDR:%.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x"]
  // CHECK:         [[C42:%.*]] = cir.const #cir.int<42> : !s32i
  // CHECK:         cir.store [[C42]], [[ADDR]]
  // CHECK-NEXT:    [[PROP_C42:%.*]] = cir.const #cir.int<42> : !s32i
  // CHECK-NEXT:    [[RES:%.*]] = cir.binop(add, %[[ARG0]], [[PROP_C42]]) : !s32i
  // CHECK-NOT:     cir.load
  // CHECK:         cir.return [[RES]] : !s32i
  cir.func @propagate_const(%arg0: !s32i) -> !s32i {
    %x = cir.alloca !s32i, !cir.ptr<!s32i>, ["x"]
    %c42 = cir.const #cir.int<42> : !s32i
    cir.store %c42, %x : !s32i, !cir.ptr<!s32i>

    %val = cir.load %x : !cir.ptr<!s32i>, !s32i
    %res = cir.binop(add, %arg0, %val) : !s32i
    cir.return %res : !s32i
  }

  // Overwritten stores with non-constants must invalidate propagation.
  //
  // CHECK-LABEL: cir.func @invalidate_on_variable_store(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i) -> !s32i
  // CHECK:         [[ADDR:%.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x"]
  // CHECK:         cir.store %[[ARG0]], [[ADDR]] :
  // CHECK:         [[LOADED:%.*]] = cir.load [[ADDR]] :
  // CHECK:         cir.return [[LOADED]] : !s32i
  cir.func @invalidate_on_variable_store(%arg0: !s32i) -> !s32i {
    %x = cir.alloca !s32i, !cir.ptr<!s32i>, ["x"]
    %c10 = cir.const #cir.int<10> : !s32i
    cir.store %c10, %x : !s32i, !cir.ptr<!s32i>

    // Variable store invalidates previous constant 10
    cir.store %arg0, %x : !s32i, !cir.ptr<!s32i>

    %val = cir.load %x : !cir.ptr<!s32i>, !s32i
    cir.return %val : !s32i
  }
}