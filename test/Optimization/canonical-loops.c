// What CanonicalLoop accepts, on the loops clang emits.
//
// RUN: clang -fclangir -emit-cir %s -o %t.cir
// RUN: smr-opt --smr-test-canonical-loops --mlir-print-op-on-diagnostic=false %t.cir -o /dev/null 2>&1 | FileCheck %s

int g;
void use(int *p);
int len(void);

void constant_bounds(int *a) {
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' lt bound, step 1, trip count 100
  for (int i = 0; i < 100; i++)
    a[i] = 0;
}

void le_bound(int *a, int n) {
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' le bound, step 1, trip count unknown
  for (int i = 0; i <= n; ++i)
    a[i] = 0;
}

void le_constant_bounds(int *a) {
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' le bound, step 2, trip count 5
  for (int i = 1; i <= 10; i += 2)
    a[i] = 0;
}

void no_iterations(int *a) {
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' lt bound, step 1, trip count 0
  for (int i = 5; i < 3; i++)
    a[i] = 0;
}

void long_iv(int *a) {
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' lt bound, step 3, trip count 3
  for (long i = 2; i < 11; i += 3)
    a[i] = 0;
}

void unsigned_iv(int *a) {
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' lt bound, step 1, trip count 4
  for (unsigned i = 0; i < 4u; i++)
    a[i] = 0;
}

void swapped(int *a, int n) {
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' lt bound, step 1, trip count unknown
  for (int i = 0; n > i; i++)
    a[i] = 0;
}

void bound_expression(int *a, int n) {
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' lt bound, step 1, trip count unknown
  for (int i = 0; i < n - 1; i++)
    a[i] = 0;
}

void decreasing(int *a, int n) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: step is not iv = iv + constant
  for (int i = n; i > 0; i--)
    a[i] = 0;
}

void iv_written(int *a, int n) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: iv is written outside the step
  for (int i = 0; i < n; i++)
    if (a[i])
      i++;
}

void iv_escapes(int n) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: iv address escapes
  for (int i = 0; i < n; i++)
    use(&i);
}

void breaks(int *a, int n) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: body leaves the loop early
  for (int i = 0; i < n; i++)
    if (a[i] < 0)
      break;
}

// The break belongs to the inner loop, so the outer one is canonical. Inner
// loops are reported first.
void inner_break(int *a, int n) {
  // CHECK: [[@LINE+3]]:5: remark: not canonical: body leaves the loop early
  // CHECK: [[@LINE+1]]:3: remark: canonical: iv 'i' lt bound, step 1, trip count unknown
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      if (a[j] < 0)
        break;
}

void bound_written(int *a, int n) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: bound may change in the loop
  for (int i = 0; i < n; i++)
    n--;
}

void bound_call(int *a) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: bound may change in the loop
  for (int i = 0; i < len(); i++)
    a[i] = 0;
}

void bound_global(int *a) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: bound may change in the loop
  for (int i = 0; i < g; i++)
    a[i] = 0;
}

void not_equal(int *a, int n) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: comparison is not iv < bound or iv <= bound
  for (int i = 0; i != n; i++)
    a[i] = 0;
}

// The IV is widened before the comparison.
void mixed_types(int *a, long n) {
  // CHECK: [[@LINE+1]]:3: remark: not canonical: comparison is not iv < bound or iv <= bound
  for (int i = 0; i < n; i++)
    a[i] = 0;
}
