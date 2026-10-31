// --smr-tiling strip-mines innermost canonical loops. The tiles are counted in
// a slot of their own, so the loop bound is evaluated as before.
//
// RUN: smr-opt --smr-tiling=size=8 %s | FileCheck %s
// RUN: smr-opt --smr-tiling=size=1 %s | FileCheck %s --check-prefix=NOOP

!s32i = !cir.int<s, 32>

module {
  // CHECK-LABEL: cir.func @strip_mine(
  // CHECK:         %[[TILE:.*]] = cir.alloca !u64i, !cir.ptr<!u64i>, ["tile"]
  // CHECK:         %[[IV:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init]
  //
  // CHECK:         cir.for : cond {
  // CHECK:           %[[I:.*]] = cir.load %[[IV]]
  // CHECK:           cir.cmp(lt, %[[I]], %arg1)
  // CHECK:         } body {
  // CHECK:           %[[ZERO:.*]] = cir.const #cir.int<0> : !u64i
  // CHECK:           cir.store %[[ZERO]], %[[TILE]]
  //
  // CHECK:           cir.for : cond {
  // CHECK:             %[[I2:.*]] = cir.load %[[IV]]
  // CHECK:             %[[IN:.*]] = cir.cmp(lt, %[[I2]], %arg1)
  // CHECK:             %[[N:.*]] = cir.load %[[TILE]]
  // CHECK:             %[[SIZE:.*]] = cir.const #cir.int<8> : !u64i
  // CHECK:             %[[ROOM:.*]] = cir.cmp(lt, %[[N]], %[[SIZE]])
  // CHECK:             %[[BOTH:.*]] = cir.select if %[[IN]] then %[[ROOM]] else
  // CHECK:             cir.condition(%[[BOTH]])
  // CHECK:           } body {
  // CHECK:             cir.ptr_stride %arg0
  // CHECK:           } step {
  // CHECK:             cir.unary(inc
  // CHECK:             cir.store {{.*}}, %[[IV]]
  // CHECK:             %[[N2:.*]] = cir.load %[[TILE]]
  // CHECK:             %[[ONE:.*]] = cir.const #cir.int<1> : !u64i
  // CHECK:             %[[NEXT:.*]] = cir.binop(add, %[[N2]], %[[ONE]])
  // CHECK:             cir.store %[[NEXT]], %[[TILE]]
  // CHECK:           }
  // CHECK:           cir.yield
  // CHECK:         } step {
  // CHECK-NEXT:      cir.yield
  // CHECK-NEXT:    }
  // CHECK-NOT:     cir.for
  // CHECK:         cir.return
  //
  // NOOP-LABEL: cir.func @strip_mine(
  // NOOP-NOT:     "tile"
  // NOOP-COUNT-1: cir.for
  // NOOP-NOT:     cir.for
  // NOOP:         cir.return
  cir.func @strip_mine(%arg0: !cir.ptr<!s32i>, %arg1: !s32i) {
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
      %4 = cir.const #cir.int<0> : !s32i
      cir.store %4, %3 : !s32i, !cir.ptr<!s32i>
      cir.yield
    } step {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.unary(inc, %2) nsw : !s32i, !s32i
      cir.store %3, %0 : !s32i, !cir.ptr<!s32i>
      cir.yield
    }
    cir.return
  }

  // A perfect nest is tiled in blocks. The point loops are the original ones,
  // inside a loop over the tiles of j that runs at least once, inside a loop
  // over the tiles of i.
  //
  // CHECK-LABEL: cir.func @nest(
  // CHECK-DAG:     %[[COUNT_J:.*]] = cir.alloca !u64i, !cir.ptr<!u64i>, ["tile.j"]
  // CHECK-DAG:     %[[COUNT_I:.*]] = cir.alloca !u64i, !cir.ptr<!u64i>, ["tile.i"]
  // CHECK-DAG:     %[[START_J:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["tile.start.j"]
  // CHECK-DAG:     %[[START_I:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["tile.start.i"]
  // CHECK-DAG:     %[[I:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init]
  // CHECK-DAG:     %[[J:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["j", init]
  //
  // CHECK:         cir.for : cond {
  // CHECK:         } body {
  // CHECK:           cir.store {{.*}}, %[[START_I]]
  // CHECK:           cir.store {{.*}}, %[[J]]
  // CHECK:           cir.do {
  // CHECK:             cir.store {{.*}}, %[[START_J]]
  // CHECK:             cir.store {{.*}}, %[[I]]
  // CHECK:             cir.store {{.*}}, %[[COUNT_I]]
  //
  // CHECK:             cir.for : cond {
  // CHECK:               cir.select
  // CHECK:             } body {
  // CHECK:               cir.store {{.*}}, %[[COUNT_J]]
  // CHECK:               %[[TILE_J:.*]] = cir.load %[[START_J]]
  // CHECK:               cir.store %[[TILE_J]], %[[J]]
  // CHECK:               cir.for : cond {
  // CHECK:                 cir.select
  // CHECK:               } body {
  // CHECK:                 cir.ptr_stride %arg0
  // CHECK:               } step {
  // CHECK:                 cir.store {{.*}}, %[[COUNT_J]]
  // CHECK:               }
  // CHECK:             } step {
  // CHECK:               cir.store {{.*}}, %[[COUNT_I]]
  // CHECK:             }
  // CHECK:           } while {
  // CHECK:             cir.condition
  // CHECK:           }
  // CHECK:         } step {
  // CHECK-NEXT:      cir.yield
  // CHECK-NEXT:    }
  // CHECK-NOT:     cir.for
  // CHECK:         cir.return
  cir.func @nest(%arg0: !cir.ptr<!s32i>, %arg1: !s32i) {
    %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init] {alignment = 4 : i64}
    %1 = cir.alloca !s32i, !cir.ptr<!s32i>, ["j", init] {alignment = 4 : i64}
    %2 = cir.const #cir.int<0> : !s32i
    cir.store %2, %0 : !s32i, !cir.ptr<!s32i>
    cir.for : cond {
      %3 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %4 = cir.cmp(lt, %3, %arg1) : !s32i, !cir.bool
      cir.condition(%4)
    } body {
      cir.store %2, %1 : !s32i, !cir.ptr<!s32i>
      cir.for : cond {
        %3 = cir.load %1 : !cir.ptr<!s32i>, !s32i
        %4 = cir.cmp(lt, %3, %arg1) : !s32i, !cir.bool
        cir.condition(%4)
      } body {
        %3 = cir.load %1 : !cir.ptr<!s32i>, !s32i
        %4 = cir.ptr_stride %arg0, %3 : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
        cir.store %2, %4 : !s32i, !cir.ptr<!s32i>
        cir.yield
      } step {
        %3 = cir.load %1 : !cir.ptr<!s32i>, !s32i
        %4 = cir.unary(inc, %3) nsw : !s32i, !s32i
        cir.store %4, %1 : !s32i, !cir.ptr<!s32i>
        cir.yield
      }
      cir.yield
    } step {
      %3 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %4 = cir.unary(inc, %3) nsw : !s32i, !s32i
      cir.store %4, %0 : !s32i, !cir.ptr<!s32i>
      cir.yield
    }
    cir.return
  }

  // A loop that fits in one tile is left as it is.
  //
  // CHECK-LABEL: cir.func @one_tile(
  // CHECK-NOT:     "tile"
  // CHECK-COUNT-1: cir.for
  // CHECK-NOT:     cir.for
  // CHECK:         cir.return
  cir.func @one_tile(%arg0: !cir.ptr<!s32i>) {
    %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init] {alignment = 4 : i64}
    %1 = cir.const #cir.int<0> : !s32i
    cir.store %1, %0 : !s32i, !cir.ptr<!s32i>
    cir.for : cond {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.const #cir.int<8> : !s32i
      %4 = cir.cmp(lt, %2, %3) : !s32i, !cir.bool
      cir.condition(%4)
    } body {
      %2 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %3 = cir.ptr_stride %arg0, %2 : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
      cir.store %1, %3 : !s32i, !cir.ptr<!s32i>
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
  // CHECK-NOT:     "tile"
  // CHECK-COUNT-1: cir.for
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
