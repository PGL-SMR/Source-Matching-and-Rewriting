$GLOBAL = [dce]

c {
void fill(int *a, int n)
{
  for (int i = 0; i < n; ++i) {
    a[i] = 0;
  }
}
} [unroll(2)]
