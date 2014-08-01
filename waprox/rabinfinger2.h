/*
 * $RCSfile: rabinfinger2.h,v $
 * $Revision: 1.1 $
 * $Date: 2009/05/18 01:37:09 $
 *
 * Copyright (c) 2006 Intel Corporation
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached INTEL-LICENSE.TXT
 * file. If you do not find these files, copies can be found by writing to
 * Intel Research Berkeley, 2150 Shattuck Avenue, Suite 1300, Berkeley, CA,
 * 94704.  Attention:  Intel License Inquiry.
 */

#ifndef _RABINPOLY_H_
#define _RABINPOLY_H_ 1

#include <sys/types.h>

typedef struct rabin_poly
{
  u_int64 moduloT[256];
  u_int64 regionT[256];
  u_int8  region_buf[256];
  int region_size;
  int region_pos;
  int shift;
  u_int64 ir_poly;      /* irreducible polynomical with degree 63 */
} RABINPOLY;

typedef struct rabin_context {
	RABINPOLY rp;
	u_int mask;
	u_int64 finger;
	int32 bsize;
	int32 min_size;
	int32 max_size;
} RabinCtx;

typedef struct RabinBoundary {
	unsigned int offset;
	u_int64 finger;
} RabinBoundary;

void InitRabinCtx(RabinCtx* rctx, int32 bsize, int window, int min, int max);
u_int GetMRCMask(int b_size);
#if 0
/* obsolete */
extern int32 GetRabinChunkSize(const u_char* src, int32 srclen, int32 b_size);
#endif
extern int32 GetRabinChunkSizeEx(RabinCtx* rctx, const u_char* src,
		int32 srclen, u_int min_mask, u_int max_mask, 
		char resetRegionBuffer, RabinBoundary* outBoundaries,
		unsigned int* outNumBoundaries);

#endif /* !_RABINPOLY_H_ */
