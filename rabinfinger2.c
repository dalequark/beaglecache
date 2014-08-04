/*
 * $RCSfile: rabinfinger2.c,v $
 * $Revision: 1.10 $
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
//#include "czipdef.h"
#include "rabinfinger2.h"
//#include "debug.h"


/*--------------------------------------------------------------------------*/
#define INT64(x) ((unsigned long long)(x))
#define MSB64 INT64(0x8000000000000000ULL)
#define MIN_RABIN_CHUNK_SIZE (64)
#define MAX_RABIN_CHUNK_SIZE (128*1024)

#define IRREDUCIBLE_POLY (0x946d1d8dcfee41e5LL)
#define NELEM(a) (sizeof(a)/sizeof(a[0]))
/*--------------------------------------------------------------------------*/
static int chunk_sizes[] = {32, 64, 128, 256, 512, 1024, 2*1024, 4*1024, 
	8*1024, 16*1024, 32*1024, 64*1024, 128*1024};
static u_int masks[] = {32 - 1 , 64 - 1, 128 - 1, 256 - 1, 512 - 1, 1024 - 1,
	2*1024 - 1, 4*1024 - 1, 8*1024 - 1, 16*1024 - 1, 32*1024 - 1,
	64*1024 - 1, 128*1024 - 1};
/*--------------------------------------------------------------------------*/




