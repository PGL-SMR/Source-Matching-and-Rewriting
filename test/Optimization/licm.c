// --smr-licm moves loop invariant code in front of the loop: operations
// without effects, loads of slots the loop does not write, and calls to the
// functions named in pure=.
//
// RUN: clang -fclangir -ffp-contract=off -emit-cir %s -o %t.cir
// RUN: smr-opt --smr-licm=pure=twice %t.cir | FileCheck %s

int twice(int x);
int other(int x);
void use(int *p);

// CHECK-LABEL: cir.func {{.*}} @scalars(
void scalars(double *a, double x, double y, int n) {
  // CHECK:         cir.fmul
  // CHECK:         cir.fadd
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK-NOT:       cir.{{f?(add|sub|mul|div|rem)}}
  // CHECK:         } step {
  for (int i = 0; i < n; ++i)
    a[i] = x * y + 3.0;
}

// CHECK-LABEL: cir.func {{.*}} @written(
int written(int *a, int n) {
  int sum = 0;
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.load
  // CHECK:           cir.add
  // CHECK:           cir.store
  for (int i = 0; i < n; ++i)
    sum += a[i];
  return sum;
}

// The product i * n leaves the inner loop, and the loads of f, m and n leave
// both.
// CHECK-LABEL: cir.func {{.*}} @nested(
void nested(double *m, double f, int n) {
  // CHECK:         cir.load
  // CHECK:         cir.load
  // CHECK:         cir.load
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.mul
  // CHECK:           cir.for
  // CHECK:           } body {
  // CHECK-NOT:         cir.mul nsw
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      m[i * n + j] *= f;
}

// k is passed to a function, so a call may write it.
// CHECK-LABEL: cir.func {{.*}} @escaped(
void escaped(int *a, int n) {
  int k = 5;
  use(&k);
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.load
  // CHECK:           cir.load
  // CHECK:           cir.ptr_stride
  // CHECK:           cir.store
  for (int i = 0; i < n; ++i)
    a[i] = k;
}

// The load of c[0] stays because the store to a[i] may write it.
// CHECK-LABEL: cir.func {{.*}} @pointer(
void pointer(double *a, double *c, int n) {
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.load
  // CHECK:           cir.ptr_stride
  // CHECK:           cir.load
  // CHECK:           cir.load
  // CHECK:           cir.fmul
  for (int i = 0; i < n; ++i)
    a[i] = a[i] * c[0];
}

// The loop may run no iteration, and then p / q must not run.
// CHECK-LABEL: cir.func {{.*}} @division(
void division(int *a, int p, int q, int n) {
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.div
  for (int i = 0; i < n; ++i)
    a[i] = p / q;
}

// CHECK-LABEL: cir.func {{.*}} @volatile_load(
void volatile_load(int *a, volatile int *v, int n) {
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.load volatile
  for (int i = 0; i < n; ++i)
    a[i] = *v;
}

// twice is in pure=, other is not. Neither takes a pointer.
// CHECK-LABEL: cir.func {{.*}} @calls(
void calls(int *a, int x, int n) {
  // CHECK:         cir.call @twice
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK-NOT:       cir.call @twice
  // CHECK:           cir.call @other
  for (int i = 0; i < n; ++i)
    a[i] = twice(x) + other(x);
}

// The argument changes in every iteration.
// CHECK-LABEL: cir.func {{.*}} @variant_call(
void variant_call(int *a, int n) {
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.call @twice
  for (int i = 0; i < n; ++i)
    a[i] = twice(i);
}

// What the condition of the if computes leaves the loop, the stores do not.
// CHECK-LABEL: cir.func {{.*}} @guarded(
void guarded(int *a, int p, int q, int n) {
  // CHECK:         cir.cast int_to_bool
  // CHECK:         cir.add
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.if
  // CHECK:             cir.store
  for (int i = 0; i < n; ++i)
    if (q)
      a[i] = p + q;
}

// The cond and the step of a loop stay as they are.
// CHECK-LABEL: cir.func {{.*}} @header(
void header(int *a, int n) {
  // CHECK:         cir.for : cond {
  // CHECK-NEXT:      cir.load
  // CHECK-NEXT:      cir.load
  // CHECK-NEXT:      cir.cmp lt
  for (int i = 0; i < n; ++i)
    a[i] = 0;
}
