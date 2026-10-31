// Unrolled and tiled loops compute what the original ones do. Every canonical
// loop in this file is transformed, then the binary output is compared with
// clang -O0.
//
// RUN: clang -O0 %s -o %t.ref
// RUN: %t.ref > %t.ref.out
// RUN: clang -fclangir -emit-cir %s -o %t.cir
//
// RUN: smr-opt --smr-unroll=factor=4 %t.cir -o %t.unroll4.cir
// RUN: cir-translate --cir-to-llvmir %t.unroll4.cir -o %t.unroll4.ll
// RUN: clang %t.unroll4.ll -o %t.unroll4
// RUN: %t.unroll4 > %t.unroll4.out
// RUN: diff %t.ref.out %t.unroll4.out
//
// RUN: smr-opt --smr-unroll=factor=3 %t.cir -o %t.unroll3.cir
// RUN: cir-translate --cir-to-llvmir %t.unroll3.cir -o %t.unroll3.ll
// RUN: clang %t.unroll3.ll -o %t.unroll3
// RUN: %t.unroll3 > %t.unroll3.out
// RUN: diff %t.ref.out %t.unroll3.out
//
// RUN: smr-opt --smr-tiling=size=4 %t.cir -o %t.tiling4.cir
// RUN: FileCheck %s --input-file=%t.tiling4.cir --check-prefix=TILED
// RUN: cir-translate --cir-to-llvmir %t.tiling4.cir -o %t.tiling4.ll
// RUN: clang %t.tiling4.ll -o %t.tiling4
// RUN: %t.tiling4 > %t.tiling4.out
// RUN: diff %t.ref.out %t.tiling4.out
//
// RUN: smr-opt --smr-tiling=size=7 %t.cir -o %t.tiling7.cir
// RUN: cir-translate --cir-to-llvmir %t.tiling7.cir -o %t.tiling7.ll
// RUN: clang %t.tiling7.ll -o %t.tiling7
// RUN: %t.tiling7 > %t.tiling7.out
// RUN: diff %t.ref.out %t.tiling7.out
//
// Unrolling first leaves the main loop and its epilogue, both get tiled.
// RUN: smr-opt --smr-unroll=factor=3 --smr-tiling=size=5 %t.cir -o %t.both.cir
// RUN: cir-translate --cir-to-llvmir %t.both.cir -o %t.both.ll
// RUN: clang %t.both.ll -o %t.both
// RUN: %t.both > %t.both.out
// RUN: diff %t.ref.out %t.both.out

// TILED-DAG: cir.alloca !u64i, !cir.ptr<!u64i>, ["tile"]
// TILED-DAG: cir.alloca !u64i, !cir.ptr<!u64i>, ["tile.j"]

#include <stdio.h>

#define N 103

int a[N], b[N];
int m[N][N];

// The harness uses while loops, which the passes leave alone.
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

// The trip count is a multiple of both factors.
static void divisible(void) {
  for (int i = 0; i < 96; i++)
    b[i] = a[i] + 1;
}

// N leaves a remainder for both factors.
static void remainder(int n) {
  for (int i = 0; i < n; i++)
    b[i] += a[i];
}

static void step_two(int n) {
  for (int i = 1; i < n; i += 2)
    b[i] = a[i] * 3;
}

// Only the inner loop is transformed.
static void nest(int n) {
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      m[i][j] = a[i] - a[j];
  for (int i = 0; i < n; i++)
    b[i] = m[i][n - 1 - i];
}

// The bounds differ, and the inner loop can be empty.
static void rectangle(int n, int k) {
  for (int i = 0; i < n; i++)
    for (int j = 0; j < k; j++)
      m[i][j] = a[i] * 3 + j;
  for (int i = 0; i < n; i++)
    b[i] = m[i][k > 0 ? k - 1 : 0];
}

// The inner bound depends on the outer IV: only the inner loop is transformed.
static void triangle(int n) {
  for (int i = 0; i < n; i++)
    for (int j = 0; j < i; j++)
      m[i][j] = a[i] + a[j];
  for (int i = 0; i < n; i++)
    b[i] = m[i][i / 2];
}

// A statement next to the inner loop: only the inner loop is transformed.
static void imperfect(int n) {
  for (int i = 0; i < n; i++) {
    b[i] += 1;
    for (int j = 0; j < n; j++)
      m[i][j] = a[j] - i;
  }
  for (int i = 0; i < n; i++)
    b[i] += m[i][n - 1];
}

// The two outer loops are tiled, the inner one is strip-mined.
static void cube(int n) {
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      for (int k = 0; k < n; k++)
        b[(i + j + k) % N] += (i ^ j) + k;
}

static void reduction(int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += a[i];
  b[0] = s;
}

static void le_bound(int n) {
  for (int i = 0; i <= n; i++)
    b[i] += 2;
}

static void long_iv(long n) {
  for (long i = 0; i < n; i++)
    b[i] = a[i] - 1;
}

static void unsigned_iv(unsigned n) {
  for (unsigned i = 0; i < n; i++)
    b[i] = a[i] ^ 5;
}

// The IV is read inside the if, in a nested region of the body.
static void nested_reads(int n) {
  for (int i = 0; i < n; i++) {
    if (a[i] > 50)
      b[i] = i;
  }
}

// The loops below are not canonical and must be left as they are.
static void decreasing(int n) {
  for (int i = n - 1; i >= 0; i--)
    b[i] += a[i];
}

static void iv_written(int n) {
  for (int i = 0; i < n; i++)
    if (a[i] % 3 == 0)
      b[i++] = 1;
}

static void breaks(int n) {
  for (int i = 0; i < n; i++) {
    if (a[i] > 90)
      break;
    b[i] = 7;
  }
}

static void bound_written(int n) {
  for (int i = 0; i < n; i++)
    b[n--] = i;
}

#define RUN(call)                                                              \
  do {                                                                         \
    reset();                                                                   \
    call;                                                                      \
    printf("%-16s %ld\n", #call, checksum());                                  \
  } while (0)

int main(void) {
  RUN(divisible());
  RUN(remainder(N));
  RUN(remainder(2));
  RUN(remainder(4));
  RUN(remainder(7));
  RUN(remainder(0));
  RUN(step_two(N));
  RUN(nest(N));
  RUN(rectangle(N, 11));
  RUN(rectangle(5, 0));
  RUN(rectangle(9, N));
  RUN(triangle(N));
  RUN(imperfect(N));
  RUN(cube(37));
  RUN(reduction(N));
  RUN(le_bound(N - 1));
  RUN(le_bound(1));
  RUN(long_iv(N));
  RUN(unsigned_iv(N));
  RUN(unsigned_iv(2));
  RUN(nested_reads(N));
  RUN(decreasing(N));
  RUN(iv_written(N));
  RUN(breaks(N));
  RUN(bound_written(N - 1));
  return 0;
}