static int 
GetDegree(u_int64 poly)
{
  int k;

  for (k = 63; (poly & (INT64(1) << k)) == 0 && k > 0; k--) ;

  return k;
}
/*--------------------------------------------------------------------------*/
u_int64
GetFingerprint(u_int64 h, u_int64 l, u_int64 poly)
{
  int i;
  int k = GetDegree(poly);

  poly <<= (63 - k);
  if (h) {
    if (h & MSB64)
      h ^= poly;
    for (i = 62; i >= 0; i--)
      if (h & INT64 (1) << i) {
	h ^= (poly >> (63 - i));
	l ^= (poly << (i + 1));
      }
  }
  for (i = 63; i >= k; i--)
    if (l & INT64 (1) << i)
      l ^= (poly >> (63 - i));

  return l;
}
/*--------------------------------------------------------------------------*/
void
MultiplyPoly(u_int64* h, u_int64* l, u_int64 x, u_int64 y)
{
  u_int64 ih = 0, il = 0;
  int i;

  if (h == NULL || l == NULL)
  {
    fprintf(stderr, "wrong multiply argument\n");
    return;
  }


  if (x & 1)
    il = y;

  for (i = 63; i > 0; i--) {
    if (x & (INT64(1) << i)) {
      ih ^= (y >> (64 - i));
      il ^= (y << i);
    }
  }
  *h = ih;
  *l = il;
}
/*--------------------------------------------------------------------------*/
static u_int64 
ConcatByte(RABINPOLY* rp, u_int64 finger, u_int8 b)
{
  finger ^= rp->regionT[rp->region_buf[rp->region_pos]];
  rp->region_buf[rp->region_pos] = b;
  rp->region_pos = (rp->region_pos + 1) % rp->region_size;

  return (finger << 8 | b) ^ rp->moduloT[finger >> rp->shift];
}
/*--------------------------------------------------------------------------*/
static void 
InitializeRabin(RABINPOLY* rp, int region_size)
{
  int i, k;
  u_int64 T, h, l;
  u_int64 sizeshift = 1;

  if (rp->ir_poly) {
    return;
  }

  rp->ir_poly     = IRREDUCIBLE_POLY;
  rp->region_size = region_size;
  rp->region_pos  = 0;

  k = GetDegree(rp->ir_poly);
  rp->shift = k - 8;

  /* calculate the modulo table */
  T = GetFingerprint(0, INT64(1) << 63, rp->ir_poly);
  for (i = 0; i < 256; i++) {
    MultiplyPoly(&h, &l, i, T);
    rp->moduloT[i] = GetFingerprint(h, l, rp->ir_poly) | (INT64(i) << k);
  }
  /* calculate the region table */
  for (i = 1; i < rp->region_size; i++) 
    sizeshift = (sizeshift << 8) ^ rp->moduloT[sizeshift >> rp->shift];
  for (i = 0; i < 256; i++) {
    MultiplyPoly(&h, &l, i, sizeshift);
    rp->regionT[i] = GetFingerprint(h, l, rp->ir_poly);
  }
}
/*--------------------------------------------------------------------------*/
static void 
ZeroRegionBuf(RABINPOLY *rp)
{
  memset(rp->region_buf, 0, sizeof(rp->region_buf));
}
/*--------------------------------------------------------------------------*/
static int 
GetNormalizedSize(int b_size)
{
  int i;
  for (i = 0; i < (int)NELEM(chunk_sizes) - 1; i++) {
    if (b_size >= chunk_sizes[i] && b_size < chunk_sizes[i+1])
      return chunk_sizes[i];
  }

  fprintf(stderr, "block size %d is out of range(%d to %d)\n",  b_size,
  		chunk_sizes[0], chunk_sizes[(int)NELEM(chunk_sizes) - 1]);
  return 0;
}
/*--------------------------------------------------------------------------*/
static int
GetMRCIndex(int b_size)
{
  int i;
  for (i = 0; i < (int)NELEM(chunk_sizes) - 1; i++) {
    if (b_size >= chunk_sizes[i] && b_size < chunk_sizes[i+1])
      return i;
  }

  fprintf(stderr, "block size %d is out of range(%d to %d)\n",  b_size,
  		chunk_sizes[0], chunk_sizes[(int)NELEM(chunk_sizes) - 1]);
  return -1;
}
/*--------------------------------------------------------------------------*/
u_int GetMRCMask(int b_size)
{
	return masks[GetMRCIndex(b_size)];
}
/*--------------------------------------------------------------------------*/
#if 0
int32 
GetRabinChunkSize(const u_char* src, int32 srclen, int32 b_size)
{
#if 0
  /* pre-computed polynomials */
    0x946d1d8dcfee41e5LL
    0xbffd0d2d1803866bLL
    0xa77031279e124d9dLL
    0xd4eaaf59cb8c3605LL
    0xdf5b417b815f94f5LL
    0x8750d49cca554e29LL
    0xac021e7b5ce814e1LL
    0xd6864a7794875001LL
    0xd1f83f66a41cfb3dLL
    0x9d24c10e18a55d97LL
    0xc19b3824ffca799bLL
#endif

  const u_char* end, *p;
  static int avg_bsize = 0;
  static u_int mask = 0;
  u_int64 finger = 0;
  static RABINPOLY rp = {{0}};
  
  if (avg_bsize == 0) {
    avg_bsize = GetNormalizedSize(b_size);
    mask = avg_bsize - 1;
    /* use the default windows size of 48 bytes */
    InitializeRabin(&rp, 48);
  }
  
  if (srclen <= MIN_RABIN_CHUNK_SIZE)
    return srclen;

  ZeroRegionBuf(&rp);
  srclen = MIN(srclen, MAX_RABIN_CHUNK_SIZE);
  end = src + srclen;
  for (p = src + MIN_RABIN_CHUNK_SIZE; p < end; p++) {
    finger = ConcatByte(&rp, finger, *p);
    if ((finger & mask) == mask) 
      return (p - src + 1);
  }
  return srclen;
}
#endif
/*--------------------------------------------------------------------------*/
void InitRabinCtx(RabinCtx* rctx, int32 bsize, int window, int min, int max)
{
	rctx->bsize = GetNormalizedSize(bsize);
	rctx->mask = rctx->bsize - 1;
	rctx->finger = 0;
	rctx->rp.ir_poly = 0;
	rctx->min_size = min;
	rctx->max_size = max;

	/* this should be done very quickly? */
	InitializeRabin(&(rctx->rp), window);

	#if 0
	/* initialize rabin only once */
	static RABINPOLY rp;
	static char init = FALSE;
	if (!init) {
		InitializeRabin(&rp, window);
		init = TRUE;
	}

	/* just copy the initialized one */
	memcpy(&(rctx->rp), &rp, sizeof(RABINPOLY));
	#endif
}
/*--------------------------------------------------------------------------*/
int32 GetRabinChunkSizeEx(RabinCtx* rctx, const u_char* src, int32 srclen,
		u_int min_mask, u_int max_mask, char resetRegionBuffer,
		RabinBoundary* outBoundaries, unsigned int* outNumBoundaries)
{
	const u_char* end, *p;
	assert(rctx != NULL);
	if (resetRegionBuffer) {
		rctx->finger = 0;
		ZeroRegionBuf(&(rctx->rp));
	}

	srclen = srclen < rctx->max_size ? srclen : rctx->max_size;
	end = src + srclen;
	*outNumBoundaries = 0;
	
	for (p = src + rctx->min_size; p < end; p++) {
		rctx->finger = ConcatByte(&(rctx->rp), rctx->finger, *p);
		if ((rctx->finger & min_mask) == min_mask) {
			/* min chunk boundary detected */
			outBoundaries[*outNumBoundaries].offset = (p - src + 1);
			outBoundaries[*outNumBoundaries].finger = rctx->finger;
			(*outNumBoundaries)++;
		}

		if (max_mask != 0 && ((rctx->finger & max_mask) == max_mask)) {
			/* max chunk boundary detected, stop here */
			assert((rctx->finger & min_mask) == min_mask);
			assert((p - src + 1) <= srclen);
			assert(outBoundaries[*outNumBoundaries - 1].offset
					== (p - src + 1));
			break;
		}
	}

	if (*outNumBoundaries == 0) {
		/* we should have at least one */
		outBoundaries[*outNumBoundaries].offset = srclen;
		outBoundaries[*outNumBoundaries].finger = rctx->finger;
		(*outNumBoundaries)++;
	}

	/* return the last chunk */
	return outBoundaries[*outNumBoundaries - 1].offset;
}
/*--------------------------------------------------------------------------*/

