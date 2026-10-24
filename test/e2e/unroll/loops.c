// Unrolled loops compute what the original ones do. Every loop in this file is
// unrolled, then the binary output is compared with clang -O0.
//
// RUN: clang -O0 %s -o %t.ref
// RUN: %t.ref > %t.ref.out
// RUN: clang -fclangir -emit-cir %s -o %t.cir
//
// RUN: smr-opt --smr-unroll=factor=4 %t.cir -o %t.4.cir
// RUN: cir-translate --cir-to-llvmir %t.4.cir -o %t.4.ll
// RUN: clang %t.4.ll -o %t.4
// RUN: %t.4 > %t.4.out
// RUN: diff %t.ref.out %t.4.out
//
// RUN: smr-opt --smr-unroll=factor=3 %t.cir -o %t.3.cir
// RUN: cir-translate --cir-to-llvmir %t.3.cir -o %t.3.ll
// RUN: clang %t.3.ll -o %t.3
// RUN: %t.3 > %t.3.out
// RUN: diff %t.ref.out %t.3.out

#include <stdio.h>

#define N 103

int a[N], b[N];
int m[N][N];

// The harness uses while loops, which smr-unroll leaves alone.
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

// Only the inner loop is unrolled.
static void nest(int n) {
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      m[i][j] = a[i] - a[j];
  for (int i = 0; i < n; i++)
    b[i] = m[i][n - 1 - i];
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
  RUN(remainder(0));
  RUN(step_two(N));
  RUN(nest(N));
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
