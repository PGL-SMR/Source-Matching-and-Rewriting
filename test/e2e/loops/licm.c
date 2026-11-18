// Hoisting loop invariant code computes what the original loops do, also when
// a loop runs no iteration and when it is unrolled or tiled afterwards. The
// binary output is compared with clang -O0.
//
// RUN: clang -O0 %s -o %t.ref
// RUN: %t.ref > %t.ref.out
// RUN: clang -fclangir -emit-cir %s -o %t.cir
//
// RUN: smr-opt --smr-licm %t.cir -o %t.licm.cir
// RUN: cir-translate --cir-to-llvmir %t.licm.cir | sed '/^target datalayout/d' > %t.licm.ll
// RUN: clang %t.licm.ll -o %t.licm
// RUN: %t.licm > %t.licm.out
// RUN: diff %t.ref.out %t.licm.out
//
// RUN: smr-opt --smr-licm=pure=twice %t.cir -o %t.pure.cir
// RUN: cir-translate --cir-to-llvmir %t.pure.cir | sed '/^target datalayout/d' > %t.pure.ll
// RUN: clang %t.pure.ll -o %t.pure
// RUN: %t.pure > %t.pure.out
// RUN: diff %t.ref.out %t.pure.out
//
// RUN: smr-opt --smr-licm=pure=twice --smr-unroll=factor=3 --smr-tiling=size=5 %t.cir -o %t.all.cir
// RUN: cir-translate --cir-to-llvmir %t.all.cir | sed '/^target datalayout/d' > %t.all.ll
// RUN: clang %t.all.ll -o %t.all
// RUN: %t.all > %t.all.out
// RUN: diff %t.ref.out %t.all.out

#include <stdio.h>

#define N 103

int a[N], b[N];
int m[N][N];

static void reset(void) {
  int i = 0;
  while (i < N) {
    a[i] = i * 7 % 101;
    b[i] = 0;
    i++;
  }
}

static long checksum(void) {
  long s = 0;
  int i = 0;
  while (i < N)
    s = s * 31 + b[i++];
  return s;
}

// A pure function: it reads nothing and has no effect.
int twice(int x) { return 2 * x; }

static void scalars(int p, int q, int n) {
  for (int i = 0; i < n; i++)
    b[i] = a[i] * p + q * 3;
}

// p / q does not run when n is 0, which would trap with q == 0.
static void division(int p, int q, int n) {
  for (int i = 0; i < n; i++)
    b[i] = a[i] + p / q;
}

static void guarded_division(int p, int q, int n) {
  for (int i = 0; i < n; i++)
    if (q)
      b[i] = a[i] + p / q;
}

// sum and the slot i change in every iteration.
static void written(int n) {
  int sum = 0;
  for (int i = 0; i < n; i++) {
    sum += a[i];
    b[i] = sum;
  }
}

static void nested(int n, int f) {
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      m[i][j] = i * n + j * f;
  for (int i = 0; i < n; i++)
    b[i] = m[i][i];
}

// The loop reads k, and a call writes it through its address.
static void bump(int *k) { *k += 1; }
static void escaped(int n) {
  int k = 5;
  bump(&k);
  for (int i = 0; i < n; i++)
    b[i] = a[i] + k;
}

static void calls(int x, int n) {
  for (int i = 0; i < n; i++)
    b[i] = a[i] + twice(x) + twice(i);
}

static void alias(int n) {
  int *c = a;
  for (int i = 0; i < n; i++)
    b[i] = c[0] + a[i];
  for (int i = 0; i < n; i++)
    a[i] = a[0] + i;
  for (int i = 0; i < n; i++)
    b[i] += a[i];
}

#define RUN(call)                                                              \
  do {                                                                         \
    reset();                                                                   \
    call;                                                                      \
    printf("%-24s %ld\n", #call, checksum());                                  \
  } while (0)

int main(void) {
  RUN(scalars(3, 4, N));
  RUN(scalars(3, 4, 0));
  RUN(division(7, 2, N));
  RUN(division(7, 0, 0));
  RUN(guarded_division(7, 2, N));
  RUN(guarded_division(7, 0, N));
  RUN(guarded_division(7, 0, 0));
  RUN(written(N));
  RUN(nested(37, 3));
  RUN(nested(0, 3));
  RUN(escaped(N));
  RUN(calls(5, N));
  RUN(calls(5, 0));
  RUN(alias(N));
  return 0;
}
