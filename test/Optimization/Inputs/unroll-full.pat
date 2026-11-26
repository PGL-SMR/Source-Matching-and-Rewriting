c {
void clear4(int *a)
{
  for (int i = 0; i < 4; ++i) {
    a[i] = 0;
  }
}
} [unroll_full(8)]
