#include <math.h>
#include <assert.h>
#include "mrc.h"
#include "util.h"

//#define MRC_DEBUG_OUTPUT

/*-----------------------------------------------------------------*/
int GetParent2(OneChunkName* pNames, int index)
{
	/* root has no parent */
	if (index == 0) {
		return 0;
	}
	return pNames[index].parentIdx;
}
/*-----------------------------------------------------------------*/
void InitOneChunk(OneChunkName* name)
{
	name->length = 0;
	name->index = 0;
	name->parentIdx = -1;
	name->level = 0;
	name->type = chunk_name;
	name->pChunk = NULL;
	name->freeChunk = FALSE;
	name->rindex = -1;
	name->fromPeer = FALSE;
	name->isHit = FALSE;
	name->isSha1Ready = FALSE;
	name->isCandidate = FALSE;
	name->offset = 0;
}
/*-----------------------------------------------------------------*/
void FlushChildren(OneChunkName* pNames, int* stack, int sp)
{
	assert(sp >= 1);
	int i;
	int hit_count = 0;
	int candidate_count = 0;
	int parent = GetParent2(pNames, stack[0]);
	for (i = 0; i < sp; i++) {
		int child = stack[i];
		/*TRACE("child %d\n", child);*/
		assert(parent == GetParent2(pNames, child));
		if (pNames[child].isHit) {
			hit_count++;
		}
		
		if (pNames[child].isCandidate) {
			candidate_count++;
		}
	}

	if (candidate_count < sp) {
		/* do nothing */
		return;
	}

	/* perform merging */
	assert(candidate_count == sp);
	if (hit_count == 0 || hit_count == sp) {
		/* all hit or all miss, make the parent as candidate */
		for (i = 0; i < sp; i++) {
			pNames[stack[i]].isCandidate = FALSE;
		}
		pNames[parent].isCandidate = TRUE;
	}
	else {
		/* mix of hit and miss, keep the children as candidates */
		for (i = 0; i < sp; i++) {
			pNames[stack[i]].isCandidate = TRUE;
		}
		pNames[parent].isCandidate = FALSE;
	}
}
/*-----------------------------------------------------------------*/
CandidateList* GetCandidateList3(MrcTree* tree, int lowLevel)
{
	/* find candidate chunks from bottom to top*/
	assert(lowLevel >= 0);
	struct timeval _start;
	UpdateCurrentTime(&_start);
	int i;
	int numCandidates = 0;
	int base = tree->pli[lowLevel].firstIndex;
	int numChunks = tree->pli[lowLevel].numNames;
	assert(numChunks <= tree->numNames);
	for (i = 0; i < numChunks; i++) {
		/* make leaves as candidates at the beginning */
		/*TRACE("leaves %d\n", base + i);*/
		tree->pNames[base + i].isCandidate = TRUE;
	}

	if (tree->mrcctx->degree == 1) {
		/* SRC, done */
		assert(lowLevel == 0);
	}
	else {
		/* MRC, check chunks bottom up */
		for (i = lowLevel; i >= 0; i--) {
			int numChunks = tree->pli[i].numNames;
			int base = tree->pli[i].firstIndex;
			int j;
			int sp = 0;
			for (j = 0; j < numChunks; j++) {
				int child = base + j;
				int parent = GetParent2(tree->pNames, child);
				assert(child < tree->numNames);
				assert(parent < tree->numNames);
				assert(parent <= child);

				/* put child in stack */
				tree->stack[sp++] = child;
				assert(sp < tree->mrcctx->maxNames);
				/* TRACE("add child %d\n", child); */

				/* check stack */
				if (j == numChunks - 1) {
					/* last chunk, flush */
					FlushChildren(tree->pNames, tree->stack, sp);
					sp = 0;
				}
				else {
					assert((j + 1) <= (numChunks - 1));
					int nextParent = GetParent2(tree->pNames,
							child + 1);
					if (nextParent != parent) {
						/* last child, flush */
						FlushChildren(tree->pNames, tree->stack, sp);
						sp = 0;
					}
				}
			}
		}
	}

	/* count number of candidates */
	for (i = 0; i < tree->numNames; i++) {
		if (tree->pNames[i].isCandidate == TRUE) {
			assert(tree->pNames[i].isSha1Ready == TRUE);
			numCandidates++;
		}
	}

	/* sort the candidate list */
	OneChunkName** candiArray = malloc(sizeof(OneChunkName*)
			* numCandidates);
	if (candiArray == NULL) {
		NiceExit(-1, "malloc failed\n");
	}
	int idx = 0;
	for (i = 0; i < tree->numNames; i++) {
		if (tree->pNames[i].isCandidate == TRUE) {
			candiArray[idx++] = &tree->pNames[i];
		}
	}
	qsort(candiArray, numCandidates, sizeof(OneChunkName*),
			CompareOneChunkName);
	
	/* make a list */
	CandidateList* candidateList = malloc(sizeof(CandidateList));
	if (candidateList == NULL) {
		NiceExit(-1, "malloc failed\n");
	}
	TAILQ_INIT(candidateList);
	int offset = 0;
	for (i = 0; i < numCandidates; i++) {
		assert(candiArray[i]->isCandidate == TRUE);
		assert(candiArray[i]->isSha1Ready == TRUE);
		OneCandidate* item = malloc(sizeof(OneCandidate));
		if (item == NULL) {
			NiceExit(-1, "malloc failed\n");
		}
		item->index = candiArray[i]->index;
		item->offset = candiArray[i]->offset;
		item->length = candiArray[i]->length;
		item->isHit = candiArray[i]->isHit;
		memcpy(item->sha1name, candiArray[i]->sha1name, SHA1_LEN);
		offset += item->length;
		TAILQ_INSERT_TAIL(candidateList, item, list);
	}
	assert(offset == tree->pNames[0].length);
	free(candiArray);

#ifdef MRC_DEBUG_OUTPUT
	TRACE("%d/%d candidate chunks, %d bytes, %d us\n", numCandidates,
			tree->numNames, tree->pNames[0].length, GetElapsedUS(&_start));
#endif
	return candidateList;
}
/*-----------------------------------------------------------------*/
#if 0
void DumpCandidateList(CandidateList* candidateList)
{
	assert(candidateList != NULL);
	OneCandidate* walk = NULL;
	int index = 0;
	int tot_len = 0;
	TAILQ_FOREACH(walk, candidateList, list) {
		TRACE("[%d] %s %d bytes, %s\n", index++,
				SHA1Str(walk->sha1name), walk->length,
				(!walk->isHit || walk->length
				 <= MIN_CHUNK_SIZE) ? "HAS" : "NONE");
		tot_len += walk->length;
	}
	TRACE("total %d bytes\n", tot_len);
}
#endif
/*-----------------------------------------------------------------*/
void FreeCandidateList(CandidateList* candidateList)
{
	assert(candidateList != NULL);
	OneCandidate* walk = NULL;
	while (!TAILQ_EMPTY(candidateList)) {
		walk = TAILQ_FIRST(candidateList);
		TAILQ_REMOVE(candidateList, walk, list);
		free(walk);
	}

	free(candidateList);
}
/*-----------------------------------------------------------------*/
void MakeTree(MRCPerLevelInfo* levInfo, int maxLevel, OneChunkName* pNames,
		int numNames)
{
	/* fill the parent index here */
	int i;
	for (i = 0; i < maxLevel - 1; i++) {
		int parentIndex = levInfo[i].firstIndex;
		int childIndex = levInfo[i + 1].firstIndex;
		int parentLength = 0;
		int childLength = 0;
		int j;
		for (j = 0; j < levInfo[i].numNames; j++) {
			assert(parentIndex < numNames);
			parentLength += pNames[parentIndex].length;
			while (TRUE) {
				assert(childIndex < numNames);
				childLength += pNames[childIndex].length;
				pNames[childIndex].parentIdx = parentIndex;
				childIndex++;
				if (childLength == parentLength) {
					break;
				}
			}
			parentIndex++;
		}
	}
}
/*-----------------------------------------------------------------*/
int CompareOneChunkName(const void *p1, const void *p2)
{
	OneChunkName* ocn1 = *((OneChunkName**)p1);
	OneChunkName* ocn2 = *((OneChunkName**)p2);
	assert(ocn1 != NULL);
	assert(ocn2 != NULL);

	if (ocn1->offset < ocn2->offset) {
		return -1;
	}
	else if (ocn1->offset > ocn2->offset) {
		return 1;
	}
	else {
		return 0;
	}
}
/*-----------------------------------------------------------------*/
MrcCtx* CreateMrcCtx(unsigned int degree,
		unsigned int avgMinChunkSize,
		unsigned int avgMaxChunkSize,
		unsigned int absMinChunkSize,
		unsigned int absMaxChunkSize,
		unsigned int rabinWindow)
{
	if (absMinChunkSize <= rabinWindow) {
		NiceExit(-1, "min chunk size should be larger than"
				" rabin window size\n");
	}

	MrcCtx* mrcctx = malloc(sizeof(MrcCtx));
	if (mrcctx == NULL) {
		NiceExit(-1, "MrcCtx malloc() failed");
	}

	mrcctx->degree = degree;
	mrcctx->avgMinChunkSize = avgMinChunkSize;
	mrcctx->avgMaxChunkSize = avgMaxChunkSize;
	mrcctx->absMinChunkSize = absMinChunkSize;
	mrcctx->absMaxChunkSize = absMaxChunkSize;
	mrcctx->rabinWindow = rabinWindow;

	mrcctx->minMask = GetMRCMask(mrcctx->avgMinChunkSize);
	mrcctx->maxMask = GetMRCMask(mrcctx->avgMaxChunkSize);
	mrcctx->maxLevel = 1;
	mrcctx->maxNames = 1;
	int chunkSize = mrcctx->avgMaxChunkSize;
	if (mrcctx->degree > 1) {
		while (TRUE) {
			chunkSize /= mrcctx->degree;
			if (chunkSize < mrcctx->avgMinChunkSize) {
				/* we stop here */
				break;
			}
			mrcctx->maxLevel++;
		}
	}

	/* use worst case max names when every chunk is 1 byte */
	mrcctx->maxNames = mrcctx->avgMaxChunkSize * mrcctx->maxLevel;

	TRACE("Degree: %d\n", mrcctx->degree);
	TRACE("Min Chunk Size: %d\n", mrcctx->avgMinChunkSize);
	TRACE("Max Chunk Size: %d\n", mrcctx->avgMaxChunkSize);
	TRACE("Max Chunk Names: %d\n", mrcctx->maxNames);
	TRACE("Max Tree Level: %d\n", mrcctx->maxLevel);

	return mrcctx;
}
/*-----------------------------------------------------------------*/
void DestroyMrcCtx(MrcCtx* mrcctx)
{
	assert(mrcctx != NULL);
	free(mrcctx);
}
/*-----------------------------------------------------------------*/
RabinCtx* CreateRabinContext(MrcCtx* mrcctx)
{
	/* init Rabin context */
	assert(mrcctx != NULL);
	RabinCtx* rctx = malloc(sizeof(RabinCtx));
	if (rctx == NULL) {
		NiceExit(-1, "RabinCtx malloc() failed");
	}

	InitRabinCtx(rctx, mrcctx->avgMinChunkSize, mrcctx->rabinWindow,
			mrcctx->absMinChunkSize, mrcctx->absMaxChunkSize);
	return rctx;
}
/*-----------------------------------------------------------------*/
void DestroyRabinContext(RabinCtx* rctx)
{
	assert(rctx != NULL);
	free(rctx);
}
/*-----------------------------------------------------------------*/
MrcTree* CreateMrcTree(MrcCtx* mrcctx)
{	
	assert(mrcctx != NULL);
	MrcTree* tree = malloc(sizeof(MrcTree));
	if (tree == NULL) {
		NiceExit(-1, "MrcTree malloc() failed");
	}
	tree->mrcctx = mrcctx;

	/* initially it's an empty tree */
	tree->numNames = 0;
	tree->maxlevel = 0;
	tree->lowlevel = 0;

	/* prepare chunk info holder */
	assert(mrcctx->maxNames > 0);
	tree->pNames = calloc(mrcctx->maxNames, sizeof(OneChunkName));
	if (tree->pNames == NULL) {
		NiceExit(-1, "OneChunkName array calloc() failed");
	}

	/* prepare MRCPerLevelInfo array */
	assert(mrcctx->maxLevel > 0);
	tree->pli = calloc(mrcctx->maxLevel, sizeof(MRCPerLevelInfo));
	if (tree->pli == NULL) {
		NiceExit(-1, "MRCPerLevelInfo array calloc() failed");
	}

	/* prepare RabinBoundary */
	tree->rabinBoundaries = malloc(sizeof(RabinBoundary) * mrcctx->avgMaxChunkSize);
	if (tree->rabinBoundaries == NULL) {
		NiceExit(-1, "RabinBoundary array malloc() failed");
	}

	/* prepare real boundary index */
	tree->pRealBoundary = calloc(mrcctx->maxNames, sizeof(char));
	if (tree->pRealBoundary == NULL) {
		NiceExit(-1, "Real boundary array calloc() failed");
	}

	/* prepare stack for qsort */
	tree->stack = malloc(sizeof(int) * mrcctx->maxNames);
	if (tree->stack == NULL) {
		NiceExit(-1, "stack malloc() failed\n");
	}

	return tree;
}
/*-----------------------------------------------------------------*/
void DestroyMrcTree(MrcTree* tree)
{
	assert(tree != NULL);
	if (tree->pNames != NULL) {
		free(tree->pNames);
	}
	if (tree->pli != NULL) {
		free(tree->pli);
	}
	if (tree->rabinBoundaries != NULL) {
		free(tree->rabinBoundaries);
	}
	if (tree->pRealBoundary != NULL) {
		free(tree->pRealBoundary);
	}
	if (tree->stack != NULL) {
		free(tree->stack);
	}
	free(tree);
}
/*-----------------------------------------------------------------*/
MrcTree* PerformPreMRC(MrcCtx* mrcctx, RabinCtx* rctx,
		char* psrc, int buf_len,
		MrcTree* tree)
{
	int rabin_buffer_len = MIN(buf_len, mrcctx->absMaxChunkSize);
	if (tree == NULL) {
		/* if tree is not given, create one */
		tree = CreateMrcTree(mrcctx);
		assert(tree != NULL);
	}

	/* prepare RabinBoundary */
	unsigned int numBoundaries = 0;

	/* detect all the RabinBoundaries */
	struct timeval _start;
	UpdateCurrentTime(&_start);
#if 0
	TRACE("init fingerprint: %llu, pos: %d\n", rctx->finger,
			rctx->rp.region_pos);
#endif
	int rb_size = GetRabinChunkSizeEx(rctx, (const u_char*)psrc,
			rabin_buffer_len, mrcctx->minMask, mrcctx->maxMask, TRUE,
			tree->rabinBoundaries, &numBoundaries);
	assert(rb_size > 0);
	assert(rb_size <= rabin_buffer_len);
	assert(numBoundaries > 0);
	assert(tree->rabinBoundaries[numBoundaries - 1].offset == rb_size);
#ifdef MRC_DEBUG_OUTPUT
	TRACE("rabin fingerprint %d/%d bytes, finger: %llu, %d us\n", rb_size,
			buf_len, tree->rabinBoundaries[numBoundaries - 1].finger,
			GetElapsedUS(&_start));
#endif
	
	/* init real boundary */
	memset(tree->pRealBoundary, FALSE, sizeof(char) * mrcctx->maxNames);
	tree->pRealBoundary[numBoundaries - 1] = TRUE;

	/* fill the chunk info */
	int chunkSize = mrcctx->avgMaxChunkSize;
	int index = 0;
	int numNames = 0;
	int level = 0;
	while (chunkSize >= mrcctx->avgMinChunkSize) {
		int lastOffset = 0;
		int length = 0;
		int i;

		UpdateCurrentTime(&_start);
		/* init per level info */
		tree->pli[level].numNames = 0;
		tree->pli[level].firstIndex = index;
		char* buffer = psrc;
		for (i = 0; i < numBoundaries; i++) {
			/* if mask matches or the last chunk */
			if (((tree->rabinBoundaries[i].finger & (chunkSize - 1))
						== (chunkSize - 1)) ||
					((i + 1) == numBoundaries)) {
				length = tree->rabinBoundaries[i].offset - lastOffset;

				if (length <= mrcctx->absMinChunkSize 
						&& tree->pRealBoundary[i] == FALSE) {
					/* move on if small and not bounded
					   by real boundaries */
					continue;
				}

				if (level == 0) {
					assert(length == rb_size);
				}

				/* fill the chunk info
				   without calculating expensive SHA1 hash */
				InitOneChunk(&(tree->pNames[index]));
				assert(length > 0);
				tree->pNames[index].length = length;
				tree->pNames[index].index = index;
				tree->pNames[index].level = level;
				tree->pNames[index].type = chunk_name;
				tree->pNames[index].pChunk = buffer;
				tree->pNames[index].offset = lastOffset;

				/* set the new boundary */
				tree->pRealBoundary[i] = TRUE;

				/* move to the next position */
				buffer += length;
				numNames++;
				index++;
				lastOffset = tree->rabinBoundaries[i].offset;

				/* update per level info */
				tree->pli[level].numNames++;
			}
		}

#ifdef MRC_DEBUG_OUTPUT
		TRACE("level: %d, %d names, %d us\n", level,
				tree->pli[level].numNames, GetElapsedUS(&_start));
#endif
		level++;
		if (mrcctx->degree == 1) {
			/* SRC, we stop here */
			break;
		}
		else {
			/* MRC, go down to the smaller chunk size */
			chunkSize /= mrcctx->degree;
		}
	}
	assert(numNames > 0);

	/* set return values */
	tree->numNames = numNames;
	tree->maxlevel = level;
	assert(tree->maxlevel > 0);

	/* make tree */
	MakeTree(tree->pli, tree->maxlevel, tree->pNames, tree->numNames);

	return tree;
}
/*-----------------------------------------------------------------*/
