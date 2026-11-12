// The optimizations of a .pat become a pass pipeline, run on the replacement
// and, for $GLOBAL, on the input after rewriting. The passes do not transform
// anything yet.
//
// RUN: smr %s %S/Inputs/dot.pat -o %t.mlir | FileCheck %s --check-prefix=LOG
// RUN: FileCheck %s --input-file=%t.mlir
//
// LOG:      Applying optimization: unroll(2)
// LOG-NEXT: Applying optimization: tiling
// LOG-NEXT: Applying optimization: dce
// LOG-NEXT: Applying optimization: canonicalize
// LOG-NEXT: Applying optimization: vectorize
// LOG:      Applying rewrite 0...
// LOG:      Applying optimization: dce
//
// CHECK-LABEL: cir.func {{.*}}@scale_dot(
// CHECK:         cir.for
//
// An invalid argument drops the rewrite.
//
// RUN: smr %s %S/Inputs/dot-bad-arg.pat -o %t.bad.mlir 2>&1 | FileCheck %s --check-prefix=BAD-ARG
// RUN: FileCheck %s --input-file=%t.bad.mlir --check-prefix=NO-REWRITE
//
// BAD-ARG:     Invalid argument '0' for optimization 'unroll': expected a positive integer.
// BAD-ARG-NOT: Applying rewrite
//
// NO-REWRITE-NOT: cir.func {{.*}}@dot(
//
// An invalid $GLOBAL argument fails the run.
//
// RUN: not smr %s %S/Inputs/dot-bad-global.pat -o %t.global.mlir 2>&1 | FileCheck %s --check-prefix=BAD-GLOBAL
//
// BAD-GLOBAL: Invalid argument 'big' for optimization 'tiling': expected a positive integer.

double scale_dot(int n, double *x, double *y) {
  double out = 0;
  for (int i = 0; i < n; ++i) {
    out += x[i] * y[i];
  }
  return 2 * out;
}
