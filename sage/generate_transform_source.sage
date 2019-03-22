#!/usr/bin/env /home/imn/sage-6.4.1-i686-Linux/sage -python

load("transformation_matrices.sage")
load("c_utils.sage")

N1 = 2
N2 = 10

print('#ifndef _TRANSFORM_MATRICES_H_')
print('#define _TRANSFORM_MATRICES_H_')

print('#include <math.h>')
print('#include "SArray.h"')
print('')

print('template <class FP> class TransformMatrices {')

print('')
print('public:')
print('')

for N in range(N1,N2+1) :

    print('inline void gll_to_coefs(FP dx, SArray<FP,%s,%s> &rslt) {'%(N,N))
    p2c,c2p = points_gll_to_coefs(N,var('x'),var('dx'))
    print(add_spaces(2,c_matrix('rslt',N,N,force_fp(p2c,129),'none',200)))
    print('}\n');

    print('inline void get_gll_points(SArray<FP,%s> &rslt) {'%(N))
    pts,wts = lobatto_weights_nodes(N,129,False,1.e-35,100)
    pts = pts / 2
    wts = wts / 2
    print(add_spaces(2,c_vector('rslt',N,force_fp(pts,129),'none',200)))
    print('}\n');

    print('inline void get_gll_weights(SArray<FP,%s> &rslt) {'%(N))
    pts,wts = lobatto_weights_nodes(N,129,False,1.e-35,100)
    pts = pts / 2
    wts = wts / 2
    print(add_spaces(2,c_vector('rslt',N,force_fp(wts,129),'none',200)))
    print('}\n');

    print('inline void coefs_to_gll(FP dx, SArray<FP,%s,%s> &rslt) {'%(N,N))
    p2c,c2p = points_gll_to_coefs(N,var('x'),var('dx'))
    print(add_spaces(2,c_matrix('rslt',N,N,force_fp(c2p,129),'none',200)))
    print('}\n');

    print('inline void coefs_to_deriv(FP dx, SArray<FP,%s,%s> &rslt) {'%(N,N))
    c2d = coefs_to_deriv(N,var('x'))
    print(add_spaces(2,c_matrix('rslt',N,N,force_fp(c2d,129),'none',200)))
    print('}\n');

    print('inline FP coefs_to_tv(SArray<FP,%s> &a) {'%(N))
    print('  FP rslt;')
    rslt = coefs_to_TV(N)
    print(add_spaces(2,c_scalar('rslt',force_fp(rslt,129),'a',200)))
    print('  return rslt;')
    print('}\n');

    if (N%2 == 1) :
        print('inline void sten_to_coefs(FP dx, SArray<FP,%s,%s> &rslt) {'%(N,N))
        p2c,c2p = stencil_to_coefs(N,var('x'),var('dx'))
        print(add_spaces(2,c_matrix('rslt',N,N,force_fp(p2c,129),'none',200)))
        print('}\n');

        print('inline void coefs_to_sten(FP dx, SArray<FP,%s,%s> &rslt) {'%(N,N))
        p2c,c2p = stencil_to_coefs(N,var('x'),var('dx'))
        print(add_spaces(2,c_matrix('rslt',N,N,force_fp(c2p,129),'none',200)))
        print('}\n');

        print('inline void sten_to_gll_lower(FP dx, SArray<FP,%s,%s,%s> &rslt) {'%(N,N,N))
        s2g = sten_to_gll_lower(N,x,dx)
        print(add_spaces(2,c_3d('rslt',N,N,N,s2g,'none',200)))
        print('}\n');

        print('inline void coefs_to_gll_lower(FP dx, SArray<FP,%s,%s,%s> &rslt) {'%(N,N,N))
        c2g = coefs_to_gll_lower(N,x,dx)
        print(add_spaces(2,c_3d('rslt',N,N,N,c2g,'none',200)))
        print('}\n');

        print('inline void weno_sten_to_coefs(FP dx, SArray<FP,%s,%s,%s> &rslt) {'%(N,N,N))
        weno = weno_sten_to_coefs(N)
        print(add_spaces(2,c_3d('rslt',N,N,(N-1)/2+2,weno,'none',200)))
        print('}\n');

print('};')

print('#endif')
