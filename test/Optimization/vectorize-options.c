// The options of vectorize are the ones of --smr-vectorize, written in the
// pattern: vf=4, ic=1, assume-no-alias=false.
//
// RUN: smr %s %S/Inputs/vectorize-vf.pat -o %t.vf.mlir
// RUN: FileCheck %s --input-file=%t.vf.mlir --check-prefix=VF
//
// VF-LABEL: cir.func {{.*}}@dot_product(
// VF:         cir.fmul {{.*}} : !cir.vector<4 x !cir.float>
// VF-NOT:     !cir.vector<8 x !cir.float>
//
// Two arrays that may overlap are not vectorized when assume-no-alias is false.
//
// RUN: smr %s %S/Inputs/vectorize-alias.pat -o %t.alias.mlir 2>&1 | FileCheck %s --check-prefix=ALIAS
// RUN: FileCheck %s --input-file=%t.alias.mlir --check-prefix=NO-VEC
//
// ALIAS: [WARN] Optimization 'vectorize(assume-no-alias=false)' changed nothing
//
// NO-VEC-LABEL: cir.func {{.*}}@saxpy(
// NO-VEC-NOT:   !cir.vector
//
// A vector width that is not a power of two drops the rewrite.
//
// RUN: smr %s %S/Inputs/vectorize-bad-vf.pat -o %t.bad.mlir 2>&1 | FileCheck %s --check-prefix=BAD
//
// BAD: Invalid option 'vf=3' for optimization 'vectorize': expected vf=N (a power of two, at least 2)
// BAD-NOT: Applying rewrite

float dot_product(float *a, float *b, int n) {
  float sum = 0.0f;
  for (int i = 0; i < n; ++i)
    sum += a[i] * b[i];
  return sum;
}

void saxpy(float *y, float *x, float alpha, int n) {
  for (int i = 0; i < n; ++i)
    y[i] = alpha * x[i] + y[i];
}
