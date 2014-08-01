#include "ils.h"
#include "peer.h"
#include "config.h"
#include "waprox.h"
#include <assert.h>

static ILSQueue* diskQueue = NULL;
static ILSQueue* networkQueue = NULL;
static ILSQueue* sendQueue = NULL;
int PerChunkResponseOverhead = sizeof(WABodyResponse)
	+sizeof(WAOneResponse);

/*-----------------------------------------------------------------*/
int CompareChunkSizes(const void *p1, const void *p2)
{
	ChunkNameInfo* cni1 = *((ChunkNameInfo**)p1);
	ChunkNameInfo* cni2 = *((ChunkNameInfo**)p2);
	assert(p1 != NULL);
	assert(p2 != NULL);

	if (cni1->length < cni2->length) {
		return 1;
	}
	else if (cni1->length > cni2->length) {
		return -1;
	}
	else {
		return 0;
	}
}
/*-----------------------------------------------------------------*/
void ILSDump(ILSQueue* queue)
{
	int i;
	int totBytes = 0;
	for (i = 0; i < queue->qlen; i++) {
		ChunkNameInfo* cni = queue->q[i];
		assert(cni != NULL);
		assert(cni->ri != NULL);
		ConnectionInfo* ci = cni->ri->source_ci;
		assert(ci != NULL);
		TRACE("[%d], idx: %d, %s, %s, %d bytes\n",ci->connID, i,
				SHA1Str(cni->sha1name),
				cni->isHit ? "HIT" : "MISS", cni->length);
		totBytes += cni->length;
	}
	TRACE("total %d bytes\n", queue->totBytes);
	assert(totBytes == queue->totBytes);
}
/*-----------------------------------------------------------------*/
void ILSReset(ILSQueue* queue)
{
	queue->qlen = 0;
	queue->totBytes = 0;
	queue->latency = 0;
}
/*-----------------------------------------------------------------*/
void ILSEnqueue(ILSQueue* queue, ChunkNameInfo* cni, int overhead)
{
	assert(cni != NULL);
	assert((queue->qlen + 1) < MAX_ILS_QUEUE);
	queue->q[queue->qlen] = cni;
	queue->qlen++;
	queue->totBytes += cni->length + overhead;
}
/*-----------------------------------------------------------------*/
ChunkNameInfo* ILSDequeue(ILSQueue* queue)
{
	if (queue->qlen == 0) {
		/* queue empty */
		return NULL;
	}
	else {
		/* return the last element in the queue */
		ChunkNameInfo* cni = queue->q[queue->qlen - 1];
		assert(cni != NULL);
		queue->qlen--;
		assert(queue->qlen >= 0);
		queue->totBytes -= cni->length;
		assert(queue->totBytes >= 0);
		return cni;
	}
}
/*-----------------------------------------------------------------*/
void ILSUpdateDiskLatency(ILSQueue* queue, unsigned int pendingChunks)
{
	/* reflect the pending disk load */
#ifdef _WAPROX_ILS_NOHISTORY
	pendingChunks = 0;
#endif
	queue->latency = g_SeekLatency * (queue->qlen + pendingChunks);
	/*TRACE("Disk : %.2f ms\n", queue->latency);*/
}
/*-----------------------------------------------------------------*/
void ILSUpdateNetworkLatency(ILSQueue* queue, unsigned int pendingBytes)
{
	/* reflect the pending network load */
	assert(g_WanLinkBW > 0); /* Kbps */
#ifdef _WAPROX_ILS_NOHISTORY
	pendingBytes = 0;
#endif
	queue->latency = (queue->totBytes + pendingBytes) * 8.0 / g_WanLinkBW;
	queue->latency += g_WanLinkRTT; /* ms */
	/*TRACE("Network: %.2f ms\n", queue->latency);*/
}
/*-----------------------------------------------------------------*/
void InitILS()
{
	/* prepare disk queue for each peer */
	if (diskQueue == NULL) {
		diskQueue = calloc(g_NumPeers, sizeof(ILSQueue));
		if (diskQueue == NULL) {
			NiceExit(-1, "ILSQueue alloc failed\n");
		}
	}

	/* prepare network queue */
	if (networkQueue == NULL) {
		networkQueue = calloc(1, sizeof(ILSQueue));
		if (networkQueue == NULL) {
			NiceExit(-1, "ILSQueue alloc failed\n");
		}
	}

	/* prepare send queue */
	if (sendQueue == NULL) {
		sendQueue = calloc(1, sizeof(ILSQueue));
		if (sendQueue == NULL) {
			NiceExit(-1, "ILSQueue alloc failed\n");
		}
	}

	/* reset queues */
	int i;
	for (i = 0; i < g_NumPeers; i++) {
		ILSReset(&diskQueue[i]);
	}
	ILSReset(networkQueue);
	ILSReset(sendQueue);
}
/*-----------------------------------------------------------------*/
int FindBestSchedule()
{
	/* sort the disk queues and calculate initial latency */
	int i;
	for (i = 0; i < g_NumPeers; i++) {
		qsort(diskQueue[i].q, diskQueue[i].qlen,
				sizeof(ChunkNameInfo*), CompareChunkSizes);
		if (g_PeerInfo[i].diskPendingChunks < 0) {
			TRACE("negative disk load, peer %s\n",
					g_PeerInfo[i].name);
			g_PeerInfo[i].diskPendingChunks = 0;
		}
		ILSUpdateDiskLatency(&diskQueue[i],
				g_PeerInfo[i].diskPendingChunks);
		TRACE("pending chunks: %d\n", g_PeerInfo[i].diskPendingChunks);
#ifdef _WAPROX_DEBUG_OUTPUT
		if (diskQueue[i].qlen > 0) {
			TRACE("Disk Queue (%s):\n", g_PeerInfo[i].name);
			ILSDump(&diskQueue[i]);
		}
#endif
	}

	/* sort the network queue and calculate latency */
	qsort(networkQueue->q, networkQueue->qlen, sizeof(ChunkNameInfo*),
			CompareChunkSizes);
	unsigned int networkPendingBytes = 0;
	for (i = 0; i < g_NumPeers; i++) {
		if (g_PeerInfo[i].diskPendingChunks < 0) {
			TRACE("negative network load, peer %s\n",
					g_PeerInfo[i].name);
			g_PeerInfo[i].networkPendingBytes = 0;
		}

		networkPendingBytes += g_PeerInfo[i].networkPendingBytes;
	}
	ILSUpdateNetworkLatency(networkQueue, networkPendingBytes);
	TRACE("pending bytes: %d\n", networkPendingBytes);
#ifdef _WAPROX_DEBUG_OUTPUT
	if (networkQueue->qlen > 0) {
		TRACE("Network Queue:\n");
		ILSDump(networkQueue);
	}
#endif

	/* find the configuration which maximizes the bandwidth saving,
	   while not degrading the throughput */
	int numMove = 0;
	while (TRUE) {
		/* find the maximum latency disk queue */
		float maxLatency = -1;
		int maxPeer = 0;
		for (i = 0; i < g_NumPeers; i++) {
			if (diskQueue[i].latency > maxLatency) {
				maxLatency = diskQueue[i].latency;
				maxPeer = i;
			}
		}

		/* check the termination condition */
		if (maxLatency <= networkQueue->latency) {
			/* now it's network bound. we're done */
			break;
		}

		/* move the smallest chunk from disk to network */
		ChunkNameInfo* cni = ILSDequeue(&diskQueue[maxPeer]);
		if (cni == NULL) {
			/* queue empty, we cannot do better */	
			break;
		}

		ILSEnqueue(networkQueue, cni, PerChunkResponseOverhead);
		assert(cni->isNetwork == FALSE);
		cni->isNetwork = TRUE;
		numMove++;

		assert(cni->ri != NULL);
		assert(cni->ri->source_ci != NULL);
		UserConnectionInfo* uci = cni->ri->source_ci->stub;
		uci->numMoves++;
		uci->moveBytes += cni->length;
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("max latency: %.2f > network latency: %.2f\n",
				maxLatency, networkQueue->latency);
		TRACE("ILS chunk move! %d bytes\n", cni->length);
#endif

		/* update the latency */
		ILSUpdateDiskLatency(&diskQueue[maxPeer],
				g_PeerInfo[maxPeer].diskPendingChunks);
		ILSUpdateNetworkLatency(networkQueue, networkPendingBytes);
	}

	return numMove;
}
/*-----------------------------------------------------------------*/
int PopulateILSQueue()
{
	int numChunksScheduled = 0;
	int numSkipped = 0;
	ConnectionInfo* ci;
	TAILQ_FOREACH(ci, &g_UserList, ciList) {
		UserConnectionInfo* uci = ci->stub;
		assert(uci != NULL);
		ReassembleInfo* ri = NULL;
		if (ci->status == cs_closed) {
			continue;
		}

#ifdef _WAPROX_REASSEMBLE_OPTIMIZE
		TAILQ_FOREACH(ri, &(uci->readyList), readyList) {
			assert(ri->numPeekResolved == ri->numNames);
			assert(ri->getSent == FALSE);
#else
		TAILQ_FOREACH(ri, &(uci->reassembleList), reassembleList) {
			assert(ri->numPeekResolved <= ri->numNames);
			if (ri->numPeekResolved != ri->numNames) {
				/* resolve still in progress */
				numSkipped++;
				continue;
			}

			if (ri->getSent == TRUE) {
				/* already scheduled, ignore */
				numSkipped++;
				continue;
			}
#endif
			ri->getSent = TRUE;
			ChunkNameInfo* cni = NULL;
			TAILQ_FOREACH(cni, &(ri->nameList), nameList) {
				if (cni->inMemory == TRUE) {
					/* it's in local memory, ignore */
					continue;
				}

				/* put chunks to appropriate queue */
				assert(cni->peerIdx != -1);
				numChunksScheduled++;
				if (cni->isHit == TRUE) {
					/* hit: fetch from disk */
					cni->isNetwork = FALSE;
					ILSEnqueue(&diskQueue[cni->peerIdx],
							cni, 0);
				}
				else {
					/* miss: fetch from network */
					cni->isNetwork = TRUE;
					ILSEnqueue(networkQueue, cni, 
						PerChunkResponseOverhead);
				}

				/* put every chunk to send queue */
				ILSEnqueue(sendQueue, cni, 0);
			}
		}

#ifdef _WAPROX_REASSEMBLE_OPTIMIZE
		/* empty ready list since we're done with it */
		while (!TAILQ_EMPTY(&uci->readyList)) {
			ReassembleInfo* ri = TAILQ_FIRST(&uci->readyList);
			assert(ri != NULL);
			assert(ri->numPeekResolved == ri->numNames);
			assert(ri->getSent == TRUE);
			TAILQ_REMOVE(&uci->readyList, ri, readyList);
		}
#endif
	}

	if (numSkipped > 0) {
		TRACE("inefficiency: %d\n", numSkipped);
	}
	return numChunksScheduled;
}
/*-----------------------------------------------------------------*/
int IntelligentLoadShedding()
{
	struct timeval _start;
	if (g_NewPeekUpdate == FALSE) {
		/* no new peek update, we don't have to do this */
		return 0;
	}

	/* reset this flag */
	g_NewPeekUpdate = FALSE;

	/* init ILS */
	UpdateCurrentTime(&_start);
	InitILS();
	#if 0
	TRACE("init takes %d us\n", GetElapsedUS(&_start));
	#endif

	/* gather chunk resolve results */
	UpdateCurrentTime(&_start);
	int numChunksScheduled = PopulateILSQueue();
	if (numChunksScheduled > 0) {
		TRACE("population takes %d us\n", GetElapsedUS(&_start));
#ifdef _WAPROX_ILS
		/* find the best schedule to minimize the latency */
		UpdateCurrentTime(&_start);
		int numMove = FindBestSchedule();
		if (numMove > 0) {
			TRACE("%d/%d chunks moved, scheduling takes %d us\n",
					numMove, numChunksScheduled,
					GetElapsedUS(&_start));
		}
#endif
	}

	/* send chunk request, but keep the original chunk order
	   in order to avoid HOL blocking */
	UpdateCurrentTime(&_start);
	assert(sendQueue->qlen == numChunksScheduled);
	int i;
	int numLocal = 0;
	int numPeer = 0;
	int numOrigin = 0;
	for (i = 0; i < sendQueue->qlen; i++) {
		ChunkNameInfo* cni = sendQueue->q[i];
		assert(cni != NULL);
		ChunkRequestType cr;
		if (cni->isNetwork) {
			/* send the chunk request
			   to the original destination proxy */
			cr = SendChunkRequest(cni, TRUE);
		}
		else {
			/* send the chunk request to the peers */
			cr = SendChunkRequest(cni, FALSE);
		}

		if (cr == cr_local) {
			numLocal++;
		}
		else if (cr == cr_peer) {
			numPeer++;
		}
		else if (cr == cr_origin) {
			numOrigin++;
		}
		else {
			assert(FALSE);
		}
	}
	if (sendQueue->qlen > 0) {
		TRACE("sending %d local, %d peer, %d origin: takes %d us\n",
				numLocal, numPeer, numOrigin,
				GetElapsedUS(&_start));
	}
	return numChunksScheduled;
}
/*-----------------------------------------------------------------*/
void SendLocalChunkRequest(ChunkNameInfo* cni, int dstPxyIdx)
{
	assert(cni != NULL);
	ReassembleInfo* ri = cni->ri;
	assert(ri->source_ci->type == ct_user);

	/* maybe we can get it from put temp cache */
	CacheValue* pValue = GetTempDiskCache(cni->sha1name);
	if (pValue == NULL) {
		if (g_MemCacheOnly == TRUE) {
			/* get it from mem cache */
			pValue = GetMemory(cni->sha1name);
			if (pValue == NULL) {
				/* put not performed yet, send remote request */
				TRACE("%s not found\n", SHA1Str(cni->sha1name));
				/*NiceExit(-1, "how come?\n");*/
				assert(dstPxyIdx != -1);
				SendRemoteChunkRequest(cni, dstPxyIdx, TRUE);
				return;
			}
		}
		else {
			/* issue disk get */
			UserConnectionInfo* uci = ri->source_ci->stub;
			SendCacheRequest(hc_get, ps_nameTreeDelivery,
					cni->sha1name, cni->length,
					NULL, uci->dstPxyIdx,
					ri->source_ci, NULL, 0,
					NULL, 0);

			/* update local disk load stat */
			g_LocalInfo->diskPendingChunks++;
			return;
		}
	}
	else {
		g_NumPutTempHits++;
	}

	/* load chunk to temp mem cache */
	assert(pValue != NULL);
	RefPutChunkCallback(cni->sha1name, pValue->pBlock,
			pValue->block_length);
	g_LocalInfo->numRedirected++;
	g_LocalInfo->bytesRedirected += cni->length;
	g_TryReconstruction = TRUE;
#ifdef _WAPROX_FAST_RECONSTRUCT
	ReconstructOriginalData(ri->source_ci);
#endif
}
/*-----------------------------------------------------------------*/
void SendRemoteChunkRequest(ChunkNameInfo* cni, int peerIdx, char isNetwork)
{
	assert(cni != NULL);
	ReassembleInfo* ri = cni->ri;
	assert(ri != NULL);
	assert(ri->source_ci != NULL);
	assert(ri->source_ci->type == ct_user);
	UserConnectionInfo* uci = (UserConnectionInfo*)ri->source_ci->stub;
	assert(uci != NULL);

	/* don't send to itself */
	assert(peerIdx != g_LocalInfo->idx);

	/* remote get: build chunk body request */
	char* request = NULL;
	unsigned int req_len = 0;
	BuildWABodyRequest(g_PeerInfo[peerIdx].netAddr,
			&request, &req_len, cni->sha1name, cni->length);
	g_NumChunkRequestSent++;

	assert(peerIdx >= 0);
	assert(peerIdx < g_NumPeers);
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("[%d] directly send chunk request %s, %d bytes to %d:%s\n",
			ri->source_ci->connID, SHA1Str(cni->sha1name),
			cni->length, peerIdx, g_PeerInfo[peerIdx].name);
#endif
	g_PeerInfo[peerIdx].numRedirected++;
	g_PeerInfo[peerIdx].bytesRedirected += cni->length;
	WriteToChunkPeer(peerIdx, request, req_len, ri->source_ci->fd);

	/* track the request in-flight */
	ConnectionID cid;
	cid.srcIP = uci->dip;
	cid.srcPort = uci->dport;
	cid.dstIP = uci->sip;
	cid.dstPort = uci->sport;

	/* original chunk request: the proxy address should be
	   the original destination proxy address */
	PutChunkRequest(cni->sha1name, cid, FALSE,
			g_PeerInfo[peerIdx].netAddr, cni->length,
			peerIdx, isNetwork);
}
/*-----------------------------------------------------------------*/
ChunkRequestType SendChunkRequest(ChunkNameInfo* cni, char forceNetwork)
{
	assert(cni != NULL);
	ReassembleInfo* ri = cni->ri;
	assert(ri->source_ci->type == ct_user);
	UserConnectionInfo* uci = (UserConnectionInfo*)(ri->source_ci->stub);
	assert(uci != NULL);

	/* set the flag, in order not to be scheduled again */
	assert(ri->getSent == TRUE);
	assert(cni->peerIdx != -1);

	if (forceNetwork == TRUE) {
		/* send remote request to the original destination proxy 
		   even in case of cache-hit */
		SendRemoteChunkRequest(cni, uci->dstPxyIdx, TRUE);
		return cr_origin;
	}
	else {
		if (g_PeerInfo[cni->peerIdx].isLocalHost == TRUE ||
				cni->peerIdx == uci->dstPxyIdx) {
			/* local request */
			SendLocalChunkRequest(cni, uci->dstPxyIdx);

			/* load update will be handled in the function */
			return cr_local;
		}
		else {
			/* remote request */
			SendRemoteChunkRequest(cni, cni->peerIdx, FALSE);
			return cr_peer;
		}
	}
}
/*-----------------------------------------------------------------*/
