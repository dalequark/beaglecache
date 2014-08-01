#include "reassemble.h"
#include "connection.h"
#include <assert.h>
#include "config.h"
#include "disk.h"
#include "waprox.h"
#include "peer.h"
#include <math.h>

/*-----------------------------------------------------------------*/
ReassembleInfo* CreateNewReassembleInfo(ConnectionInfo* source_ci,
		unsigned int seq_no, ChunkTreeInfo* cti)
{
	ReassembleInfo* ri = malloc(sizeof(ReassembleInfo));
	if (ri == NULL) {
		NiceExit(-1, "ReassembleInfo allocation failed\n");
	}
	assert(source_ci != NULL);
	assert(source_ci->type == ct_user);
	UserConnectionInfo* uci = (UserConnectionInfo*)source_ci->stub;
	assert(uci != NULL);
	ri->source_ci = source_ci;
	ri->cti = cti;
#ifdef _WAPROX_FULL_MRC_TREE
	assert(ri->cti != NULL);
#else
	assert(ri->cti == NULL);
#endif
	ri->seq_no = seq_no;
	ri->numNames = 0;
	ri->numPeekResolved = 0;
	ri->getSent = FALSE;
	TAILQ_INIT(&ri->nameList);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("seq: %d just created\n", ri->seq_no);
#endif

	/* append this to the list */
	TAILQ_INSERT_TAIL(&(uci->reassembleList), ri, reassembleList);
	return ri;
}
/*-----------------------------------------------------------------*/
ReassembleInfo* GetReassembleInfo(ReassembleList* rl, unsigned int seq_no)
{
	ReassembleInfo* ri = NULL;
	TAILQ_FOREACH(ri, rl, reassembleList) {
		if (ri->seq_no == seq_no) {
			break;
		}
	}
	return ri;
}
/*-----------------------------------------------------------------*/
void DestroyReassembleList(ReassembleList* rl)
{
	while (!TAILQ_EMPTY(rl)) {
		ReassembleInfo* ri = TAILQ_FIRST(rl);
		TAILQ_REMOVE(rl, ri, reassembleList);

		/* destroy the chunk name list */
		assert(ri != NULL);
		DestroyReassembleInfo(ri);
	}
}
/*-----------------------------------------------------------------*/
void DestroyReassembleInfo(ReassembleInfo* ri)
{
	/* destroy the chunk name list */
	assert(ri != NULL);
	assert(TAILQ_EMPTY(&(ri->nameList)) == FALSE);
	while (!TAILQ_EMPTY(&(ri->nameList))) {
		ChunkNameInfo* cni = TAILQ_FIRST(&(ri->nameList));
		assert(cni != NULL);
		TAILQ_REMOVE(&(ri->nameList), cni, nameList);
		free(cni);
	}

#ifdef _WAPROX_FULL_MRC_TREE
	assert(ri->cti != NULL);
#endif
	if (ri->cti != NULL) {
		/* destroy chunk tree info */
		DestroyChunkTreeInfo(ri->cti);
	}

	free(ri);
}
/*-----------------------------------------------------------------*/
void ReconstructOriginalData(ConnectionInfo* target_ci)
{
	struct timeval _start;
	UpdateCurrentTime(&_start);
	assert(target_ci->type == ct_user);
	UserConnectionInfo* uci = target_ci->stub;
	assert(uci != NULL);
	int num_processed = 0;
	while (!TAILQ_EMPTY(&(uci->reassembleList))) {
		ReassembleInfo* ri = TAILQ_FIRST(&(uci->reassembleList));
		if (ReconstructOneChunkTree(target_ci, ri)) {
			TAILQ_REMOVE(&(uci->reassembleList), ri,
					reassembleList);
			DestroyReassembleInfo(ri);
			num_processed++;
		}
		else {
			break;
		}
	}

	if (uci->closeBytes != 0 && uci->bytesRX == uci->closeBytes) {
		/* there's no outstanding data */
		TRACE("[%d] close here:%d, fd=%d\n", target_ci->connID,
				uci->bytesRX, target_ci->fd);
		SetDead(target_ci, FALSE);
		#if 0
		DestroyConnectionInfo(target_ci);
		#endif
	}
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("%d RI processed, %d us taken\n", num_processed,
			GetElapsedUS(&_start));
#endif
}
/*-----------------------------------------------------------------*/
int CheckOneChunkTree(ReassembleInfo* ri)
{
	struct timeval _start;
	UpdateCurrentTime(&_start);
	ChunkNameInfo* walk;
	CacheValue* value = NULL;
	char complete = TRUE;
	int numChecked = 0;
	int buf_len = 0;
	TAILQ_FOREACH(walk, &(ri->nameList), nameList) {
		value = SearchCache(walk->sha1name);
		if (value == NULL) {
			complete = FALSE;
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("[%d] waiting %s \n", ri->source_ci->connID,
					SHA1Str(walk->sha1name));
#endif
			break;
		}
		else {
			/* decrement ref_count for synchronization */
			value->ref_count--;
			buf_len += value->block_length;
			numChecked++;
			if (value->ref_count < 0) {
				TRACE("duplicate chunks in MRC: %s\n",
						SHA1Str(walk->sha1name));
				complete = FALSE;
				break;
			}
		}
	}

	if (numChecked > 0) {
		/* recover ref_count */
		TAILQ_FOREACH(walk, &(ri->nameList), nameList) {
			if (numChecked == 0) {
				break;
			}

			value = SearchCache(walk->sha1name);
			assert(value != NULL);
			value->ref_count++;
			assert(value->ref_count >= 0);
			numChecked--;
		}
	}

#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("checking %d us taken\n", GetElapsedUS(&_start));
#endif
	assert(numChecked == 0);
	return complete ? buf_len : 0;
}
/*-----------------------------------------------------------------*/
int ReconstructOneChunkTree(ConnectionInfo* target_ci, ReassembleInfo* ri)
{
	struct timeval _start;
	UpdateCurrentTime(&_start);
	UserConnectionInfo* uci = target_ci->stub;
	assert(uci != NULL);
	assert(ri->source_ci != NULL);
	assert(ri->source_ci == target_ci);

#ifndef _WAPROX_FULL_MRC_TREE
#ifndef _WAPROX_FAST_RECONSTRUCT
	if (ri->getSent == FALSE) {
		TRACE("resolve not complete, seq: %d\n", ri->seq_no);
		return FALSE;
	}
#endif
#endif

	if (TAILQ_EMPTY(&(ri->nameList))) {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("empty, seq: %d\n", ri->seq_no);
#endif
		return FALSE;
	}

	/* 1. check if we have any complete MRC or not */
	/* TODO: possibly cause HOL blocking especially for MRC! */
	ChunkNameInfo* walk;
	CacheValue* value = NULL;
	int buf_len = CheckOneChunkTree(ri);
	if (buf_len == 0) {
		return FALSE;
	}

	/* 2. flush a complete MRC */
	assert(buf_len <= MAX_CHUNK_SIZE);
	char* chunkBuffer = NULL;
	if (chunkBuffer == NULL) {
		chunkBuffer = malloc(buf_len);
		if (chunkBuffer == NULL) {
			NiceExit(-1, "chunk buffer allocation failed\n");
		}
	}

	int offset = 0;
	TAILQ_FOREACH(walk, &(ri->nameList), nameList) {
		assert(walk->ri == ri);
		value = SearchCache(walk->sha1name);
		assert(value != NULL);
		assert(value->block_length == walk->length);
		/* send the data to the original destination */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] %d bytes reconstructed, %s\n",
				target_ci->connID, value->block_length,
				SHA1Str(walk->sha1name));
