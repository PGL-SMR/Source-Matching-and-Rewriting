$pipe_vec = [vectorize]

c {
void sdot0(int N, float *x, float *y, float out)
{
  for (int i = 0; i < N; ++i) {
    out += x[i] * y[i];
  }
}
} [$pipe_vec]