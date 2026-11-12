// RUN: clang -fclangir -emit-cir %s -o %t.cir
// RUN: smr-opt --smr-fast-math --smr-licm --smr-vectorize="vf=8 ic=2" %t.cir | FileCheck %s

// CHECK-LABEL: cir.func {{.*}} @dot_product(
float dot_product(float *a, float *b, int n) {
  float sum = 0.0f;
  // CHECK:         cir.vec.splat
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.cast bitcast {{.*}} -> !cir.ptr<!cir.vector<!cir.float x 8>>
  // CHECK:           cir.load align(4) {{.*}} !cir.vector<!cir.float x 8>
  // CHECK:           cir.binop(mul, {{.*}}) : !cir.vector<!cir.float x 8>
  // CHECK:           cir.binop(add, {{.*}}) : !cir.vector<!cir.float x 8>
  // CHECK:         } step {
  // CHECK:         cir.vec.extract
  // CHECK:         cir.for
  for (int i = 0; i < n; ++i)
    sum += a[i] * b[i];
  return sum;
}

// CHECK-LABEL: cir.func {{.*}} @saxpy(
void saxpy(float *y, float *x, float alpha, int n) {
  // alpha é broadcast via cir.vec.splat fora do loop
  // CHECK:         cir.vec.splat
  // CHECK:         cir.for
  // CHECK:         } body {
  // CHECK:           cir.llvm.intrinsic "fma" {{.*}} : (!cir.vector<!cir.float x 8>, !cir.vector<!cir.float x 8>, !cir.vector<!cir.float x 8>) -> !cir.vector<!cir.float x 8>
  // CHECK:           cir.store align(4) {{.*}} !cir.vector<!cir.float x 8>
  // CHECK:         } step {
  for (int i = 0; i < n; ++i)
    y[i] = alpha * x[i] + y[i];
}