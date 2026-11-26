// A pattern selects the loop and unroll_full replaces it by its iterations.
//
// RUN: smr %s %S/Inputs/unroll-full.pat -o %t.mlir | FileCheck %s --check-prefix=LOG
// RUN: FileCheck %s --input-file=%t.mlir
//
// LOG: Applying optimization: unroll_full(8)

// CHECK-LABEL: cir.func {{.*}}@clear4(
// CHECK-NOT:     cir.for
// CHECK:         cir.return
void clear4(int *a) {
  for (int i = 0; i < 4; ++i) {
    a[i] = 0;
  }
}
