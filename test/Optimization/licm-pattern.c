// A pattern selects the loop and licm runs on the replacement. The function
// named in its argument is asserted to be pure, which only matters for calls.
//
// RUN: smr %s %S/Inputs/licm.pat -o %t.mlir | FileCheck %s --check-prefix=LOG
// RUN: FileCheck %s --input-file=%t.mlir
//
// LOG: Applying optimization: licm(twice)

// CHECK-LABEL: cir.func {{.*}}@scale(
// CHECK:         cir.mul
// CHECK:         cir.for
// CHECK:         } body {
// CHECK-NOT:       cir.mul
// CHECK:         } step {
void scale(int *v, int p, int q, int n) {
  for (int i = 0; i < n; ++i) {
    v[i] = p + q * 3;
  }
}
