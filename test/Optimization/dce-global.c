// $GLOBAL = [$dce] runs on the whole input, here with nothing matched. A local
// that is only written goes away together with the computation that fed it.
// The slot of a parameter stays even if the parameter is unused, and so does a
// volatile local.
//
// RUN: smr %s %S/Inputs/global-dce.pat -o %t.mlir
// RUN: FileCheck %s --input-file=%t.mlir

// CHECK-LABEL: cir.func {{.*}}@f(
// CHECK-NOT:     cir.alloca "dead"
// CHECK-NOT:     cir.add
// CHECK-DAG:     cir.alloca "n" align({{[0-9]+}}) init
// CHECK-DAG:     cir.alloca "unused_param" align({{[0-9]+}}) init
// CHECK-DAG:     cir.alloca "flag" align({{[0-9]+}}) init
// CHECK:         cir.return

int f(int n, int unused_param) {
  int dead = n + 1;
  volatile int flag = 0;
  return n * 2;
}
