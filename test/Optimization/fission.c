// --smr-fission gives each statement of the body of an innermost canonical
// loop a loop of its own. What a statement needs is computed again in its
// loop. A body that cannot be cut by the values we see stays.
//
// RUN: clang -fclangir -ffp-contract=off -emit-cir %s -o %t.cir
// RUN: smr-opt --smr-fission %t.cir | FileCheck %s

// CHECK-LABEL: cir.func {{.*}} @two(
// CHECK:         cir.for
// CHECK:           cir.fmul
// CHECK:           cir.store align(4) {{.*}} : !cir.float
// CHECK-NOT:       cir.shift
// CHECK:         cir.store {{.*}} : !s32i
// CHECK:         cir.for
// CHECK:           cir.shift
// CHECK-NOT:       cir.fmul
// CHECK:           cir.store align(4) {{.*}} : !s32i
// CHECK-NOT:     cir.for
void two(float *a, float *b, float *c, int *f, int *g, int n) {
  for (int i = 0; i < n; ++i) {
    a[i] = b[i] * c[i];
    f[i] = g[i] << 2;
  }
}

// CHECK-LABEL: cir.func {{.*}} @three(
// CHECK:         cir.for
// CHECK:         cir.for
// CHECK:         cir.for
// CHECK-NOT:     cir.for
void three(int *a, int *b, int *c, int n) {
  for (int i = 0; i < n; ++i) {
    a[i] = 1;
    b[i] = 2;
    c[i] = 3;
  }
}

// The second statement needs the load of t[i] that the first computes too.
// CHECK-LABEL: cir.func {{.*}} @shared(
// CHECK:         cir.for
// CHECK:           cir.load
// CHECK:         cir.for
// CHECK:           cir.load
void shared(int *a, int *b, int *t, int n) {
  for (int i = 0; i < n; ++i) {
    a[i] = t[i] + 1;
    b[i] = t[i] * 2;
  }
}

// One statement has nothing to split.
// CHECK-LABEL: cir.func {{.*}} @single(
// CHECK:         cir.for
// CHECK-NOT:     cir.for
void single(int *a, int n) {
  for (int i = 0; i < n; ++i)
    a[i] = 0;
}

// The second statement reads the scalar the first one writes.
// CHECK-LABEL: cir.func {{.*}} @scalar(
// CHECK:         cir.for
// CHECK-NOT:     cir.for
void scalar(int *a, int *b, int n) {
  int s = 0;
  for (int i = 0; i < n; ++i) {
    s = a[i];
    b[i] = s;
  }
}

// CHECK-LABEL: cir.func {{.*}} @call(
// CHECK:         cir.for
// CHECK:           cir.call @f
// CHECK:         cir.for
// CHECK:           cir.store
void f(int);
void call(int *a, int n) {
  for (int i = 0; i < n; ++i) {
    f(i);
    a[i] = i;
  }
}
