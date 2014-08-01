#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <event.h>
#include <assert.h>
#include <openssl/sha.h>
#include <arpa/inet.h>
#include <math.h>
#include <signal.h>
#include "util.h"
#include "rabinfinger2.h"
#include "hashtable.h"
#include "chunkcache.h"
#include "config.h"
#include "peer.h"
#include "pptpwaprox.h"
#include "log.h"
#include "waprox.h"
#include "disk.h"
#include "chunkrequest.h"
#include "ils.h"
#include "dnshelper.h"
#include "diskhelper.h"
#include "httphelper.h"
#ifdef _WAPROX_USE_SCTP
	#include <netinet/sctp.h>
	#include "sctpglue.h"
#endif

/* statistics */
unsigned int g_BytesUserTX = 0;
unsigned int g_BytesUserRX = 0;
unsigned int g_BytesPeerTX = 0;
unsigned int g_BytesPeerRX = 0;
unsigned int g_BytesPptpTX = 0;
unsigned int g_BytesPptpRX = 0;
unsigned int g_BytesCacheTX = 0;
unsigned int g_BytesCacheRX = 0;
unsigned int g_BytesDnsTX = 0;
unsigned int g_BytesDnsRX = 0;
unsigned int g_BytesBlockTX = 0;
unsigned int g_BytesBlockRX = 0;
unsigned int g_Hits = 0;
unsigned int g_Misses = 0;
unsigned int g_RareMisses = 0;
unsigned int g_MissRedirected = 0;
unsigned int g_NumChunkRequests = 0;
unsigned int g_NumTotUserConns = 0;

/* debug */
unsigned int g_NumBlocked = 0;
unsigned int g_NumCacheRequestSent = 0;
unsigned int g_NumCacheResponseReceived = 0;
unsigned int g_NumChunkRequestSent = 0;
unsigned int g_NumChunkResponseReceived = 0;
unsigned int g_NumPeekRequestSent = 0;
unsigned int g_NumPeekResponseReceived = 0;
unsigned int g_NumPutRequestSent = 0;
unsigned int g_NumPutResponseReceived = 0;
unsigned int g_NumPptpRequestSent = 0;
unsigned int g_NumPptpResponseReceived = 0;
unsigned int g_NumPutTempHits = 0;

/* performance debug */
unsigned int g_PdNumNameTrees = 0;
unsigned int g_PdPeakNumNameTrees = 0;
unsigned int g_PdNumChunkRequests = 0;
unsigned int g_PdPeakNumChunkRequests = 0;
unsigned int g_PdNumFirstRequests = 0;
unsigned int g_PdPeakNumFirstRequests = 0;
unsigned int g_PdNumSecondRequests = 0;
unsigned int g_PdPeakNumSecondRequests = 0;
/* TODO: measure disk cache average response time */

/* listening sockets */
int g_acceptSocketUser = -1;
int g_acceptSocketName = -1;
int g_acceptSocketChunk = -1;
int g_acceptSocketPing = -1;
int g_acceptSocketHttp = -1;

/* event flag */
static int g_NumReadCI;
static int g_NumWriteCI;
static eventState g_SetAcceptUser;
static eventState g_SetAcceptName;
static eventState g_SetAcceptChunk;
static eventState g_SetPing;
static eventState g_SetAcceptHttp;
static struct timeval g_LastTimer;
static struct timeval g_LastBufferTimeout;
static struct timeval g_LastDebugTimeout;
static struct timeval g_StartTime;

/* some globals */
char g_TryReconstruction = FALSE;
char g_NewPeekUpdate = FALSE;
char isSWaprox = FALSE;

#ifdef _WAPROX_RESPONSE_FIFO
/* for FIFO chunk response */
ChunkResponseList g_ChunkResponseList;
static int g_NumChunkResponses;
#endif

/*-----------------------------------------------------------------*/
void CheckPeek(ReassembleInfo* ri)
{
	assert(ri != NULL);
	if (ri->numPeekResolved == ri->numNames) {
		/* resolve complte */
		assert(ri->source_ci != NULL);
		assert(ri->source_ci->type == ct_user);
		UserConnectionInfo* uci = ri->source_ci->stub;
#ifdef _WAPROX_REASSEMBLE_OPTIMIZE
		TAILQ_INSERT_TAIL(&uci->readyList, ri, readyList);
#endif

#ifdef _WAPROX_FAST_RECONSTRUCT
		ReconstructOriginalData(ri->source_ci);
#endif
	}
}
/*-----------------------------------------------------------------*/
void DistributePutRequest(u_char* sha1name, char* block, int length,
		int dstPxyId)
{
	/* PUT it into cache */
	int idx = PickPeerToRedirect(sha1name);
	if (idx == -1) {
		NiceExit(-1, "no live peer\n");
	}

	if (g_PeerInfo[idx].isLocalHost == TRUE || idx == dstPxyId) {
		/* local */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("local put %s to %s (HRW peer %s)\n", SHA1Str(sha1name),
				g_LocalInfo->name, g_PeerInfo[idx].name);
#endif
		if (g_MemCacheOnly == TRUE) {
			PutMemory(sha1name, block, length);
		}
		else {
			PutDisk(sha1name, block, length);
		}
	}
	else {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("remote put %s to %s\n", SHA1Str(sha1name),
				g_PeerInfo[idx].name);
#endif
		char* request = NULL;
		unsigned int req_len = 0;
#if 0
		/* remote: send peek request first */
		BuildWAPeekRequest(&request, &req_len,
				sha1name);
		g_NumPeekRequestSent++;
#endif
		/* remote: just send put request without peeking 
		   peers are in the same LAN! */
		BuildWAPutRequest(&request, &req_len, sha1name, block, length);
		g_NumPutRequestSent++;

		/* use streamID 0. doesn't matter if we are not using SCTP */
		WriteToChunkPeer(idx, request, req_len, 0);
			
		/* keep this chunk in temp mem cache
		   until remote PUT operation finishes */
		if (GetTempDiskCache(sha1name) == NULL) {
			PutTempDiskCache(sha1name, block, length);
		}
	}
}
/*-----------------------------------------------------------------*/
void UpdatePeekResult(ReassembleInfo* ri, ChunkNameInfo* cni, char isHit,
		char hasContent)
{
	assert(ri != NULL);
	assert(ri->source_ci != NULL);
	assert(ri->source_ci->type == ct_user);
	UserConnectionInfo* uci = (UserConnectionInfo*)(ri->source_ci->stub);
	cni->isHit = isHit;
	if (cni->isHit == TRUE && hasContent == FALSE) {
		g_Hits++;
		uci->numHits++;
		uci->hitBytes += cni->length;
	}
	else {
		uci->numMisses++;
		g_Misses++;
		uci->missBytes += cni->length;
	}

	ri->numPeekResolved++;
	assert(ri->numPeekResolved <= ri->numNames);
	g_NewPeekUpdate = TRUE;
}
/*-----------------------------------------------------------------*/
#if 0
void LastProcessNameTreeDelivery2(ReassembleInfo* ri)
{
	/* update stats */
	assert(ri != NULL);
	assert(ri->source_ci != NULL);
	assert(ri->source_ci->type == ct_user);
	UserConnectionInfo* uci = (UserConnectionInfo*)(ri->source_ci->stub);
	TRACE("[%d] %d resolve complete. send chunk request if needed\n",
			ri->source_ci->connID, ri->seq_no);
	assert(uci != NULL);
	assert(ri->getSent == FALSE);
	ChunkNameInfo* cni;
	int numChunks = 0;
	TAILQ_FOREACH(cni, &(ri->nameList), nameList) {
		numChunks++;
		if (cni->inMemory == TRUE) {
			/* already in temp mem cache */
			assert(SearchCache(cni->sha1name) != NULL);
			continue;
		}

		assert(cni->peerIdx != -1);
		if (cni->isHit == TRUE) {
			/* HIT: consider mem/disk case separately */
			if (g_PeerInfo[cni->peerIdx].isLocalHost == TRUE
					|| cni->peerIdx == uci->dstPxyIdx) {
				if (g_MemCacheOnly == TRUE) {
					/* memory hit */
					CacheValue* pValue = GetMemory(cni->sha1name);
					if (pValue == NULL) {
						TRACE("rare miss: %s\n",
								SHA1Str(cni->sha1name));
						NiceExit(-1, "rare miss?\n");
						SendRemoteChunkRequest(cni,
								uci->dstPxyIdx);
					}
					else {
						/* hit: load chunk to temp mem cache */
						RefPutChunkCallback(cni->sha1name, pValue->pBlock, pValue->block_length);
					}
				}
				else {
					/* disk hit */
					SendCacheRequest(hc_get, ps_nameTreeDelivery,
							cni->sha1name, cni->length,
							NULL, ri->source_ci, NULL, 0,
							NULL, 0);
				}
			}
			else {
				/* remote get */
				SendRemoteChunkRequest(cni, uci->dstPxyIdx);
			}
		}
		else {
			/* MISS: directly send it to the original sender */
			SendRemoteChunkRequest(cni, uci->dstPxyIdx);
		}
	}

	/* set flag */
	ri->getSent = TRUE;
#ifdef _WAPROX_FAST_RECONSTRUCT
	ReconstructOriginalData(ri->source_ci);
#endif
	g_TryReconstruction = TRUE;
	assert(numChunks == ri->numNames);
	TRACE("%d chunks processed\n", numChunks);
}
#endif
/*-----------------------------------------------------------------*/
void LastProcessNameTreeDelivery(ConnectionInfo* ci, ChunkTreeInfo* cti)
{
	struct timeval _start;
	UpdateCurrentTime(&_start);

	/* update stats */
	UserConnectionInfo* uci = ci->stub;
	assert(uci != NULL);
	g_Hits += cti->numLocalHits;
	uci->numHits += cti->numLocalHits;
	uci->hitBytes += cti->bytesHits;
	uci->numMisses += cti->numMissingChunks;
	g_Misses += cti->numMissingChunks;
	uci->missBytes += cti->bytesMissingChunks;

	/* if all done for this chunk tree, make name list */
	OneChunkName* names = cti->pNames;
	int numEntries = 0;
	int* resultArray = MissingChunkPolicy(cti, &numEntries);
	int i;
	int totBytes = 0;
	assert(numEntries <= g_maxNames);
	
	/* find the correct ReassembleInfo */
	ReassembleInfo* ri = GetReassembleInfo(&uci->reassembleList,
			cti->seq_no);
	assert(ri != NULL);
	assert(ri->cti == cti);

	for (i = 0; i < numEntries; i++) {
		int chunkIdx = resultArray[i];
		if (names[chunkIdx].length == 0) {
			continue;
		}

		/* append the chunk name to the list */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] append chunk index: %d, %s, %d bytes to seq: %d\n",
				ci->connID, chunkIdx,
				SHA1Str(names[chunkIdx].sha1name),
				names[chunkIdx].length, ri->cti->seq_no);
#endif
		AppendChunkName(ci, ri, names[chunkIdx].sha1name,
				names[chunkIdx].length,
				cti->chunkState[chunkIdx]);
		totBytes += names[chunkIdx].length;

		if (cti->chunkState[chunkIdx] == TRUE) {
			/* cache hit: we don't send request  */
			assert(SearchCache(names[chunkIdx].sha1name) != NULL);
			g_LocalInfo->numRedirected++;
			g_LocalInfo->bytesRedirected += names[chunkIdx].length;
			continue;
		}

#if 0
		if (GetChunkRequestList(names[chunkIdx].sha1name) != NULL) {
			/* this request is already in-progress! */
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("alredy requested!\n");
#endif
			continue;
		}
#endif

		/* build chunk body request */
		char* request = NULL;
		unsigned int req_len = 0;
		BuildWABodyRequest(g_PeerInfo[uci->dstPxyIdx].netAddr,
				&request, &req_len,
				names[chunkIdx].sha1name,
				names[chunkIdx].length);
		g_NumChunkRequestSent++;

		/* pick the target server by R-HRW,
		   redirect this request to other peers */
		int idx = PickPeerToRedirect(names[chunkIdx].sha1name);
		if (idx == -1) {
			NiceExit(-1, "no live peer\n");
		}
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] redirect chunk request %s to %s, dest pxy: %s\n",
				ci->connID, SHA1Str(names[chunkIdx].sha1name),
				g_PeerInfo[idx].name,
				g_PeerInfo[uci->dstPxyIdx].name);
#endif
		g_PeerInfo[idx].numRedirected++;
		g_PeerInfo[idx].bytesRedirected += names[chunkIdx].length;
		WriteToChunkPeer(idx, request, req_len, ci->fd);

		/* track the request in-flight */
		ConnectionID cid;
		cid.srcIP = uci->dip;
		cid.srcPort = uci->dport;
		cid.dstIP = uci->sip;
		cid.dstPort = uci->sport;

		if (idx != uci->dstPxyIdx) {
			/* this is truly redirected one.
			   we set it here not to put it into cache later */
			names[chunkIdx].fromPeer = TRUE;
		}
		else {
			names[chunkIdx].fromPeer = FALSE;
		}

		/* original chunk request: the proxy address should be
		   the original destination proxy address */
		PutChunkRequest(names[chunkIdx].sha1name, cid, FALSE,
				g_PeerInfo[uci->dstPxyIdx].netAddr,
				names[chunkIdx].length, uci->dstPxyIdx, FALSE);
	}

	/* tot length should be the same as root chunk's length */
	assert(totBytes == names[0].length);
	assert(totBytes <= MAX_CHUNK_SIZE);

	/* send block */
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("%d appended, %d us\n", numEntries, GetElapsedUS(&_start));
#endif

#ifdef _WAPROX_FAST_RECONSTRUCT
	ReconstructOriginalData(ci);