#endif
		assert(VerifySHA1((const byte*)value->pBlock,
				walk->length, walk->sha1name));

		/* copy the chunk in buffer */
		memcpy(chunkBuffer + offset, value->pBlock,
				value->block_length);
		offset += value->block_length;

		/* NOTE: don't free walk here,
		   just put it into message queue.
		   freed after actually sent */

		/* update name info */
		uci->pendingNames--;
		uci->pendingBytes -= value->block_length;
		assert(uci->pendingNames >= 0);
		assert(uci->pendingBytes >= 0);

		/* flush cache here */
		RefFlushChunk(walk->sha1name);
	}
	assert(offset == buf_len);

	/* 3. put MRC chunks in the cache for the future access */
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("flush seq %d, %d bytes\n", ri->seq_no, buf_len);
#endif
	if (!g_NoCache && ((g_ClientChunking && !uci->isForward) ||
			(g_ServerChunking && uci->isForward))) {
#ifdef _WAPROX_FULL_MRC_TREE
		if (g_MemCacheOnly) {
			PutMissingChunks(ri->cti, chunkBuffer, buf_len,
					PutMemory);
		}
		else {
			/* TODO: still use memory */
			PutMissingChunks(ri->cti, chunkBuffer, buf_len,
					PutDisk);
		}
#else
		QueuePutMessage(&(uci->rctx4put), chunkBuffer, buf_len,
				uci->dstPxyIdx, uci->seq_no, ri);
#endif
	}

#ifdef _WAPROX_INTEGRITY_CHECK
	if (g_MemCacheOnly) {
		TAILQ_FOREACH(walk, &(ri->nameList), nameList) {
			assert(walk->ri == ri);

			/* check only mine */
			if (walk->peerIdx != g_LocalInfo->idx
					|| walk->peerIdx != uci->dstPxyIdx) {
				continue;
			}

			if (GetMemory(walk->sha1name) == NULL) {
				TRACE("%s missing\n", SHA1Str(walk->sha1name));
				NiceExit(-1, "debug exit\n");
			}
		}
	}
#endif
	/* 4. send the complete chunk block */
	SendData(target_ci, chunkBuffer, buf_len);

	TRACE("%d bytes, %d us taken\n", buf_len, GetElapsedUS(&_start));

	return TRUE;
}
/*-----------------------------------------------------------------*/
ChunkNameInfo* AppendChunkName(ConnectionInfo* ci, ReassembleInfo* ri,
		u_char* sha1name, unsigned int length, char isHit)
{
	assert(length > 0);
	assert(ci->type == ct_user);
	ChunkNameInfo* cni = (ChunkNameInfo*)malloc(sizeof(ChunkNameInfo));
	if (cni == NULL) {
		NiceExit(-1, "ChunkNameInfo memory allocation failed\n");
	}

	cni->length = length;
	cni->isHit = isHit;
	cni->inMemory = FALSE;
	cni->peerIdx = -1;
	cni->ri = ri;
	cni->isNetwork = FALSE;
	UserConnectionInfo* uci = ci->stub;
	memcpy(cni->sha1name, sha1name, SHA1_LEN);
	uci->pendingNames++;
	uci->pendingBytes += length;

	/* append the name in the list (in order) */
	TAILQ_INSERT_TAIL(&(ri->nameList), cni, nameList);
	return cni;
}
/*-----------------------------------------------------------------*/
