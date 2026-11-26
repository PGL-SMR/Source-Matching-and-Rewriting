// --smr-unroll-full replaces the innermost loops that have a known trip count
// of at most max-trip-count (32 by default) by one copy of the body for each
// iteration.
//
// RUN: clang -fclangir -ffp-contract=off -emit-cir %s -o %t.cir
// RUN: smr-opt --smr-unroll-full %t.cir | FileCheck %s
// RUN: smr-opt --smr-unroll-full=max-trip-count=3 %t.cir | FileCheck %s --check-prefix=LIMIT

// CHECK-LABEL: cir.func {{.*}} @four(
// LIMIT-LABEL: cir.func {{.*}} @four(
void four(int *a) {
  // CHECK-NOT:     cir.for
  // CHECK-COUNT-4: cir.store
  // CHECK:         cir.return
  // LIMIT:         cir.for
  for (int i = 0; i < 4; ++i)
    a[i] = i;
}

// The second copy reads the IV plus the step, so 0, 2 and 4 are stored.
// CHECK-LABEL: cir.func {{.*}} @step(
void step(int *a) {
  // CHECK-NOT:     cir.for
  // CHECK:         cir.const #cir.int<2>
  // CHECK:         cir.const #cir.int<4>
  // CHECK:         cir.return
  for (int i = 0; i < 6; i += 2)
    a[i] = 1;
}

// CHECK-LABEL: cir.func {{.*}} @large(
void large(int *a) {
  // CHECK:         cir.for
  for (int i = 0; i < 100; ++i)
    a[i] = 0;
}

// CHECK-LABEL: cir.func {{.*}} @unknown(
void unknown(int *a, int n) {
  // CHECK:         cir.for
  for (int i = 0; i < n; ++i)
    a[i] = 0;
}

// The loop leaves 8 in i, the slot gets it.
// CHECK-LABEL: cir.func {{.*}} @after(
int after(int *a) {
  int i;
  // CHECK-NOT:     cir.for
  // CHECK:         cir.const #cir.int<8>
  // CHECK:         cir.add
  // CHECK:         cir.store
  for (i = 0; i < 8; ++i)
    a[i] = 0;
  return i;
}

// Only the innermost loop is unrolled.
// CHECK-LABEL: cir.func {{.*}} @nest(
void nest(int *a, int n) {
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK-NOT:       cir.for
  // CHECK-COUNT-3:   cir.store
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < 3; ++j)
      a[i * 3 + j] = j;
}