#endif
	g_TryReconstruction = TRUE;
}
/*-----------------------------------------------------------------*/
void PostProcessNameTreeDelivery(CacheTransactionInfo* cci, char isHit,
		char* block, int block_length)
{
	assert(g_MemCacheOnly == FALSE);
	ConnectionInfo* ci = cci->sourceCI;
	assert(ci != NULL);
	assert(ci->type == ct_user);
	UserConnectionInfo* uci = ci->stub;
	assert(uci != NULL);
	ChunkTreeInfo* cti = cci->pCTInfo;

	/* update the result hit/miss */
	UpdateLookupResult(cti, isHit, cci->index);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("got response, seq=%d, index=%d, level=%d, remain=%d, "
			"missing=%d\n", cti->seq_no, cci->index,
			cti->curResolvingLevel, cti->numChunkUnresolved,
			cti->numMissingChunks);
#endif
	if (isHit == TRUE) {
		/* cache hit: put it in memory cache for reconstruction */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("hit!\n");
#endif
		assert(block_length > 0 && block != NULL);
		RefPutChunkCallback(cci->sha1name, block, block_length);
	}
	else {
		/* cache miss */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("miss!\n");
#endif
	}

	if (cti->numChunkUnresolved > 0) {
		/* still have to wait more results */
		return;
	}

	/* if all done for the current level, start the next level */
	assert(cti->numChunkUnresolved == 0);
	if (cti->numMissingChunks == 0) {
		/* all found hit! don't go down further */
		assert(cti->bytesMissingChunks == 0);
	}
	else {
		/* still missing some chunks */
		assert(cti->bytesMissingChunks > 0);
		while (cti->curResolvingLevel + 1 < cti->numLevels) {
			/* let's go down if we are not leaf yet */
			cti->curResolvingLevel++;
			if (!ResolveOneLevelDisk(cti, cti->curResolvingLevel,
						ci)) {
				/* we should wait the reply */
#ifdef _WAPROX_DEBUG_OUTPUT
				TRACE("resolving level %d...\n",
						cti->curResolvingLevel);
#endif
				return;
			}
		}
		
		/* we're at leaf */
	}

	/* all done for this chunk tree */
	assert(cti->numChunkUnresolved == 0);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("one chunk resolution complete!\n");
#endif
	LastProcessNameTreeDelivery(ci, cti);
}
/*-----------------------------------------------------------------*/
void PostProcessNameTreeDelivery2(CacheTransactionInfo* cci, char isHit,
		char* block, int block_length)
{
	assert(g_MemCacheOnly == FALSE);
	ConnectionInfo* ci = cci->sourceCI;
	assert(ci != NULL);
	assert(ci->type == ct_user);
	UserConnectionInfo* uci = ci->stub;
	assert(uci != NULL);

	if (isHit == TRUE) {
		/* cache hit: put it in memory cache for reconstruction */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("hit!\n");
#endif
		assert(block_length > 0 && block != NULL);
		assert(block_length == cci->chunkLen);
		RefPutChunkCallback(cci->sha1name, block, block_length);
#if _WAPROX_FAST_RECONSTRUCT
		ReconstructOriginalData(ci);
#endif
		g_TryReconstruction = TRUE;
		g_LocalInfo->numRedirected++;
		g_LocalInfo->bytesRedirected += cci->chunkLen;
	}
	else {
		/* cache miss: ask the original sender */
		g_RareMisses++;
		TRACE("very rare miss! %s %d bytes, total: %d\n",
				SHA1Str(cci->sha1name), block_length,
				g_RareMisses);

		/* send chunk request to the original sender */
		/* build chunk body request */
		char* request = NULL;
		unsigned int req_len = 0;
		BuildWABodyRequest(g_PeerInfo[uci->dstPxyIdx].netAddr,
				&request, &req_len,
				cci->sha1name, cci->chunkLen);
		g_NumChunkRequestSent++;

#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("send chunk request %s to original dest pxy: %s\n",
				SHA1Str(cci->sha1name),
				g_PeerInfo[uci->dstPxyIdx].name);
#endif
		g_PeerInfo[uci->dstPxyIdx].numRedirected++;
		g_PeerInfo[uci->dstPxyIdx].bytesRedirected += cci->chunkLen;
		WriteToChunkPeer(uci->dstPxyIdx, request, req_len, ci->fd);

		/* track the request in-flight */
		ConnectionID cid;
		cid.srcIP = uci->dip;
		cid.srcPort = uci->dport;
		cid.dstIP = uci->sip;
		cid.dstPort = uci->sport;

#if 0
		if (idx != uci->dstPxyIdx) {
			/* this is truly redirected one.
			   we set it here not to put it
			   into cache later */
			names[chunkIdx].fromPeer = TRUE;
		}
		else {
			names[chunkIdx].fromPeer = FALSE;
		}
#endif

		/* original chunk request: the proxy address should be
		   the original destination proxy address */
		PutChunkRequest(cci->sha1name, cid, FALSE,
				g_PeerInfo[uci->dstPxyIdx].netAddr,
				cci->chunkLen, uci->dstPxyIdx, TRUE);
	}
}
/*-----------------------------------------------------------------*/
void PostProcessBodyRequest(ConnectionInfo* ci, char isHit,
		ChunkRequestOpaque* opaque, char* block)
{
	assert(opaque != NULL);
	WAOneRequest* oneReq = (WAOneRequest*)&opaque->request;
	assert(ci->type == ct_chunk);
	int block_length = ntohl(oneReq->length);
	assert(block_length > 0);
	if (isHit == TRUE) {
		/* send chunk body response */
		assert(block != NULL);
		char* response = NULL;
		unsigned int res_len = 0;

		/* it's an original request. (1-hop)
		   send response to the retPxyAddr1 */
		BuildWABodyResponse(&response, &res_len, oneReq->sha1name, 
				block, block_length);

		g_BytesBlockTX += block_length;
		ChunkConnectionInfo* cci = ci->stub;
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("HIT: original request, sent %s, %d bytes\n",
				SHA1Str(oneReq->sha1name), block_length);
#endif

#ifdef _WAPROX_RESPONSE_FIFO
		opaque->msg = response;
		opaque->msg_len = res_len;
		opaque->peerIdx = cci->peerIdx;
		opaque->streamID = ci->fd;

		/* check if it's in-order delivery or not */
		while (!TAILQ_EMPTY(&g_ChunkResponseList)) {
			ChunkRequestOpaque* pOpq = TAILQ_FIRST(
					&g_ChunkResponseList);
			if (pOpq->msg == NULL) {
				/* we're still waiting the former request */
				TRACE("out-of-order response, %d remains\n",
						g_NumChunkResponses);
				break;
			}
			TAILQ_REMOVE(&g_ChunkResponseList, pOpq, list);
			WriteToChunkPeer(pOpq->peerIdx, pOpq->msg,
					pOpq->msg_len, pOpq->streamID);
			free(pOpq);
			g_NumChunkResponses--;
			assert(g_NumChunkResponses >= 0);
		}
#else 
		WriteToChunkPeer(cci->peerIdx, response, res_len, ci->fd);
#endif
	}
	else {
		/* miss: redirect body request to the original peer */
		PeerInfo* pi = GetPeerInfoByAddr(oneReq->orgPxyAddr);

		/* do not propagate further */
		if (pi == g_LocalInfo) {
			/* maybe we already handled this one and deleted */
			/*NiceExit(-1, "strange.. already handled?\n");*/
			TRACE("strange.. already handled?\n");
			return;
		}

		char* request = NULL;
		unsigned int req_len = 0;
		BuildWABodyRequest(oneReq->orgPxyAddr, &request, &req_len,
				oneReq->sha1name, block_length);
		g_NumChunkRequestSent++;
		WriteToChunkPeer(pi->idx, request, req_len, ci->fd);
		g_MissRedirected++;
		TRACE("MISS %d: forward redirected request %s (2-hop) to %s\n",
				g_MissRedirected, SHA1Str(oneReq->sha1name),
				pi->name);
		/* track the request in-flight */
		/* forwared chunk request: the proxy address should be
		   the previous hop proxy address */
		PutChunkRequest(oneReq->sha1name, opaque->cid, TRUE,
				opaque->prevPxyAddr, block_length,
				pi->idx, TRUE);
	}
}
/*-----------------------------------------------------------------*/
void PostProcessPeekRequest(ConnectionInfo* ci, char isHit,
		PeekRequestOpaque* opaque)
{
	assert(opaque != NULL);
	WAOnePeekRequest* oneReq = (WAOnePeekRequest*)&opaque->request;
	assert(ci->type == ct_chunk);

	/* send peek response */
	char* response = NULL;
	unsigned int res_len = 0;

	BuildWAPeekResponse(&response, &res_len, oneReq->sha1name, isHit);
	ChunkConnectionInfo* cci = ci->stub;
	WriteToChunkPeer(cci->peerIdx, response, res_len, ci->fd);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("peek response %s: %s\n", (isHit ? "HIT" : "MISS"),
			SHA1Str(oneReq->sha1name));
#endif
}
/*-----------------------------------------------------------------*/
void PostProcessCachePUT(CacheTransactionInfo* cci)
{
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("PUT done, %d bytes\n", cci->chunkLen);
#endif
	PutDone(cci->sha1name);
}
/*-----------------------------------------------------------------*/
void PostProcessCacheGET(CacheTransactionInfo* cci, char isHit,
		char* block, int block_length)
{
	if (cci->ps == ps_nameTreeDelivery) {
		/* try reassmeble */
		/* check sourceCI is valid! */
		ConnectionInfo* target_ci = GetDestinationConnection(
				g_ConnMap, cci->cid.srcIP, cci->cid.srcPort,
				cci->cid.dstIP, cci->cid.dstPort);
		if (target_ci != NULL) {
			/* connection still there! */
			assert(cci->sourceCI->type == ct_user);
#ifdef _WAPROX_FULL_MRC_TREE
			PostProcessNameTreeDelivery(cci, isHit, block,
					block_length);
#else
			PostProcessNameTreeDelivery2(cci, isHit, block,
					block_length);
#endif
		}
		else {
			TRACE("connection not found, but we go ahead\n");
			if (isHit == TRUE) {
				assert(block_length >= 0);
				RefPutChunkCallback(cci->sha1name, block,
						block_length);
				g_TryReconstruction = TRUE;
			}
			else {
				/* send chunk request to the original sender */
				/* build chunk body request */
				char* request = NULL;
				unsigned int req_len = 0;
				int dstPxyId = cci->dstPxyId;
				assert(dstPxyId != -1);
				BuildWABodyRequest(g_PeerInfo[dstPxyId].netAddr,
						&request, &req_len,
						cci->sha1name, cci->chunkLen);
				g_NumChunkRequestSent++;

#ifdef _WAPROX_DEBUG_OUTPUT
				TRACE("send chunk request %s to "
						"original dest pxy: %s\n",
						SHA1Str(cci->sha1name),
						g_PeerInfo[dstPxyId].name);
#endif
				g_PeerInfo[dstPxyId].numRedirected++;
				g_PeerInfo[dstPxyId].bytesRedirected += cci->chunkLen;
				WriteToChunkPeer(dstPxyId,
						request, req_len, -1);
			}
		}

		/* update local disk load stat */
		g_LocalInfo->diskPendingChunks--;
	}
	else if (cci->ps == ps_bodyRequest) {
		/* send response if hit, ask the original sender if miss */
		assert(cci->sourceCI->type == ct_chunk);
		PostProcessBodyRequest(cci->sourceCI, isHit,
				cci->pOpaque, block);
#ifdef _WAPROX_RESPONSE_FIFO
		/* we set it to NULL in order not to be free()'d */
		cci->pOpaque = NULL;
#endif
	}
	else if (cci->ps == ps_keepalive) {
		/* do nothing */
	}
	else {
		assert(FALSE);
	}
}
/*-----------------------------------------------------------------*/
void PostProcessCachePEEK(CacheTransactionInfo* cci, char isHit)
{
	if (cci->ps == ps_nameTreeDelivery) {
		/* check sourceCI is valid! */
		ConnectionInfo* target_ci = GetDestinationConnection(
				g_ConnMap, cci->cid.srcIP, cci->cid.srcPort,
				cci->cid.dstIP, cci->cid.dstPort);
		if (target_ci != NULL) {
			/* connection still there! */
			assert(cci->sourceCI->type == ct_user);
			PeekRequestOpaque2* opq = (PeekRequestOpaque2*)
					cci->pOpaque;
			assert(cci->pOpaque != NULL);
			UpdatePeekResult(opq->ri, opq->cni, isHit, FALSE);
		}
		else {
			/* ignore... */
			TRACE("connection already deleted!\n");
		}
	}
	else if (cci->ps == ps_peekRequest) {
		/* just send chunk_peek response */
		assert(cci->sourceCI->type == ct_chunk);
		PostProcessPeekRequest(cci->sourceCI, isHit, cci->pOpaque);
	}
	else if (cci->ps == ps_userData) {
		/* TODO: complete MRC for sending if we use disk.
		   we're using memory name hint now */
	}
	else if (cci->ps == ps_putDisk) {
		/* check peek result, and put disk only when it's miss */
		if (isHit == TRUE) {
			/* hit: don't put it again */
			PutDone(cci->sha1name);
		}
		else {
			/* miss: issue a new WAPRXO-PUT request */
			SendCacheRequest(hc_put, ps_none, cci->sha1name,
					cci->chunkLen, cci->pChunk, -1,
					NULL, NULL, 0, NULL, 0);
		}
	}
	else if (cci->ps == ps_keepalive) {
		/* do nothing */
	}
	else {
		assert(FALSE);
	}
}
/*-----------------------------------------------------------------*/
void ProcessExceptionalCacheResponse(ConnectionInfo* ci, char* buffer,
		int buf_len)
{
	/* we can't understand, but process it anyway */
	assert(buf_len > 0);
	CacheConnectionInfo* cache_info = ci->stub;
	assert(cache_info != NULL);
	CacheTransactionInfo* cci = cache_info->pCurTrans;
	assert(cci != NULL);
	TRACE("Exception: %s\n", buffer);
	if (cci->method == hc_put) {
		PostProcessCachePUT(cci);
	}
	else if (cci->method == hc_get) {
		/* regard this as cache miss */
		PostProcessCacheGET(cci, FALSE, NULL, cci->chunkLen);
	}
	else {
		assert(FALSE);
	}

	g_NumCacheResponseReceived++;
	g_BytesCacheRX += buf_len;
}
/*-----------------------------------------------------------------*/
int ProcessCacheResponse(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(buf_len > 0);
	CacheConnectionInfo* cache_info = ci->stub;
	assert(cache_info != NULL);
	CacheTransactionInfo* cci = cache_info->pCurTrans;
	assert(cci != NULL);
	char* pos1 = strchr(buffer, '\n');
	if (pos1 == NULL) {
		/* we need more */
		return 0;
	}

	char* pos_space = strchr(buffer, ' ');
	if (pos_space == NULL) {
		/* should have any response code */
		TRACE("%s\n", buffer);
		return -1;
	}

	assert(buf_len >= 12);
	int respCode = atoi(pos_space + 1);
	if (respCode == 302) {
		if (cci->method == hc_get) {
			/*
			   HTTP/0.9 302 OK\r\n
			   Content-Length: 100\r\n
			   <data>
			   <connection-close>
			 */
			char* pos2 = strchr(pos1 + 1, '\n');
			if (pos2 == NULL) {
				/* we need more */
				return 0;
			}
			assert(buf_len >= 24);
			int content_length = atoi(pos1 + 16);
			if (content_length != cci->chunkLen) {
				TRACE("Content-Length: %d\n", content_length);
				TRACE("chunkLen: %d\n", cci->chunkLen);
				assert(FALSE);
			}
			int header_length = pos2 - buffer + 1 + 2;
			if ((content_length + header_length) > buf_len) {
				/* we need more */
				return 0;
			}
			else {
				assert((content_length + header_length)
						== buf_len);
#ifdef _WAPROX_DEBUG_OUTPUT
				TRACE("[%d] 302 GET %s, %d bytes, %d us\n",
						ci->fd, SHA1Str(cci->sha1name),
						content_length,
						GetElapsedUS(&cci->sentTime));
#endif
				/* cache hit */
				PostProcessCacheGET(cci, TRUE,
						buffer + header_length,
						content_length);
			}
		}
		else if (cci->method == hc_put) {
			/* HTTP/0.9 302 OK\r\n */
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("[%d] 302 PUT response, %s, %d bytes, %d us\n",
					ci->fd, SHA1Str(cci->sha1name),
					buf_len, GetElapsedUS(&cci->sentTime));
#endif
			PostProcessCachePUT(cci);
		}
		else if (cci->method == hc_peek) {
			/* HTTP/0.9 302 POSSIBLE HIT\r\n */
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("[%d] 302 PEEK response, %s, %d bytes, %d us\n",
					ci->fd, SHA1Str(cci->sha1name),
					buf_len, GetElapsedUS(&cci->sentTime));
#endif
			PostProcessCachePEEK(cci, TRUE);
		}
		else {
			assert(FALSE);
		}
	}
	else if (respCode == 400) {
		/*
		   HTTP/0.9 400 NOT FOUND\r\n
		   <connection-close>
		 */
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("[%d] %s 400 response, %s %d bytes, %d us\n",
					ci->fd, GetHCMethodName(cci->method),
					SHA1Str(cci->sha1name), cci->chunkLen,
					GetElapsedUS(&cci->sentTime));
#endif
		if (cci->method == hc_get) {
			/* cache miss */
			PostProcessCacheGET(cci, FALSE, NULL, cci->chunkLen);
		}
		else if (cci->method == hc_peek) {
			/* cache miss */
			PostProcessCachePEEK(cci, FALSE);
		}
		else {
			assert(FALSE);
		}
	}
	else {
		/* we can't understand */
		TRACE("%s\n", buffer);
		return -1;
	}

	/* process success */
	g_BytesCacheRX += buf_len;
	g_NumCacheResponseReceived++;

	/* make this cache connection available */
	DestroyCacheTransactionInfo(cci);
	cache_info->pCurTrans = NULL;
	IssuePendingCacheRequests();
	return buf_len;
}
/*-----------------------------------------------------------------*/
int ProcessDnsResponse(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_dns);
	DnsConnectionInfo* dci = ci->stub;
	assert(dci != NULL);
	DnsTransactionInfo* dti = dci->pCurTrans;
	assert(dti != NULL);

	/* check response */
	if (buf_len < sizeof(DnsResponse)) {
		/* response not complete */
		return 0;
	}
	DnsResponse* resp = (DnsResponse*)buffer;

	/* find the target connection */
	ConnectionInfo* target_ci = SearchConnection(g_PseudoConnMap,
			dti->sip, dti->sport, 0, 0);
	if (target_ci == NULL) {
		TRACE("connection deleted?\n");
		return buf_len;
	}
	assert(target_ci->type == ct_user);
	UserConnectionInfo* uci = target_ci->stub;
	assert(uci->sip == dti->sip);
	assert(uci->sport == dti->sport);
	assert(uci->dip == 0);
	assert(uci->dport == 0);

	/* remove pseudo connection */
	if (RemoveConnection(g_PseudoConnMap, uci->sip, uci->sport,
				0, 0) != TRUE) {
		NiceExit(-1, "something wrong...\n");
	}

	/* make it a real connection */
	uci->dip = ntohl(resp->addr);
	uci->dport = dti->port;
	OpenUserConnection(target_ci);

	/* make this dns connection available */
	DestroyDnsTransactionInfo(dti);
	dci->pCurTrans = NULL;
	IssuePendingDnsRequests();
	g_BytesCacheRX += buf_len;
	return buf_len;
}
/*-----------------------------------------------------------------*/
void SendWACloseConnection(ConnectionInfo* ci, unsigned int bytesTX)
{
	UserConnectionInfo* uci = ci->stub;
	assert(uci != NULL);
	if (uci->dip == 0 || uci->dport == 0) {
		/* it's still pseudo connection.
		   we don't have to send msg */
		return;
	}

	char* msg = NULL;
	unsigned int msg_len = 0;
	BuildWACloseConnection(uci->sip, uci->sport,
			uci->dip, uci->dport,
			bytesTX, &msg, &msg_len);
#if 0
	assert(uci->nameCI != NULL);
#endif
#ifdef _WAPROX_NAME_SCHEDULE
	QueueControlData(uci, msg, msg_len, ci->fd);
#else
	WriteToNamePeer(uci->dstPxyIdx, uci->nameFlowId,
			msg, msg_len, ci->fd);
#endif
}
/*-----------------------------------------------------------------*/
void SendWANameAck(ConnectionInfo* ci, unsigned int ack)
{
	assert(ci->type == ct_user);
	UserConnectionInfo* uci = ci->stub;
	assert(uci->pendingBytes >= 0);
	/*
	TRACE("write buffer length: %d\n", ci->lenWrite);
	TRACE("pending names: %d, bytes: %d\n", uci->pendingNames,
			uci->pendingBytes);
	*/

	/* TODO: whether send or not if the orignal ci is closed */
	/* send WANameAck message */
	char* msg = NULL;
	unsigned int msg_len = 0;
	BuildWANameAck(uci->sip, uci->sport, uci->dip,
			uci->dport, &msg, &msg_len, ack);
#ifdef _WAPROX_NAME_SCHEDULE
	QueueControlData(uci, msg, msg_len, ci->fd);
#else
	WriteToNamePeer(uci->dstPxyIdx, uci->nameFlowId, msg, msg_len, ci->fd);
#endif

	/* TODO: set persist timer? */
}
/*-----------------------------------------------------------------*/
int SendData(ConnectionInfo* ci, char* msg, int msg_len)
{
	return SendDataPrivate(ci, msg, msg_len, -1);
}
/*-----------------------------------------------------------------*/
int QueueControlData(UserConnectionInfo* uci, char* msg, int msg_len,
		int streamID)
{
	assert(msg != NULL && msg_len > 0);
	MessageInfo* mi = (MessageInfo*)malloc(sizeof(MessageInfo));
	if (mi == NULL) {
		NiceExit(-1, "MessageInfo memory allocation failed\n");
	}
	mi->msg = msg;
	mi->len = msg_len;
	mi->streamID = streamID;
	TAILQ_INSERT_TAIL(&(uci->controlMsgList), mi, messageList);
	return TRUE;
}
/*-----------------------------------------------------------------*/
int SendDataPrivate(ConnectionInfo* ci, char* msg, int msg_len, int streamID)
{
	assert(msg != NULL && msg_len > 0);

	#if 0
	/* perform merging waprox messages to save more bandwidth */
	MessageInfo* last = TAILQ_LAST(&(ci->msgListWrite), MessageList);
	char msgType;
	int hdr, ret = 0;
	if (ci->type == ct_chunk && last != NULL) {
		hdr = VerifyChunkHeader(last->msg, last->len, &msgType);
		assert(hdr == 1);
		if (msgType == TYPE_WABODYREQUEST) {
			/* merge body request */
			ret = MergeTwoBodyRequest(&last->msg,
					&last->len, msg, msg_len);
		}
		else if (msgType == TYPE_WABODYRESPONSE) {
			/* merge body response */
			ret = MergeTwoBodyResponse(&last->msg,
					&last->len, msg, msg_len);
		}
		else {
			assert(FALSE);
		}
	}
	else if (ci->type == ct_control && last != NULL) {
		hdr = VerifyControlHeader(last->msg, last->len, &msgType);
		assert(hdr == 1);
		if (msgType == TYPE_WANAMEDELIVERY) {
			/* merge name delivery */
			ret = MergeTwoNameDelivery(&last->msg,
					&last->len, msg, msg_len);
		}
		/* TODO: piggyback NAMEACK! */
	}

	if (ret > 0) {
		/* merge success! */
		ci->lenWrite += ret;
		g_MasterCIsetByFD[ci->fd] = ci;
		return TRUE;	
	}
	#endif

	MessageInfo* mi = (MessageInfo*)malloc(sizeof(MessageInfo));
	if (mi == NULL) {
		NiceExit(-1, "MessageInfo memory allocation failed\n");
	}
	mi->msg = msg;
	mi->len = msg_len;
	mi->streamID = streamID;
	TAILQ_INSERT_TAIL(&(ci->msgListWrite), mi, messageList);

	/* we check only for user connection */
	if (ci->type == ct_user) {
		UserConnectionInfo* uci = ci->stub;
		if (ci->lenWrite + msg_len >= uci->maxWindowSize * 2) {
			/* write buffer full */
			TRACE("type=%d, fd=%d, %d/%d bytes\n",
					ci->type, ci->fd,
					ci->lenWrite + msg_len,
					uci->maxWindowSize);
			NiceExit(-1, "user write buffer full\n");
			return FALSE;
		}
	}
	
	ci->lenWrite += msg_len;

#ifdef _WAPROX_USE_SCTP
	/* try sending it first only for non-proxy connections */
	if (ci->type != ct_chunk && ci->type != ct_control) {
		Write(ci, FALSE);
	}
	else {
		/* for proxy connections: event will trigger write */
		SctpSendAll(ci);
	}
#else
	/* try sending data */
	if (ci->isWritable == TRUE) {
		Write(ci, FALSE);
	}
#endif

	return TRUE;
}
/*-----------------------------------------------------------------*/
ConnectionInfo* GetDestinationConnection(struct hashtable* ht,
		unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport)
{
	/* find the target connection info, but in REVERSE direction!! */
	assert(sip != 0 && sport != 0 && dip != 0 && dport != 0);
	assert(ht != NULL);
	ConnectionInfo* target_ci = SearchConnection(ht, dip, dport,
			sip, sport);
	if (target_ci == NULL) {
		TRACE("there's no such connection #1\n");
		PrintInetAddress(dip, dport, sip, sport);
		/*return NULL;*/
		/* we try it again */
		target_ci = SearchConnection(ht, sip, sport, dip, dport);
		if (target_ci == NULL) {
			TRACE("there's no such connection #2\n");
			PrintInetAddress(sip, sport, dip, dport);
			return NULL;
		}
		else {
			TRACE("we get it at the second try\n");
		}
	}
	
	return target_ci;
}
/*-----------------------------------------------------------------*/
int ProcessReadBuffer(ConnectionInfo* ci)
{
	int num_processed = 0;
	if (ci->type == ct_user) {
		num_processed = ProcessUserData(ci, FALSE);

		/* update the current time */
		UserConnectionInfo* uci = ci->stub;
		UpdateCurrentTime(&(uci->lastAccessTime));
	}
	else if (ci->type == ct_control) {
		num_processed = ProcessControlData(ci, ci->bufRead,
				ci->lenRead);
	}
	else if (ci->type == ct_chunk) {
		num_processed = ProcessChunkData(ci, ci->bufRead, ci->lenRead);
	}
	else if (ci->type == ct_pptpd) {
		num_processed = ProcessNATResponse(ci->bufRead, ci->lenRead);
	}
	else if (ci->type == ct_cache) {
		num_processed = ProcessCacheResponse(ci, ci->bufRead,
				ci->lenRead);
	}
	else if (ci->type == ct_dns) {
		num_processed = ProcessDnsResponse(ci, ci->bufRead,
				ci->lenRead);
	}
	else {
		assert(FALSE);
	}

	if (num_processed == -1) {
		/* give up this connection */
		TRACE("protocol violation, drop this peer connection\n");
		TRACE("type: %d, fd=%d\n", ci->type, ci->fd);
		/*
		if (ci->type == ct_cache) {
			ProcessExceptionalCacheResponse(ci,
					ci->bufRead, ci->lenRead);
		}
		*/
		SetDead(ci, TRUE);
		#if 0
		DestroyConnectionInfo(ci);
		#endif
	}
	else if (num_processed == 0) {
		/* wait */
		/*TRACE("waiting...fd=%d\n", ci->fd);*/
	}
	else {
		assert(num_processed > 0);
		AdjustReadBuffer(ci, num_processed);
		/*
		TRACE("connection_type: %d, %d bytes processed\n",
			ci->type, num_processed);
		*/
		/* force to close cache connection */
		#if 0
		if (ci->type == ct_cache && num_processed > 0) {
			TRACE("force to close cache connection\n");
			DestroyConnectionInfo(ci);
		}
		#endif
	}
	
	return num_processed;
}
/*-----------------------------------------------------------------*/
void EventReadCB(int fd, short event, void *arg)
{
	ConnectionInfo *ci = arg;
	if (fd != ci->fd) {
		TRACE("fd=%d, ci->fd=%d\n", fd, ci->fd);
		NiceExit(-1, "something wrong?\n");
	}
	
	/* if not, something wrong with flow control */
	assert(RECV_BUFSIZE - ci->lenRead > 0);

	int numRead = 0;
#ifdef _WAPROX_USE_KERNEL_SCTP
	if (ci->type == ct_control || ci->type == ct_chunk) {
		struct sockaddr from;
		socklen_t fromlen = sizeof(from);
		struct sctp_sndrcvinfo sinfo;
		int msg_flags;
		numRead = sctp_recvmsg(ci->fd, ci->bufRead + ci->lenRead,
				RECV_BUFSIZE - ci->lenRead, &from, &fromlen,
				&sinfo, &msg_flags);
		TRACE("streamid: %d, %d bytes rcvd\n",
				sinfo.sinfo_stream, numRead);
	}
	else {
		numRead = read(ci->fd, ci->bufRead + ci->lenRead,
				RECV_BUFSIZE - ci->lenRead);
	}
#else
	numRead = read(ci->fd, ci->bufRead + ci->lenRead,
			RECV_BUFSIZE - ci->lenRead);
#endif

#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("[%d] fd: %d, type: %d, %d bytes read\n", ci->connID, ci->fd,
			ci->type, numRead);
