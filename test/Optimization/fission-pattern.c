// A pattern selects the loop and fission gives each statement a loop.
//
// RUN: smr %s %S/Inputs/fission.pat -o %t.mlir | FileCheck %s --check-prefix=LOG
// RUN: FileCheck %s --input-file=%t.mlir
//
// LOG: Applying optimization: fission

// CHECK-LABEL: cir.func {{.*}}@split(
// CHECK:         cir.for
// CHECK:         cir.for
// CHECK-NOT:     cir.for
void split(int *a, int *b, int n) {
  for (int i = 0; i < n; ++i) {
    a[i] = 1;
    b[i] = 2;
  }
}
