// smr-cc sends a C file through smr when a pattern applies to it: a rewrite or
// a $GLOBAL optimization. Any other unit is compiled by plain clang, so with no
// pattern the program is what clang gives. SMR_CC_ALL sends every unit through
// the CIR path, the control of an evaluation of that path. Anything that is not
// the compilation of one C file goes to clang.
//
// RUN: rm -f %t.*.log
// RUN: clang -O3 -ffp-contract=off %s -o %t.ref
// RUN: %t.ref | FileCheck %s --check-prefix=RESULT
//
// No pattern: plain clang.
// RUN: env SMR_CC_LOG=%t.plain.log %smr_cc -O3 -c %s -o %t.plain.o
// RUN: %smr_cc %t.plain.o -o %t.plain
// RUN: %t.plain | FileCheck %s --check-prefix=RESULT
// RUN: FileCheck %s --input-file=%t.plain.log --check-prefix=PLAIN
//
// No pattern with SMR_CC_ALL: the CIR path.
// RUN: env SMR_CC_ALL=1 SMR_CC_LOG=%t.all.log %smr_cc -O3 -c %s -o %t.all.o
// RUN: %smr_cc %t.all.o -o %t.all
// RUN: %t.all | FileCheck %s --check-prefix=RESULT
// RUN: FileCheck %s --input-file=%t.all.log --check-prefix=ALL
//
// RUN: env SMR_CC_LOG=%t.rewrite.log SMR_PATS=%S/../Optimization/Inputs/dot.pat %smr_cc -O3 -c %s -o %t.rewrite.o
// RUN: %smr_cc %t.rewrite.o -o %t.rewrite
// RUN: %t.rewrite | FileCheck %s --check-prefix=RESULT
// RUN: FileCheck %s --input-file=%t.rewrite.log --check-prefix=REWRITE
//
// A pattern that matches nothing leaves the unit to plain clang. One with a
// $GLOBAL optimization sends it through smr even with no rewrite.
// RUN: env SMR_CC_LOG=%t.nomatch.log SMR_PATS=%S/Inputs/smr-cc-nomatch.pat %smr_cc -O3 -c %s -o %t.nomatch.o
// RUN: %smr_cc %t.nomatch.o -o %t.nomatch
// RUN: %t.nomatch | FileCheck %s --check-prefix=RESULT
// RUN: FileCheck %s --input-file=%t.nomatch.log --check-prefix=NOMATCH
// RUN: env SMR_CC_LOG=%t.global.log SMR_PATS=%S/Inputs/smr-cc-global.pat %smr_cc -O3 -c %s -o %t.global.o
// RUN: FileCheck %s --input-file=%t.global.log --check-prefix=GLOBAL
//
// Include paths and macros reach the preprocessor, smr itself takes none.
// RUN: env SMR_CC_ALL=1 SMR_CC_LOG=%t.flags.log %smr_cc -O3 -I %S/Inputs -DSCALE=3 -c %S/Inputs/smr-cc-flags.c -o %t.flags.o
// RUN: %smr_cc %t.flags.o -o %t.flags
// RUN: %t.flags | FileCheck %s --check-prefix=FLAGS
//
// RESULT: 64.0
// PLAIN:   SKIP {{.*}}smr-cc.c
// ALL:     OK {{.*}}smr-cc.c rewrites=0
// REWRITE: OK {{.*}}smr-cc.c rewrites=1
// NOMATCH: SKIP {{.*}}smr-cc.c
// GLOBAL:  OK {{.*}}smr-cc.c rewrites=0
// FLAGS:   21

#include <stdio.h>

double scale_dot(int n, double *x, double *y) {
  double out = 0;
  for (int i = 0; i < n; ++i) {
    out += x[i] * y[i];
  }
  return 2 * out;
}

int main(void) {
  double x[5] = {1, 2, 3, 4, 5};
  double y[5] = {1, 1, 2, 2, 3};
  printf("%.1f\n", scale_dot(5, x, y));
  return 0;
}
