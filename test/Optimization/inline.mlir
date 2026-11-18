// --smr-inline inlines calls to functions defined within the module,
// resolving nested call chains until a fixed point is reached.
//
// RUN: smr-opt --smr-inline %s | FileCheck %s

!s32i = !cir.int<s, 32>

module {
  cir.func private @add(%arg0: !s32i, %arg1: !s32i) -> !s32i {
    %0 = cir.add %arg0, %arg1 : !s32i
    cir.return %0 : !s32i
  }

  // Test simple single-level function inlining.
  //
  // CHECK-LABEL: cir.func @simple_inline(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i, %[[ARG1:.*]]: !s32i) -> !s32i
  // CHECK-NOT:     cir.call
  // CHECK:         %[[RES:.*]] = cir.add %[[ARG0]], %[[ARG1]] : !s32i
  // CHECK:         cir.return %[[RES]] : !s32i
  cir.func @simple_inline(%arg0: !s32i, %arg1: !s32i) -> !s32i {
    %0 = cir.call @add(%arg0, %arg1) : (!s32i, !s32i) -> !s32i
    cir.return %0 : !s32i
  }

  cir.func private @leaf(%arg0: !s32i) -> !s32i {
    %c1 = cir.const #cir.int<1> : !s32i
    %0 = cir.add %arg0, %c1 : !s32i
    cir.return %0 : !s32i
  }

  cir.func private @mid(%arg0: !s32i) -> !s32i {
    %0 = cir.call @leaf(%arg0) : (!s32i) -> !s32i
    %c2 = cir.const #cir.int<2> : !s32i
    %1 = cir.add %0, %c2 : !s32i
    cir.return %1 : !s32i
  }

  // Test nested inlining (mid calls leaf, caller calls mid).
  // The fixed-point loop should inline both levels into the caller.
  //
  // CHECK-LABEL: cir.func @nested_inline(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i) -> !s32i
  // CHECK-NOT:     cir.call
  // CHECK:         %[[C1:.*]] = cir.const #cir.int<1> : !s32i
  // CHECK:         %[[ADD1:.*]] = cir.add %[[ARG0]], %[[C1]] : !s32i
  // CHECK:         %[[C2:.*]] = cir.const #cir.int<2> : !s32i
  // CHECK:         %[[ADD2:.*]] = cir.add %[[ADD1]], %[[C2]] : !s32i
  // CHECK:         cir.return %[[ADD2]] : !s32i
  cir.func @nested_inline(%arg0: !s32i) -> !s32i {
    %0 = cir.call @mid(%arg0) : (!s32i) -> !s32i
    cir.return %0 : !s32i
  }

  // Declaration without body (external function).
  cir.func private @external_fn(!s32i) -> !s32i

  // External functions without definition body cannot be inlined
  // and must remain as cir.call operations.
  //
  // CHECK-LABEL: cir.func @no_inline_external(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i) -> !s32i
  // CHECK:         %[[RES:.*]] = cir.call @external_fn(%[[ARG0]]) : (!s32i) -> !s32i
  // CHECK:         cir.return %[[RES]] : !s32i
  cir.func @no_inline_external(%arg0: !s32i) -> !s32i {
    %0 = cir.call @external_fn(%arg0) : (!s32i) -> !s32i
    cir.return %0 : !s32i
  }
}