int main(int argc, char** argv)
{
  #define WAPROX_MIN_CHUNK_SIZE   (30)
  #define WAPROX_RABIN_WINDOW     (16)
  #define WAPROX_MRC_MIN          (0)
  #define WAPROX_MRC_MAX          (128*1024)

  RabinCtx* context =(RabinCtx*) malloc(sizeof(RabinCtx));
  int32 chunkSize;
  int g_MinChunkSize = 64; /* in bytes */
  int g_ChunkSize = 1024; /* in bytes */
  FILE *fp;
  char* src;
  long lSize;
  int i;
  char* buffer;
  if(argc < 2)
  {
    printf("Usage: %s inputfile\n", argv[0]);
    exit(1);
  }
  if( !(fp = fopen(argv[1], "r")) )
  {
    printf("Can't open file %s\n", argv[1]);
    exit(1);
  }

  fseek(fp, 0L, SEEK_END);
  lSize = ftell(fp);
  rewind(fp);

  if(! (src = calloc(1, lSize + 1)) ){
    fclose(fp);
    printf("Memory allocation failed");
    exit(1);
  };

  if(1 != fread(src, lSize, 1, fp))
  {
    fclose(fp);
    free(src);
    printf("Read failed");
    exit(1);  
  }
  fclose(fp);


  static RabinBoundary* rabinBoundaries = NULL;
  unsigned int numBoundaries = 0;

  rabinBoundaries = (RabinBoundary*) malloc(sizeof(RabinBoundary) * g_ChunkSize);
  if (rabinBoundaries == NULL) {
          fprintf(stderr, "RabinBoundary alloc failed\n");
          return 1;
  }

  u_int min_mask = GetMRCMask(g_MinChunkSize);
  u_int max_mask = GetMRCMask(g_ChunkSize);
  

  InitRabinCtx(context, g_MinChunkSize, WAPROX_RABIN_WINDOW,
                WAPROX_MRC_MIN, WAPROX_MRC_MAX);

  chunkSize = GetRabinChunkSizeEx(context, (const u_char*) src, strlen(src), min_mask, max_mask, 1, rabinBoundaries, &numBoundaries);
  printf("Rabin Fingerprinted %d/%d bytes, finger: %lu, %d\n", chunkSize, (int) strlen(src), rabinBoundaries[numBoundaries-1].finger, numBoundaries);

  for(i=0; i<numBoundaries; i++)
  {
    if(i == 0)
    {
      buffer = malloc(rabinBoundaries[0].offset + 2);
      for(int j = 0; j <= rabinBoundaries[0].offset; j++)
      {
        buffer[j] = src[j];
      }
      buffer[rabinBoundaries[0].offset + 1] = '\0';
    }
     else    
    {
      if(! (buffer = malloc(rabinBoundaries[i].offset - rabinBoundaries[i-1].offset+ 2)))  exit(1);
      for(int j = 0; j <= (rabinBoundaries[i].offset - rabinBoundaries[i-1].offset); j++)
      {
        buffer[j] = src[rabinBoundaries[i-1].offset + 1 + j];
      }
      buffer[rabinBoundaries[i].offset - rabinBoundaries[i-1].offset + 1] = '\0';
    }
    printf("%d'th Fingerprint finger: %lu Fingerprint offset: %d\n Content: %s\n\n", i, rabinBoundaries[i].finger, rabinBoundaries[i].offset, buffer);
    free(buffer);
  }
  
  free(src);
  free(rabinBoundaries);

  return 0;
}
