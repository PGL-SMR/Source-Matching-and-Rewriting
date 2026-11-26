$pipe = [vectorize]

c {
void ddot0(int N, double *x, double *y, double out)
{
  for (int i = 0; i < N; ++i) {
    out += x[i] * y[i];
  }
}
} [$pipe]