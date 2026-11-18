// The parameters of a replacement stand for variables of the matched code, and
// writing them is how the replacement returns its result. Here out is the
// variable t of dot, so the store to it must survive $dce.
//
// RUN: smr %s %S/Inputs/dce-write-param.pat -o %t.mlir
// RUN: FileCheck %s --input-file=%t.mlir

// CHECK-LABEL: cir.func {{.*}}@dot(
// CHECK:         %[[T:.*]] = cir.alloca "t" align({{[0-9]+}}) init : !cir.ptr<!cir.double>
// CHECK-NOT:     cir.for
// CHECK:         cir.fadd
// CHECK:         %[[SUM:.*]] = cir.fadd
// CHECK-NEXT:    cir.store align(8) %[[SUM]], %[[T]]
// CHECK:         cir.return

double dot(int n, double *x, double *y) {
  double t = 0.0;
  for (int i = 0; i < n; ++i) {
    t += x[i] * y[i];
  }
  return t;
}