#endif
	ci->lenRead += numRead;
	if (numRead == 0) {
		/* EOF */
		TRACE("EOF - close %d\n", ci->fd);
		CloseConnection(ci);
		return;
	}
	else if (numRead == -1) {
		/* error */
		int sverrno = errno;
		TRACE("Error - close %d, %s\n", ci->fd, strerror(sverrno));
		SetDead(ci, TRUE);
		#if 0
		DestroyConnectionInfo(ci);
		#endif
		return;
	}
}
/*-----------------------------------------------------------------*/
void CloseConnection(ConnectionInfo* ci)
{
	if (ci->status == cs_closed) {
		/* if already set dead, don't bother */
		return;
	}

	/* force processing remaining data in the buffer */
	ci->status = cs_done;
	if (ci->type == ct_user) {
		UserConnectionInfo* uci = ci->stub;
		TRACE("user connection close: fd=%d, %d/%d bytes\n",
				ci->fd, ci->lenRead,
				uci->bytesTX + ci->lenRead);

		if (ci->lenRead > 0) {
			/* try to process right away */
			int num_processed = ProcessUserData(ci, TRUE);
			AdjustReadBuffer(ci, num_processed);
			return;
		}

		/* inform the other it's closed anyway */
		SendWACloseConnection(ci, uci->bytesTX);
		ci->status = cs_finsent;

		if (uci->numSent > 0) {
			/* we need to wait ACK, then close */
			return;
		}
	}
	else if (ci->type == ct_control) {
		/* TODO: what if prox connection closes
		   in the middle of operation? */
		ProcessControlData(ci, ci->bufRead, ci->lenRead);
		ControlConnectionInfo* nci = ci->stub;
		if (nci->peerIdx != -1) {
			TRACE("name connection close: %s\n",
					g_PeerInfo[nci->peerIdx].name);
		}
		else {
			TRACE("name connection close: %d\n", ci->fd);
		}
	}
	else if (ci->type == ct_chunk) {
		/* TODO: what if prox connection closes
				in the middle of operation? */
		ProcessChunkData(ci, ci->bufRead, ci->lenRead);
		ChunkConnectionInfo* cci = ci->stub;
		if (cci->peerIdx != -1) {
			TRACE("chunk connection close: %s\n",
					g_PeerInfo[cci->peerIdx].name);
		}
		else {
			TRACE("chunk connection close: %d\n", ci->fd);
		}
	}
	else if (ci->type == ct_pptpd) {
		if (ci->lenRead > 0) {
			ProcessNATResponse(ci->bufRead, ci->lenRead);
		}
		TRACE("pptpd NAT connection close: %d\n", ci->fd);
	}
	else if (ci->type == ct_cache) {
		if (ci->lenRead > 0) {
			ProcessCacheResponse(ci, ci->bufRead, ci->lenRead);
			#if 0
			int num_processed = ProcessCacheResponse(
					ci, ci->bufRead, ci->lenRead);
			if (num_processed <= 0) {
				/* exception handling */
				ProcessExceptionalCacheResponse(ci,
						ci->bufRead, ci->lenRead);
			}
			#endif
		}

		TRACE("cache connection close: %d, %d bytes\n",
				ci->fd, ci->lenRead);
	}
	else if (ci->type == ct_dns) {
		NiceExit(-1, "this shouldn't be happened\n");
	}
	else if (ci->type == ct_disk) {
		NiceExit(-1, "this shouldn't be happened\n");
	}
	else {
		assert(FALSE);
	}

	SetDead(ci, FALSE);
	#if 0
	DestroyConnectionInfo(ci);
	#endif
}
/*-----------------------------------------------------------------*/
void PrintProxStat()
{
	float saving = 0;
	float pure_saving = 0;
	int i;
	int tot = g_BytesUserTX + g_BytesUserRX;
	if (tot > 0) {
		saving =  100.0 -  100.0 * (g_BytesPeerTX + g_BytesPeerRX)
			/ tot;
	}
	if ((g_BytesBlockTX + g_BytesBlockRX) > 0) {
		pure_saving = 100.0 - 100.0 * (g_BytesBlockTX +
				g_BytesBlockRX) / tot;
	}

/*#ifdef _WAPROX_DEBUG_OUTPUT*/
	static unsigned int sec_elapsed = 0;
	TRACE("------------------------------------------------\n");
	TRACE("%d seconds elapsed\n", sec_elapsed++);
	TRACE("mem cache: %d used, %d max\n", g_CacheSize,
			g_MaxCacheSize * 1024 * 1024);
	TRACE("chunks: %d cached, %d hits, %d misses, %d put temp hits\n",
			g_NumKeys, g_Hits, g_Misses, g_NumPutTempHits);
	TRACE("# items in send name hint: %d\n",
			hashtable_count(send_hint_ht));
	TRACE("# items in disk name hint: %d\n",
			hashtable_count(disk_hint_ht));
	TRACE("# items in put temp cache: %d\n",
			hashtable_count(put_tmp_ht));
	TRACE("# items in core temp cache: %d\n",
			hashtable_count(tmp_ht));
	TRACE("# items in mem cache: %d\n",
			hashtable_count(mem_ht));
	TRACE("# items in chunk request track table: %d\n",
			hashtable_count(chunk_request_ht));
	TRACE("# items in peek request track table: %d\n",
			hashtable_count(peek_request_ht));
	TRACE("# pending disk requests: %d 1st, %d 2nd\n",
			g_NumPendingFirstTransactions,
			g_NumPendingSecondTransactions)
	TRACE("disk put queue size: %d msgs, %d bytes (max: %d MB)\n",
			g_NumPutMessages, g_CurPutQueueSize,
			g_PutQueueSize);
	TRACE("total put discarded: %d bytes\n", g_TotalPutDiscarded);
	TRACE("conns: %d users (tot: %d), %d deads, %d peers, %d caches\n",
			g_NumUserConns, g_NumTotUserConns, g_NumDeadConns,
			g_NumPeerConns, g_NumCacheConns);
	TRACE("peers: %d live, %d total\n", g_NumLivePeers, g_NumPeers);
	TRACE("user bytes (TX/RX/Tot): %d / %d / %d bytes\n",
			g_BytesUserTX, g_BytesUserRX,
			g_BytesUserTX + g_BytesUserRX);
	TRACE("peer bytes (TX/RX/Tot): %d / %d / %d bytes\n",
			g_BytesPeerTX, g_BytesPeerRX,
			g_BytesPeerTX + g_BytesPeerRX);
	TRACE("pptp bytes (TX/RX/Tot): %d / %d / %d bytes\n",
			g_BytesPptpTX, g_BytesPptpRX,
			g_BytesPptpTX + g_BytesPptpRX);
	TRACE("bandwidth saving (user/proxy): %.2f %%\n", saving);
	TRACE("trees: %d trees/sec (peak: %d)\n",
			g_PdNumNameTrees, g_PdPeakNumNameTrees);
	TRACE("%d body req/sec (peak: %d), %d tot, %d rare-miss, "
			"%d miss-redirect\n",
			g_PdNumChunkRequests, g_PdPeakNumChunkRequests,
			g_NumChunkRequests, g_RareMisses, g_MissRedirected);
	TRACE("%d 1st req/sec (peak: %d), %d 2nd req/sec (peak: %d)\n",
			g_PdNumFirstRequests, g_PdPeakNumFirstRequests,
			g_PdNumSecondRequests, g_PdPeakNumSecondRequests);
	#if 0
	for (i = 0; i < g_NumPeers; i++) {
		TRACE("[%d] %s (RTT: %d ms): %d redirected,"
				" %d bytes\n", i,
				g_PeerInfo[i].name,
				g_PeerInfo[i].RTT,
				g_PeerInfo[i].numRedirected,
				g_PeerInfo[i].bytesRedirected);
	}
	#endif
	TRACE("------------------------------------------------\n");
/*#endif*/

	/* write stat log */
	char statData[4096];
	int length = snprintf(statData, sizeof(statData),
			"%d Hit: %d %d | %d %d UserConn: %d %d PeerConn: %d %d "
			"CacheConn: %d PptpReq: %d %d ChunkReq: %d %d "
			"CacheReq: %d %d BytesUser: %d %d BytesPeer: %d %d "
			"BytesPptp: %d %d BytesCache: %d %d BytesDns: %d %d"
			"BytesTotal: %d %d Saving: %.2f %.2f PerLevel: ",
			(int)timeex(NULL), g_Hits, g_Misses,
			g_CacheSize, g_NumKeys,
			g_NumUserConns, g_NumBlocked,
			g_NumPeerConns, g_NumLivePeers, g_NumCacheConns,
			g_NumPptpRequestSent, g_NumPptpResponseReceived,
			g_NumChunkRequestSent, g_NumChunkResponseReceived,
			g_NumCacheRequestSent, g_NumCacheResponseReceived,
			g_BytesUserTX, g_BytesUserRX,
			g_BytesPeerTX, g_BytesPeerRX,
			g_BytesPptpTX, g_BytesPptpRX,
			g_BytesCacheTX, g_BytesCacheRX,
			g_BytesDnsTX, g_BytesDnsRX,
			(g_BytesPeerTX + g_BytesPeerRX +
			 g_BytesPptpTX + g_BytesPptpRX),
			(g_BytesUserTX + g_BytesUserRX), saving, pure_saving);

	/* per level stats */
	for (i = 0; i < g_maxLevel; i++) {
		length += snprintf(statData + length,
				sizeof(statData) - length,
				"%d ", g_PerLevelStats[i].numHits);
	}
	length += snprintf(statData + length, sizeof(statData) - length,
			"| ");
	for (i = 0; i < g_maxLevel; i++) {
		length += snprintf(statData + length,
				sizeof(statData) - length,
				"%d ", g_PerLevelStats[i].bytesHits);
	}
	length += snprintf(statData + length, sizeof(statData) - length,
			"|\n");
	WriteLog(hstatLog, statData, length, TRUE);
}
/*-----------------------------------------------------------------*/
void SendNATRequest(ConnectionInfo* ci)
{
	/* connect to pptpd NAT first */
	if (g_PptpdNatCI == NULL) {
		int fd = MakeLoopbackConnection(g_PptpNatServerPort, TRUE);
		if (fd == -1) {
			NiceExit(-1, "pptpd NAT connection failed\n");
		}
		else {
			TRACE("pptpd NAT connection success[%d]\n", fd);
			g_PptpdNatCI = CreateNewConnectionInfo(fd, ct_pptpd);
			if (g_PptpdNatCI == NULL) {
				NiceExit(-1, "pptpd NATconnection creation failed\n");
			}
		}
	}

	/* send NAT request */
	NATRequest* req = malloc(sizeof(NATRequest));
	if (req == NULL) {
		NiceExit(-1, "NATRequest memory allocation failed\n");
	}
	UserConnectionInfo* uci = ci->stub;
	req->ip = htonl(uci->sip);
	req->port = htons(uci->sport);
	g_NumPptpRequestSent++;
	SendData(g_PptpdNatCI, (char*)req, sizeof(NATRequest));
	TRACE("pptpd NAT request, %s:%d\n", InetToStr(uci->sip), uci->sport);
}
/*-----------------------------------------------------------------*/
void WriteToNamePeer(int peerIdx, int flowId, char* msg, int len, int streamID)
{
	/* we don't send to itself */
	assert(peerIdx != g_LocalInfo->idx);
	assert(peerIdx != -1);
	assert(flowId >= 0 && flowId < g_MaxPeerConns);
	assert(len > 0);
	PeerInfo* pi = &g_PeerInfo[peerIdx];
#ifdef _WAPROX_USE_SCTP
	SctpAssociatePeer(peerIdx, ct_control);

#else
	if (pi->nameCI[0] == NULL) {
		/* connect to peer first */
		int i, peerfd;
		for (i = 0; i < g_MaxPeerConns; i++) {
			if (pi->nameCI[i] != NULL) {
				/* if already connected, why bother? */
				continue;
			}
#ifdef _WAPROX_USE_KERNEL_SCTP
			peerfd = MakeSctpConnection(NULL, pi->netAddr,
					g_NameListeningPort, TRUE, MAX_NUM_CI);
#else
			peerfd = MakeConnection(NULL, pi->netAddr,
					g_NameListeningPort, TRUE);
#endif
			if (peerfd == -1) {
				EXIT_TRACE("name connection failed: %s\n",
						pi->name);
			}

#ifdef _WAPROX_USE_KERNEL_SCTP
			if (DisableSctpNagle(peerfd) != 0) {
				NiceExit(-1, "DisableSctpNagle failed\n");
			}
#else
			if (DisableNagle(peerfd) != 0) {
				NiceExit(-1, "DisableNagle failed\n");
			}
#endif
			TRACE("name connection success: %s[%d]\n",
					pi->name, peerfd);
			pi->nameCI[i] = CreateNewConnectionInfo(peerfd,
					ct_control);
			CreateControlConnectionInfo(pi->nameCI[i]);
			ControlConnectionInfo* nci = pi->nameCI[i]->stub;
			nci->peerIdx = peerIdx;
			nci->flowIdx = i;
		}
	}
	#if 0
	else {
		/* we reuse existing connection */
		ControlConnectionInfo* nci = pi->nameCI[0]->stub;
		assert(nci->peerIdx == peerIdx);
		assert(nci->flowIdx != -1);
	}
	#endif
#endif

	SendDataPrivate(g_PeerInfo[peerIdx].nameCI[flowId], msg, len, streamID);

#ifdef _WAPROX_USE_SCTP
	WriteToSctpPeer(peerIdx, ct_control);
#endif

	/* make this peer alive even when we lose some ping messages */
	UpdatePeerLiveness(pi);
}
/*-----------------------------------------------------------------*/
void WriteToChunkPeer(int peerIdx, char* msg, int len, int streamID)
{
	assert(peerIdx != -1);
	assert(len > 0);
	PeerInfo* pi = &g_PeerInfo[peerIdx];
#ifdef _WAPROX_USE_SCTP
	SctpAssociatePeer(peerIdx, ct_chunk);
#else
	if (pi->chunkCI[0] == NULL) {
		/* connect to peer first */
		int i, peerfd;
		for (i = 0; i < g_MaxPeerConns; i++) {
			if (pi->chunkCI[i] != NULL) {
				/* if already connected, why bother? */
				continue;
			}
#ifdef _WAPROX_USE_KERNEL_SCTP
			peerfd = MakeSctpConnection(NULL, pi->netAddr,
					g_ChunkListeningPort, TRUE, MAX_NUM_CI);
#else
			peerfd = MakeConnection(NULL, pi->netAddr,
					g_ChunkListeningPort, TRUE);
#endif
			if (peerfd == -1) {
				EXIT_TRACE("chunk connection failed: %s\n",
						pi->name);
			}

#ifdef _WAPROX_USE_KERNEL_SCTP
			if (DisableSctpNagle(peerfd) != 0) {
				NiceExit(-1, "DisableSctpNagle failed\n");
			}
#else
			if (DisableNagle(peerfd) != 0) {
				NiceExit(-1, "DisableNagle failed\n");
			}
#endif
			TRACE("chunk connection success: %s[%d]\n",
					pi->name, peerfd);
			pi->chunkCI[i] = CreateNewConnectionInfo(peerfd,
					ct_chunk);
			CreateChunkConnectionInfo(pi->chunkCI[i]);
			ChunkConnectionInfo* cci = pi->chunkCI[i]->stub;
			cci->peerIdx = peerIdx;
			cci->flowIdx = i;
		}
	}
	#if 0
	else {
		/* we reuse existing connection */
		ChunkConnectionInfo* cci = pi->chunkCI[0]->stub;
		assert(cci->peerIdx == peerIdx);
		assert(nci->flowIdx != -1);
	}
	#endif
#endif

	/* chunk flow: round-robin */
	int flowID = GetNextChunkFlowID(peerIdx);
	SendDataPrivate(g_PeerInfo[peerIdx].chunkCI[flowID],
			msg, len, streamID);

#ifdef _WAPROX_USE_SCTP
	WriteToSctpPeer(peerIdx, ct_chunk);
#endif

	/* make this peer alive even when we lose some ping messages */
	UpdatePeerLiveness(pi);
}
/*-----------------------------------------------------------------*/
int ProcessNATResponse(char* buffer, int buf_len)
{
	int num_processed = 0;
	while (buf_len >= sizeof(NATResponse)) {
		NATResponse* resp = (NATResponse*)buffer;

		/* search pseudo connection table
		   to find corresponding user ci */
		ConnectionInfo* ci = SearchConnection(g_PseudoConnMap,
				ntohl(resp->ip), ntohs(resp->port), 0, 0);
		if (ci == NULL) {
			EXIT_TRACE("Pseudo connection(%s:%d) is not found\n",
					InetToStr(ntohl(resp->ip)),
					ntohs(resp->port));
			return -1;
		}
		assert(ci->type == ct_user);
		UserConnectionInfo* uci = ci->stub;
		assert(uci->sip == ntohl(resp->ip));
		assert(uci->sport == ntohs(resp->port));
		assert(uci->dip == 0);
		assert(uci->dport == 0);

		/* NOTE: both pptpd and waprox store ip/port
		   in network order!! */
		uci->dip = ntohl(resp->org_ip);
		uci->dport = ntohs(resp->org_port);

		char* sip = strdup(InetToStr(uci->sip));
		char* dip = strdup(InetToStr(uci->dip));

		TRACE("NAT Response: %s:%d -> %s:%d\n",
				sip, uci->sport, dip, uci->dport);
		if (sip != NULL)
			free(sip);
		if (dip != NULL)
			free(dip);

		/* remove pseudo connection */
		if (RemoveConnection(g_PseudoConnMap, uci->sip, uci->sport,
					0, 0) != TRUE) {
			NiceExit(-1, "something wrong...\n");
		}
		assert(SearchConnection(g_PseudoConnMap, ntohl(resp->ip),
					ntohs(resp->port), 0, 0) == NULL);

		/* make it a real connection */
		OpenUserConnection(ci);

		/* move forward buffer */
		buffer += sizeof(NATResponse);
		buf_len -= sizeof(NATResponse);
		num_processed += sizeof(NATResponse);

		g_NumPptpResponseReceived++;
	}

	g_BytesPptpRX += num_processed;
	return num_processed;
}
/*-----------------------------------------------------------------*/
int ProcessOneRabinChunking2(ConnectionInfo* ci, char* psrc,
		int buf_len, char** msg, unsigned int* msg_len)
{
	/* get MRC first */
	assert(ci != NULL);
	UserConnectionInfo* uci = ci->stub;
	assert(uci != NULL);
	int numNames = 0;
	int rb_size = 0;
	int maxLevel = 0;
	MRCPerLevelInfo* pli = NULL;
	#if 0
	OneChunkName* pNames = DoOneMRC(&(uci->rctx), psrc, buf_len, FALSE,
			&numNames, &rb_size, &maxLevel, &pli);
	assert(numNames > 0);

	/* prepare candidate list */
	CandidateList* candidateList = GetCandidateList(pNames, pli, maxLevel);
	#endif

	OneChunkName* pNames = DoPreMRC(&(uci->rctx), psrc, buf_len,
			&numNames, &rb_size, &maxLevel, &pli);
	assert(numNames > 0);
	assert(rb_size <= buf_len);

	*msg = NULL;
	*msg_len = 0;
	uci->numChunks++;
	if (g_SkipFirstChunk == TRUE && uci->numChunks <= 1) {
		/* skip the first chunk: send raw message */
		/* TODO: skip resolving tree as well */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] skip first chunk %d bytes\n", ci->connID, rb_size);
#endif
		BuildWARawMessage(uci->sip, uci->sport, uci->dip,
				uci->dport, msg, msg_len, psrc, rb_size);
		return rb_size;
	}

	/* make tree */
	MakeTree(pli, maxLevel, pNames, numNames);

	/* resolve tree with name hint */
	int lowLevel = ResolveWithHint(pNames, numNames, psrc, rb_size, pli,
			maxLevel, TRUE);
	assert(lowLevel < maxLevel);
	
	/* find candidate chunks */
	CandidateList* candidateList = GetCandidateList2(pNames, numNames,
			pli, lowLevel);

	/* calculate the total message length */
	int numItems = 0;
	int newmsg_len = GetCandidateMessageLength(candidateList, &numItems);

	/* create new chunk block - keep only sent names */
	ChunkBuffer* newChunk = CreateNewChunkBuffer2(psrc, rb_size,
			uci->bytesTX, pNames, numNames, candidateList,
			lowLevel);

	/* append it to chunk list */
	TAILQ_INSERT_TAIL(&(uci->chunkBufferList), newChunk, bufferList);
	uci->numChunkBuffers++;

	BuildWANameTreeDelivery3(uci->sip, uci->sport, uci->dip,
			uci->dport, msg, msg_len, psrc, pNames,
			candidateList, numItems, newmsg_len);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("[%d] deliver chunk %d, %d bytes, %d names\n",
			ci->connID, uci->numChunks - 1,
			pNames[0].length, numItems);
	DumpCandidateList(candidateList);
