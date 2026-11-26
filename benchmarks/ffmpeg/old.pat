$pipe = [vectorize]

c {
void sdot0(int N, float *x, float *y, float out)
{
  for (int i = 0; i < N; ++i) {
    out += x[i] * y[i];
  }
}
} [$pipe]

c {
void axpy_1_9(int n, float *x, float *y, float alpha){
int i;
for(i=0;i<n;++i) {
y[i]+=(x[i])*(alpha) ;
} 
}
} [$pipe]

c {
void axpy_1_13(int n, double *x, double *y, double alpha){
int i;
for(i=0;i<n;++i) {
y[i]+=(x[i])*(alpha) ;
} 
}
} [$pipe]

c {
void copy_1_3(int n, float *x, float *y){
int i;
for(i=0;i<n;++i) {
y[i] =x[i] ;
} 
}
} [$pipe]

c {
void scal_1_5(int n, float *x, float alpha){
int i;
for(i=0;i<n;++i) {
x[i] =(x[i])*(alpha) ;
} 
}
} [$pipe]