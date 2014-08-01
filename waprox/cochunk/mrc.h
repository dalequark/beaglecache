#ifndef __MRC_H__
#define __MRC_H__

#include "czipdef.h"
#include "rabinfinger2.h"
#include "queue.h"

typedef enum chunkType {
	chunk_name,
	chunk_both,
	chunk_index,
	chunk_raw
} chunkType;

typedef struct OneChunkName {
	/* mandatory info - sent over network */
	chunkType type;
	int length;
	u_char sha1name[SHA1_LEN];
	char* pChunk;
	char freeChunk;
	int rindex;

	/* auxiliary info - internal only */
	int index;
	int parentIdx;
	int level;

	/* can be either used as name hint or put hint */
	char isHit;

	/* if sha1 is computed or not */
	char isSha1Ready;

	/* is candidate or not */
	char isCandidate;

	/* offset for sorting */
	int offset;

	/* where this chunk is from? */
	char fromPeer;
} OneChunkName;

typedef struct MRCPerLevelInfo {
	int numNames;
	int firstIndex;
} MRCPerLevelInfo;

typedef struct OneCandidate {
	int index;
	int offset;
	int length;
	char isHit;
	u_char sha1name[SHA1_LEN];
	TAILQ_ENTRY(OneCandidate) list;
	TAILQ_ENTRY(OneCandidate) deadList;
} OneCandidate;

typedef TAILQ_HEAD(CandidateList, OneCandidate) CandidateList;

typedef struct MrcCtx {
	/* average chunk size range */
	unsigned int avgMinChunkSize;
	unsigned int avgMaxChunkSize;

	/* absolute chunk size limits */
	unsigned int absMinChunkSize;
	unsigned int absMaxChunkSize;

	/* degree */
	unsigned int degree;

	/* rabin window size */
	unsigned int rabinWindow;

	/* for internal use */
	unsigned int maxLevel;
	unsigned int maxNames;
	unsigned int minMask;
	unsigned int maxMask;
} MrcCtx;

typedef struct MrcTree {
	/* chunk name array */
	OneChunkName* pNames;

	/* number of names in the array */
	int numNames;

	/* level info */
	MRCPerLevelInfo* pli;

	/* level */
	int maxlevel;
	int lowlevel;

	/* for temporary store */
	RabinBoundary* rabinBoundaries;
	char* pRealBoundary;
	int* stack;

	/* back pointer to MRC Context */
	MrcCtx* mrcctx;
} MrcTree;

MrcCtx* CreateMrcCtx(unsigned int degree,
		unsigned int avgMinChunkSize,
		unsigned int avgMaxChunkSize,
		unsigned int absMinChunkSize,
		unsigned int absMaxChunkSize,
		unsigned int rabinWindow);
void DestroyMrcCtx(MrcCtx* mrcctx);
RabinCtx* CreateRabinContext(MrcCtx* mrcctx);
void DestroyRabinContext(RabinCtx* rctx);
MrcTree* CreateMrcTree(MrcCtx* mrcctx);
void DestroyMrcTree(MrcTree* tree);
MrcTree* PerformPreMRC(MrcCtx* mrcctx, RabinCtx* rctx,
		char* psrc, int buf_len,
		MrcTree* tree);

void InitOneChunk(OneChunkName* name);
void DumpCandidateList(CandidateList* candidateList);
void FreeCandidateList(CandidateList* candidateList);
void MakeTree(MRCPerLevelInfo* levInfo, int maxLevel, OneChunkName* pNames,
		int numNames);
void FlushChildren(OneChunkName* pNames, int* stack, int sp);
CandidateList* GetCandidateList3(MrcTree* tree, int lowLevel);
int CompareOneChunkName(const void *p1, const void *p2);
	
#endif /*__MRC_H__*/
