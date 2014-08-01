#include <math.h>
#include <assert.h>
#include "mrc_tree.h"
#include "util.h"
#include "config.h"
#include "disk.h"
#include "protocol.h"
#include "peer.h"
#include "waprox.h"

/*#define _WAPROX_DEBUG_RESOLVE*/

/* multi-resolution chunking tree parameters */
int g_maxNames = 1;
int g_maxLevel = 1;
PerLevelStats* g_PerLevelStats = NULL;

static char init = FALSE;
#if 0
static int* parents = NULL;
static int* levels = NULL;
static int* firstIndexByLevel = NULL;
#endif
static OneChunkName* pNamesForQsort = NULL;

/* message queue for storing chunks into cache */
PutMessageList g_PutMessageList;
int g_NumPutMessages = 0;
unsigned int g_CurPutQueueSize = 0;
unsigned int g_TotalPutDiscarded = 0;

/*-----------------------------------------------------------------*/
void InitMRCParams()
{
	if (init == TRUE) {
		TRACE("already initialized\n");
		return;
	}

	g_maxLevel = 1;
	g_maxNames = 1;
	int chunkSize = g_ChunkSize;
	if (g_NumMRCWays > 1) {
		while (TRUE) {
			chunkSize /= g_NumMRCWays;
			if (chunkSize < g_MinChunkSize) {
				/* we stop here */
				break;
			}
			g_maxNames += pow(g_NumMRCWays, g_maxLevel);
			g_maxLevel++;
		}
	}
	g_maxNames = g_ChunkSize * g_maxLevel;

	/* pre-compute and build tables for level/parent lookup */
	#if 0
	int i, j, index = 0;
	levels = malloc(sizeof(int) * g_maxNames);
	if (levels == NULL) {
		NiceExit(-1, "level table allocation failed\n");
	}
	parents = malloc(sizeof(int) * g_maxNames);
	if (parents == NULL) {
		NiceExit(-1, "parent table allocation failed\n");
	}
	firstIndexByLevel = malloc(sizeof(int) * g_maxLevel);
	if (firstIndexByLevel == NULL) {
		NiceExit(-1, "firstIndexByLevel table allocation failed\n");
	}
	for (i = 0; i < g_maxLevel; i++) {
		int numNodes = pow(g_NumMRCWays, i);
		firstIndexByLevel[i] = index;
		for (j = 0; j < numNodes; j++) {
			levels[index] = i;
			parents[index] = (int)(floor((index - 1)
						/ g_NumMRCWays));
			index++;
		}
	}
	assert(index == g_maxNames);
	#endif

	/* create PerLevelStats object */
	g_PerLevelStats = calloc(g_maxLevel, sizeof(PerLevelStats));
	if (g_PerLevelStats == NULL) {
		NiceExit(-1, "PerLevelStats allocation failed\n");
	}

	TRACE("Num Ways: %d\n", g_NumMRCWays);
	TRACE("Max ChunkTree Names: %d\n", g_maxNames);
	TRACE("Max ChunkTree Level: %d\n", g_maxLevel);
	init = TRUE;
}
/*-----------------------------------------------------------------*/
int GetTreeLevel2(OneChunkName* pNames, int index)
{
	return pNames[index].level;
}
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
int GetFirstIndex2(ChunkTreeInfo* cti, int level)
{
	return cti->perLevelInfo[level].firstIndex;
}

