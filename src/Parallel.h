
#ifndef _PARALLEL_H_
#define _PARALLEL_H_

#include "SArray.h"
#include "const.h"

class Parallel {

public:

  int nranks;
  int myrank;
  int nproc_x;
  int nproc_y;
  int px;
  int py;
  ulong i_beg;
  ulong j_beg;
  ulong i_end;
  ulong j_end;
  int masterproc;
  SArray<int,3,3> neigh;
};

#endif