#endif

	return rb_size;
}
/*-----------------------------------------------------------------*/
int ProcessOneRabinChunking(ConnectionInfo* ci, char* psrc,
		int buf_len, char** msg, unsigned int* msg_len)
{
	assert(ci != NULL);
	UserConnectionInfo* uci = ci->stub;
	assert(uci != NULL);

	/* get MRC first */
	int numNames = 0;
	int rb_size = 0;
	int maxLevel = 0;
	MRCPerLevelInfo* pli = NULL;
	OneChunkName* pNames = DoOneMRC(&(uci->rctx), psrc, buf_len, TRUE,
			&numNames, &rb_size, &maxLevel, &pli);
	assert(numNames > 0);

	/* create new chunk block - we still keep the whole tree */
	ChunkBuffer* newChunk = CreateNewChunkBuffer(psrc, rb_size,
			uci->bytesTX, pNames, numNames);

	/* append it to chunk list */
	TAILQ_INSERT_TAIL(&(uci->chunkBufferList), newChunk, bufferList);
	uci->numChunkBuffers++;
	uci->numChunks++;

	/* prepare ChunkTreeInfo */
	static ChunkTreeInfo* pCTI = NULL;
	if (pCTI == NULL) {
		pCTI = CreateNewChunkTreeInfo(g_maxNames, g_maxLevel);
	}

	/* fill ChunkTreeInfo with chunks we just created */
	InitChunkTreeInfo(pCTI, pNames, numNames, 0);

	if (g_SkipFirstChunk == TRUE && uci->numChunks <= 1) {
		/* skip the first chunk: send raw message */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] skip first chunk %d bytes\n", ci->connID, rb_size);
#endif
		BuildWARawMessage(uci->sip, uci->sport, uci->dip,
				uci->dport, msg, msg_len, psrc, rb_size);
	}
	else {
		#if 0
		BuildWANameTreeDelivery(uci->sip, uci->sport, uci->dip,
				uci->dport, msg, msg_len,
				numNames, (char*)pNames,
				sizeof(OneChunkName) * numNames);
		#endif
		if (numNames == 1) {
			assert(pCTI->numNames == 1);
		}
		BuildWANameTreeDelivery2(uci->sip, uci->sport, uci->dip,
				uci->dport, msg, msg_len, numNames,
				pNames, pCTI->numLevels);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] deliver chunk %s, %d bytes, %d names\n",
				ci->connID, SHA1Str(pNames[0].sha1name),
				pNames[0].length, numNames);
#endif
	}

	return rb_size;
}
/*-----------------------------------------------------------------*/
int ProcessUserData(ConnectionInfo* ci, int forceProcess)
{
	assert(ci->type == ct_user);
	UserConnectionInfo* uci = ci->stub;
	int num_processed = 0;
	char* buffer = ci->bufRead;
	int buf_len = ci->lenRead;

	if (uci->isHttpProxy == TRUE) {
		/* parse HTTP request and get destination connection */
		unsigned short port = 0;
		char* name = GetHttpHostName(buffer, buf_len, &port);
		if (name == NULL) {
			/* host info not found. let's see more data */
			return 0;
		}

		#if 0
		/* host name is ready. perform DNS lookup */
		SendDnsRequest(uci, name, strlen(name), port);
		#endif

		/* remove pseudo connection */
		assert(uci->dip == 0);
		assert(uci->dport == 0);
		if (RemoveConnection(g_PseudoConnMap, uci->sip, uci->sport,
					0, 0) != TRUE) {
			NiceExit(-1, "something wrong...\n");
		}

		/* make it a real connection */
		struct in_addr tmp;
		if (inet_aton(name, &tmp) == 0) {
			NiceExit(-1, "inte_aton failed\n");
		}
		uci->dip = ntohl(tmp.s_addr);
		uci->dport = port;

		/* free name */
		free(name);

		/* set this flag false to indicate
		   we're waiting DNS response */
		uci->isHttpProxy = FALSE;
		
		/* consume the first line having an extra header */
		char* line = strchr(buffer, '\n');
		if (line == NULL) {
			NiceExit(-1, "something wrong\n");
		}
		AdjustReadBuffer(ci, (line - buffer + 1));

		OpenUserConnection(ci);

		/* we return 0 here, since we processed it just before
		   it's ugly...*/
		return 0;
	}

	if (uci->dip == 0 && uci->dport == 0) {
		TRACE("wating for NAT or DNS response...\n");
		return 0;
	}

	if (ci->status == cs_block) {
		TRACE("currently blocked: wait for ACK...\n");
		return 0;
	}

	/* do rabin fingerprint here*/
	int src_off;
	for (src_off = 0; src_off < buf_len; ) {
		/* advance */
		buffer = ci->bufRead + src_off;

		char* msg = NULL;
		unsigned int msg_len = 0;
		int one_process = 0;
		if ((!g_ClientChunking && uci->isForward) ||
				(!g_ServerChunking && !uci->isForward)) {
			/* send a raw message to the proxy */
			BuildWARawMessage(uci->sip, uci->sport, uci->dip,
					uci->dport, &msg, &msg_len,
					buffer, buf_len - src_off);
			one_process = buf_len - src_off;
		}
		else {
			/* perform rabin chunking */
			if (ci->status == cs_active && g_BufferTimeout > 0
					&& forceProcess == FALSE
					&& (buf_len - src_off) <
					MIN_RABIN_BUFSIZE) {
				/* wait until we have enough buffer
				   to generate chunks */
				uci->lastCumulativeAck = 0;
#ifdef _WAPROX_DEBUG_OUTPUT
				TRACE("buffering... %d\n", (buf_len - src_off));
#endif
				break;
			}
#ifdef _WAPROX_FULL_MRC_TREE
			one_process = ProcessOneRabinChunking(ci, buffer,
					buf_len - src_off, &msg, &msg_len);
#else
			one_process = ProcessOneRabinChunking2(ci, buffer,
					buf_len - src_off, &msg, &msg_len);
#endif
			#if 0
			if (msg_len >= buf_len - src_off) {
				TRACE("don't do this...%d >= %d\n",
						msg_len ,buf_len - src_off);
			}
			#endif
		}

		/* send packet */
		#if 0
		assert(uci->nameCI != NULL);
		#endif
#ifdef _WAPROX_NAME_SCHEDULE
		QueueControlData(uci, msg, msg_len, ci->fd);
#else
		WriteToNamePeer(uci->dstPxyIdx, uci->nameFlowId,
				msg, msg_len, ci->fd);
#endif

		/* update transmitted bytes counter */
		uci->bytesTX += one_process;
		g_BytesUserRX += one_process;
		src_off += one_process;
		num_processed += one_process;
		uci->numSent += one_process;

		/* TODO: FLOWCONTROL: we stop reading data from user */
		if (uci->numSent > uci->maxWindowSize * 0.8) {
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("stop reading fd:%d, %d bytes read\n",
					ci->fd, uci->numSent);
#endif
			ci->status = cs_block;
			g_NumBlocked++;
			assert(g_NumBlocked >= 0
					&& g_NumBlocked <= g_NumUserConns);
			break;
		}
	}

	if (ci->status == cs_done) {
		int remain = ci->lenRead - num_processed;
		SendWACloseConnection(ci, uci->bytesTX + remain);
		ci->status = cs_finsent;
	}

	return num_processed;
}
/*-----------------------------------------------------------------*/
int ProcessOpenConnection(ConnectionInfo* ci, char* buffer, int buf_len)
{
	WAOpenConnection* msg = (WAOpenConnection*)buffer;
	unsigned int sip = ntohl(msg->hdr.connID.srcIP);
	unsigned short sport = ntohs(msg->hdr.connID.srcPort);
	unsigned int dip = ntohl(msg->hdr.connID.dstIP);
	unsigned short dport = ntohs(msg->hdr.connID.dstPort);

	/* get peer proxy address */
	ControlConnectionInfo* nci = ci->stub;
	assert(nci != NULL);
	int peerfd = nci->peerIdx;

	assert(sip != 0 && sport != 0 && dip != 0 && dport != 0);
	/*
	ControlConnectionInfo* nci = ci->stub;
	assert(SearchConnection(nci->connmap, dip, dport,
			sip, sport) == NULL);
	*/
	assert(SearchConnection(g_ConnMap, dip, dport,
			sip, sport) == NULL);

	/* make a new outgoing connection */
	TRACE("connecting to %s:%d...\n", InetToStr(dip), dport);
	int newfd = MakeConnection(NULL, htonl(dip), dport, TRUE);
	if (newfd > 0) {
		if (DisableNagle(newfd) != 0) {
			NiceExit(-1, "DisableNagle failed\n");
		}
		TRACE("connected, fd=%d\n", newfd);
		ConnectionInfo* newci = CreateNewConnectionInfo(newfd,
				ct_user);
		CreateUserConnectionInfo(newci, FALSE);
		UserConnectionInfo* uci = newci->stub;
		/* we switch source and destination here */
		uci->sip = dip;
		uci->sport = dport;
		uci->dip = sip;
		uci->dport = sport;
		uci->dstPxyIdx = peerfd;
		uci->nameFlowId = GetNextControlFlowID(uci->dstPxyIdx);

		/* put this connection into dst proxy's user list */
		TAILQ_INSERT_TAIL(&g_PeerInfo[uci->dstPxyIdx].userList,
				newci, ciList2);
		g_PeerInfo[uci->dstPxyIdx].numUsers++;
		#if 0
		uci->nameCI = ci;
		#endif
		/*
		PutConnection(nci->connmap, newci);
		*/
		if (PutConnection(g_ConnMap, newci) != TRUE) {
			NiceExit(-1, "something wrong...\n");
		}
	}
	else {
		/* if connection fails,
		   close the incoming connection as well */
		/* NOTE: non-blocking connect() always success? */
		NiceExit(-1, "OpenConnection failed!\n");
		#if 0
		char* buffer = NULL;
		unsigned int buf_len = 0;
		BuildWACloseConnection(dip, dport, sip, sport,
				0, &buffer, &buf_len);
		WriteToNamePeer(peerfd, buffer, buf_len, 0);
		#endif
	}

	return sizeof(WAOpenConnection);
}
/*-----------------------------------------------------------------*/
int ProcessNameTreeDelivery(ConnectionInfo* ci, char* buffer, int buf_len)
{
	WANameDelivery* msg = (WANameDelivery*)buffer;
	int tot_length = ntohl(msg->hdr.length);
	int numTrees = ntohl(msg->numTrees);
	assert(tot_length <= buf_len);
	assert(numTrees >= 0);
	assert(numTrees == 1);
	unsigned int sip = ntohl(msg->hdr.connID.srcIP);
	unsigned short sport = ntohs(msg->hdr.connID.srcPort);
	unsigned int dip = ntohl(msg->hdr.connID.dstIP);
	unsigned short dport = ntohs(msg->hdr.connID.dstPort);
	assert(sip != 0 && sport != 0 && dip != 0 && dport != 0);
	/*
	   ControlConnectionInfo* nci = ci->stub;
	 */
	ConnectionInfo* target_ci = GetDestinationConnection(g_ConnMap,
			sip, sport, dip, dport);
	if (target_ci == NULL) {
		/* connection may be closed in the process
		   for some reason */
		TRACE("connection lost?\n");

		/* just consume it,
		   return the total length of this message */
		return tot_length;
	}

	int i;
	int tree_offset = sizeof(WANameDelivery);
	for (i = 0; i < numTrees; i++) {
		#if 0
		int numNames = ntohl(tree->numNames);
		OneChunkName* names = (OneChunkName*)((char*)tree
				+ sizeof(WAOneTree));
		/*PrintChunkTree2(names, numNames);*/
		#endif

		UserConnectionInfo* uci = target_ci->stub;
		assert(uci != NULL);

		int next = 0;
		ChunkTreeInfo* cti = DecodeOneTree(buffer + tree_offset,	
				buf_len - tree_offset, uci->seq_no, &next);
		tree_offset += next;
		g_PdNumNameTrees++;

		#if 0
		/* TODO: handle seq_no overflow! */
		ChunkTreeInfo* cti = CreateNewChunkTreeInfo(names, numNames);
		if (cti == NULL) {
			NiceExit(-1, "ChunkTreeInfo allocation failed\n");
		}
		InitChunkTreeInfo(cti, names, numNames, uci->seq_no++);
		#endif

		/* create new ReassembleInfo */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("got root name %s, %d bytes\n",
				SHA1Str(cti->pNames[0].sha1name),
				cti->pNames[0].length);
#endif
		CreateNewReassembleInfo(target_ci, uci->seq_no++, cti);

		/* start resolving from the root chunk */
		cti->bytesMissingChunks = cti->pNames[0].length;
		assert(cti->bytesMissingChunks > 0);

		if (g_MemCacheOnly == FALSE) {
			int i;
			char complete = TRUE;
			for (i = 0; i < cti->numLevels; i++) {
				if (!ResolveOneLevelDisk(cti, i, target_ci)) {
					/* we should wait the reply */
					complete = FALSE;
					break;
				}
				else {
					/* check the next level */
					continue;
				}
			}

			if (complete == TRUE) {
				/* every chunk is resolved */
				LastProcessNameTreeDelivery(target_ci, cti);
			}
		}
		else {
			/* perform memory cache lookup */
			ResolveMRCTreeInMemory(cti, MemoryCacheCallBack);
			LastProcessNameTreeDelivery(target_ci, cti);
		}

		/* next tree */
		#if 0
		tree = (WAOneTree*)((char*)names
				+ sizeof(OneChunkName) * numNames);
		#endif
	}

	return tot_length;
}
/*-----------------------------------------------------------------*/
int ProcessNameTreeDelivery2(ConnectionInfo* ci, char* buffer, int buf_len)
{
	WANameDelivery* msg = (WANameDelivery*)buffer;
	int tot_length = ntohl(msg->hdr.length);
	int numTrees = ntohl(msg->numTrees);
	assert(tot_length <= buf_len);
	assert(numTrees >= 0);
	assert(numTrees == 1);
	unsigned int sip = ntohl(msg->hdr.connID.srcIP);
	unsigned short sport = ntohs(msg->hdr.connID.srcPort);
	unsigned int dip = ntohl(msg->hdr.connID.dstIP);
	unsigned short dport = ntohs(msg->hdr.connID.dstPort);
	assert(sip != 0 && sport != 0 && dip != 0 && dport != 0);
	ConnectionInfo* target_ci = GetDestinationConnection(g_ConnMap,
			sip, sport, dip, dport);
	if (target_ci == NULL) {
		/* connection may be closed in the process
		   for some reason */
		TRACE("connection lost?\n");

		/* just consume it,
		   return the total length of this message */
		return tot_length;
	}

	int i;
	UserConnectionInfo* uci = target_ci->stub;
	assert(uci != NULL);
	int offset = sizeof(WANameDelivery);
	ConnectionID cid;
	cid.srcIP = uci->dip;
	cid.srcPort = uci->dport;
	cid.dstIP = uci->sip;
	cid.dstPort = uci->sport;

	for (i = 0; i < numTrees; i++) {
		WAOneTree* pTree = (WAOneTree*)(buffer + offset);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] %d levels, %d names\n", target_ci->connID,
				pTree->numLevels, ntohl(pTree->numNames));