#if 0
/*-----------------------------------------------------------------*/
int GetTreeLevel(int index)
{
	return levels[index];
}
/*-----------------------------------------------------------------*/
int GetParent(int index)
{
	return parents[index];
}
/*-----------------------------------------------------------------*/
int GetFirstIndex(int level)
{
	return firstIndexByLevel[level];
}
#endif
/*-----------------------------------------------------------------*/
int CompareChunkIndex(const void *p1, const void *p2)
{
	assert(pNamesForQsort != NULL);
	int index1 = *((int*)p1);
	int index2 = *((int*)p2);
	int level1 = GetTreeLevel2(pNamesForQsort, index1);
	int level2 = GetTreeLevel2(pNamesForQsort, index2);
	assert(index1 < g_maxNames);
	assert(index2 < g_maxNames);
	assert(level1 < g_maxLevel);
	assert(level2 < g_maxLevel);
	int org_idx1 = index1;
	int org_idx2 = index2;

	while (level1 != level2) {
		/* adjust level and index */
		if (level1 < level2) {
			index2 = GetParent2(pNamesForQsort, index2);
			level2--;
		}
		else if (level1 > level2) {
			index1 = GetParent2(pNamesForQsort, index1);
			level1--;
		}
	}

	assert(level1 == level2);
	if (index1 < index2) {
		return -1;
	}
	else if (index1 > index2) {
		return 1;
	}
	else {
		TRACE("comparing %d vs. %d\n", org_idx1, org_idx2);
		TRACE("%d != %d\n", index1, index2);
		assert(FALSE);
		return 0;
	}
}
/*-----------------------------------------------------------------*/
void PrintChunkTree(OneChunkName* names)
{
	int i;
	int sum[g_maxLevel];
	for (i = 0; i < g_maxLevel; i++) {
		sum[i] = 0;
	}

	TRACE("START\n");
	for (i = 0; i < g_maxNames; i++) {
		int level = GetTreeLevel2(names, i);
		if (names[i].length == 0) {
			continue;
		}
		TRACE("level: %d, type: %d, index: %d, parent: %d, %d bytes\n",
				level, names[i].type, i, GetParent2(names, i),
				names[i].length);
		sum[level] += names[i].length;
	}

	int debugSum = sum[0];
	for (i = 0; i < g_maxLevel; i++) {
		TRACE("sum of level %d: = %d\n", i, sum[i]);
		assert(debugSum == sum[i]);
	}
	TRACE("END\n");
}
/*-----------------------------------------------------------------*/
#if 0
void PrintChunkTree2(OneChunkName* names, int numNames)
{
	OneChunkName* pNew = CreateNewMRCTree(names, numNames);
	PrintChunkTree(pNew);
	free(pNew);
}
#endif
/*-----------------------------------------------------------------*/
#if 0
void GenerateMRC2(MrcCtx* mrc_ctx, int level, int chunkSize, char* buffer,
		int buf_len, int index, OneChunkName* names, int* numNames,
		ChunkCallBack cb)
{
	struct timeval time1, time2, result;
	unsigned int us = 0;
	UpdateCurrentTime(&time1);
	int rb_size = 0;
	assert(*numNames == 0);
	int org_len = buf_len;
	while (buf_len > 0) {
		if (level == 0) {
			/* root level boundary already determined */
			rb_size = buf_len;
			assert(chunkSize == g_ChunkSize);
		}
		else {
			/* keep the sliding window */
			rb_size = GetRabinChunkSizeMrcCtx(mrc_ctx,
					(const u_char*)buffer, buf_len,
					chunkSize, FALSE);
		}

		/* fill the chunk info */
		assert(rb_size > 0);
		Calc_SHA1Sig((const byte*)buffer, rb_size,
				names[index].sha1name);
		InitOneChunk(&(names[index]));
		names[index].length = rb_size;
		names[index].index = index;
		names[index].level = level;
		names[index].type = chunk_name;
		names[index].pChunk = buffer;

		/* call per chunk callback function here */
		if (cb != NULL) {
			cb(names[index].sha1name, buffer, rb_size);
		}

		/* move to the next position */
		buf_len -= rb_size;
		buffer += rb_size;
		(*numNames)++;
		index++;
	}
	UpdateCurrentTime(&time2);
	timersub(&time2, &time1, &result);
	us = result.tv_sec * 1000000 + result.tv_usec;
	TRACE("%d bytes, level=%d, %d names created, %d us\n",
			org_len, level, *numNames, us);
}
/*-----------------------------------------------------------------*/
OneChunkName* MakePreMRCTree(MrcCtx* mrc_ctx, char* buffer, int buf_len,
		int numLevels, int* outNumNames, ChunkCallBack cb)
{
	/* outNumNames should be initialized to zero */
	assert(*outNumNames == 0);
	static OneChunkName* pNames = NULL;
	if (pNames == NULL) {
		pNames = calloc(g_maxNames, sizeof(OneChunkName));
		if (pNames == NULL) {
			NiceExit(-1, "MRC tree allocation failed\n");
		}
	}

#if 0
	/* we start from the index zero */
	GenerateMultiResolutionChunks(buffer, buf_len,
		startLevel, pNames, 0, outNumNames, cb);
#endif

	/* n-ary MRC */
	int chunkSize = g_ChunkSize;
	int level = 0;
	while (TRUE) {
		int names = 0;
		GenerateMRC2(mrc_ctx, level, chunkSize, buffer, buf_len,
				*outNumNames, pNames, &names, cb);
		assert(names >= 0);
		*outNumNames += names;
		assert(*outNumNames < g_maxNames);
		/*
		TRACE("level: %d, %d names, %d bytes\n", level, names, buf_len);
		*/

		/* next level */
		level++;
		chunkSize /= g_NumMRCWays;
		if (level >= numLevels) {
			break;
		}
	}

	/* return unordered MRC tree */
	assert(*outNumNames < g_maxNames);
	/*
	TRACE("%d names, %d bytes\n", *outNumNames, buf_len);
	*/
	return pNames;
}
#endif
/*-----------------------------------------------------------------*/
#if 0
void GenerateMultiResolutionChunks(char* buffer, int buf_len,
		int level, OneChunkName* names, int index,
		int* numNames, ChunkCallBack cb)
{
	/* generate SHA1-name for the chunk */
	u_char digest[SHA1_LEN];
	Calc_SHA1Sig((const byte*)buffer, buf_len, digest);
	assert(buf_len > 0);
	names[*numNames].index = index;
	names[*numNames].length = buf_len;
	memcpy(names[*numNames].sha1name, digest, SHA1_LEN);
	(*numNames)++;

	/* call per chunk callback function here */
	if (cb != NULL) {
		cb(digest, buffer, buf_len);
	}

	int chunkSize = g_ChunkSize / pow(g_NumMRCWays, level + 1);
	if (level + 1 < g_maxLevel) {
		assert(chunkSize >= g_MinChunkSize);
		int i, rb_size;
		for (i = 0; i < g_NumMRCWays - 1; i++) {
			rb_size = GetRabinChunkSize((const u_char*)buffer,
					buf_len, chunkSize);
			/* call its subtree */
			GenerateMultiResolutionChunks(buffer, rb_size,
					level + 1, names,
					g_NumMRCWays * index + 1 + i,
					numNames, cb);

			if (buf_len - rb_size == 0) {
				/* stop generating zero-size tree */
				return;
			}
			else {
				buffer += rb_size;
				buf_len -= rb_size;
			}
		}

		/* call its last subtree */
		GenerateMultiResolutionChunks(buffer, buf_len,
				level + 1, names,
				g_NumMRCWays * index + g_NumMRCWays,
				numNames, cb);
	}
}
/*-----------------------------------------------------------------*/
int GetRootChunkLevel(char* buffer, int buf_len)
{
	int i, rb_size = 0;
	int chunkSize = g_ChunkSize;
	for (i = 0; i < g_maxLevel; i++) {
		rb_size = GetRabinChunkSize((const u_char*)buffer,
				buf_len, chunkSize);
		if (rb_size < buf_len) {
			/* chunks found within this buffer */
			break;
		}
		chunkSize /= g_NumMRCWays;
	}

	return i;
}
#endif
/*-----------------------------------------------------------------*/
#if 0
OneChunkName* CreateNewMRCTree(OneChunkName* names, int numNames)
{
	assert(numNames <= g_maxNames);
	OneChunkName* pNewTree = calloc(numNames, sizeof(OneChunkName));
	if (pNewTree == NULL) {
		NiceExit(-1, "MRC tree allocation failed\n");
	}

	int i;
	for (i = 0; i < numNames; i++) {
		assert(names[i].index < numNames);
		memcpy(&pNewTree[names[i].index], &names[i],
				sizeof(OneChunkName));
	}

	/* return a regular tree
	   from the collection of unordered chunk names */
	return pNewTree;
}
#endif
/*-----------------------------------------------------------------*/
void InitChunkTreeInfo(ChunkTreeInfo* pCTInfo, OneChunkName* names,
		int numNames, int seq_no)
{
	assert(numNames <= g_maxNames);
	assert(pCTInfo != NULL);
	assert(pCTInfo->chunkState != NULL);
	memset(pCTInfo->chunkState, 0, numNames * sizeof(char));
	assert(pCTInfo->pNames != NULL);
	memset(pCTInfo->pNames, 0, numNames * sizeof(OneChunkName));
	int i;
	for (i = 0; i < numNames; i++) {
		assert(names[i].index < numNames);
		memcpy(&pCTInfo->pNames[names[i].index], &names[i],
				sizeof(OneChunkName));
	}
	pCTInfo->numNames = numNames;
	pCTInfo->numLevels = g_maxLevel;

	MRCPerLevelInfo* levInfo = pCTInfo->perLevelInfo;
	OneChunkName* pNames = pCTInfo->pNames;

	/* fill the per level info here */
	int totLength = pNames[0].length;
	int index = 0;
	int numNames2 = 0;
	for (i = 0; i < pCTInfo->numLevels; i++) {
		int offset = 0;
		levInfo[i].numNames = 0;
		levInfo[i].firstIndex = index;
		while (TRUE) {
			assert(index < numNames);
			offset += pNames[index].length;
			pNames[index].level = i;
			levInfo[i].numNames++;
			index++;
			numNames2++;
			if (offset == totLength) {
				break;
			}
		}
		if (numNames2 == pCTInfo->numNames) {
			pCTInfo->numLevels = i + 1;
			break;
		}
	}

	/* debug */
	for (i = 0; i < pCTInfo->numLevels; i++) {
		int j;
		int length = 0;
		for (j = 0; j < levInfo[i].numNames; j++) {
			int newIdx = GetFirstIndex2(pCTInfo, i) + j;
			length += pNames[newIdx].length;
		}
		assert(length == totLength);
	}

	/* fill the parent index here */
	for (i = 0; i < pCTInfo->numLevels - 1; i++) {
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

	/* assign the right chunk type */
	for (i = 0; i < pCTInfo->numLevels; i++) {
		int numChunks = levInfo[i].numNames;
		int newIdx = GetFirstIndex2(pCTInfo, i);
		int j;
		for (j = 0; j < numChunks; j++) {
			assert(newIdx < numNames);
			int parent = GetParent2(pNames, newIdx);
			if (newIdx > 0 && (pNames[parent].length ==
						pNames[newIdx].length)) {
				/* if non-root and it is the same
				   as its parent: index */
				pNames[newIdx].type = chunk_index;
				pNames[newIdx].rindex = parent;
			}
			else if (pNames[newIdx].length <=
					WAPROX_MIN_CHUNK_SIZE) {
				/* raw chunk */
				pNames[newIdx].type = chunk_raw;
			}
			else {
				assert(pNames[newIdx].type == chunk_name);
			}

			newIdx++;
		}
	}

	assert(names[0].length > 0);
	pCTInfo->curResolvingLevel = 0;
	pCTInfo->numChunkUnresolved = 1;
	pCTInfo->lowLevel = 0;
	pCTInfo->numMissingChunks = 0;
	pCTInfo->bytesMissingChunks = 0;
	pCTInfo->numLocalHits = 0;
	pCTInfo->bytesHits = 0;
	pCTInfo->numLocalMisses = 0;
	pCTInfo->seq_no = seq_no;
}
/*-----------------------------------------------------------------*/
ChunkTreeInfo* CreateNewChunkTreeInfo(int numNames, char numLevels)
{
	assert(numNames <= g_maxNames);
	assert(numLevels <= g_maxLevel);
	ChunkTreeInfo* pCTInfo = malloc(sizeof(ChunkTreeInfo));
	if (pCTInfo == NULL) {
		NiceExit(-1, "ChunkTreeInfo allocation failed\n");
	}
	pCTInfo->chunkState = calloc(numNames, sizeof(char));
	if (pCTInfo->chunkState == NULL) {
		NiceExit(-1, "chunkState allocation failed\n");
	}
	pCTInfo->perLevelInfo = calloc(g_maxLevel, sizeof(MRCPerLevelInfo));
	if (pCTInfo->perLevelInfo == NULL) {
		NiceExit(-1, "perLevelInfo allocation failed\n");
	}
	/*pCTInfo->pNames = CreateNewMRCTree(names, numNames);*/
	pCTInfo->pNames = calloc(numNames, sizeof(OneChunkName));
	if (pCTInfo->pNames == NULL) {
		NiceExit(-1, "MRC tree allocation failed\n");
	}
	/* we copy the names by InitChunkTreeInfo() */

	pCTInfo->numNames = numNames;
	pCTInfo->numLevels = numLevels;
	return pCTInfo;
}
/*-----------------------------------------------------------------*/
void DestroyChunkTreeInfo(ChunkTreeInfo* cti)
{
	if (cti->pNames != NULL) {
		int i = 0;
		for (i = 0; i < cti->numNames; i++) {
			if (cti->pNames[i].freeChunk) {
				assert(cti->pNames[i].pChunk != NULL);
				assert(cti->pNames[i].length > 0);
				free(cti->pNames[i].pChunk);
			}
		}
		free(cti->pNames);
	}

	if (cti->perLevelInfo != NULL) {
		free(cti->perLevelInfo);
	}

	if (cti->chunkState != NULL) {
		free(cti->chunkState);
	}

	free(cti);
}
/*-----------------------------------------------------------------*/
void UpdateLookupResult(ChunkTreeInfo* cti, char isHit, int newIdx)
{
	assert(cti->numChunkUnresolved > 0);
	cti->numChunkUnresolved--;
	cti->chunkState[newIdx] = isHit;
	if (isHit == TRUE) {
		/* cache hit */
		cti->bytesMissingChunks -= cti->pNames[newIdx].length;
		assert(cti->bytesMissingChunks >= 0);
		g_PerLevelStats[cti->curResolvingLevel].numHits++;
		g_PerLevelStats[cti->curResolvingLevel].bytesHits +=
			cti->pNames[newIdx].length;
		cti->numLocalHits++;
		cti->bytesHits += cti->pNames[newIdx].length;
	}
	else {
		/* cache miss */
		cti->numMissingChunks++;
		cti->numLocalMisses++;
	}
}
/*-----------------------------------------------------------------*/
int* ResolveOneLevel(ChunkTreeInfo* cti, int level)
{
	assert(level < g_maxLevel);
	int i;
	int numChunks = cti->perLevelInfo[level].numNames;
	cti->curResolvingLevel = level;
	cti->numMissingChunks = 0;
	cti->numChunkUnresolved = 0;
	cti->lowLevel = MAX(cti->lowLevel, level);
	static int* chunksToLookup = NULL;
	if (chunksToLookup == NULL) {
		chunksToLookup = malloc(sizeof(int) * g_maxNames);
		if (chunksToLookup == NULL) {
			NiceExit(-1, "chunksToLookup allocation failed\n");
		}
	}

	for (i = 0; i < numChunks; i++) {
		int newIdx = GetFirstIndex2(cti, level) + i;
		int parent = GetParent2(cti->pNames, newIdx);
		if (level > 0) {
			assert(newIdx > parent);
		}
		assert(newIdx < g_maxNames);

		/* set this to MISS before we resolve it */
		cti->chunkState[newIdx] = FALSE;

		#if 0
		if (cti->pNames[newIdx].type == chunk_raw ||
				cti->pNames[newIdx].type == chunk_both) {
			/* if content is available, it's hit */
			cti->chunkState[newIdx] = TRUE;
			continue;
		}

		if (cti->pNames[newIdx].type == chunk_index) {
			/* if index chunk, copy state from it */
			int rindex = cti->pNames[newIdx].rindex;
			assert(rindex >= 0);
			cti->chunkState[newIdx] = cti->chunkState[rindex];
			continue;
		}
		#endif

		if (level > 0 && cti->chunkState[parent] == TRUE) {
			/* if non root and parent is hit,
			   don't lookup */
			cti->chunkState[newIdx] = TRUE;
			continue;
		}

		if (cti->pNames[newIdx].length == 0) {
			/* if length is zero, don't lookup */
			cti->chunkState[newIdx] = TRUE;
			continue;
		}

		if (level > 0 && cti->pNames[newIdx].length ==
				cti->pNames[parent].length) {
			/* if chunk is the same as its parent,
			   it should be cache miss
			   and we don't have to lookup */
			assert(cti->chunkState[parent] == FALSE);
			cti->chunkState[newIdx] = FALSE;
			cti->numMissingChunks++;
			continue;
		}

		/* remember chunks to lookup */
		chunksToLookup[cti->numChunkUnresolved++] = newIdx;
	}

	assert(cti->numChunkUnresolved <= numChunks);
	return chunksToLookup;
}
/*-----------------------------------------------------------------*/
void ResolveMRCTreeInMemory(ChunkTreeInfo* cti, LookupCallBack cb)
{
	assert(cb != NULL);
	/* Pass 1. lookup from root to leaf:
	   count # of lookups, # of missing chunk requests, and bytes */
	cti->bytesMissingChunks = cti->pNames[0].length;
	cti->numMissingChunks = 0;
	cti->lowLevel = 0;
	cti->numLocalHits = 0;
	cti->numLocalMisses = 0;
	cti->bytesHits = 0;
	int prevNumMissingChunks = 0;
	int prevBytesMissingChunks = cti->pNames[0].length * 2;
	assert(prevBytesMissingChunks > cti->pNames[0].length);
	assert(cti->bytesMissingChunks > 0);
	int i;
	for (i = 0; i < cti->numLevels; i++) {
		/* generate chunk name list to lookup */	
		int* chunksToLookup = ResolveOneLevel(cti, i);

		/* perform lookup here */
		int j = 0;
		while (cti->numChunkUnresolved > 0) {
			int newIdx = chunksToLookup[j++];
			int isHit = cb(cti->pNames[newIdx].sha1name);
			UpdateLookupResult(cti, isHit, newIdx);
		}

		/* for debug */
		if (prevBytesMissingChunks > cti->bytesMissingChunks) {
			prevBytesMissingChunks = cti->bytesMissingChunks;
			prevNumMissingChunks = cti->numMissingChunks;
		}
		else if (prevBytesMissingChunks == cti->bytesMissingChunks) {
			assert(prevNumMissingChunks <= cti->numMissingChunks);
		}
		else {
			assert(FALSE);
		}

		if (cti->numMissingChunks == 0) {
			/* all chunks are resolved to be cache hit!
			   we stop here */
			assert(cti->bytesMissingChunks == 0);
			break;
		}
	}
	assert(cti->numMissingChunks >= 0);
	assert(cti->bytesMissingChunks >= 0);
	assert(cti->bytesMissingChunks == prevBytesMissingChunks);
	assert(cti->numMissingChunks >= prevNumMissingChunks);

	if (cti->bytesMissingChunks > 0) {
		assert(cti->numMissingChunks > 0);
	}

	assert(cti->bytesHits + cti->bytesMissingChunks
			== cti->pNames[0].length);
}
/*-----------------------------------------------------------------*/
int* MissingChunkPolicy(ChunkTreeInfo* cti, int* outNumEntries)
{
	/* TODO: implement chunk request policy here
	   current policy is a very simple one */
	static int* resultArray = NULL;
	if (resultArray == NULL) {
		resultArray = malloc(sizeof(int) * g_maxNames);
		if (resultArray == NULL) {
			NiceExit(-1, "resultArrary creation failed\n");
		}
	}

	assert(*outNumEntries == 0);
	/*
	GetChunkNameList(0, cti->chunkState, 0, resultArray, outNumEntries);
	*/
	/*PrintChunkTreeCache(cti);*/
	GetChunkNameList2(cti, resultArray, outNumEntries);
	/* TODO: merge small chunk misses into one large chunk miss */
	assert(*outNumEntries <= g_maxNames);
	pNamesForQsort = cti->pNames;
	qsort(resultArray, *outNumEntries, sizeof(int), CompareChunkIndex);

	#if 0
	/* TODO: merge requests */
	int i;
	for (i = 0; i < *outNumEntries; i+ = 2) {
	}
	#endif

	return resultArray;
}
/*-----------------------------------------------------------------*/
void GetChunkNameList2(ChunkTreeInfo* cti, int* resultArray, int* resultIdx)
{
	assert(*resultIdx == 0);
	int i;
	int totLength = cti->pNames[0].length;
	int offset = 0;
	for (i = 0; i < cti->numNames; i++) {
		int parent = GetParent2(cti->pNames, i);
		int level = GetTreeLevel2(cti->pNames, i);
		int length = cti->pNames[i].length;

		if (level == (cti->numLevels - 1)) {
			/* leaf */
			if (level == 0) {
				/* root --> single resolution */
				assert(cti->numNames == 1);
				resultArray[*resultIdx] = i;
				(*resultIdx)++;
				offset += length;
				return;
			}

			if (cti->chunkState[parent] == FALSE) {
				/* parent is miss, add it */
				resultArray[*resultIdx] = i;
				(*resultIdx)++;
				offset += length;
			}
			else {
				/* parent is hit, ignore */
				assert(cti->chunkState[i] == TRUE);
			}
		}
		else {
			/* non-leaf */
			if (cti->chunkState[parent] == FALSE) {
				/* parent is miss */
				if (cti->chunkState[i] == TRUE) {
					/* i'm hit, add it */
					resultArray[*resultIdx] = i;
					(*resultIdx)++;
					offset += length;
				}
				else {
					/* i'm miss, too */
					assert(cti->chunkState[i] == FALSE);
				}
			}
			else {
				/* parent is hit */
				if (i == 0) {
					/* if root, add it */
					resultArray[*resultIdx] = i;
					(*resultIdx)++;
					offset += length;
				}
				else {
					/* if non-root, ignore */
					assert(cti->chunkState[i] == TRUE);
				}
			}
		}

		if (offset == totLength) {
			/* all done! */
			break;
		}
	}
	assert(*resultIdx <= cti->numNames);
}
/*-----------------------------------------------------------------*/
void GetChunkNameList(int level, char* states, int index,
		int* resultArray, int* resultIdx, char maxLevel)
{
	if (states[index] == TRUE || level + 1 >= maxLevel) {
		/* cache hit or leaf level: we stop here */
		resultArray[*resultIdx] = index;
		(*resultIdx)++;
	}
	else {
		/* cache miss: get name list from its children */
		int i;
		for (i = 0; i < g_NumMRCWays; i++) {
			GetChunkNameList(level + 1, states,
					g_NumMRCWays * index + 1 + i,
					resultArray, resultIdx, maxLevel);
		}
	}
}
/*-----------------------------------------------------------------*/
#if 0
void SamplePolicy(ChunkTreeInfo* cti, char* block)
{
	int i, j, newIdx, numChunks;
	OneChunkName* names = cti->pNames;
	char cache_status_tmp[g_maxNames];
	int backupNumMissing = cti->numMissingChunks;

	/* copy cache_status */
	for (i = 0; i < g_maxNames; i++) {
		cache_status_tmp[i] = cti->chunkState[i];
	}

	/* traverse from leaf to root: coalesce chunk requests */
	for (i = cti->lowLevel; i >= 0; i--) {
		if (cti->numMissingChunks < 2) {
			/* we should have at least 2 chunks to merge */
			break;
		}

		int numCoalesced = 0;
		char stop = TRUE;
		numChunks = cti->perLevelInfo[i].numNames;
		for (j = 0; j < numChunks; j += g_NumMRCWays) {
			newIdx = GetFirstIndex2(cti, i) + j;
			int parent = GetParent2(names, newIdx);
			int k;
			int numMiss = 0;
			int numZero = 0;
			for (k = 0; k < g_NumMRCWays; k++) {
				assert(GetParent2(names, newIdx + k) == parent);
				if (cache_status_tmp[newIdx + k] == FALSE) {
					numMiss++;
				}

				if (names[newIdx + k].length == 0) {
					assert(cache_status_tmp[newIdx + k]
							== TRUE);
					numZero++;
				}
			}

			if (numMiss > 0 && numMiss + numZero == g_NumMRCWays) {
				/* all children are miss, 
				   coalesce these chunk requests */
				numCoalesced += (numMiss - 1);
				stop = FALSE;
			}
			else {
				/* stop processing in the upper level */
				cache_status_tmp[GetParent2(names, newIdx)]
					= TRUE;
			}
		}

		if (stop == TRUE) {
			/* nothing to merge: we stop here */
			break;
		}
		else {
			assert(numCoalesced >= 0);
			cti->numMissingChunks -= numCoalesced;
		}
	}

	assert(cti->numMissingChunks >= 0);
	/*
	TRACE("policy #2: # missing chunks: %d, bytes: %d\n",
			cti->numMissingChunks, cti->bytesMissingChunks);
	*/
	if (cti->numMissingChunks > backupNumMissing) {
		/* debug! */
		PrintChunkTreeCache(cti);
		assert(FALSE);
	}

	if (cti->bytesMissingChunks > 0) {
		if (cti->numMissingChunks == 0) {
			/* debug! */
			PrintChunkTreeCache(cti);
			TRACE("# missing chunks: %d, bytes: %d\n",
					backupNumMissing,
					cti->bytesMissingChunks);
			assert(FALSE);
		}
	}
}
#endif
/*-----------------------------------------------------------------*/
void PrintChunkTreeCache(ChunkTreeInfo* cti)
{
	OneChunkName* names = cti->pNames;
	char* cache_status = cti->chunkState;
	int i;
	int sum[g_maxLevel];
	for (i = 0; i < g_maxLevel; i++) {
		sum[i] = 0;
	}

	TRACE("START\n");
	for (i = 0; i < cti->numNames; i++) {
		int level = GetTreeLevel2(names, i);
		if (names[i].length == 0) {
			continue;
		}
		TRACE("level: %d, %d bytes, type: %d, %s\n", level,
				names[i].length, names[i].type,
				cache_status[i] ? "Hit" : "Miss");
		sum[level] += names[i].length;
	}
	for (i = 0; i < g_maxLevel; i++) {
		TRACE("sum of level %d: = %d\n", i, sum[i]);
	}
	TRACE("END\n");
}
/*-----------------------------------------------------------------*/
void PutMissingChunks(ChunkTreeInfo* cti, char* buffer, int buf_len,
		ChunkCallBack cb)
{
	int i, j;
	OneChunkName* names = cti->pNames;
	assert(cti->lowLevel < cti->numLevels);
	for (i = cti->lowLevel; i >= 0; i--) {
		int numChunks = cti->perLevelInfo[i].numNames;
		int offset = 0;
		int newIdx = GetFirstIndex2(cti, i);
		for (j = 0; j < numChunks; j++) {
			assert(newIdx < cti->numNames);
			/* we don't put it if it's from peer!! */
			if (cti->chunkState[newIdx] == FALSE &&
					names[newIdx].length > 0 &&
					names[newIdx].fromPeer == FALSE) {
				/* MISS: PUT it into cache */
				cb(names[newIdx].sha1name, buffer + offset,
						names[newIdx].length);
			}

			/* advance the offset */
			offset += names[newIdx].length;
			assert(offset <= buf_len);
			newIdx++;
		}
	}
}
/*-----------------------------------------------------------------*/
void ActualPutMissingChunks(char* buffer, int buf_len, int dstPxyId)
{
	/* prepare rabin context */
	static RabinCtx rctx;
	static char init;
	if (init == FALSE) {
		TRACE("this should be init only once\n");
		memset(&rctx, 0, sizeof(RabinCtx));
		InitRabinCtx(&rctx, g_MinChunkSize, WAPROX_RABIN_WINDOW,
				WAPROX_MRC_MIN, WAPROX_MRC_MAX);
		init = TRUE;
	}

	/* find rabin boundaries */
	struct timeval _start;
	UpdateCurrentTime(&_start);
	int numNames = 0;
	int rb_size = 0;
	int maxLevel = 0;
	MRCPerLevelInfo* pli = NULL;
	OneChunkName* pNames = DoPreMRC(&rctx, buffer, buf_len, &numNames,
			&rb_size, &maxLevel, &pli);
	assert(numNames > 0);
	assert(rb_size == buf_len);

	/* make tree */
	MakeTree(pli, maxLevel, pNames, numNames);

	/* resolve tree with disk put hint */
	int lowLevel = ResolveWithHint(pNames, numNames, buffer, rb_size,
			pli, maxLevel, FALSE);

	/* put only missing chunks */
	int i;
	int numPutd = 0;
	for (i = 0; i < numNames; i++) {
		if (pNames[i].level > lowLevel) {
			/* we stop here */
			break;
		}

		if (pNames[i].isHit == FALSE) {
			assert(pNames[i].isSha1Ready == TRUE);
			if (GetDiskHint(pNames[i].sha1name) == FALSE) {
				DistributePutRequest(pNames[i].sha1name,
						pNames[i].pChunk,
						pNames[i].length,
						dstPxyId);
				#if 0
				TRACE("put disk %s, %d bytes\n",
						SHA1Str(pNames[i].sha1name),
						pNames[i].length);
				#endif
				PutDiskHint(pNames[i].sha1name,
						pNames[i].length);
			}

			numPutd++;
		}
	}
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("%d/%d chunks put, %d bytes, %d us\n", numPutd, numNames,
			buf_len, GetElapsedUS(&_start));
#endif
}
/*-----------------------------------------------------------------*/
void QueuePutMessage(RabinCtx* rctx, char* buffer, int buf_len, int dstPxyId,
		int seq_no, ReassembleInfo* ri)
{
	/* don't store chunks and return here
	   if we are not going to store chunks */
	if (g_SkipFirstChunk == TRUE && seq_no == 0) {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("skipping the first block, %d bytes\n", buf_len);
#endif
		return;
	}

	/* check root name if we have (it's cheap) */
	if (ri != NULL) {
		ChunkNameInfo* cni = TAILQ_FIRST(&ri->nameList);
		if (cni != NULL && cni->length == buf_len) {
			/* only one root name, just use it */
			/* if root hit, skip we're done */
			if (GetDiskHint(cni->sha1name) == TRUE) {
#ifdef _WAPROX_DEBUG_OUTPUT
				TRACE("skip Rabin! %d bytes\n", buf_len);
#endif
				return;
			}
		}
	}

	#if 0
	/* not root name, let's compute ourselves */
	u_char sha1name[SHA1_LEN];
	Calc_SHA1Sig((const byte*)buffer, buf_len, sha1name);

	/* if root hit, skip we're done */
	if (GetDiskName(sha1name) == TRUE) {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("skip Rabin! %d bytes\n", buf_len);
#endif
		return;
	}
	#endif

	/* if queue too long, discard it: simple drop tail */
	if ((g_CurPutQueueSize + buf_len) > (g_PutQueueSize * 1024 * 1024)) {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("disk put queue full %d MB, discard chunk %d bytes\n",
				g_CurPutQueueSize, buf_len);
#endif
		g_TotalPutDiscarded += buf_len;
		return;
	}

#ifdef _WAPROX_LAZY_PUT
	/* queue message here and process when not busy */
	PutMessageInfo* pmi = malloc(sizeof(PutMessageInfo));
	if (pmi == NULL) {
		NiceExit(-1, "PutMessageInfo malloc failed\n");
	}
	pmi->msg = malloc(buf_len);
	if (pmi->msg == NULL) {
		NiceExit(-1, "message malloc failed\n");
	}
	memcpy(pmi->msg, buffer, buf_len);
	pmi->msg_len = buf_len;
	pmi->dstPxyId = dstPxyId;
	TAILQ_INSERT_TAIL(&g_PutMessageList, pmi, messageList);
	g_NumPutMessages++;
	g_CurPutQueueSize += pmi->msg_len;
	assert(g_CurPutQueueSize <= (g_PutQueueSize * 1024 * 1024));
#else
	ActualPutMissingChunks(buffer, buf_len, dstPxyId); 
#endif
	return;
}
/*-----------------------------------------------------------------*/
#if 0
void PutMissingChunks1(RabinCtx* rctx, char* buffer, int buf_len, int dstPxyId)
{
	struct timeval _start;
	UpdateCurrentTime(&_start);
	int numNames = 0;
	int rb_size = 0;
	int maxLevel = 0;
	MRCPerLevelInfo* pli = NULL;
	OneChunkName* pNames = DoOneMRC(rctx, buffer, buf_len, FALSE,
			&numNames, &rb_size, &maxLevel, &pli);
	assert(numNames > 0);
	assert(rb_size == buf_len);
	int i;
	int numPutd = 0;
	int offset = 0;
	for (i = 0; i < numNames; i++) {
		/* distribute PUT request among peers */
		assert(pNames[i].length <= rb_size);
		CheckNameHint(&pNames[i], FALSE);
		assert(VerifySHA1((const byte*)buffer + offset,
					pNames[i].length, pNames[i].sha1name));

		/* check put hint name table */
		if (GetDiskHint(pNames[i].sha1name) == FALSE) {
			/* store only non-put'd chunk */
			DistributePutRequest(pNames[i].sha1name,
						buffer + offset,
						pNames[i].length, dstPxyId);
			numPutd++;
			PutDiskHint(pNames[i].sha1name, pNames[i].length);
		}

		/* move on the next chunk */
		offset += pNames[i].length;
		if (offset == rb_size) {
			/* end of the whole block, reset */
			offset = 0;
		}
	}
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("%d/%d chunks put, %d bytes, %d us\n", numPutd, numNames,
			buf_len, GetElapsedUS(&_start));
#endif
}
#endif
/*-----------------------------------------------------------------*/
void CopyToMessage(char* buffer, int* buflen, void* msg, int msglen)
{
	assert(msglen > 0);
	memcpy(buffer + *buflen, msg, msglen); 
	*buflen += msglen;
}
/*-----------------------------------------------------------------*/
void CopyFromMessage(void* msg, int msglen, void* buffer, int* offset)
{
	assert(*offset >= 0);
	assert(msglen > 0);
	memcpy(msg, buffer + *offset, msglen);
	*offset += msglen;
}
/*-----------------------------------------------------------------*/
int GetAcutalTreeSize(OneChunkName* pNames, int numNames)
{
	int i;
	int size = 0;
	for (i = 0; i < numNames; i++) {
		/* type */
		size += sizeof(pNames[i].type);

		if (pNames[i].type == chunk_name) {
			/* length, name */
			size += sizeof(pNames[i].length);
			size += SHA1_LEN;
		}
		else if (pNames[i].type == chunk_both) {
			/* length, name, content */
			size += sizeof(pNames[i].length);
			size += SHA1_LEN;
			size += pNames[i].length;
		}
		else if (pNames[i].type == chunk_raw) {
			/* length, content */
			size += sizeof(pNames[i].length);
			size += pNames[i].length;
		}
		else if (pNames[i].type == chunk_index) {
			/* rindex */
			size += sizeof(pNames[i].rindex);
		}
		else {
			assert(FALSE);
		}
	}

	return size;
}
/*-----------------------------------------------------------------*/
#if 0
ChunkTreeInfo* DecodeOneTree2(char* buffer, unsigned int buf_len, int seq,
		int* numConsumed)
{
	WAOneTree* tree = (WAOneTree*)buffer;
	int numNames = ntohl(tree->numNames);
	int numLevels = tree->numLevels;
	assert(numLevels == 1);
	OneChunkName* pNames = malloc(sizeof(OneChunkName) * numNames);
	if (pNames == NULL) {
		NiceExit(-1, "OneChunkName allocation failed\n");
	}

	int j;
	int offset = sizeof(WAOneTree);
	for (j = 0; j < numNames; j++) {
		u_char sha1name[SHA1_LEN];
		int length;
		char isHit;
		char content[MAX_CHUNK_SIZE];
		
		memcpy(sha1name, buffer + offset, SHA1_LEN);
		offset += SHA1_LEN;
		memcpy(&length, buffer + offset, sizeof(int));
		offset += sizeof(int);
		memcpy(&isHit, buffer + offset, sizeof(char));
		offset += sizeof(char);

		TRACE("name: %s, length: %d, hasbody: %d\n",
				SHA1Str(sha1name), length, isHit);
		if (isHit == FALSE) {
			memcpy(content, buffer + offset, length);
			offset += length;
		}
		assert(offset <= buf_len);
	}
	assert(offset <= buf_len);

	#if 0
	ChunkTreeInfo* cti = CreateNewChunkTreeInfo(pNames, numNames, numLevels);
	if (cti == NULL) {
		NiceExit(-1, "ChunkTreeInfo allocation failed\n");
	}
	InitChunkTreeInfo(cti, pNames, numNames, seq);
	#endif

	*numConsumed = offset;
	return NULL;
}
#endif
/*-----------------------------------------------------------------*/
ChunkTreeInfo* DecodeOneTree(char* buffer, unsigned int buf_len, int seq,
		int* numConsumed)
{
	WAOneTree* tree = (WAOneTree*)buffer;
	int numNames = ntohl(tree->numNames);
	int numLevels = tree->numLevels;
	static OneChunkName* pNames = NULL;
	if (pNames == NULL) {
		pNames = malloc(sizeof(OneChunkName) * g_maxNames);
		if (pNames == NULL) {
			NiceExit(-1, "OneChunkName allocation failed\n");
		}
	}

	int j;
	int offset = sizeof(WAOneTree);
	for (j = 0; j < numNames; j++) {
		/* init name */
		InitOneChunk(&(pNames[j]));
		pNames[j].index = j;
		pNames[j].type = chunk_name;

		/* get type first */
		CopyFromMessage(&(pNames[j].type), sizeof(chunkType),
				buffer, &offset);

		if (pNames[j].type == chunk_name) {
			/* get length, name */
			CopyFromMessage(&(pNames[j].length),
					sizeof(pNames[j].length),
					buffer, &offset);
			CopyFromMessage(pNames[j].sha1name, SHA1_LEN,
					buffer, &offset);
			pNames[j].pChunk = NULL;
			pNames[j].freeChunk = FALSE;
			pNames[j].rindex = -1;
		}
		else if (pNames[j].type == chunk_both) {
			/* get length, name, content */
			CopyFromMessage(&(pNames[j].length),
					sizeof(pNames[j].length),
					buffer, &offset);
			CopyFromMessage(pNames[j].sha1name, SHA1_LEN,
					buffer, &offset);
			pNames[j].pChunk = malloc(pNames[j].length);
			if (pNames[j].pChunk == NULL) {
				NiceExit(-1, "chunk alloc failed\n");
			}
			CopyFromMessage(pNames[j].pChunk, pNames[j].length,
					buffer, &offset);
			pNames[j].freeChunk = TRUE;
		}
		else if (pNames[j].type == chunk_raw) {
			/* get length, content */
			CopyFromMessage(&(pNames[j].length),
					sizeof(pNames[j].length),
					buffer, &offset);
			pNames[j].pChunk = malloc(pNames[j].length);
			if (pNames[j].pChunk == NULL) {
				NiceExit(-1, "chunk alloc failed\n");
			}
			CopyFromMessage(pNames[j].pChunk, pNames[j].length,
					buffer, &offset);
			pNames[j].freeChunk = TRUE;
		}
		else if (pNames[j].type == chunk_index) {
			/* get rindex */
			CopyFromMessage(&(pNames[j].rindex),
					sizeof(pNames[j].rindex),
					buffer, &offset);
			assert(j > pNames[j].rindex);
			pNames[j].length = pNames[pNames[j].rindex].length;
		}
		else {
			assert(FALSE);
		}
		assert(offset <= buf_len);
	}
	assert(offset <= buf_len);

	ChunkTreeInfo* cti = CreateNewChunkTreeInfo(numNames, numLevels);
	if (cti == NULL) {
		NiceExit(-1, "ChunkTreeInfo allocation failed\n");
	}
	InitChunkTreeInfo(cti, pNames, numNames, seq);

	*numConsumed = offset;
	return cti;
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
int ResolveWithHint(OneChunkName* pNames, int numNames, char* buffer,
		int buf_len, MRCPerLevelInfo* pli, int maxLevel, char isSender)
{
	/* start from root, check name hint */
	struct timeval _start;
	UpdateCurrentTime(&_start);
	int i;
	int lowLevel = 0;
	int numSha1Compute = 0;
	for (i = 0; i < maxLevel; i++) {
		int numHits = 0;
		int numChunks = pli[i].numNames;
		int newIdx = pli[i].firstIndex;
		int j;
		int offset = 0;
		struct timeval _start2;
		UpdateCurrentTime(&_start2);
		lowLevel = i;
		for (j = 0; j < numChunks; j++) {
			assert(newIdx < numNames);
			int parent = GetParent2(pNames, newIdx);
			OneChunkName* cur = &pNames[newIdx];
			OneChunkName* par = &pNames[parent];
			assert(offset + cur->length <= buf_len);
			assert(offset == cur->offset);

			if (cur->length <= WAPROX_MIN_CHUNK_SIZE) {
				/* regard small chunks as hit,
				   so we never put this in hint */
				CheckNameHint(cur, isSender);
				numSha1Compute++;
				cur->isHit = TRUE;
			}
			else {
#ifdef _WAPROX_NO_SHA1_OPTIMIZE
			CheckNameHint(cur, isSender);
			numSha1Compute++;
#else
			if (newIdx == 0) {
				/* root: compute SHA1 always */
				CheckNameHint(cur, isSender);
				numSha1Compute++;
				assert(VerifySHA1((const byte*)buffer + offset,
							cur->length,
							cur->sha1name));
			}
			else {
				/* non-root */
				if (par->isHit == TRUE) {
					/* parent hit, ignore */
					cur->isHit = TRUE;
#ifdef _WAPROX_DEBUG_RESOLVE
					CheckNameHint(cur, isSender);
					TRACE("pass: %s, parent: %d\n", SHA1Str(cur->sha1name), par->index);
					assert(cur->isHit == TRUE);
#endif
				}
				else {
					/* same as parent, just copy */
					if (par->length == cur->length) {
						cur->isHit = par->isHit;
						assert(par->isSha1Ready
								== TRUE);
						assert(cur->isHit == FALSE);
						memcpy(cur->sha1name,
								par->sha1name,
								SHA1_LEN);
						cur->isSha1Ready = TRUE;
#ifdef _WAPROX_DEBUG_RESOLVE
						CheckNameHint(cur, isSender);
						TRACE("pass: %s\n", SHA1Str(cur->sha1name));
						assert(cur->isHit == FALSE);
#endif
					}
					else {
						/* calculate SHA1 and check */
						CheckNameHint(cur, isSender);
						numSha1Compute++;
					}
					assert(VerifySHA1((const byte*)
								buffer + offset,
								cur->length,
								cur->sha1name));
				}
			}
#endif
			}
			newIdx++;
			offset += cur->length;
			if (cur->isHit == TRUE) {
				numHits++;
			}
		}

#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("level %d, %d names, %d bytes, sha1 %d us\n",
				i, numChunks, buf_len, GetElapsedUS(&_start2));
#endif
		if (numHits == numChunks) {
			/* everything is hit, this level */
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("all hit! we're done now\n");
#endif
			break;
		}
	}
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("up to level %d, resolve %d names, %d SHA1, total %d us\n",
			lowLevel, numNames, numSha1Compute,
			GetElapsedUS(&_start));
#endif
	return lowLevel;
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
CandidateList* GetCandidateList2(OneChunkName* pNames, int numNames,
		MRCPerLevelInfo* pli, int lowLevel)
{
	/* find candidate chunks from bottom to top*/
	assert(lowLevel >= 0);
	struct timeval _start;
	UpdateCurrentTime(&_start);
	int i;
	int numCandidates = 0;
	int base = pli[lowLevel].firstIndex;
	int numChunks = pli[lowLevel].numNames;
	assert(numChunks <= numNames);
	for (i = 0; i < numChunks; i++) {
		/* make leaves as candidates at the beginning */
		/*TRACE("leaves %d\n", base + i);*/
		pNames[base + i].isCandidate = TRUE;
	}

	if (g_NumMRCWays == 1) {
		/* SRC, done */
		assert(lowLevel == 0);
	}
	else {
		/* prepare stack */
		static int* stack = NULL;
		if (stack == NULL) {
			stack = malloc(sizeof(int) * g_maxNames);
			if (stack == NULL) {
				NiceExit(-1, "malloc failed\n");
			}
		}

		/* MRC, check chunks bottom up */
		for (i = lowLevel; i >= 0; i--) {
			int numChunks = pli[i].numNames;
			int base = pli[i].firstIndex;
			int j;
			int sp = 0;
			for (j = 0; j < numChunks; j++) {
				int child = base + j;
				int parent = GetParent2(pNames, child);
				assert(child < numNames);
				assert(parent < numNames);
				assert(parent <= child);

				/* put child in stack */
				stack[sp++] = child;
				assert(sp < g_maxNames);
				/* TRACE("add child %d\n", child); */

				/* check stack */
				if (j == numChunks - 1) {
					/* last chunk, flush */
					FlushChildren(pNames, stack, sp);
					sp = 0;
				}
				else {
					assert((j + 1) <= (numChunks - 1));
					int nextParent = GetParent2(pNames,
							child + 1);
					if (nextParent != parent) {
						/* last child, flush */
						FlushChildren(pNames, stack,
								sp);
						sp = 0;
					}
				}
			}
		}
	}

	/* count number of candidates */
	for (i = 0; i < numNames; i++) {
		if (pNames[i].isCandidate == TRUE) {
			assert(pNames[i].isSha1Ready == TRUE);
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
	for (i = 0; i < numNames; i++) {
		if (pNames[i].isCandidate == TRUE) {
			candiArray[idx++] = &pNames[i];
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
	assert(offset == pNames[0].length);
	free(candiArray);

#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("%d/%d candidate chunks, %d bytes, %d us\n", numCandidates,
			numNames, pNames[0].length, GetElapsedUS(&_start));
#endif
	return candidateList;
}
/*-----------------------------------------------------------------*/
CandidateList* GetCandidateList(OneChunkName* pNames, MRCPerLevelInfo* pli,
		int maxLevel)
{
	CandidateList* chunkList = malloc(sizeof(CandidateList));
	if (chunkList == NULL) {
		NiceExit(-1, "malloc failed\n");
	}
	TAILQ_INIT(chunkList);

	/* start making a candidate list with leaf chunks */
	int numItems = 0;
	int offset = 0;
	int i;
	struct timeval _start;
	UpdateCurrentTime(&_start);
	for (i = 0; i < pli[maxLevel - 1].numNames; i++) {
		OneCandidate* item = malloc(sizeof(OneCandidate));
		if (item == NULL) {
			NiceExit(-1, "malloc failed\n");
		}
		item->index = pli[maxLevel - 1].firstIndex + i;
		item->offset = offset;
		item->length = pNames[item->index].length;
		item->isHit = GetSendHint(pNames[item->index].sha1name);
		memcpy(item->sha1name, pNames[item->index].sha1name, SHA1_LEN);
		offset += item->length;
		TAILQ_INSERT_TAIL(chunkList, item, list);
		numItems++;
	}

	if (g_NumMRCWays == 1) {
		/* SRC, done */
	}
	else {
		/* MRC, check chunks bottom up */
		assert(maxLevel - 2 >= 0);
		for (i = maxLevel - 2; i >= 0; i--) {
			int numChunks = pli[i].numNames;
			int base = pli[i].firstIndex;
			int j;
			int cur_offset = 0;
			int cur_length = 0;
			char isHit = FALSE;
			for (j = 0; j < numChunks; j++) {
				int parent = base + j;
				cur_length = pNames[parent].length;
				isHit = GetSendHint(pNames[parent].sha1name);
				UpdateCandidateList(pNames, chunkList,
						parent, isHit,
						cur_offset, cur_length);

				/* advance offset */
				cur_offset += cur_length;
			}
		}
	}

#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("%d us taken\n", GetElapsedUS(&_start));
#endif
	return chunkList;
}
/*-----------------------------------------------------------------*/
void UpdateCandidateList(OneChunkName* pNames, CandidateList* chunkList,
		int index, char isHit, int offset, int length)
{
	static CandidateList deadList;
	TAILQ_INIT(&deadList);

	/* remove children */
	OneCandidate* walk;
	int numChildren = 0;
	int numMisses = 0;
	int child_length = 0;
	TAILQ_FOREACH(walk, chunkList, list) {
		if ((offset <= walk->offset) && ((walk->offset + walk->length)
				<= (offset + length))) {
			/* child */
			TAILQ_INSERT_TAIL(&deadList, walk, deadList);
			numChildren++;
			child_length += walk->length;
			if (walk->isHit == FALSE) {
				numMisses++;
			}
		}
	}
	assert(length == child_length);

	if (isHit == TRUE || numMisses == numChildren) {
		/* if parent is hit or all children are miss */
		/* first, remove children */
		while (!TAILQ_EMPTY(&deadList)) {
			walk = TAILQ_FIRST(&deadList);
			TAILQ_REMOVE(chunkList, walk, list);
			TAILQ_REMOVE(&deadList, walk, deadList);
			free(walk);
		}

		/* then, insert parent */
		OneCandidate* item = malloc(sizeof(OneCandidate));
		if (item == NULL) {
			NiceExit(-1, "malloc failed\n");
		}
		item->index = index;
		item->offset = offset;
		item->length = length;
		item->isHit = isHit;
		memcpy(item->sha1name, pNames[index].sha1name, SHA1_LEN);

		/* TODO: implement more efficiently */
		char success = FALSE;
		TAILQ_FOREACH(walk, chunkList, list) {
			if ((offset < walk->offset) && walk->offset
					== (offset + length)) {
				/* child */
				TAILQ_INSERT_BEFORE(walk, item, list);
				success = TRUE;
				break;
			}
		}
		if (success == FALSE) {
			TAILQ_INSERT_TAIL(chunkList, item, list);
		}
	}

	#if 0
	int i = 0;
	int offset2 = 0;
	TAILQ_FOREACH(walk, chunkList, list) {
		assert(offset2 == walk->offset);
		TRACE("%d, offset: %d, length: %d, hint: %s\n", i++,
				walk->offset, walk->length,
				walk->isHit ? "HIT" : "MISS");
		offset2 += walk->length;
	}
	#endif
}
/*-----------------------------------------------------------------*/
void PutTempCacheCandidateList(CandidateList* chunkList, char* buffer,
		int buflen)
{
	OneCandidate* walk = NULL;
	int offset = 0;
	TAILQ_FOREACH(walk, chunkList, list) {
		RefPutChunkCallback(walk->sha1name,
				buffer + offset, walk->length);
		offset += walk->length;
		assert(offset <= buflen);
	}
	assert(offset == buflen);
}
/*-----------------------------------------------------------------*/
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
				 <= WAPROX_MIN_CHUNK_SIZE) ? "HAS" : "NONE");
		tot_len += walk->length;
	}
	TRACE("total %d bytes\n", tot_len);
}
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
OneChunkName* DoOneMRC(RabinCtx* rctx, char* psrc, int buf_len,
		char putTempCache, int* outNumNames, int* outRb_size,
		int* outMaxLevel, MRCPerLevelInfo** outPli)
{
	/* find rabin boundaries */
	OneChunkName* pNames = DoPreMRC(rctx, psrc, buf_len, outNumNames,
			outRb_size, outMaxLevel, outPli);
	assert(*outRb_size <= buf_len);

	/* calc SHA1 of all chunks for now */
	struct timeval _start;
	UpdateCurrentTime(&_start);
	MRCCalcSHA1(pNames, *outNumNames, putTempCache);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("SHA1 computation %d names, %d us\n", *outNumNames,
			GetElapsedUS(&_start));
#endif

	/* set return values */
	return pNames;
}
/*-----------------------------------------------------------------*/
void MRCCalcSHA1(OneChunkName* pNames, int numNames, char putTempCache)
{
	int i;
	for (i = 0; i < numNames; i++) {
		char* buffer = pNames[i].pChunk;
		assert(buffer != NULL);
		int length = pNames[i].length;
		assert(length > 0);
		Calc_SHA1Sig((const byte*)buffer, length, pNames[i].sha1name);
		if (putTempCache) {
			RefPutChunkCallback(pNames[i].sha1name,
					buffer, length);
		}
	}
}
/*-----------------------------------------------------------------*/
OneChunkName* DoPreMRC(RabinCtx* rctx, char* psrc, int buf_len,
		int* outNumNames, int* outRb_size, int* outMaxLevel,
		MRCPerLevelInfo** outPli)
{
	if (WAPROX_MIN_CHUNK_SIZE <= WAPROX_RABIN_WINDOW) {
		NiceExit(-1, "min chunk size should be larger than"
				" rabin window size\n");
	}

	int rabin_buffer_len = MIN(buf_len, MAX_CHUNK_SIZE);

	/* prepare MRCPerLevelInfo */
	static MRCPerLevelInfo* pli = NULL;
	if (pli == NULL) {
		pli = malloc(sizeof(MRCPerLevelInfo) * g_maxLevel);
		if (pli == NULL) {
			NiceExit(-1, "malloc failed\n");
		}
	}

	/* prepare RabinBoundary */
	static RabinBoundary* rabinBoundaries = NULL;
	unsigned int numBoundaries = 0;
	if (rabinBoundaries == NULL) {
		rabinBoundaries = malloc(sizeof(RabinBoundary) * g_ChunkSize);
		if (rabinBoundaries == NULL) {
			NiceExit(-1, "RabinBoundary alloc failed\n");
		}
	}

	/* detect all the RabinBoundaries */
	struct timeval _start;
	UpdateCurrentTime(&_start);
#if 0
	TRACE("init fingerprint: %llu, pos: %d\n", rctx->finger,
			rctx->rp.region_pos);
#endif
	static char init = FALSE;
	u_int min_mask = 0;
	u_int max_mask = 0;
	if (init == FALSE) {
		min_mask = GetMRCMask(g_MinChunkSize);
		max_mask = GetMRCMask(g_ChunkSize);
	}

	int rb_size = GetRabinChunkSizeEx(rctx, (const u_char*)psrc,
			rabin_buffer_len, min_mask, max_mask, TRUE,
			rabinBoundaries, &numBoundaries);
	assert(rb_size > 0);
	assert(rb_size <= rabin_buffer_len);
	assert(numBoundaries > 0);
	assert(rabinBoundaries[numBoundaries - 1].offset == rb_size);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("rabin fingerprint %d/%d bytes, finger: %llu, %d us\n", rb_size,
			buf_len, rabinBoundaries[numBoundaries - 1].finger,
			GetElapsedUS(&_start));
#endif
	
	/* prepare chunk info holder */
	static OneChunkName* pNames = NULL;
	if (pNames == NULL) {
		pNames = calloc(g_maxNames, sizeof(OneChunkName));
		if (pNames == NULL) {
			NiceExit(-1, "MRC tree allocation failed\n");
		}
	}

	/* prepare real boundary index */
	static char* pRealBoundary = NULL;
	if (pRealBoundary == NULL) {
		pRealBoundary = calloc(g_maxNames, sizeof(char));
		if (pRealBoundary == NULL) {
			NiceExit(-1, "Real boundary allocation failed\n");
		}
	}

	/* init real boundary */
	memset(pRealBoundary, FALSE, sizeof(char) * g_maxNames);
	pRealBoundary[numBoundaries - 1] = TRUE;

	/* fill the chunk info */
	int chunkSize = g_ChunkSize;
	int index = 0;
	int numNames = 0;
	int level = 0;
	while (chunkSize >= g_MinChunkSize) {
		int lastOffset = 0;
		int length = 0;
		int i;

		UpdateCurrentTime(&_start);
		/* init per level info */
		pli[level].numNames = 0;
		pli[level].firstIndex = index;
		char* buffer = psrc;
		for (i = 0; i < numBoundaries; i++) {
			/* if mask matches or the last chunk */
			if (((rabinBoundaries[i].finger & (chunkSize - 1))
						== (chunkSize - 1)) ||
					((i + 1) == numBoundaries)) {
				length = rabinBoundaries[i].offset - lastOffset;

				if (length <= WAPROX_MIN_CHUNK_SIZE
						&& pRealBoundary[i] == FALSE) {
					/* move on if small and not bounded
					   by real boundaries */
					continue;
				}

				if (level == 0) {
					assert(length == rb_size);
				}

				/* fill the chunk info
				   without calculating expensive SHA1 hash */
				InitOneChunk(&(pNames[index]));
				assert(length > 0);
				pNames[index].length = length;
				pNames[index].index = index;
				pNames[index].level = level;
				pNames[index].type = chunk_name;
				pNames[index].pChunk = buffer;
				pNames[index].offset = lastOffset;

				/* set the new boundary */
				pRealBoundary[i] = TRUE;

				/* move to the next position */
				buffer += length;
				numNames++;
				index++;
				lastOffset = rabinBoundaries[i].offset;

				/* update per level info */
				pli[level].numNames++;
			}
		}

#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("level: %d, %d names, %d us\n", level,
				pli[level].numNames, GetElapsedUS(&_start));
#endif
		level++;
		if (g_NumMRCWays == 1) {
			/* SRC, we stop here */
			break;
		}
		else {
			/* MRC, go down to the smaller chunk size */
			chunkSize /= g_NumMRCWays;
		}
	}
	assert(numNames > 0);

	/* set return values */
	*outNumNames = numNames;
	*outRb_size = rb_size;
	*outMaxLevel = level;
	*outPli = pli;
	assert(*outMaxLevel >= 0);
	return pNames;
}
/*-----------------------------------------------------------------*/
void MakeTree(MRCPerLevelInfo* levInfo, int maxLevel, OneChunkName* pNames,
		int numNames)
{
	struct timeval _start;
	UpdateCurrentTime(&_start);
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
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("tree build for %d names, %d us\n", numNames,
			GetElapsedUS(&_start));
#endif
}
/*-----------------------------------------------------------------*/
void CheckNameHint(OneChunkName* cur, char isSender)
{
	Calc_SHA1Sig((const byte*)cur->pChunk, cur->length, cur->sha1name);
	cur->isSha1Ready = TRUE;

	/* check name hint table */
	if (isSender) {
		/* name hint */
		if (GetSendHint(cur->sha1name) == TRUE) {
			/* we put this chunk before */
			cur->isHit = TRUE;
		}
		else {
			/* we put this chunk name after ACKed */
			#if 0
			PutSendHint(cur->sha1name, cur->length);
			#endif
			cur->isHit = FALSE;
		}
	}
	else {
		/* put disk hint */
		if (GetDiskHint(cur->sha1name) == TRUE) {
			/* we put this chunk before */
			cur->isHit = TRUE;
		}
		else {
			/* we put this chunk name when we actually PUT */
			#if 0
			PutDiskHint(cur->sha1name, cur->length);
			#endif
			cur->isHit = FALSE;
		}
	}

#ifdef _WAPROX_DEBUG_RESOLVE
	TRACE("idx: %d, %s, %d bytes, %s, parent: %d\n", cur->index,
			SHA1Str(cur->sha1name), cur->length,
			cur->isHit ? "HIT" : "MISS", cur->parentIdx);
#endif
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
