// --smr-const-fold evaluates constant expressions at compile time.
//
// RUN: smr-opt --smr-const-fold %s | FileCheck %s

!s32i = !cir.int<s, 32>

module {
  // Test basic constant folding of a binary operation (1 + 2 -> 3).
  //
  // CHECK-LABEL: cir.func @fold_add(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i) -> !s32i
  // CHECK:         %[[RES:.*]] = cir.const #cir.int<3> : !s32i
  // CHECK-NOT:     cir.binop(add
  // CHECK:         cir.return %[[RES]] : !s32i
  cir.func @fold_add(%arg0: !s32i) -> !s32i {
    %c1 = cir.const #cir.int<1> : !s32i
    %c2 = cir.const #cir.int<2> : !s32i
    %0 = cir.binop(add, %c1, %c2) : !s32i
    cir.return %0 : !s32i
  }

  // Operations with variable operands must NOT be folded.
  //
  // CHECK-LABEL: cir.func @no_fold_variable(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i) -> !s32i
  // CHECK:         %[[C1:.*]] = cir.const #cir.int<1> : !s32i
  // CHECK:         %[[RES:.*]] = cir.binop(add, %[[ARG0]], %[[C1]]) : !s32i
  // CHECK:         cir.return %[[RES]] : !s32i
  cir.func @no_fold_variable(%arg0: !s32i) -> !s32i {
    %c1 = cir.const #cir.int<1> : !s32i
    %0 = cir.binop(add, %arg0, %c1) : !s32i
    cir.return %0 : !s32i
  }
}