#endif

		offset += sizeof(WAOneTree);
		int j;

		/* create new ReassembleInfo */
		ReassembleInfo* ri = CreateNewReassembleInfo(target_ci,
				uci->seq_no++, NULL);

		int numNames = ntohl(pTree->numNames);
		for (j = 0; j < numNames; j++) {
			WAOneChunk* pChunk = (WAOneChunk*)(buffer + offset);
			int clength = ntohl(pChunk->length);
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("[%d] seq: %d, idx: %d, %s, %s, %d bytes\n",
					target_ci->connID, ri->seq_no, j,
					pChunk->hasContent ? "HAS" : "NONE",
					SHA1Str(pChunk->sha1name), clength);
#endif
			offset += sizeof(WAOneChunk);
			ChunkNameInfo* cni = AppendChunkName(target_ci, ri,
					pChunk->sha1name, clength,
					pChunk->hasContent);
			ri->numNames++;

			/* pick the target server by R-HRW */
			int idx = PickPeerToRedirect(pChunk->sha1name);
			if (idx == -1) {
				NiceExit(-1, "no live peer\n");
			}
			cni->peerIdx = idx;

			/* resolve the chunks by PEEK */
			if (pChunk->hasContent == TRUE) {
				/* put this into temp cache */
				RefPutChunkCallback(pChunk->sha1name,
						buffer + offset, clength);
				cni->inMemory = TRUE;
				UpdatePeekResult(ri, cni, TRUE, TRUE);
				assert(cni->isHit == TRUE);
				offset += clength;
				g_TryReconstruction = TRUE;
			}
			else {
#ifdef _WAPROX_NO_PEEK
				/* just trust the sender proxy and
				   assume we have the chunk */
				UpdatePeekResult(ri, cni, TRUE, FALSE);
#else
				/* resolve this with peek */
				PeekOneChunk(pChunk, ri, cni, cid, ci->fd,
						uci->dstPxyIdx);
#endif
			}
		}
		g_PdNumNameTrees++;
		CheckPeek(ri);
	}

	assert(offset == tot_length);
	return tot_length;
}
/*-----------------------------------------------------------------*/
void PeekOneChunk(WAOneChunk* pChunk, ReassembleInfo* ri, ChunkNameInfo* cni,
		ConnectionID cid, int streamID, int dstPxyIdx)
{
	/* check local cache if the chunk is for me, or for dst proxy */
	assert(dstPxyIdx != -1);
	assert(dstPxyIdx != g_LocalInfo->idx);
	assert(cni->peerIdx != -1);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("chunk %s, HRW peer: %s\n", SHA1Str(pChunk->sha1name),
			g_PeerInfo[cni->peerIdx].name);
#endif
	if (g_PeerInfo[cni->peerIdx].isLocalHost == TRUE
			|| cni->peerIdx == dstPxyIdx) {
		if (g_MemCacheOnly == TRUE) {
			char isHit = (GetMemory(pChunk->sha1name) != NULL)
				? TRUE : FALSE;
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("local peek %s, %s\n", SHA1Str(pChunk->sha1name),
					(isHit ? "HIT" : "MISS"));
#endif
			UpdatePeekResult(ri, cni, isHit, FALSE);
		}
		else {
			/* check local cache with hc_peek() */
			PeekRequestOpaque2* opq = malloc(
					sizeof(PeekRequestOpaque2));
			if (opq == NULL) {
				NiceExit(-1, "malloc failed\n");
			}

			/* fill opaque info */
			bzero(&opq->cid, sizeof(ConnectionID));
			opq->ri = ri;
			opq->cni = cni;
			ConnectionInfo* target_ci = GetDestinationConnection(
					g_ConnMap, cid.srcIP, cid.srcPort,
					cid.dstIP, cid.dstPort);
			if (target_ci == NULL) {
				NiceExit(-1, "connection not found\n");
			}

#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("check local peek request %s\n",
					SHA1Str(pChunk->sha1name));
#endif
			SendCacheRequest(hc_peek, ps_nameTreeDelivery,
					pChunk->sha1name, 0, NULL, dstPxyIdx,
					target_ci, NULL, 0, opq,
					sizeof(PeekRequestOpaque2));
		}
	}
	else {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("send peek request %s to %s\n",
				SHA1Str(pChunk->sha1name),
				g_PeerInfo[cni->peerIdx].name);
#endif
		/* build chunk peek request */
		char* request = NULL;
		unsigned int req_len = 0;
		BuildWAPeekRequest(&request, &req_len, pChunk->sha1name);
		WriteToChunkPeer(cni->peerIdx, request, req_len, streamID);
		g_NumPeekRequestSent++;

		/* track the peek request in-flight */
		PutPeekRequest(ri, cni, pChunk->sha1name, cid);
	}
}
/*-----------------------------------------------------------------*/
int ProcessBodyRequest(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_chunk);
	WABodyRequest* msg = (WABodyRequest*)buffer;
	int tot_len = ntohl(msg->hdr.length);
	assert(tot_len <= buf_len);
	int numRequests = ntohl(msg->numRequests);
	assert(numRequests >= 1);
	int i;
	for (i = 0; i < numRequests; i++) {
		WAOneRequest* req = (WAOneRequest*)(buffer +
				sizeof(WABodyRequest) +
				(i * sizeof(WAOneRequest)));
		int length = ntohl(req->length);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("got %s request, %d bytes\n", SHA1Str(req->sha1name),
				length);
#endif
		g_PdNumChunkRequests++;
		g_NumChunkRequests++;

		ChunkRequestOpaque* opq = calloc(1, sizeof(ChunkRequestOpaque));
		if (opq == NULL) {
			NiceExit(-1, "ChunkRequestOpaque alloc failed\n");
		}

		/* cid is meaningless when redirection */
		bzero(&opq->cid, sizeof(ConnectionID));
		memcpy(&opq->request, req, sizeof(WAOneRequest));
		ChunkConnectionInfo* cci = ci->stub;
		assert(cci->peerIdx != -1);
		memcpy(&opq->prevPxyAddr, &g_PeerInfo[cci->peerIdx].netAddr,
				sizeof(in_addr_t));
#ifdef _WAPROX_RESPONSE_FIFO
		opq->msg = NULL;
		opq->msg_len = 0;
		opq->peerIdx = -1;
		opq->streamID = -1;
		TAILQ_INSERT_TAIL(&g_ChunkResponseList, opq, list);
		g_NumChunkResponses++;
		#if 0
		TRACE("add one request: %s, %d remains\n",
				SHA1Str(req->sha1name), g_NumChunkResponses);
		#endif
#endif

		/* perform temp-memory cache lookup first */
		CacheValue* pValue = SearchCache(req->sha1name);
		if (pValue == NULL) {
			/* maybe we can get it from put temp cache */
			pValue = GetTempDiskCache(req->sha1name);
			if (pValue != NULL) {
				/* copy it to temp cache */
				g_NumPutTempHits++;
				RefPutChunkCallback(req->sha1name,
						pValue->pBlock,
						pValue->block_length);
				pValue = SearchCache(req->sha1name);
				assert(pValue != NULL);
			}
		}

		if (pValue != NULL) {
			/* hit */
			assert(pValue->block_length == length);
			PostProcessBodyRequest(ci, TRUE, opq, pValue->pBlock);
#ifndef _WAPROX_RESPONSE_FIFO
			free(opq);
#endif
		}
		else {
			/* miss */
			if (g_MemCacheOnly == TRUE) {
				pValue = GetMemory(req->sha1name);
				if (pValue != NULL) {
					assert(pValue->block_length == length);
					PostProcessBodyRequest(ci, TRUE, opq,
							pValue->pBlock);
				}
				else {
					PostProcessBodyRequest(ci, FALSE,
							opq, NULL);
				}
#ifndef _WAPROX_RESPONSE_FIFO
				free(opq);
#endif
			}
			else {
				/* if not in memory, lookup the disk */
				#if 0
				if (IsPutInProgress(req->sha1name)) {
					NiceExit(-1, "PUT in progress\n");
				}
				#endif

				/* give only Body header info as opaque */
				SendCacheRequest(hc_get, ps_bodyRequest,
						req->sha1name, length,
						NULL, -1, ci, NULL, 0, opq,
						sizeof(ChunkRequestOpaque));
			}
		}
	}

	return tot_len;
}
/*-----------------------------------------------------------------*/
void ProcessOneChunkRequest(ChunkRequestInfo* cri, u_char* sha1name,
		char* block, int blength, int streamID)
{
	assert(cri != NULL);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("chunk request %s took %d us\n", SHA1Str(sha1name),
			GetElapsedUS(&cri->sentTime));
#endif
	if (cri->isRedirected == TRUE) {
		/* if redirected response,
		   forward it to the original requestor */
		char* response = NULL;
		unsigned int res_len = 0;
		BuildWABodyResponse(&response, &res_len, sha1name,
				block, blength);
		PeerInfo* pi = GetPeerInfoByAddr(cri->pxyAddr);
		WriteToChunkPeer(pi->idx, response, res_len, streamID);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("forward response to the prev proxy %s\n", pi->name);
#endif
		/* store this chunk in my cache */
		if (g_NoCache == FALSE) {
			if (g_MemCacheOnly == TRUE) {
				PutMemory(sha1name, block, blength);
			}
			else {
				PutDisk(sha1name, block, blength);
			}
		}
	}
	else {
		/* NOTE: it may keep some chunks forever.. 
		   but will work and be enough for now 
		   sol #1: use connID + sha1name as a key
		   sol #2: set timer to flush expired chunks */
		/*TRACE("connection not found, but we go ahead\n");*/
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("put temp cache %s, %d bytes\n", SHA1Str(sha1name),
				blength);
#endif
		RefPutChunkCallback(sha1name, block, blength);
		g_TryReconstruction = TRUE;

		g_NumChunkResponseReceived++;
		g_BytesBlockRX += blength;

#ifdef _WAPROX_FAST_RECONSTRUCT
		/* if original response, reconstruct it */
		ConnectionInfo* target_ci = GetDestinationConnection(
				g_ConnMap, cri->cid.srcIP, cri->cid.srcPort,
				cri->cid.dstIP, cri->cid.dstPort);
		if (target_ci != NULL) {
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("try reconstruction: %s, %d bytes\n",
					SHA1Str(sha1name), blength);
#endif
			ReconstructOriginalData(target_ci);
		}
		else {
			/* perhaps handled by the previous response */
			TRACE("connection not found?\n");
		}
#endif
		
		/* update load stat */
		if (cri->isNetwork) {
			g_PeerInfo[cri->peerIdx].networkPendingBytes -=
				cri->length;
		}
		else {
			g_PeerInfo[cri->peerIdx].diskPendingChunks--;
		}
	}
}
/*-----------------------------------------------------------------*/
int ProcessBodyResponse(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_chunk);
	WABodyResponse* msg = (WABodyResponse*)buffer;
	int numResponses = ntohl(msg->numResponses);
	int tot_len = ntohl(msg->hdr.length);
	assert(tot_len <= buf_len);
	assert(numResponses >= 1);
	int i;
	WAOneResponse* resp = (WAOneResponse*)(buffer +
			sizeof(WABodyResponse));
	for (i = 0; i < numResponses; i++) {
		unsigned int block_length = ntohl(resp->length);
		assert(block_length > 0);
		char* block = (char*)resp + sizeof(WAOneResponse);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("fd: %d, got body response %s, %d\n", ci->fd,
				SHA1Str(resp->sha1name), block_length);
#endif
		ChunkRequestList* list = GetChunkRequestList(resp->sha1name);
		if (list == NULL) {
			/* somebody needs it
			   (we should make it clear this case) */
			RefPutChunkCallback(resp->sha1name, block,
					block_length);
			resp = (WAOneResponse*)(block + block_length);
			continue;
		}

		ChunkRequestInfo* req_walk;
		int numEntries = 0;
		if (!TAILQ_EMPTY(list)) {
			req_walk = TAILQ_FIRST(list);
			assert(req_walk != NULL);
			assert(req_walk->length == block_length);
			numEntries++;
			ProcessOneChunkRequest(req_walk, resp->sha1name,
					block, block_length, ci->fd);
		}
		#if 0
		TAILQ_FOREACH(req_walk, list, reqList) {
			assert(req_walk->length == block_length);
			numEntries++;
			ProcessOneChunkRequest(req_walk, resp->sha1name,
					block, block_length, ci->fd);
		}
		#endif

#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("%d entry processed\n", numEntries);
#endif

		/* destroy processed chunk request info */
		DestroyChunkRequestList(resp->sha1name);
		assert(GetChunkRequestList(resp->sha1name) == NULL);

		/* next body */
		resp = (WAOneResponse*)(block + block_length);
	}

	return tot_len;
}
/*-----------------------------------------------------------------*/
int ProcessPeekRequest(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_chunk);
	WAPeekRequest* msg = (WAPeekRequest*)buffer;
	int tot_len = ntohl(msg->hdr.length);
	assert(tot_len <= buf_len);
	int numRequests = ntohl(msg->numRequests);
	assert(numRequests >= 1);
	int i;
	for (i = 0; i < numRequests; i++) {
		WAOnePeekRequest* req = (WAOnePeekRequest*)(buffer +
				sizeof(WAPeekRequest) +
				(i * sizeof(WAOnePeekRequest)));
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("got peek request: %s\n", SHA1Str(req->sha1name));
#endif
		PeekRequestOpaque* opq = malloc(sizeof(PeekRequestOpaque));
		if (opq == NULL) {
			NiceExit(-1, "PeekRequestOpaque alloc failed\n");
		}

		/* fill opaque info */
		bzero(&opq->cid, sizeof(ConnectionID));
		memcpy(&opq->request, req, sizeof(WAOnePeekRequest));
		ChunkConnectionInfo* cci = ci->stub;
		assert(cci->peerIdx != -1);

		/* perform temp-memory cache lookup first */
		CacheValue* pValue = SearchCache(req->sha1name);
		if (pValue != NULL) {
			/* hit */
			PostProcessPeekRequest(ci, TRUE, opq);
			free(opq);
		}
		else {
			/* miss */
			if (g_MemCacheOnly == TRUE) {
				pValue = GetMemory(req->sha1name);
				if (pValue != NULL) {
					/* memory hit */
					PostProcessPeekRequest(ci, TRUE, opq);
				}
				else {
					/* memory miss */
					PostProcessPeekRequest(ci, FALSE, opq);
				}
				free(opq);
			}
			else {
				/* if not in memory, lookup the disk */
				#if 0
				if (IsPutInProgress(req->sha1name)) {
					NiceExit(-1, "PUT in progress\n");
				}
				#endif

				/* give only Body header info as opaque */
				SendCacheRequest(hc_peek, ps_peekRequest,
						req->sha1name, 0,
						NULL, -1, ci, NULL, 0, opq,
						sizeof(PeekRequestOpaque));
			}
		}
	}

	return tot_len;
}
/*-----------------------------------------------------------------*/
int ProcessPeekResponse(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_chunk);
	WAPeekResponse* msg = (WAPeekResponse*)buffer;
	int numResponses = ntohl(msg->numResponses);
	int tot_len = ntohl(msg->hdr.length);
	assert(tot_len <= buf_len);
	assert(numResponses >= 1);
	int i;
	WAOnePeekResponse* resp = (WAOnePeekResponse*)(buffer +
			sizeof(WAPeekResponse));
	for (i = 0; i < numResponses; i++) {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("got peek response %s, %s\n", SHA1Str(resp->sha1name),
				(resp->isHit ? "HIT" : "MISS"));
#endif
		/* update lookup result */
		PeekRequestList* list = GetPeekRequestList(resp->sha1name);
		if (list == NULL) {
			/* ignore if already handled */
			TRACE("info not found, already handled?\n");
			resp++;
			continue;
		}

		PeekRequestInfo* req_walk;
		int numEntries = 0;
		TAILQ_FOREACH(req_walk, list, reqList) {
			numEntries++;
			ProcessOnePeekRequest(req_walk, resp->sha1name,
					resp->isHit);
		}

#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("%d entry processed\n", numEntries);
#endif

		/* destroy processed chunk request info */
		DestroyPeekRequestList(resp->sha1name);
		assert(GetPeekRequestList(resp->sha1name) == NULL);

		/* next response */
		resp++;
	}

	return tot_len;
}
/*-----------------------------------------------------------------*/
void ProcessOnePeekRequest(PeekRequestInfo* pri, u_char* sha1name, char isHit)
{
	assert(pri != NULL);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("peek request %s took %d us\n", SHA1Str(sha1name),
			GetElapsedUS(&pri->sentTime));
#endif
	ConnectionInfo* target_ci = GetDestinationConnection(
			g_ConnMap, pri->cid.srcIP, pri->cid.srcPort,
			pri->cid.dstIP, pri->cid.dstPort);
	if (target_ci != NULL) {
		/* update ReassembleInfo */
		UpdatePeekResult(pri->ri, pri->cni, isHit, FALSE);

		/* check peek results */
		CheckPeek(pri->ri);
	}
	else {
		NiceExit(-1, "connection not found?\n");
	}

	g_NumPeekResponseReceived++;
}
/*-----------------------------------------------------------------*/
int ProcessPutRequest(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_chunk);
	WAPutRequest* msg = (WAPutRequest*)buffer;
	int numRequests = ntohl(msg->numRequests);
	int tot_len = ntohl(msg->hdr.length);
	assert(tot_len <= buf_len);
	assert(numRequests >= 1);
	int i;
	int offset = sizeof(WAPutRequest);
	for (i = 0; i < numRequests; i++) {
		WAOnePutRequest* req = (WAOnePutRequest*)(buffer + offset);
		int blength = ntohl(req->length);
		char* block = buffer + offset + sizeof(WAOnePutRequest);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("got put request %s, %d bytes\n",
				SHA1Str(req->sha1name), blength);
#endif
		/* put contents if not there */
		if (g_MemCacheOnly == TRUE) {
			PutMemory(req->sha1name, block, blength);
		}
		else {
			PutDisk(req->sha1name, block, blength);
		}

		/* respond it */
		char* response = NULL;
		unsigned int resp_len = 0;
		BuildWAPutResponse(&response, &resp_len, req->sha1name);
		g_NumPutResponseReceived++;
		ChunkConnectionInfo* cci = ci->stub;
		WriteToChunkPeer(cci->peerIdx, response, resp_len, ci->fd);

		/* next request */
		offset += sizeof(WAOnePutRequest) + blength;
	}

	return tot_len;
}
/*-----------------------------------------------------------------*/
int ProcessPutResponse(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_chunk);
	WAPutResponse* msg = (WAPutResponse*)buffer;
	int numResponses = ntohl(msg->numResponses);
	int tot_len = ntohl(msg->hdr.length);
	assert(tot_len <= buf_len);
	assert(numResponses >= 1);
	int i;
	int offset = sizeof(WAPutResponse);
	for (i = 0; i < numResponses; i++) {
		WAOnePutResponse* resp = (WAOnePutResponse*)(buffer + offset);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("got put response %s\n", SHA1Str(resp->sha1name));
#endif
		PutDone(resp->sha1name);

		/* next request */
		offset += sizeof(WAOnePutResponse);
	}

	return tot_len;
}
/*-----------------------------------------------------------------*/
int ProcessNameAck(ConnectionInfo* ci, char* buffer, int buf_len)
{
	int num_processed = sizeof(WANameAck);
	WANameAck* msg = (WANameAck*)buffer;
	unsigned int ack = ntohl(msg->ack);
	assert(ack >= 0);
	unsigned int sip = ntohl(msg->hdr.connID.srcIP);
	unsigned short sport = ntohs(msg->hdr.connID.srcPort);
	unsigned int dip = ntohl(msg->hdr.connID.dstIP);
	unsigned short dport = ntohs(msg->hdr.connID.dstPort);

	assert(sip != 0 && sport != 0 && dip != 0 && dport != 0);
	/*
	ControlConnectionInfo* nci = ci->stub;
	*/
	/* TODO: check the order!*/
	ConnectionInfo* target_ci = GetDestinationConnection(g_ConnMap,
			sip, sport, dip, dport);
	if (target_ci == NULL) {
		/* the corresponding connection can be already closed */
		TRACE("the other side already done?\n");
	}
	else {
		assert(target_ci != NULL);
		UserConnectionInfo* uci = target_ci->stub;
		assert(uci != NULL);
		assert(uci->lastAcked <= ack);
		if (ack > uci->bytesTX) {
			TRACE("ack: %d, uci->bytesTX: %d\n", ack, uci->bytesTX);
		}
		assert(ack <= uci->bytesTX);
		unsigned int bytesAcked = ack - uci->lastAcked;
		uci->lastCumulativeAck += bytesAcked;
		uci->numSent -= bytesAcked;
		assert(uci->numSent >= 0);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] ACK: %d, acked: %d, outstanding window: %d\n",
				target_ci->connID, ack, bytesAcked,
				uci->numSent);
