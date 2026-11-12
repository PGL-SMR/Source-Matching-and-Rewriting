// --smr-tiling strip-mines innermost canonical loops and tiles nests. A tile
// ends at min(iv + span, bound), computed without overflow when the tile starts,
// so the loop of a tile has one comparison like the original.
//
// RUN: smr-opt --smr-tiling=size=8 %s | FileCheck %s
// RUN: smr-opt --smr-tiling=size=1 %s | FileCheck %s --check-prefix=NOOP

!s32i = !cir.int<s, 32>

module {
  // CHECK-LABEL: cir.func @strip_mine(
  // CHECK:         %[[IV:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init]
  //
  // CHECK:         cir.for : cond {
  // CHECK:           %[[I:.*]] = cir.load %[[IV]]
  // CHECK:           cir.cmp(lt, %[[I]], %arg1)
  // CHECK:         } body {
  // CHECK:           %[[START:.*]] = cir.load %[[IV]]
  // CHECK:           %[[UB:.*]] = cir.cast integral %arg1 : !s32i -> !u32i
  // CHECK:           %[[UI:.*]] = cir.cast integral %[[START]] : !s32i -> !u32i
  // CHECK:           %[[DIST:.*]] = cir.binop(sub, %[[UB]], %[[UI]])
  // CHECK:           %[[SPAN:.*]] = cir.const #cir.int<8> : !u32i
  // CHECK:           %[[MORE:.*]] = cir.cmp(gt, %[[DIST]], %[[SPAN]])
  // CHECK:           %[[NEXT:.*]] = cir.binop(add, %[[START]], {{.*}}) : !s32i
  // CHECK:           %[[END:.*]] = cir.select if %[[MORE]] then %[[NEXT]] else %arg1
  //
  // CHECK:           cir.for : cond {
  // CHECK:             %[[J:.*]] = cir.load %[[IV]]
  // CHECK:             cir.cmp(lt, %[[J]], %[[END]])
  // CHECK:           } body {
  // CHECK:             cir.ptr_stride %arg0
  // CHECK:           } step {
  // CHECK:             cir.unary(inc
  // CHECK:           }
  // CHECK:           cir.yield
  // CHECK:         } step {
  // CHECK-NEXT:      cir.yield
  // CHECK-NEXT:    }
  // CHECK-NOT:     cir.for
  // CHECK:         cir.return
  //
  // NOOP-LABEL: cir.func @strip_mine(
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
  // CHECK-DAG:     %[[START_J:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["tile.start.j"]
  // CHECK-DAG:     %[[START_I:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["tile.start.i"]
  // CHECK-DAG:     %[[I:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init]
  // CHECK-DAG:     %[[J:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["j", init]
  //
  // CHECK-NOT:     cir.if
  // CHECK:         cir.for : cond {
  // CHECK:         } body {
  // CHECK:           cir.store {{.*}}, %[[START_I]]
  // CHECK:           cir.store {{.*}}, %[[J]]
  // CHECK:           cir.do {
  // CHECK:             cir.store {{.*}}, %[[START_J]]
  // CHECK:             cir.store {{.*}}, %[[I]]
  // CHECK:             %[[END_I:.*]] = cir.select
  //
  // CHECK:             cir.for : cond {
  // CHECK:               cir.cmp(lt, {{.*}}, %[[END_I]])
  // CHECK:             } body {
  // CHECK:               %[[TILE_J:.*]] = cir.load %[[START_J]]
  // CHECK:               cir.store %[[TILE_J]], %[[J]]
  // CHECK:               %[[END_J:.*]] = cir.select
  // CHECK:               cir.for : cond {
  // CHECK:                 cir.cmp(lt, {{.*}}, %[[END_J]])
  // CHECK:               } body {
  // CHECK:                 cir.ptr_stride %arg0
  // CHECK:               } step {
  // CHECK:               }
  // CHECK:             } step {
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

  // Three loops, and a statement that only writes a slot local to the body
  // before the inner loop. Only the two inner loops are tiled, and the loop
  // above them runs in full inside the tiles, guarded by its own condition and
  // the one of k. The tiles of j go outside the tiles of k.
  //
  // CHECK-LABEL: cir.func @chain(
  // CHECK-DAG:     %[[K:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["k", init]
  // CHECK-DAG:     %[[START_K:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["tile.start.i"]
  // CHECK-DAG:     %[[START_J:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["tile.start.j"]
  // CHECK-DAG:     cir.alloca !s32i, !cir.ptr<!s32i>, ["part", init]
  //
  // CHECK:         cir.if
  // CHECK:           cir.if
  // CHECK:             cir.for : cond {
  // CHECK:               cir.cmp(lt, {{.*}}, %arg1)
  // CHECK:             } body {
  // CHECK:               cir.store {{.*}}, %[[START_J]]
  // CHECK:               cir.do {
  // CHECK:                 cir.store {{.*}}, %[[START_K]]
  // CHECK:                 cir.for : cond {
  // CHECK:                   cir.cmp(lt, {{.*}}, %arg3)
  // CHECK:                 } body {
  // CHECK:                   %[[TILE_K:.*]] = cir.load %[[START_K]]
  // CHECK:                   cir.store %[[TILE_K]], %[[K]]
  // CHECK:                   %[[END_K:.*]] = cir.select
  // CHECK:                   cir.for : cond {
  // CHECK:                     cir.cmp(lt, {{.*}}, %[[END_K]])
  // CHECK:                   } body {
  // CHECK:                     cir.store {{.*}} : !s32i, !cir.ptr<!s32i>
  // CHECK:                     cir.load %[[START_J]]
  // CHECK:                     %[[END_J:.*]] = cir.select
  // CHECK:                     cir.for : cond {
  // CHECK:                       cir.cmp(lt, {{.*}}, %[[END_J]])
  // CHECK:                     } body {
  // CHECK:                       cir.ptr_stride %arg0
  // CHECK:                     }
  // CHECK:                   }
  // CHECK:                 }
  // CHECK:               } while {
  // CHECK:                 cir.cmp(lt, {{.*}}, %arg2)
  // CHECK:             }
  // CHECK-NOT:     cir.for
  // CHECK:         cir.return
  cir.func @chain(%arg0: !cir.ptr<!s32i>, %arg1: !s32i, %arg2: !s32i, %arg3: !s32i) {
    %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["p", init] {alignment = 4 : i64}
    %1 = cir.alloca !s32i, !cir.ptr<!s32i>, ["k", init] {alignment = 4 : i64}
    %2 = cir.alloca !s32i, !cir.ptr<!s32i>, ["j", init] {alignment = 4 : i64}
    %3 = cir.const #cir.int<0> : !s32i
    cir.store %3, %0 : !s32i, !cir.ptr<!s32i>
    cir.for : cond {
      %4 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %5 = cir.cmp(lt, %4, %arg3) : !s32i, !cir.bool
      cir.condition(%5)
    } body {
      cir.store %3, %1 : !s32i, !cir.ptr<!s32i>
      cir.for : cond {
        %4 = cir.load %1 : !cir.ptr<!s32i>, !s32i
        %5 = cir.cmp(lt, %4, %arg2) : !s32i, !cir.bool
        cir.condition(%5)
      } body {
        %4 = cir.alloca !s32i, !cir.ptr<!s32i>, ["part", init] {alignment = 4 : i64}
        %5 = cir.load %0 : !cir.ptr<!s32i>, !s32i
        %6 = cir.load %1 : !cir.ptr<!s32i>, !s32i
        %7 = cir.binop(add, %5, %6) : !s32i
        cir.store %7, %4 : !s32i, !cir.ptr<!s32i>
        cir.store %3, %2 : !s32i, !cir.ptr<!s32i>
        cir.for : cond {
          %8 = cir.load %2 : !cir.ptr<!s32i>, !s32i
          %9 = cir.cmp(lt, %8, %arg1) : !s32i, !cir.bool
          cir.condition(%9)
        } body {
          %8 = cir.load %2 : !cir.ptr<!s32i>, !s32i
          %9 = cir.ptr_stride %arg0, %8 : (!cir.ptr<!s32i>, !s32i) -> !cir.ptr<!s32i>
          %10 = cir.load %4 : !cir.ptr<!s32i>, !s32i
          cir.store %10, %9 : !s32i, !cir.ptr<!s32i>
          cir.yield
        } step {
          %8 = cir.load %2 : !cir.ptr<!s32i>, !s32i
          %9 = cir.unary(inc, %8) nsw : !s32i, !s32i
          cir.store %9, %2 : !s32i, !cir.ptr<!s32i>
          cir.yield
        }
        cir.yield
      } step {
        %4 = cir.load %1 : !cir.ptr<!s32i>, !s32i
        %5 = cir.unary(inc, %4) nsw : !s32i, !s32i
        cir.store %5, %1 : !s32i, !cir.ptr<!s32i>
        cir.yield
      }
      cir.yield
    } step {
      %4 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %5 = cir.unary(inc, %4) nsw : !s32i, !s32i
      cir.store %5, %0 : !s32i, !cir.ptr<!s32i>
      cir.yield
    }
    cir.return
  }

  // A statement that writes memory next to the inner loop cannot run again for
  // every tile, so the nest is not tiled and only the inner loop is strip-mined.
  //
  // CHECK-LABEL: cir.func @writes_memory(
  // CHECK-NOT:     cir.do
  // CHECK-NOT:     cir.if
  // CHECK:         cir.return
  cir.func @writes_memory(%arg0: !cir.ptr<!s32i>, %arg1: !s32i) {
    %0 = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init] {alignment = 4 : i64}
    %1 = cir.alloca !s32i, !cir.ptr<!s32i>, ["j", init] {alignment = 4 : i64}
    %2 = cir.const #cir.int<0> : !s32i
    cir.store %2, %0 : !s32i, !cir.ptr<!s32i>
    cir.for : cond {
      %3 = cir.load %0 : !cir.ptr<!s32i>, !s32i
      %4 = cir.cmp(lt, %3, %arg1) : !s32i, !cir.bool
      cir.condition(%4)
    } body {
      cir.store %2, %arg0 : !s32i, !cir.ptr<!s32i>
      cir.store %2, %1 : !s32i, !cir.ptr<!s32i>
      cir.for : cond {
        %3 = cir.load %1 : !cir.ptr<!s32i>, !s32i
        %4 = cir.cmp(lt, %3, %arg1) : !s32i, !cir.bool
        cir.condition(%4)
      } body {
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
