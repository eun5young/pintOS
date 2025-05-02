#include "threads/fixed_point.h"

int int_to_fp(int n)         { return n * F; }
int fp_to_int_round(int x)   { return x >= 0 ? (x + F / 2) / F : (x - F / 2) / F; }
int add_fp(int x, int y)     { return x + y; }
int sub_fp(int x, int y)     { return x - y; }
int mult_fp(int x, int y)    { return ((int64_t)x) * y / F; }
int div_fp(int x, int y)     { return ((int64_t)x) * F / y; }
int mult_mixed(int x, int n) { return x * n; }
int div_mixed(int x, int n)  { return x / n; }
int add_mixed(int x, int n)  { return x + n * F; }
int sub_mixed(int x, int n)  {return x - n * F;}