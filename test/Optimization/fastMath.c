// RUN: clang -fclangir -emit-cir %s -o %t.cir
// RUN: smr-opt --smr-fast-math --smr-licm %t.cir | FileCheck %s

int __isnan(double);

// CHECK-LABEL: cir.func {{.*}} @algebra_and_recip(
void algebra_and_recip(double *a, double scale, int n) {
  // 1.0 / scale é gerado pelo fast-math e içado antes do loop pelo licm
  // CHECK:         cir.binop(div
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK-NOT:       cir.binop(div
  // CHECK-NOT:       cir.binop(add
  // CHECK:           cir.binop(mul
  for (int i = 0; i < n; ++i)
    a[i] = ((a[i] + 0.0) * 1.0) / scale;
}

// CHECK-LABEL: cir.func {{.*}} @fma_fusion(
double fma_fusion(double a, double b, double c) {
  // CHECK:         cir.llvm.intrinsic "fma"
  // CHECK-NOT:     cir.binop(mul
  // CHECK-NOT:     cir.binop(add
  return a * b + c;
}

// CHECK-LABEL: cir.func {{.*}} @nan_cmp(
int nan_cmp(double x) {
  // CHECK-NOT:     cir.cmp(ne
  // CHECK:         cir.const #false
  return x != x;
}

// CHECK-LABEL: cir.func {{.*}} @nan_call(
int nan_call(double x) {
  // CHECK-NOT:     cir.call @__isnan
  // CHECK:         cir.const #cir.int<0>
  return __isnan(x);
}