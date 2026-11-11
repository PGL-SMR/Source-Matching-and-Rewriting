// --smr-mem2reg promotes memory allocas and eliminates store/load instructions.
//
// RUN: smr-opt --smr-mem2reg %s | FileCheck %s

!s32i = !cir.int<s, 32>

module {
  // Test basic Mem2Reg promotion: cir.alloca, cir.store and cir.load are eliminated.
  //
  // CHECK-LABEL: cir.func @promote_basic(
  // CHECK-SAME:    %[[ARG0:.*]]: !s32i) -> !s32i
  // CHECK-NOT:     cir.alloca
  // CHECK-NOT:     cir.store
  // CHECK-NOT:     cir.load
  // CHECK:         %[[RES:.*]] = cir.binop(add, %[[ARG0]], %[[ARG0]]) : !s32i
  // CHECK:         cir.return %[[RES]] : !s32i
  cir.func @promote_basic(%arg0: !s32i) -> !s32i {
    %x = cir.alloca !s32i, !cir.ptr<!s32i>, ["x"]
    cir.store %arg0, %x : !s32i, !cir.ptr<!s32i>

    %val = cir.load %x : !cir.ptr<!s32i>, !s32i
    %res = cir.binop(add, %arg0, %val) : !s32i
    cir.return %res : !s32i
  }
}