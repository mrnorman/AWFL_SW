
#ifndef _CONST_H_
#define _CONST_H_

#include "math.h"
#include <Kokkos_Core.hpp>

typedef float         real;
typedef unsigned long ulong;
typedef unsigned int  uint;

#ifdef __NVCC__
  typedef Kokkos::View<real*     ,Kokkos::LayoutRight,Kokkos::Device<Kokkos::Cuda,Kokkos::CudaUVMSpace>> real1d;
  typedef Kokkos::View<real**    ,Kokkos::LayoutRight,Kokkos::Device<Kokkos::Cuda,Kokkos::CudaUVMSpace>> real2d;
  typedef Kokkos::View<real***   ,Kokkos::LayoutRight,Kokkos::Device<Kokkos::Cuda,Kokkos::CudaUVMSpace>> real3d;
  typedef Kokkos::View<real****  ,Kokkos::LayoutRight,Kokkos::Device<Kokkos::Cuda,Kokkos::CudaUVMSpace>> real4d;
  typedef Kokkos::View<real***** ,Kokkos::LayoutRight,Kokkos::Device<Kokkos::Cuda,Kokkos::CudaUVMSpace>> real5d;
  typedef Kokkos::View<real******,Kokkos::LayoutRight,Kokkos::Device<Kokkos::Cuda,Kokkos::CudaUVMSpace>> real6d;
#else
  typedef Kokkos::View<real*     ,Kokkos::LayoutRight> real1d;
  typedef Kokkos::View<real**    ,Kokkos::LayoutRight> real2d;
  typedef Kokkos::View<real***   ,Kokkos::LayoutRight> real3d;
  typedef Kokkos::View<real****  ,Kokkos::LayoutRight> real4d;
  typedef Kokkos::View<real***** ,Kokkos::LayoutRight> real5d;
  typedef Kokkos::View<real******,Kokkos::LayoutRight> real6d;
#endif

#ifdef __NVCC__
#define _HOSTDEV __host__ __device__
#else
#define _HOSTDEV 
#endif

inline _HOSTDEV real operator"" _fp( long double x ) {
  return static_cast<real>(x);
}

#define hs 1

#define numState 3

#define idH  0
#define idHU 1
#define idHV 2

// Some physical constants
#define PI   3.1415926535897932384626433832795028842_fp
#define GRAV 9.8_fp

inline _HOSTDEV double mypow ( double const x , double const p ) { return pow (x,p); }
inline _HOSTDEV float  mypow ( float  const x , float  const p ) { return powf(x,p); }
inline _HOSTDEV double mysqrt( double const x ) { return sqrt (x); }
inline _HOSTDEV float  mysqrt( float  const x ) { return sqrtf(x); }
inline _HOSTDEV double myfabs( double const x ) { return fabs (x); }
inline _HOSTDEV float  myfabs( float  const x ) { return fabsf(x); }

template <class T> inline _HOSTDEV T min( T const v1 , T const v2 ) {
  if (v1 < v2) { return v1; }
  else         { return v2; }
}
template <class T> inline _HOSTDEV T max( T const v1 , T const v2 ) {
  if (v1 > v2) { return v1; }
  else         { return v2; }
}

#endif
