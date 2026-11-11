// --smr-cse eliminates redundant memory-effect-free operations (e.g. cir.binop,
// cir.const, cir.cast) when an equivalent operation dominates the call site.
//
// RUN: smr-opt --smr-cse %s | FileCheck %s

!s32i = !cir.int<s, 32>

module {
  // Test basic Common Subexpression Elimination for binary operations and constants.
  //
  // CHECK-LABEL: cir.func @simple_cse(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i, %[[ARG1:.*]]: !s32i) -> !s32i
  // CHECK:         %[[C1:.*]] = cir.const #cir.int<1> : !s32i
  // CHECK-NEXT:    %[[ADD1:.*]] = cir.binop(add, %[[ARG0]], %[[ARG1]]) : !s32i
  // CHECK-NEXT:    %[[RES:.*]] = cir.binop(mul, %[[ADD1]], %[[C1]]) : !s32i
  // CHECK-NOT:     cir.const #cir.int<1>
  // CHECK-NOT:     cir.binop(add
  // CHECK:         cir.return %[[RES]] : !s32i
  cir.func @simple_cse(%arg0: !s32i, %arg1: !s32i) -> !s32i {
    %0 = cir.const #cir.int<1> : !s32i
    %1 = cir.binop(add, %arg0, %arg1) : !s32i
    
    // Redundant constant and redundant addition (must be eliminated)
    %2 = cir.const #cir.int<1> : !s32i
    %3 = cir.binop(add, %arg0, %arg1) : !s32i
    
    %4 = cir.binop(mul, %3, %2) : !s32i
    cir.return %4 : !s32i
  }

  // Operations with side effects (like cir.store) or operations reading memory
  // must NOT be incorrectly eliminated.
  //
  // CHECK-LABEL: cir.func @preserve_side_effects(
  // CHECK-SAME:    %[[ARG0:.*]]: !cir.ptr<!s32i>, %[[ARG1:.*]]: !cir.ptr<!s32i>)
  // CHECK:         %[[VAL1:.*]] = cir.load %[[ARG0]]
  // CHECK:         cir.store %[[VAL1]], %[[ARG1]]
  // CHECK:         %[[VAL2:.*]] = cir.load %[[ARG0]]
  // CHECK:         cir.store %[[VAL2]], %[[ARG1]]
  // CHECK:         cir.return
  cir.func @preserve_side_effects(%arg0: !cir.ptr<!s32i>, %arg1: !cir.ptr<!s32i>) {
    %0 = cir.load %arg0 : !cir.ptr<!s32i>, !s32i
    cir.store %0, %arg1 : !s32i, !cir.ptr<!s32i>

    // Must not be eliminated because store/load interact with memory state
    %1 = cir.load %arg0 : !cir.ptr<!s32i>, !s32i
    cir.store %1, %arg1 : !s32i, !cir.ptr<!s32i>
    cir.return
  }
}