#endif
		uci->lastAcked = ack;

		/* TODO: FLOWCONTROL: resume reading again */
		int threshold = uci->maxWindowSize * 0.2;
		if (target_ci->status == cs_block &&
				(uci->maxWindowSize - uci->numSent)
				>= threshold) {
			target_ci->status = cs_active;
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("[%d] resume reading fd:%d, win:%d bytes\n",
					target_ci->connID, target_ci->fd,
					uci->numSent);
#endif
			g_NumBlocked--;
			assert(g_NumBlocked >= 0);
		}

		/* destroy ACKed chunk buffers */
		int numDestroyed = DestroyChunkBufferList(
				&(uci->chunkBufferList), ack, TRUE);
		uci->numChunkBuffers -= numDestroyed;
		assert(uci->numChunkBuffers >= 0);

		/* if we are done, send connection close message
		   to the other side */
		if (target_ci->status == cs_finsent && uci->numSent == 0) {
			#if 0
			char* msg = NULL;
			unsigned int msg_len = 0;
			BuildWACloseConnection(uci->sip, uci->sport,
					uci->dip, uci->dport,
					uci->bytesTX, &msg, &msg_len);
			assert(uci->nameCI != NULL);
			SendData(uci->nameCI, msg, msg_len);
			#endif
			TRACE("[%d] user connection close: %d done\n",
					target_ci->fd, target_ci->fd);
			SetDead(target_ci, FALSE);
			#if 0
			DestroyConnectionInfo(target_ci);
			#endif
		}
		else {
			#if 0
			if (uci->lastCumulativeAck >= g_ChunkSize * 5 &&
					target_ci->lenRead > 0) {
				/* ACK window is open, let's process more! */
				TRACE("force processing by ACK\n");
				uci->lastCumulativeAck = 0;
				int num_processed = ProcessUserData(target_ci,
						TRUE);
				AdjustReadBuffer(target_ci, num_processed);
			}
			#endif
		}
	}

	assert(num_processed <= buf_len);
	return num_processed;
}
/*-----------------------------------------------------------------*/
int ProcessCloseConnection(ConnectionInfo* ci, char* buffer, int buf_len)
{
	WACloseConnection* msg = (WACloseConnection*)buffer;
	unsigned int sip = ntohl(msg->hdr.connID.srcIP);
	unsigned short sport = ntohs(msg->hdr.connID.srcPort);
	unsigned int dip = ntohl(msg->hdr.connID.dstIP);
	unsigned short dport = ntohs(msg->hdr.connID.dstPort);
	unsigned int bytesTX = ntohl(msg->bytesTX);

	/* find the connection info to delete */
	/*
	ControlConnectionInfo* nci = ci->stub;
	*/
	ConnectionInfo* target_ci = GetDestinationConnection(g_ConnMap,
			sip, sport, dip, dport);
	if (target_ci != NULL) {
		UserConnectionInfo* uci = target_ci->stub;
		/* since the connection on the other side
		   is already disconnected,
		   we don't have to process our user data, just close */
		if (bytesTX == 0) {
			/* abnormal termination */
			TRACE("[%d] abnormal close\n", target_ci->connID);
			SetDead(target_ci, TRUE);
		}
		else if (uci->bytesRX == bytesTX) {
			/* there's no outstanding data */
			TRACE("[%d] close now: %d bytes\n",
					target_ci->connID, bytesTX);
			SetDead(target_ci, FALSE);
			#if 0
			DestroyConnectionInfo(target_ci);
			#endif
		}
		else {
			/* still in progress */
			TRACE("[%d] still in progress: (%d/%d) bytes\n",
					target_ci->connID, uci->bytesRX,
					bytesTX);
			uci->closeBytes = bytesTX;
		}
	}
	else {
		TRACE("connection already deleted?\n");
	}

	return sizeof(WACloseConnection);
}
/*-----------------------------------------------------------------*/
int ProcessRawMessage(ConnectionInfo* ci, char* buffer, int buf_len)
{
	WARawMessage* msg = (WARawMessage*)buffer;
	unsigned int sip = ntohl(msg->hdr.connID.srcIP);
	unsigned short sport = ntohs(msg->hdr.connID.srcPort);
	unsigned int dip = ntohl(msg->hdr.connID.dstIP);
	unsigned short dport = ntohs(msg->hdr.connID.dstPort);
	char* data = buffer + sizeof(WARawMessage);
	int msg_len = ntohl(msg->msg_len);
	assert(msg_len > 0);
	int tot_length = ntohl(msg->hdr.length);
	assert(tot_length <= buf_len);
	/* find the connection info to deliver */
	/*
	ControlConnectionInfo* nci = ci->stub;
	*/

	#if 0
	/* put it in cache */
	if (g_NoCache == FALSE) {
		u_char digest[SHA1_LEN];
		Calc_SHA1Sig((const byte*)data, msg_len, digest);
		if (g_MemCacheOnly) {
			PutMemory(digest, data, msg_len);
		}
		else {
			/* TODO: still use memory */
			PutDisk(digest, data, msg_len);
		}
	}
	#endif

	ConnectionInfo* target_ci = GetDestinationConnection(g_ConnMap,
			sip, sport, dip, dport);
	if (target_ci != NULL) {
		/* send immediately to user */
		char* dupmsg = malloc(sizeof(char) * msg_len);
		if (dupmsg == NULL) {
			NiceExit(-1, "msg allocation failed\n");
		}
		memcpy(dupmsg, data, msg_len);
		SendData(target_ci, dupmsg, msg_len);
		UserConnectionInfo* uci = target_ci->stub;
		assert(uci != NULL);
		uci->missBytes += msg_len;

#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("[%d] raw message seq: %d, %d bytes\n",
				target_ci->connID, uci->seq_no, msg_len);
#endif

		/* advance rabin window */
#ifndef _WAPROX_FULL_MRC_TREE
		/* NOTE: 'dupmsg' can be freed by SendData() above.
		   use 'data' instead */
		if (!g_NoCache && ((g_ClientChunking && !uci->isForward) ||
					(g_ServerChunking && uci->isForward))) {
			QueuePutMessage(&(uci->rctx4put), data, msg_len,
					uci->dstPxyIdx, uci->seq_no, NULL);
		}
#endif

		/* advance seq_no */
		uci->seq_no++;
	}
	else {
		TRACE("connection already deleted?\n");
	}

	return tot_length;
}
/*-----------------------------------------------------------------*/
int ProcessControlData(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_control);
	int num_processed = 0;
	char msgType; 
	int one_process = 0;

	while (buf_len > 0) {
		int hdr = VerifyControlHeader(buffer, buf_len, &msgType);
		if (hdr == 0) {
			/* we need more data, just wait */
			break;
		}
		else if (hdr == -1) {
			/* error - give up */
			return -1;
		}
		else {
			if (msgType == TYPE_WAOPENCONNECTION) {
				one_process = ProcessOpenConnection(ci,
						buffer, buf_len);
			}
			else if (msgType == TYPE_WANAMEDELIVERY) {
#ifdef _WAPROX_FULL_MRC_TREE
				one_process = ProcessNameTreeDelivery(ci,
						buffer, buf_len);
#else
				one_process = ProcessNameTreeDelivery2(ci,
						buffer, buf_len);
#endif
			}
			else if (msgType == TYPE_WANAMEACK) {
				one_process = ProcessNameAck(ci,
						buffer, buf_len);
			}
			else if (msgType == TYPE_WACLOSECONNECTION) {
				one_process = ProcessCloseConnection(ci,
						buffer, buf_len);
			}
			else if (msgType == TYPE_WARAWMESSAGE) {
				one_process = ProcessRawMessage(ci,
						buffer, buf_len);
			}
			else {
				assert(FALSE);
			}

			if (one_process == 0) {
				/* we need more data, just wait */
				break;
			}
			else if (one_process == -1) {
				/* give up */
				return -1;
			}
			else {
				/* control buffer */
				num_processed += one_process;
				buffer += one_process;
				buf_len -= one_process;
			}
		}
	}

	g_BytesPeerRX += num_processed;
	return num_processed;
}
/*-----------------------------------------------------------------*/
int ProcessChunkData(ConnectionInfo* ci, char* buffer, int buf_len)
{
	assert(ci->type == ct_chunk);
	int num_processed = 0;
	char msgType; 
	int one_process = 0;

	while (buf_len > 0) {
		int hdr = VerifyChunkHeader(buffer, buf_len, &msgType);
		if (hdr == 0) {
			/* we need more data, just wait */
			break;
		}
		else if (hdr == -1) {
			/* error - give up */
			return -1;
		}
		else {
			if (msgType == TYPE_WABODYREQUEST) {
				one_process = ProcessBodyRequest(ci,
						buffer, buf_len);
			}
			else if (msgType == TYPE_WABODYRESPONSE) {
				one_process = ProcessBodyResponse(ci,
						buffer, buf_len);
			}
			else if (msgType == TYPE_WAPEEKREQUEST) {
				one_process = ProcessPeekRequest(ci,
						buffer, buf_len);
			}
			else if (msgType == TYPE_WAPEEKRESPONSE) {
				one_process = ProcessPeekResponse(ci,
						buffer, buf_len);
			}
			else if (msgType == TYPE_WAPUTREQUEST) {
				one_process = ProcessPutRequest(ci,
						buffer, buf_len);
			}
			else if (msgType == TYPE_WAPUTRESPONSE) {
				one_process = ProcessPutResponse(ci,
						buffer, buf_len);
			}
			else {
				assert(FALSE);
			}

			if (one_process == 0) {
				/* we need more data, just wait */
				break;
			}
			else if (one_process == -1) {
				/* give up */
				return -1;
			}
			else {
				/* control buffer */
				num_processed += one_process;
				buffer += one_process;
				buf_len -= one_process;
			}
		}
	}

	g_BytesPeerRX += num_processed;
	return num_processed;
}
/*-----------------------------------------------------------------*/
void AdjustReadBuffer(ConnectionInfo* ci, int num_processed)
{
	/* shift remaining buffer to the first position */
	assert(num_processed >= 0);
	memmove(ci->bufRead, ci->bufRead + num_processed,
			ci->lenRead - num_processed);
	ci->lenRead -= num_processed;
	assert(ci->lenRead >= 0);
}
/*-----------------------------------------------------------------*/
void TimerCB(int fd, short event, void *arg)
{
	/* peer health check */
	CheckNodeStatus();

	/* disk cache connection check */
	CheckDiskCacheConnections();

	/* TODO: remove idle connection waiting for additional data */
}
/*-----------------------------------------------------------------*/
void BufferTimeoutCB(int fd, short event, void *arg)
{
	struct timeval tv, result;
	UpdateCurrentTime(&tv);

	/* check pending buffers and flush them */
	ConnectionInfo* walk;
	TAILQ_FOREACH(walk, &g_UserList, ciList) {
		assert(walk->type == ct_user);
		UserConnectionInfo* uci = walk->stub;
		timersub(&tv, &(uci->lastAccessTime), &result);
		if ((result.tv_sec * 1000) + (result.tv_usec / 1000) 
				>= g_BufferTimeout && walk->lenRead > 0
				&& walk->status == cs_active
				&& uci->dip != 0 && uci->dport != 0) {
			int num_processed = ProcessUserData(walk, TRUE);
			AdjustReadBuffer(walk, num_processed);
			TRACE("buffer timeout: %d bytes processed\n",
					num_processed);
		}
	}
}
/*-----------------------------------------------------------------*/
void OpenUserConnection(ConnectionInfo* ci)
{
	assert(ci->type == ct_user);
	UserConnectionInfo* uci = ci->stub;
	assert(uci->dip != 0);
	assert(uci->dport != 0);

	/* determine the destination proxy here */
	uci->dstPxyIdx = PickDestinationProxy(uci->sip, uci->sport,
			uci->dip, uci->dport);
	if (uci->dstPxyIdx == -1) {
		NiceExit(-1, "no live peer\n");
	}
	TRACE("destination proxy: %s\n", g_PeerInfo[uci->dstPxyIdx].name);
	uci->nameFlowId = GetNextControlFlowID(uci->dstPxyIdx);

	/* put this connection into dst proxy's user list */
	TAILQ_INSERT_TAIL(&g_PeerInfo[uci->dstPxyIdx].userList, ci, ciList2);
	g_PeerInfo[uci->dstPxyIdx].numUsers++;

	/* also put in the global table */
	if (PutConnection(g_ConnMap, ci) != TRUE) {
		NiceExit(-1, "something wrong...\n");
	}

	/* send a open connection message to other proxy */
	char* msg = NULL;
	unsigned int msg_len = 0;
	BuildWAOpenConnection(uci->sip, uci->sport,
			uci->dip, uci->dport, &msg, &msg_len);
#ifdef _WAPROX_NAME_SCHEDULE
	QueueControlData(uci, msg, msg_len, ci->fd);
#else
	WriteToNamePeer(uci->dstPxyIdx, uci->nameFlowId, msg, msg_len, ci->fd);
#endif
	#if 0
	assert(uci->nameCI == NULL);
	uci->nameCI = g_PeerInfo[uci->dstPxyIdx].nameCI;
	#endif

	/* process pending data now: minimize any delay */
	/* to minimize delay connecting,
	   we deliver name and body together */
	int num_processed = ProcessUserData(ci, TRUE);
	AdjustReadBuffer(ci, num_processed);
}
/*-----------------------------------------------------------------*/
void InitUserConnection(ConnectionInfo* ci, char isHttpProxy)
{
	/* get peer ip/port information */
	UserConnectionInfo* uci = ci->stub;
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	if (getpeername(ci->fd, (struct sockaddr*)&addr, &addr_len) < 0) {
		NiceExit(-1, "getpeername failed");
	}

	uci->sip = ntohl(addr.sin_addr.s_addr);
	uci->sport = ntohs(addr.sin_port);
	uci->dip = 0;
	uci->dport = 0;
	uci->isHttpProxy = isHttpProxy;
	if (uci->isHttpProxy == TRUE) {
		/* we open connection after seeing GET request */
		PutConnection(g_PseudoConnMap, ci);
		return;
	}

	if (!g_UseFixedDest) {
		TRACE("pptpd NAT request, %s:%d\n",
				InetToStr(uci->sip), uci->sport);
		/* make a new pseudo connection entry */
		PutConnection(g_PseudoConnMap, ci);
		SendNATRequest(ci);
	}
	else {
		/* make a new regular connection entry */
		assert(g_FixedDestInfo != NULL);
		uci->dip = ntohl(g_FixedDestInfo->netAddr);
		uci->dport = g_FixedDestPort;
		OpenUserConnection(ci);
	}
}
/*-----------------------------------------------------------------*/
void SocketAcceptCB(int fd, short event, void *arg)
{
	/* accept new connection */
	struct sockaddr sa;
	socklen_t len = sizeof(struct sockaddr);
	int newfd = accept(fd, &sa, &len);
	if (newfd < 0) {
		int sverrno = errno;
		TRACE("accept: %s\n", strerror(sverrno));
		NiceExit(-1, "socket accept failed\n");
	}
	
	if (fd == g_acceptSocketUser) {
		TRACE("new user socket created: %d\n", newfd);
		ConnectionInfo* ci = CreateNewConnectionInfo(newfd, ct_user);
		CreateUserConnectionInfo(ci, TRUE);
		InitUserConnection(ci, FALSE);
	}
	else if (fd == g_acceptSocketName) {
		TRACE("new name socket created: %d\n", newfd);
		int peerIdx = GetPeerIdxFromFD(newfd);
		int i;
		for (i = 0; i < g_MaxPeerConns; i++) {
			if (g_PeerInfo[peerIdx].nameCI[i] == NULL) {
				/* empty entry found */
				break;
			}
		}

		if (i == g_MaxPeerConns) {
			/* we already have all the connections */
			TRACE("name connections full. close %d\n", newfd);
			close(newfd);
		}
		else {
			/* make a new connection info */
			ConnectionInfo* ci = CreateNewConnectionInfo(newfd,
					ct_control);
			CreateControlConnectionInfo(ci);
			ControlConnectionInfo* nci = ci->stub;
			nci->peerIdx = peerIdx;
			nci->flowIdx = i;
			g_PeerInfo[peerIdx].nameCI[i] = ci;
		}
	}
	else if (fd == g_acceptSocketChunk) {
		TRACE("new chunk socket created: %d\n", newfd);
		int peerIdx = GetPeerIdxFromFD(newfd);
		int i;
		for (i = 0; i < g_MaxPeerConns; i++) {
			if (g_PeerInfo[peerIdx].chunkCI[i] == NULL) {
				break;
			}
		}

		if (i == g_MaxPeerConns) {
			/* we already have all the connections */
			TRACE("chunk connections full. close %d\n", newfd);
			close(newfd);
		}
		else {
			/* make a new connection info */
			ConnectionInfo* ci = CreateNewConnectionInfo(newfd,
					ct_chunk);
			CreateChunkConnectionInfo(ci);
			ChunkConnectionInfo* cci = ci->stub;
			cci->peerIdx = peerIdx;
			cci->flowIdx = i;
			g_PeerInfo[peerIdx].chunkCI[i] = ci;
		}
	}
	else if (fd == g_acceptSocketHttp) {
		TRACE("new http user socket created: %d\n", newfd);
		ConnectionInfo* ci = CreateNewConnectionInfo(newfd, ct_user);
		CreateUserConnectionInfo(ci, TRUE);
		InitUserConnection(ci, TRUE);
	}
	else {
		assert(FALSE);
	}
}
/*-----------------------------------------------------------------*/
void PingCB(int fd, short event, void *arg)
{
	assert(fd == g_acceptSocketPing);
	struct sockaddr_in server;
	int recvd;
	socklen_t structlength;
	char buffer[2048];
	structlength = sizeof(server);
	memset(buffer, 0, sizeof(buffer));
	recvd = recvfrom(fd, &buffer, sizeof(buffer), 0,
			(struct sockaddr*) &server, &structlength);
	if (recvd == -1) {
		TRACE("Ping packet receive failure\n");
		return;
	}

	char msgType; 
	if (VerifyPing(buffer, recvd, &msgType) <= 0 ||
			msgType != TYPE_WAPING) {
		/* protocol error, discard it */
		TRACE("Ping packet protocol error\n");
		return;
	}

	/* process the ping packet */
	ProcessPing(buffer, recvd);
}
/*-----------------------------------------------------------------*/
void InitializeProxy(int debug, char* proxconf, char* peerconf)
{
	/* initalize the event library */
	event_init();

	/* init log files */
	LogInit(debug);

	/* read configuration file */
	ReadConf(type_proxconf, proxconf == NULL ? PROXCONF : proxconf);
	ReadConf(type_peerconf, peerconf == NULL ? PEERCONF : peerconf);

	if (g_LocalInfo == NULL) {
		NiceExit(-1, "local host info is NULL\n");
	}

	/* initialize multi-resolution chunking tree */
	InitMRCParams();

#ifdef _WAPROX_USE_SCTP
	if (sctp_initLibrary() != 0) {
		NiceExit(-1, "sctp_initLibrary failed\n");
	}
#endif

#ifdef _WAPROX_RESPONSE_FIFO
	TAILQ_INIT(&g_ChunkResponseList);
#endif

	/* init disk cache scheduler */
	InitDiskCache();

	/* init connection parameters */
	InitConns();

	/* initalize master set */
	int i;
	for (i = 0; i < MAX_NUM_CI; i++) {
		g_MasterCIsetByFD[i] = NULL;
	}

	/* create the chunk cache */
	if (!CreateCache(g_MaxCacheSize * 1024 * 1024)) {
		NiceExit(-1, "cache table creation failed\n");
	}

	/* create Disk helper process for flushing chunk name hints 
	   (we do this as early as possible to minimize memory footprint) */
	if (!CreateDiskHelper()) {
		NiceExit(-1, "Disk helper process creation failed\n");
	}

	/* create DNS helper processes */
	if (!CreateDnsHelpers()) {
		NiceExit(-1, "DNS helper process creation failed\n");
	}

	if (g_LocalInfo->idx == g_FirstPeerIdx) {
		/* this means I'm the S-Waprox */
		TRACE("I'm S-Waprox!\n");
		isSWaprox = TRUE;
	}
	else {
		TRACE("I'm R-Waprox!\n");
		isSWaprox = FALSE;
	}

	/* only for R-Waprox */
	if (!isSWaprox) { 
		/* create a listening socket for users (clients) */
		g_acceptSocketUser = CreatePrivateAcceptSocket(
				g_UserListeningPort, TRUE);
		if (SetSocketBufferSize(g_acceptSocketUser, SOCKBUF_SIZE)
				!= 0) {
			NiceExit(-1, "SetSocketBufferSize failed\n");
		}
		if (DisableNagle(g_acceptSocketUser) != 0) {
			NiceExit(-1, "DisableNagle failed\n");
		}

		/* create a listening socket for HTTP proxy */
		g_acceptSocketHttp = CreatePrivateAcceptSocket(
				g_HttpProxyPort, TRUE);
		if (SetSocketBufferSize(g_acceptSocketHttp, SOCKBUF_SIZE)
				!= 0) {
			NiceExit(-1, "SetSocketBufferSize failed\n");
		}
		if (DisableNagle(g_acceptSocketHttp) != 0) {
			NiceExit(-1, "DisableNagle failed\n");
		}
	}
	else {
		/* only for S-Waprox */
		/* create a listening socket for name delivery */
#ifdef _WAPROX_USE_KERNEL_SCTP
		g_acceptSocketName = CreateSctpAcceptSocket(
				g_NameListeningPort, TRUE, FALSE, MAX_NUM_CI);
		if (DisableSctpNagle(g_acceptSocketName) != 0) {
			NiceExit(-1, "DisableSctpNagle failed\n");
		}
#else
		g_acceptSocketName = CreatePrivateAcceptSocket(
				g_NameListeningPort, TRUE);
		if (DisableNagle(g_acceptSocketName) != 0) {
			NiceExit(-1, "DisableNagle failed\n");
		}
#endif
		if (SetSocketBufferSize(g_acceptSocketName, SOCKBUF_SIZE)
				!= 0) {
			NiceExit(-1, "SetSocketBufferSize failed\n");
		}

		/* create a listening socket for chunk request/response */
#ifdef _WAPROX_USE_KERNEL_SCTP
		g_acceptSocketChunk = CreateSctpAcceptSocket(
				g_ChunkListeningPort, TRUE, FALSE, MAX_NUM_CI);
		if (DisableSctpNagle(g_acceptSocketChunk) != 0) {
			NiceExit(-1, "DisableSctpNagle failed\n");
		}
#else
		g_acceptSocketChunk = CreatePrivateAcceptSocket(
				g_ChunkListeningPort, TRUE);
		if (DisableNagle(g_acceptSocketChunk) != 0) {
			NiceExit(-1, "DisableNagle failed\n");
		}
#endif
		if (SetSocketBufferSize(g_acceptSocketChunk, SOCKBUF_SIZE)
				!= 0) {
			NiceExit(-1, "SetSocketBufferSize failed\n");
		} 
	}

	/* create a UDP ping socket for health check */
	g_acceptSocketPing = CreatePublicUDPSocket(g_ProxyPingPort);
	if (SetSocketBufferSize(g_acceptSocketPing, SOCKBUF_SIZE) != 0) {
		NiceExit(-1, "SetSocketBufferSize failed\n");
	}

	/* create chunk request table */
	if (!CreateChunkRequestTable()) {
		NiceExit(-1, "chunk request table creation failed\n");
	}

	/* create connection table(s) */
	if (!CreateConnectionTables()) {
		NiceExit(-1, "connection table creation failed\n");
	}

	/* init timer */
	UpdateCurrentTime(&g_LastTimer);
	UpdateCurrentTime(&g_LastBufferTimeout);
	UpdateCurrentTime(&g_LastDebugTimeout);
	UpdateCurrentTime(&g_StartTime);

	/* call this once early */
	CheckNodeStatus();

	/* create cache connections */
	CheckDiskCacheConnections();
}
/*-----------------------------------------------------------------*/
void CleanupProxy()
{
	/* destroy cache connections */
	DestroyCacheConnections();

	/* destroy connection table */
	DestroyConnectionTables();

	/* destrocy chunk request table */
	DestroyChunkRequestTable();

	/* destroy the cache */
	DestroyCache();

	/* destroy the peer list */
	DestroyPeerList();
}
/*-----------------------------------------------------------------*/
void EventSetCB(int fd, short event, void *arg)
{
	if (event == EV_TIMEOUT) {
		/* if timeout event, do nothing */
		return;
	}

	/* set corresponding flags to process later in the loop */
	assert(fd > 0);
	if (fd == g_acceptSocketUser) {
		/* user */
		assert(event == EV_READ);
		g_SetAcceptUser = es_triggered;
	}
	else if (fd == g_acceptSocketName) {
		/* name */
		assert(event == EV_READ);
		g_SetAcceptName = es_triggered;
	}
	else if (fd == g_acceptSocketChunk) {
		/* chunk */
		assert(event == EV_READ);
		g_SetAcceptChunk = es_triggered;
	}
	else if (fd == g_acceptSocketPing) {
		/* ping */
		assert(event == EV_READ);
		g_SetPing = es_triggered;
	}
	else if (fd == g_acceptSocketHttp) {
		/* http */
		assert(event == EV_READ);
		g_SetAcceptHttp = es_triggered;
	}
	else {
		/* read / write */
		if (event == EV_READ) {
			assert(g_MyEventSet[fd].esRead == es_added);
			g_MyEventSet[fd].esRead = es_triggered;
			g_NumReadCI++;
			assert(g_NumReadCI >= 0 && g_NumReadCI < MAX_NUM_CI);
		}
		else if (event == EV_WRITE) {
			assert(g_MyEventSet[fd].esWrite == es_added);
			g_MyEventSet[fd].esWrite = es_triggered;
			g_NumWriteCI++;
			/* re-enable writable flag */
			assert(g_MasterCIsetByFD[fd] != NULL);
			g_MasterCIsetByFD[fd]->isWritable = TRUE;
			assert(g_NumWriteCI >= 0 && g_NumWriteCI < MAX_NUM_CI);
		}
		else {
			assert(FALSE);
		}
	}
}
/*-----------------------------------------------------------------*/
void AddCIEvents()
{
	int i;
	for (i = 0; i < MAX_NUM_CI; i++) {
		if (g_MasterCIsetByFD[i] == NULL) {
			continue;
		}

		/* read */
		if (g_MyEventSet[i].esRead == es_none &&
				g_MasterCIsetByFD[i]->status == cs_active &&
				g_MasterCIsetByFD[i]->lenRead < RECV_BUFSIZE) {
			event_set(&g_MyEventSet[i].evRead, i, EV_READ,
					EventSetCB, NULL);
			if (event_add(&g_MyEventSet[i].evRead, NULL) != 0) {
				NiceExit(-1, "event_add failed\n");
			}
			g_MyEventSet[i].esRead = es_added;
		}

		/* enable write only when we have things to write */
		if (g_MyEventSet[i].esWrite == es_none &&
				g_MasterCIsetByFD[i]->lenWrite > 0) {
			event_set(&g_MyEventSet[i].evWrite, i, EV_WRITE,
					EventSetCB, NULL);
			if (event_add(&g_MyEventSet[i].evWrite, NULL) != 0) {
				NiceExit(-1, "event_add failed\n");
			}
			g_MyEventSet[i].esWrite = es_added;
		}
	}
}
/*-----------------------------------------------------------------*/
void CheckTimerEvents()
{
	struct timeval cur_time, result;
	UpdateCurrentTime(&cur_time);
	timersub(&cur_time, &g_LastTimer, &result);

	if (result.tv_sec >= g_TimerInterval) {
		/* timer */
		TimerCB(-1, 0, NULL);
		UpdateCurrentTime(&g_LastTimer);
	}

	timersub(&cur_time, &g_LastBufferTimeout, &result);
	if ((result.tv_sec * 1000) + (result.tv_usec / 1000) >=
			g_BufferTimeout && g_BufferTimeout > 0) {
		/* buffer timeout */
		BufferTimeoutCB(-1, 0, NULL);
		UpdateCurrentTime(&g_LastBufferTimeout);
	}

	timersub(&cur_time, &g_LastDebugTimeout, &result);
	if (result.tv_sec >= 1) {
		/* count performance rate */
		g_PdPeakNumNameTrees = MAX(g_PdPeakNumNameTrees,
				g_PdNumNameTrees);
		g_PdPeakNumChunkRequests = MAX(g_PdPeakNumChunkRequests,
				g_PdNumChunkRequests);
		g_PdPeakNumFirstRequests = MAX(g_PdPeakNumFirstRequests,
				g_PdNumFirstRequests);
		g_PdPeakNumSecondRequests = MAX(g_PdPeakNumSecondRequests,
				g_PdNumSecondRequests);
		g_PdNumNameTrees = 0;
		g_PdNumChunkRequests = 0;
		g_PdNumFirstRequests = 0;
		g_PdNumSecondRequests = 0;
		UpdateCurrentTime(&g_LastDebugTimeout);

		/* print the proxy status information */
		PrintProxStat();
	}

	#if 0
	timersub(&cur_time, &g_StartTime, &result);
	if (result.tv_sec >= 90) {
		/* exit here for gprof */
		TRACE("exit for gprof\n");
		exit(0);
	}
	#endif
}
/*-----------------------------------------------------------------*/
void Write(ConnectionInfo* ci, char eventTriggered)
{
	if (ci->type == ct_user && ci->isAbnormal) {
		/* if abnormally closed user connection,
		   we don't (can't) write any message */
		assert(ci->status == cs_closed);
		return;
	}

	int stop = FALSE;
	unsigned int bytesSent = 0;
	MessageInfo* walk = NULL;

	while (!TAILQ_EMPTY(&(ci->msgListWrite)) && stop == FALSE) {
		TAILQ_FOREACH(walk, &(ci->msgListWrite), messageList) {
#if 0
#ifdef _WAPROX_DEBUG_OUTPUT
			if (ci->type == ct_user) {
				TRACE("Id: %d, length: %d\n",
						ci->connID, walk->len);
			}
#endif
#endif
			int ret = ActualWrite(ci, walk->msg, walk->len,
					walk->streamID);
			if (ret == -1) {
				/* error */
				SetDead(ci, TRUE);
				#if 0
				DestroyConnectionInfo(ci);
				#endif
				return;
			}
			else if (ret == 0) {
				/* try next time */
				stop = TRUE;
				break;
			}
			else if (ret == 1) {
				/* success */
				/* remove the sent message from the list */
				TAILQ_REMOVE(&(ci->msgListWrite),
						walk, messageList);
				bytesSent += walk->len;
				free(walk->msg);
				free(walk);
				break;
			}
			else {
				assert(FALSE);
			}
		}
	}

	/* if we couldn't send them all here, try next time */
	if (ci->lenWrite > 0) {
		g_MasterCIsetByFD[ci->fd] = ci;
#ifdef _WAPROX_USE_SCTP
		/* register write callback: only for non-proxy connections */
		if (ci->type != ct_control && ci->type != ct_chunk
				&& ci->fd != -1) {
			SctpAddWriteEvent(ci->fd, &SctpUserEventDispatchCB);
		}
#endif
	}

	if (bytesSent > 0) {
		PostWrite(ci, bytesSent, eventTriggered);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("fd: %d, connection_type: %d, %d bytes wrote\n",
			ci->fd, ci->type, bytesSent);
#endif
	}
}
/*-----------------------------------------------------------------*/
int ActualWrite(ConnectionInfo* ci, char* bufSend, unsigned int lenSend,
		int streamID)
{
	assert(ci->firstMsgOffset >= 0);
	assert(ci->firstMsgOffset < lenSend);
	int actualLenSend = lenSend - ci->firstMsgOffset;
	assert(actualLenSend > 0);
	int numWrite = 0;
#ifdef _WAPROX_USE_KERNEL_SCTP
	if (ci->type == ct_chunk || ci->type == ct_control) {
		/* give socket number to strema id */
		assert(ci->firstMsgOffset == 0);
		assert(streamID != -1);
		numWrite = sctp_sendmsg(ci->fd, bufSend + ci->firstMsgOffset,
				actualLenSend, NULL, 0, 0, 0, streamID, 0, 0);
	}
	else {
		numWrite = write(ci->fd, bufSend + ci->firstMsgOffset,
				actualLenSend);
	}
#else
	numWrite = write(ci->fd, bufSend + ci->firstMsgOffset,
			actualLenSend);
#endif

	if (numWrite == 0) {
		/* error? */
		TRACE("write error?, close %d\n", ci->fd);
		return -1;
	}
	else if (numWrite == -1) {
#ifdef _WAPROX_DEBUG_OUTPUT
		int errno_bak = errno;
		TRACE("error[%d]: %s\n", ci->fd, strerror(errno_bak));
#endif
		if (errno == EAGAIN || errno == EINTR || errno == EPIPE) {
			/* busy: try again later */
			ci->isWritable = FALSE;
			return 0;
		}
		else {
			/* error */
			TRACE("write error, close %d\n", ci->fd);
			return -1;
		}
	}
	else {
		if (numWrite != actualLenSend) {
			/* we stop here, but record the offset */
			assert(numWrite < lenSend);
			ci->firstMsgOffset += numWrite;
			return 0;
		}
		else {
			/* write successfully */
			ci->firstMsgOffset = 0;
			ci->lenWrite -= lenSend;
			return 1;
		}
	}
}
/*-----------------------------------------------------------------*/
void PostWrite(ConnectionInfo* ci, unsigned int bytesSent, char eventTriggered)
{
	assert(bytesSent > 0);
	if (ci->type == ct_user) {
		UserConnectionInfo* uci = ci->stub;

		/* update bytes wrote here */
		uci->bytesRX += bytesSent;

		/* send feedback message here */
		SendWANameAck(ci, uci->bytesRX);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("bytes Sent: %d, ACK: %d\n", bytesSent,
				uci->bytesRX);
#endif
#if 0
		if (uci->closeBytes == 0) {
			/* don't send it when the other side is done */
			SendWANameAck(ci, bytesSent);
		}
#endif
		/* if it's fired due to write event,
		   we can safely destory connection here */
		if (eventTriggered && uci->closeBytes != 0 &&
				uci->bytesRX == uci->closeBytes) {
			/* there's no outstanding data */
			TRACE("close here:%d, fd=%d\n", uci->bytesRX, ci->fd);
			SetDead(ci, FALSE);
			#if 0
			DestroyConnectionInfo(ci);
			#endif
		}
		else {
			/* let ReconstructOriginalData() handle this */
		}

		g_BytesUserTX += bytesSent;
	}
	else if (ci->type == ct_control) {
		g_BytesPeerTX += bytesSent;
	}
	else if (ci->type == ct_chunk) {
		g_BytesPeerTX += bytesSent;
	}
	else if (ci->type == ct_pptpd) {
		g_BytesPptpTX += bytesSent;
	}
	else if (ci->type == ct_cache) {
		g_BytesCacheTX += bytesSent;
	}
	else if (ci->type == ct_dns) {
		g_BytesDnsTX += bytesSent;
	}
	else if (ci->type == ct_disk) {
	}
	else {
		assert(FALSE);
	}
}
/*-----------------------------------------------------------------*/
int ScheduleNameDelivery()
{
	int i;
	int numMessages = 0;
	for (i = 0; i < g_NumPeers; i++) {
		if (g_PeerInfo[i].numUsers == 0) {
			/* check next peer */
			continue;
		}

		ConnectionInfo* ci = NULL;
		MessageInfo* mi = NULL;
		char stop = FALSE;

		/* schedule every message for this proxy at this turn */
		while (stop == FALSE) {
			/* round-robin scheduling */
			int numFinished = 0;
			TAILQ_FOREACH(ci, &g_PeerInfo[i].userList, ciList2) {
				assert(ci->type == ct_user);
				UserConnectionInfo* uci = ci->stub;
				assert(uci != NULL);
				if (TAILQ_EMPTY(&(uci->controlMsgList))) {
					numFinished++;
					if (g_PeerInfo[i].numUsers
							== numFinished) {
						/* we're done */
						stop = TRUE;
						break;
					}

					/* check next user */
					continue;
				}

				/* get the first message */
				mi = TAILQ_FIRST(&(uci->controlMsgList));
				assert(mi != NULL);

				/* remove the first message */
				TAILQ_REMOVE(&(uci->controlMsgList),
						mi, messageList);

				/* queue this message to control CI */
				WriteToNamePeer(uci->dstPxyIdx,
						uci->nameFlowId,
						mi->msg, mi->len, ci->fd);
				numMessages++;
				
				/* free message info holder */
				free(mi);
			}
		}
	}

	return numMessages;
}
/*-----------------------------------------------------------------*/
void ProcessReadWrite()
{
	/* main event dispatch loop */
	int i;
	struct timeval _start;
	int num_processed = 0;

	/* read: put messages into queue, not write queue */
	if (g_NumReadCI > 0) {
		/*
		TRACE("# read: %d\n", g_NumReadCI);
		*/
		UpdateCurrentTime(&_start);
		for (i = 0; i < MAX_NUM_CI; i++) {
			if (g_MyEventSet[i].esRead == es_triggered) {
				g_MyEventSet[i].esRead = es_none;
				assert(g_MasterCIsetByFD[i] != NULL);
				EventReadCB(i, 0, g_MasterCIsetByFD[i]);
				/*
				   TRACE("reading...fd=%d\n", i);
				 */
				if (event_del(&g_MyEventSet[i].evRead) != 0) {
					NiceExit(-1, "event_del failed\n");
				}
			}

			if (g_MasterCIsetByFD[i] != NULL &&
					g_MasterCIsetByFD[i]->status
					!= cs_block &&
					g_MasterCIsetByFD[i]->lenRead > 0) {
				/* process existing buffer */
				int ret = ProcessReadBuffer(
						g_MasterCIsetByFD[i]);
				if (ret >= 0) {
					num_processed += ret;
				}
			}
		}
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("%d reads and process, %d bytes, total %d us\n",
				g_NumReadCI, num_processed,
				GetElapsedUS(&_start));
#endif

#ifndef _WAPROX_FULL_MRC_TREE
		/* perform ILS */
		UpdateCurrentTime(&_start);
		int numChunksILS = IntelligentLoadShedding();
		if (numChunksILS > 0) {
			TRACE("%d chunks ILS scheduled, %d us\n", numChunksILS,
					GetElapsedUS(&_start));
		}
#endif

		/* try reconstruction if we read something */
		UpdateCurrentTime(&_start);
#ifdef _WAPROX_FAST_RECONSTRUCT
		{
#else
		if (g_TryReconstruction) {
#endif
			ConnectionInfo* ci;
			TAILQ_FOREACH(ci, &g_UserList, ciList) {
				assert(ci->type == ct_user);
				if (ci->status != cs_closed) {
					ReconstructOriginalData(ci);
				}
			}
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("reconstruction, %d us\n",
					GetElapsedUS(&_start));
#endif
		}
		g_TryReconstruction = FALSE;
	}

#ifdef _WAPROX_NAME_SCHEDULE
	/* some scheduling before write: round-robin, etc. */
	UpdateCurrentTime(&_start);
	int numSchedule = ScheduleNameDelivery();
	if (numSchedule > 0) {
		TRACE("%d name messages scheduled, %d us\n",
				numSchedule, GetElapsedUS(&_start));
	}
#endif

	/* write: just flush messages to network! */
	if (g_NumWriteCI > 0) {
		UpdateCurrentTime(&_start);
		/*
		TRACE("# write: %d\n", g_NumWriteCI);
		*/
		for (i = 0; i < MAX_NUM_CI; i++) {
			if (g_MyEventSet[i].esWrite == es_triggered) {
				g_MyEventSet[i].esWrite = es_none;
				ConnectionInfo* ci = g_MasterCIsetByFD[i];
				/* ci may be deleted when processing read */
				if (ci != NULL && ci->lenWrite > 0) {
					/* it can be unwritable
					   by the same token */
					if (ci->isWritable == TRUE) {
						Write(ci, TRUE);
					}
				}
				/*
				   TRACE("writing...%d bytes, fd=%d\n",
				   ci->lenWrite, i);
				 */
				if (event_del(&g_MyEventSet[i].evWrite) != 0) {
					NiceExit(-1, "event_del failed\n");
				}
			}
		}
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("%d writes and process, total %d us\n", g_NumWriteCI,
				GetElapsedUS(&_start));
#endif
	}

	/* reset counter */
	g_NumWriteCI = 0;
	g_NumReadCI = 0;

	/* clean up closed connections */
	UpdateCurrentTime(&_start);
	int numClosed = CleanUpClosedConnections();
	if (numClosed > 0) {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("%d connections closed, %d us\n", numClosed,
				GetElapsedUS(&_start));
#endif
	}
}
/*-----------------------------------------------------------------*/
void ProcessPutMessageQueue()
{
	/* put chunks into disk only when not busy */
	PutMessageInfo* walk = NULL;
	while (!TAILQ_EMPTY(&g_PutMessageList)) {
		walk = TAILQ_FIRST(&g_PutMessageList);
		TAILQ_REMOVE(&g_PutMessageList, walk, messageList);
		ActualPutMissingChunks(walk->msg, walk->msg_len,
				walk->dstPxyId);
		g_NumPutMessages--;
		assert(g_NumPutMessages >= 0);
		g_CurPutQueueSize -= walk->msg_len;
		assert(g_CurPutQueueSize >= 0);
		free(walk->msg);
		free(walk);
		if (g_NumPutMessages % 100 == 0) {
			TRACE("%d put messages left\n", g_NumPutMessages);
		}
		/* one at a time */
		break;
	}
}
/*-----------------------------------------------------------------*/
int CleanUpClosedConnections()
{
	int count = 0;
	ConnectionInfo* ci;
	ConnectionInfo* toDestroy[MAX_NUM_CI];
	ConnectionList freelist;
	TAILQ_INIT(&freelist);
	TAILQ_FOREACH(ci, &g_DeadConnList, ciDead) {
		assert(ci != NULL);
		/*if (ci->type == ct_user && !ci->isAbnormal) {*/
		if (ci->type == ct_user) {
			UserConnectionInfo* uci = ci->stub;
			if (!TAILQ_EMPTY(&(uci->controlMsgList))) {
				/* still pending control messages */
				continue;
			}
		}

		if (ci->status == cs_closed) {
			/* just add it to the free list */
			TAILQ_INSERT_TAIL(&freelist, ci, ciFree);
			toDestroy[count++] = ci;
			assert(count < MAX_NUM_CI);
		}
		else {
			assert(FALSE);
		}
	}

	TAILQ_FOREACH(ci, &freelist, ciFree) {
		/* remove the connection from the dead list */
		TAILQ_REMOVE(&g_DeadConnList, ci, ciDead);	
	}
	
	int i;
	for (i = 0; i < count; i++) {
		/* destroy here */
		DestroyConnectionInfo(toDestroy[i]);
		g_NumDeadConns--;
		assert(g_NumDeadConns >= 0);
	}

	#if 0
	int i;
	for (i = 0; i < MAX_NUM_CI; i++) {
		if (g_MasterCIsetByFD[i] != NULL) {
			ConnectionInfo* ci = g_MasterCIsetByFD[i];
			if (ci->type == ct_user) {
				UserConnectionInfo* uci = ci->stub;
				if (!TAILQ_EMPTY(&(uci->controlMsgList))) {
					/* still pending messages */
					continue;
				}
			}

			if (ci->status == cs_closed) {
				DestroyConnectionInfo(g_MasterCIsetByFD[i]);
				count++;
			}
		}
	}
	#endif

	return count;
}
/*-----------------------------------------------------------------*/
void ProcessListenEvents()
{
	/* create new connections here */
	if (g_SetAcceptUser == es_triggered) {
		SocketAcceptCB(g_acceptSocketUser, 0, NULL);
		g_SetAcceptUser = es_added;
	}

	if (g_SetAcceptName == es_triggered) {
		SocketAcceptCB(g_acceptSocketName, 0, NULL);
		g_SetAcceptName = es_added;
	}

	if (g_SetAcceptChunk == es_triggered) {
		SocketAcceptCB(g_acceptSocketChunk, 0, NULL);
		g_SetAcceptChunk = es_added;
	}

	if (g_SetAcceptHttp == es_triggered) {
		SocketAcceptCB(g_acceptSocketHttp, 0, NULL);
		g_SetAcceptHttp = es_added;
	}

	/* ping */
	if (g_SetPing == es_triggered) {
		PingCB(g_acceptSocketPing, 0, NULL);
		g_SetPing = es_added;
	}
}
/*-----------------------------------------------------------------*/
void SetPersistReadEvent(int fd)
{
	event_set(&g_MyEventSet[fd].evRead, fd, EV_READ | EV_PERSIST,
			EventSetCB, NULL);
	if (event_add(&g_MyEventSet[fd].evRead, NULL) != 0) {
		NiceExit(-1, "event_add failed\n");
	}
}
/*-----------------------------------------------------------------*/
int WaproxMain(int debug, char* proxconf, char* peerconf)
{
/*
	TRACE("sizeof(ConnectionID): %d\n", sizeof(ConnectionID));
	TRACE("sizeof(WABodyRequest): %d\n", sizeof(WABodyRequest));
	TRACE("sizeof(WANameAck): %d\n", sizeof(WANameAck));
	TRACE("sizeof(WACloseConnection): %d\n", sizeof(WACloseConnection));
	TRACE("sizeof(WAOpenConnection): %d\n", sizeof(WAOpenConnection));
	TRACE("sizeof(WAHeader): %d\n", sizeof(WAHeader));
	NiceExit(-1, "afasd");
*/
	InitializeProxy(debug, proxconf, peerconf);

	/* clear ready CI set and flags */
	int i;
	for (i = 0; i < MAX_NUM_CI; i++) {
		g_MyEventSet[i].esRead = es_none;
		g_MyEventSet[i].esWrite = es_none;
	}
	g_NumReadCI = 0;
	g_NumWriteCI = 0;

	if (!isSWaprox) {
		/* R-Waprox only */
		g_MyEventSet[g_acceptSocketUser].esRead = es_added;
		g_MyEventSet[g_acceptSocketHttp].esRead = es_added;
		g_SetAcceptUser = es_added;
		g_SetAcceptHttp = es_added;
	}
	else {
		/* S-Waprox only */
		g_MyEventSet[g_acceptSocketName].esRead = es_added;
		g_MyEventSet[g_acceptSocketChunk].esRead = es_added;
		g_SetAcceptName = es_added;
		g_SetAcceptChunk = es_added;
	}
	g_MyEventSet[g_acceptSocketPing].esRead = es_added;
	g_SetPing = es_added;

	/* ignore SIGPIPE when write() */
	signal(SIGPIPE, SIG_IGN);

#ifdef _WAPROX_USE_SCTP
	SctpMain();
#else
	/* add listening events at the beginning */
	if (!isSWaprox) {
		/* R-Waprox only */
		SetPersistReadEvent(g_acceptSocketUser);
		SetPersistReadEvent(g_acceptSocketHttp);
	}
	else {
		/* S-Waprox only */
		SetPersistReadEvent(g_acceptSocketName);
		SetPersistReadEvent(g_acceptSocketChunk);
	}
	SetPersistReadEvent(g_acceptSocketPing);

	/* run */
	TRACE("start running (using libevent %s with %s)...\n",
			event_get_version(), event_get_method());

	while (TRUE) {
		/* add read/write events for actual connections */
		AddCIEvents();	

		/* check timer events */
		CheckTimerEvents();

		/* execute single-pass event-loop, blocking here */
		struct timeval timer_tv;
		if (TAILQ_EMPTY(&g_PutMessageList)) {
			/* nothing to put, wake up every 10ms, no busy wait */
			FillTimevalMillisec(&timer_tv, 10);
		}
		else {
			/* we must process put messages, don't wait */
			FillTimevalMillisec(&timer_tv, 0);
		}

		if (event_loopexit(&timer_tv) != 0) {
			NiceExit(-1, "event_loopexit failed\n");
		}
		if (event_loop(EVLOOP_ONCE) != 0) {
			NiceExit(-1, "event_loop failed\n");
		}

		/* strictly priority queue */
		if (g_NumReadCI == 0 && g_NumWriteCI == 0) {
		#if 0
		if (g_NumUserConns == 0) {
		#endif
			/* we're not busy: process put messages */
			ProcessPutMessageQueue();

			/* empty temp mem cache */
			if (g_NumUserConns == 0) {
				EmptyTempMemCache();

				/* TODO:
				   1. empty chunk/peek request track table
				   2. empty put temp memory cache
				*/
			}
		}

		/* we're busy: call handlers, scheduling, etc. */
		ProcessReadWrite();		

		/* accept new connections, ping */
		ProcessListenEvents();
	}
#endif

	/* Note: will not reach below... */
	CleanupProxy();
	return (0);
}
/*-----------------------------------------------------------------*/
