// The smr passes are registered in smr-opt and accept their options. They do
// not transform anything yet, so the module comes back unchanged.
//
// RUN: smr-opt --smr-unroll %s | FileCheck %s
// RUN: smr-opt --smr-unroll=factor=2 %s | FileCheck %s
// RUN: smr-opt --smr-tiling %s | FileCheck %s
// RUN: smr-opt --smr-tiling=size=16 %s | FileCheck %s
// RUN: smr-opt --smr-dce %s | FileCheck %s
// RUN: not smr-opt --smr-unroll=step=2 %s 2>&1 | FileCheck %s --check-prefix=BAD-OPTION

// BAD-OPTION: no such option step

!s32i = !cir.int<s, 32>

module {
  // CHECK-LABEL: cir.func @add(
  // CHECK:         %[[SUM:.*]] = cir.add nsw %arg0, %arg1 : !s32i
  // CHECK:         cir.return %[[SUM]] : !s32i
  cir.func @add(%arg0: !s32i, %arg1: !s32i) -> !s32i {
    %0 = cir.add nsw %arg0, %arg1 : !s32i
    cir.return %0 : !s32i
  }

  // CHECK-LABEL: cir.func @use(
  // CHECK:         cir.call @add(
  cir.func @use(%arg0: !s32i) -> !s32i {
    %0 = cir.call @add(%arg0, %arg0) : (!s32i, !s32i) -> !s32i
    cir.return %0 : !s32i
  }
}
