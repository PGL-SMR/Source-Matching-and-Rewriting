// --smr-unroll copies the body of innermost canonical loops. The main loop runs
// while the last copy is in range, and the original loop after it runs the
// iterations left over.
//
// RUN: smr-opt --smr-unroll=factor=3 %s | FileCheck %s
// RUN: smr-opt --smr-unroll=factor=1 %s | FileCheck %s --check-prefix=NOOP

!s32i = !cir.int<s, 32>

module {
  // CHECK-LABEL: cir.func @loop_unroll_epilogue(
  // CHECK:         %[[IV:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init]
  //
  // CHECK:         cir.for : cond {
  // CHECK:           %[[I:.*]] = cir.load %[[IV]]
  // CHECK:           %[[TWO:.*]] = cir.const #cir.int<2> : !s32i
  // CHECK:           %[[LAST:.*]] = cir.binop(add, %[[I]], %[[TWO]])
  // CHECK:           cir.cmp(lt, %[[LAST]], %arg1)
  // CHECK:         } body {
  // CHECK:           %[[I0:.*]] = cir.load %[[IV]]
  // CHECK:           cir.ptr_stride %arg0, %[[I0]]
  // CHECK:           %[[I1:.*]] = cir.load %[[IV]]
  // CHECK:           %[[ONE:.*]] = cir.const #cir.int<1> : !s32i
  // CHECK:           %[[J1:.*]] = cir.binop(add, %[[I1]], %[[ONE]])
  // CHECK:           cir.ptr_stride %arg0, %[[J1]]
  // CHECK:           %[[I2:.*]] = cir.load %[[IV]]
  // CHECK:           %[[TWO2:.*]] = cir.const #cir.int<2> : !s32i
  // CHECK:           %[[J2:.*]] = cir.binop(add, %[[I2]], %[[TWO2]])
  // CHECK:           cir.ptr_stride %arg0, %[[J2]]
  // CHECK-NOT:       cir.ptr_stride
  // CHECK:         } step {
  // CHECK:           %[[S:.*]] = cir.load %[[IV]]
  // CHECK:           %[[THREE:.*]] = cir.const #cir.int<3> : !s32i
  // CHECK:           %[[NEXT:.*]] = cir.binop(add, %[[S]], %[[THREE]])
  // CHECK:           cir.store %[[NEXT]], %[[IV]]
  // CHECK:         }
  //
  // CHECK:         cir.for : cond {
  // CHECK:           %[[E:.*]] = cir.load %[[IV]]
  // CHECK:           cir.cmp(lt, %[[E]], %arg1)
  // CHECK:         } body {
  // CHECK:           cir.ptr_stride
  // CHECK-NOT:       cir.ptr_stride
  // CHECK:         } step {
  // CHECK:           cir.unary(inc
  // CHECK:         }
  // CHECK-NOT:     cir.for
  // CHECK:         cir.return
  //
  // NOOP-LABEL: cir.func @loop_unroll_epilogue(
  // NOOP-COUNT-1: cir.for
  // NOOP-NOT:     cir.for
  // NOOP:         cir.return
  cir.func @loop_unroll_epilogue(%arg0: !cir.ptr<!s32i>, %arg1: !s32i) {
    %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init] {alignment = 4 : i64}
    %1 = cir.const #cir.int<0> : !s32i
    cir.store %1, %0 : !s32i, !cir.ptr<!s32i>
    cir.for : cond {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.cmp(lt, %2, %arg1) : !s32i, !cir.bool
      cir.condition(%3)
    } body {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.ptr_stride %arg0, %2 : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
      %4 = cir.load %3 : !cir.ptr<!s32i>, !s32i
      %5 = cir.unary(inc, %4) nsw : !s32i, !s32i
      cir.store %5, %3 : !s32i, !cir.ptr<!s32i>
      cir.yield
    } step {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.unary(inc, %2) nsw : !s32i, !s32i
      cir.store %3, %0 : !s32i, !cir.ptr<!s32i>
      cir.yield
    }
    cir.return
  }

  // A decreasing loop is not canonical: it stays as it is.
  //
  // CHECK-LABEL: cir.func @decreasing(
  // CHECK:         cir.for : cond {
  // CHECK:         } step {
  // CHECK:           cir.unary(dec
  // CHECK:         }
  // CHECK-NOT:     cir.for
  // CHECK:         cir.return
  cir.func @decreasing(%arg0: !cir.ptr<!s32i>, %arg1: !s32i) {
    %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init] {alignment = 4 : i64}
    cir.store %arg1, %0 : !s32i, !cir.ptr<!s32i>
    cir.for : cond {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.const #cir.int<0> : !s32i
      %4 = cir.cmp(gt, %2, %3) : !s32i, !cir.bool
      cir.condition(%4)
    } body {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.ptr_stride %arg0, %2 : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
      %4 = cir.const #cir.int<0> : !s32i
      cir.store %4, %3 : !s32i, !cir.ptr<!s32i>
      cir.yield
    } step {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.unary(dec, %2) nsw : !s32i, !s32i
      cir.store %3, %0 : !s32i, !cir.ptr<!s32i>
      cir.yield
    }
    cir.return
  }
}
