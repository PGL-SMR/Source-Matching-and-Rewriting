// Unrolled and tiled loops compute what the original ones do. Every canonical
// loop in this file is transformed, then the binary output is compared with
// clang -O0.
//
// RUN: clang -O0 %s -o %t.ref
// RUN: %t.ref > %t.ref.out
// RUN: clang -fclangir -emit-cir %s -o %t.cir
//
// RUN: smr-opt --smr-unroll=factor=4 %t.cir -o %t.unroll4.cir
// RUN: cir-translate --cir-to-llvmir %t.unroll4.cir | sed '/^target datalayout/d' > %t.unroll4.ll
// RUN: clang %t.unroll4.ll -o %t.unroll4
// RUN: %t.unroll4 > %t.unroll4.out
// RUN: diff %t.ref.out %t.unroll4.out
//
// RUN: smr-opt --smr-unroll=factor=3 %t.cir -o %t.unroll3.cir
// RUN: cir-translate --cir-to-llvmir %t.unroll3.cir | sed '/^target datalayout/d' > %t.unroll3.ll
// RUN: clang %t.unroll3.ll -o %t.unroll3
// RUN: %t.unroll3 > %t.unroll3.out
// RUN: diff %t.ref.out %t.unroll3.out
//
// RUN: smr-opt --smr-tiling=size=4 %t.cir -o %t.tiling4.cir
// RUN: FileCheck %s --input-file=%t.tiling4.cir --check-prefix=TILED
// RUN: cir-translate --cir-to-llvmir %t.tiling4.cir | sed '/^target datalayout/d' > %t.tiling4.ll
// RUN: clang %t.tiling4.ll -o %t.tiling4
// RUN: %t.tiling4 > %t.tiling4.out
// RUN: diff %t.ref.out %t.tiling4.out
//
// RUN: smr-opt --smr-tiling=size=7 %t.cir -o %t.tiling7.cir
// RUN: cir-translate --cir-to-llvmir %t.tiling7.cir | sed '/^target datalayout/d' > %t.tiling7.ll
// RUN: clang %t.tiling7.ll -o %t.tiling7
// RUN: %t.tiling7 > %t.tiling7.out
// RUN: diff %t.ref.out %t.tiling7.out
//
// Unrolling first leaves the main loop and its epilogue, both get tiled.
// RUN: smr-opt --smr-unroll=factor=3 --smr-tiling=size=5 %t.cir -o %t.both.cir
// RUN: cir-translate --cir-to-llvmir %t.both.cir | sed '/^target datalayout/d' > %t.both.ll
// RUN: clang %t.both.ll -o %t.both
// RUN: %t.both > %t.both.out
// RUN: diff %t.ref.out %t.both.out

// TILED-DAG: cir.alloca "tile.start.i" align({{[0-9]+}}) : !cir.ptr<!s32i>
// TILED-DAG: cir.alloca "tile.start.j" align({{[0-9]+}}) : !cir.ptr<!s32i>
// TILED-DAG: cir.if

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

// The IVs are declared at the top of the function, as in the gemm of darknet,
// and a statement that only writes a local slot comes before the inner loop:
// the two inner loops are tiled and the outer one runs in full inside the tiles.
// Any of the loops can be empty.
static void ikj(int ni, int nk, int nj) {
  int i, j, k;
  for (i = 0; i < ni; i++)
    for (k = 0; k < nk; k++) {
      int part = a[i] % 7;
      for (j = 0; j < nj; j++)
        m[i][j] += part * ((k + j) % 5);
    }
  for (i = 0; i < ni; i++)
    b[i] += m[i][nj > 0 ? nj - 1 : 0];
}

static void ikj_declared(int ni, int nk, int nj) {
  for (int i = 0; i < ni; i++)
    for (int k = 0; k < nk; k++) {
      int part = a[i] % 7;
      for (int j = 0; j < nj; j++)
        m[i][j] -= part * ((k * 3 + j) % 5);
    }
  for (int i = 0; i < ni; i++)
    b[i] += m[i][nj > 0 ? nj - 1 : 0];
}

// A call before the inner loop cannot run again for every tile: not tiled.
static int calls;
static int bump(void) { return ++calls; }
static void prelude_call(int n) {
  for (int i = 0; i < n; i++)
    for (int k = 0; k < n; k++) {
      int t = bump();
      for (int j = 0; j < n; j++)
        m[i][j] += t % 3;
    }
  b[0] += calls;
}

static void le_nest(int n) {
  for (int i = 0; i <= n; i++)
    for (int j = 0; j <= n; j++)
      m[i][j] = a[i] + j;
  for (int i = 0; i <= n; i++)
    b[i] += m[i][n];
}

static void long_nest(long n) {
  for (long i = 0; i < n; i++)
    for (long j = 0; j < n; j++)
      m[i][j] = a[j] * 2 - i;
  for (long i = 0; i < n; i++)
    b[i] += m[i][n - 1];
}

static void unsigned_nest(unsigned n) {
  for (unsigned i = 0; i < n; i++)
    for (unsigned j = 0; j < n; j++)
      m[i][j] = a[i] ^ j;
  for (unsigned i = 0; i < n; i++)
    b[i] += m[i][n - 1];
}

static void step_nest(int n) {
  for (int i = 1; i < n; i += 2)
    for (int j = 2; j < n; j += 3)
      m[i][j] = a[i] + a[j];
  for (int i = 1; i < n; i += 2)
    b[i] += m[i][2];
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
  RUN(ikj(N, N, N));
  RUN(ikj(7, 0, 5));
  RUN(ikj(0, 9, 9));
  RUN(ikj(9, 9, 0));
  RUN(ikj(13, 17, 19));
  RUN(ikj(N, 2, N));
  RUN(ikj_declared(N, N, N));
  RUN(ikj_declared(5, 70, 3));
  RUN(prelude_call(40));
  RUN(le_nest(N - 1));
  RUN(le_nest(0));
  RUN(long_nest(N));
  RUN(unsigned_nest(N));
  RUN(unsigned_nest(0));
  RUN(step_nest(N));
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
