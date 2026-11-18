// Fused loops compute what the original ones do: the binary output is
// compared with clang -O0.
//
// RUN: clang -O0 %s -o %t.ref
// RUN: %t.ref > %t.ref.out
// RUN: clang -fclangir -emit-cir %s -o %t.cir
// RUN: smr-opt --smr-fusion %t.cir -o %t.fusion.cir
// RUN: cir-translate --cir-to-llvmir %t.fusion.cir | sed '/^target datalayout/d' > %t.fusion.ll
// RUN: clang %t.fusion.ll -o %t.fusion
// RUN: %t.fusion > %t.fusion.out
// RUN: diff %t.ref.out %t.fusion.out

#include <stdio.h>

#define N 41

static float a[N * N], b[N * N], c[N * N], d[N * N];

static void reset(void) {
  for (int i = 0; i < N * N; ++i) {
    a[i] = (i % 7) * 0.5f + 1.0f;
    b[i] = 0.0f;
    c[i] = 0.0f;
    d[i] = 0.0f;
  }
}

static void dump(const char *name) {
  double sum = 0;
  for (int i = 0; i < N * N; ++i)
    sum += b[i] + 3.0 * c[i] + 5.0 * d[i];
  printf("%s %.4f\n", name, sum);
}

__attribute__((noinline)) void chain(int n) {
  for (int i = 0; i < n; ++i)
    b[i] = a[i] * 2.0f;
  for (int j = 0; j < n; ++j)
    c[j] = b[j] + 1.0f;
}

__attribute__((noinline)) void same_iv(int n) {
  int i;
  for (i = 0; i < n; ++i)
    b[i] = a[i] + 1.0f;
  for (i = 0; i < n; ++i)
    c[i] = b[i] * b[i];
}

__attribute__((noinline)) void three(int n) {
  for (int i = 0; i < n; ++i)
    b[i] = a[i];
  for (int i = 0; i < n; ++i)
    c[i] = b[i] + a[i];
  for (int i = 0; i < n; ++i)
    d[i] = c[i] - b[i];
}

__attribute__((noinline)) float sums(int n) {
  float s = 0.0f, t = 0.0f;
  for (int i = 0; i < n; ++i)
    s += a[i];
  for (int i = 0; i < n; ++i)
    t += a[i] * a[i];
  return s + t;
}

__attribute__((noinline)) void inner(int n, int m) {
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < m; ++j)
      b[i * m + j] = a[i * m + j] + 1.0f;
    for (int j = 0; j < m; ++j)
      c[i * m + j] = b[i * m + j] * 2.0f;
  }
}

__attribute__((noinline)) void not_fused(int n, int m) {
  for (int i = 0; i < n; ++i)
    b[i] = a[i];
  for (int i = 0; i < m; ++i)
    c[i] = b[i] + 1.0f;
}

int main(void) {
  int sizes[] = {0, 1, 5, 40};
  for (int k = 0; k < 4; ++k) {
    int n = sizes[k];
    printf("n=%d\n", n);
    reset(); chain(n); dump("chain");
    reset(); same_iv(n); dump("same_iv");
    reset(); three(n); dump("three");
    reset(); printf("sums %.4f\n", sums(n));
    reset(); inner(n, 3); dump("inner");
    reset(); inner(3, n); dump("inner2");
    reset(); not_fused(n, 7); dump("not_fused");
  }
  return 0;
}
