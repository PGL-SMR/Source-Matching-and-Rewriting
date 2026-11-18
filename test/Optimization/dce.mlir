// --smr-dce erases operations nobody uses, the slots that are only written and
// the private functions nobody references. Calls are kept, and so is every
// public function.
//
// RUN: smr-opt --smr-dce %s | FileCheck %s

!s32i = !cir.int<s, 32>

module {
  // CHECK-LABEL: cir.func @dead_values(
  // CHECK-NOT:     cir.alloca
  // CHECK-NOT:     cir.const
  // CHECK-NOT:     cir.load
  // CHECK-NOT:     cir.ptr_stride
  // CHECK-NOT:     cir.{{f?(add|sub|mul|div|rem)}}
  // CHECK:         cir.return %arg0
  cir.func @dead_values(%arg0: !s32i, %arg1: !cir.ptr<!s32i>) -> !s32i {
    %0 = cir.alloca "unused" align(4) : !cir.ptr<!s32i>
    %1 = cir.const #cir.int<7> : !s32i
    %2 = cir.load %arg1 : !cir.ptr<!s32i>, !s32i
    %3 = cir.ptr_stride %arg1, %1 : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
    %4 = cir.add %2, %1 : !s32i
    cir.return %arg0 : !s32i
  }

  // A slot that is only stored to goes with its stores, and the values that
  // fed them follow. A slot that is read, one whose address is used otherwise,
  // a volatile one and the slot of a parameter stay.
  //
  // CHECK-LABEL: cir.func @store_only(
  // CHECK-NOT:     cir.alloca "written"
  // CHECK-DAG:     cir.alloca "read" align({{[0-9]+}}) init
  // CHECK-DAG:     cir.alloca "escapes" align({{[0-9]+}}) init
  // CHECK-DAG:     cir.alloca "vol" align({{[0-9]+}}) init
  // CHECK-DAG:     cir.alloca "param" align({{[0-9]+}}) init
  // CHECK-NOT:     cir.{{f?(add|sub|mul|div|rem)}}
  // CHECK:         cir.return
  cir.func @store_only(%arg0: !s32i, %arg1: !s32i) -> !s32i {
    %param = cir.alloca "param" align(4) init : !cir.ptr<!s32i>
    %written = cir.alloca "written" align(4) init : !cir.ptr<!s32i>
    %read = cir.alloca "read" align(4) init : !cir.ptr<!s32i>
    %escapes = cir.alloca "escapes" align(4) init : !cir.ptr<!s32i>
    %vol = cir.alloca "vol" align(4) init : !cir.ptr<!s32i>
    cir.store %arg0, %param : !s32i, !cir.ptr<!s32i>
    %sum = cir.add %arg0, %arg1 : !s32i
    cir.store %sum, %written : !s32i, !cir.ptr<!s32i>
    cir.store %arg1, %read : !s32i, !cir.ptr<!s32i>
    cir.store %arg1, %escapes : !s32i, !cir.ptr<!s32i>
    cir.store volatile %arg1, %vol : !s32i, !cir.ptr<!s32i>
    %value = cir.load %read : !cir.ptr<!s32i>, !s32i
    cir.call @take_address(%escapes) : (!cir.ptr<!s32i>) -> ()
    cir.return %value : !s32i
  }

  cir.func private @take_address(!cir.ptr<!s32i>)

  // A call may have effects: it stays even when its result is unused.
  //
  // CHECK-LABEL: cir.func @effects(
  // CHECK:         cir.call @used_private(%arg0)
  // CHECK:         cir.store %arg0, %arg1
  // CHECK:         cir.return
  cir.func @effects(%arg0: !s32i, %arg1: !cir.ptr<!s32i>) {
    %0 = cir.call @used_private(%arg0) : (!s32i) -> !s32i
    cir.store %arg0, %arg1 : !s32i, !cir.ptr<!s32i>
    cir.return
  }

  // CHECK:       cir.func {{.*}}@used_private(
  cir.func internal private @used_private(%arg0: !s32i) -> !s32i {
    cir.return %arg0 : !s32i
  }

  // CHECK-NOT:   @unused_private
  cir.func internal private @unused_private(%arg0: !s32i) -> !s32i {
    cir.return %arg0 : !s32i
  }

  // CHECK-NOT:   @unused_declaration
  cir.func private @unused_declaration(!s32i) -> !s32i

  // CHECK:       cir.func @public_unused(
  cir.func @public_unused(%arg0: !s32i) -> !s32i {
    cir.return %arg0 : !s32i
  }
}
