#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

#define F (1 << 14)

int int_to_fp(int n);
int fp_to_int_round(int x);
int add_fp(int x, int y);
int sub_fp(int x, int y);
int mult_fp(int x, int y);
int div_fp(int x, int y);
int mult_mixed(int x, int n);
int div_mixed(int x, int n);
int add_mixed(int x, int n);
int sub_mixed(int x, int n);


#endif /* threads/fixed-point.h */