#include "disk.h"
#include "config.h"
#include <assert.h>
#include "waprox.h"
#include "hashtable.h"
#include "peer.h"

/*-----------------------------------------------------------------*/
/*
	HashCache as a caching layer
	1. GET:
		 <connection-open>             
	Request:   WAPROX-GET http://waprox-123 HTTP/0.9\r\n
		Content-Length: 100\r\n
		\r\n

	Response: HTTP/0.9 302 OK\r\n
		 Content-Length: 100\r\n
		 <data>

		 <connection-close>

	2. PUT:
	      <connection-open>
	Request: WAPROX-PUT http://waprox-123 HTTP/0.9\r\n
	      Content-Length: 100\r\n
	      \r\n
	      <data>
	      <connection-close>
	Response: No Response 

	3. PEEK:
		 <connection-open>             
	Request:   WAPROX-PEEK http://waprox-123 HTTP/0.9\r\n\r\n

	Response #1: HTTP/1.0 302 POSSIBILE HIT\r\n
	Response #2: HTTP/1.0 400 NOT FOUND\r\n
*/
/*-----------------------------------------------------------------*/
/* pending cache request list */
CacheRequestList g_FirstClassRequestList;
CacheRequestList g_SecondClassRequestList;

int g_NumPendingFirstTransactions = 0;
int g_NumPendingSecondTransactions = 0;

/*-----------------------------------------------------------------*/
void InitDiskCache()
{
	TAILQ_INIT(&g_FirstClassRequestList);
	TAILQ_INIT(&g_SecondClassRequestList);
	TAILQ_INIT(&g_PutMessageList);
}
/*-----------------------------------------------------------------*/
char* GetHCMethodName(hcMethod method)
{
	if (method == hc_put) {
		return "PUT";
	}
	else if (method == hc_get) {
		return "GET";
	}
	else if (method == hc_peek) {
		return "PEEK";
	}
	else {
		assert(FALSE);
		return "Error";
	}
}
/*-----------------------------------------------------------------*/
void MakeNewCacheConnection(int i)
{
	/* make a new connection */
	int fd = MakeLoopbackConnection(g_CacheServerPort, TRUE);
	if (fd == -1) {
		NiceExit(-1, "cache connection failed\n");
	}
	else {
		if (DisableNagle(fd) != 0) {
			NiceExit(-1, "DisableNagle failed\n");
		}
		g_CacheConnections[i] = CreateNewConnectionInfo(fd, ct_cache);
		if (g_CacheConnections[i] == NULL) {
			NiceExit(-1, "new connection info failed\n");
		}
		CreateCacheConnectionInfo(g_CacheConnections[i], i);
		TRACE("cache connection success[%d], total: %d\n",
				fd, g_NumCacheConns);
	}
}
/*-----------------------------------------------------------------*/
void CheckDiskCacheConnections()
{
	if (g_MemCacheOnly == TRUE) {
		/* disk cache is disabled */
		return;
	}

	#if 0
	if (g_MaxCacheConns == g_NumCacheConns) {
		/* everything is fine */
		return;
	}
	#endif

	int i;
	struct timeval cur_time, result;
	UpdateCurrentTime(&cur_time);
	for (i = 0; i < g_MaxCacheConns; i++) {
		if (g_CacheConnections[i] != NULL) {
			/* if there're pending requests, skip */
			if ((g_NumPendingFirstTransactions +
						g_NumPendingSecondTransactions)
					>= g_MaxCacheConns * 10) {
				continue;
			}

			/* check if idle, and issue keep-alive message */
			CacheConnectionInfo* cci = g_CacheConnections[i]->stub;
			assert(cci != NULL);
			timersub(&cur_time, &cci->lastAccessTime, &result);
			if (result.tv_sec >= 15) {
				TRACE("keep alive for HashCache\n");
				/* 20 bytes including '\0' */
				const char* dummyName = "KeepAliveDummyChunk";
				SendCacheRequest(hc_get, ps_keepalive,
						(u_char*)dummyName,
						0, NULL, -1,
						NULL, NULL, 0,
						NULL, 0);
			}
		}
		else {
			/* make a new connection */
			MakeNewCacheConnection(i);
		}
	}
}
/*-----------------------------------------------------------------*/
int DestroyCacheConnections()
{
	int i;
	for (i = 0; i < g_MaxCacheConns; i++) {
		DestroyConnectionInfo(g_CacheConnections[i]);
		g_CacheConnections[i] = NULL;
	}
	return TRUE;
}
/*-----------------------------------------------------------------*/
ConnectionInfo* GetIdleCacheConn()
{
	int i;
	for (i = 0; i < g_MaxCacheConns; i++) {
		if (g_CacheConnections[i] == NULL) {
			continue;
		}

		CacheConnectionInfo* cci = g_CacheConnections[i]->stub;
		if (cci->pCurTrans == NULL) {
			/* found available conn */
			return g_CacheConnections[i];
		}
	}

	/* every conn is busy */
	return NULL;
}
/*-----------------------------------------------------------------*/
void PutDone(u_char* sha1name)
{
	/* free this chunk from put temp cache */
	if (GetTempDiskCache(sha1name) != NULL) {
		DeleteTempDiskCache(sha1name);
	}
}
/*-----------------------------------------------------------------*/
void IssueCacheRequest(ConnectionInfo* ci, CacheTransactionInfo* cci)
{
#define MAX_URL_LEN	(256)
	assert(ci->type == ct_cache);
	CacheConnectionInfo* cache_conn = ci->stub;
	assert(cache_conn->pCurTrans == NULL);
	/* allocate a transaction to this connnection */
	cache_conn->pCurTrans = cci;
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("[%d] sending %s, %s, %d bytes, ps=%d\n", ci->fd,
			GetHCMethodName(cci->method), SHA1Str(cci->sha1name),
			cci->chunkLen, cci->ps);
#endif

	/* send request */
	char* request = NULL;
	int req_len = 0;
	if (cci->method == hc_put) {
		/* PUT request */
		assert(cci->chunkLen > 0);
		assert(cci->pChunk != NULL);
		request = malloc(MAX_URL_LEN + cci->chunkLen);
		if (request == NULL) {
			NiceExit(-1, "request memory allocation failed\n");
		}
		req_len = sprintf(request,
				"WAPROX-PUT http://waprox-%s HTTP/1.1\r\n"
				/*"Connection: Close\r\n\r\n"*/
				"Content-Length: %d\r\n\r\n",
				SHA1Str(cci->sha1name), cci->chunkLen);
		/* attach chunk body */
		memcpy(request + req_len, cci->pChunk, cci->chunkLen);
		req_len += cci->chunkLen;
	}
	else if (cci->method == hc_get) {
		/* GET request */
		assert(cci->pChunk == NULL);
		request = malloc(MAX_URL_LEN);
		if (request == NULL) {
			NiceExit(-1, "request memory allocation failed\n");
		}
		req_len = sprintf(request,
				"WAPROX-GET http://waprox-%s HTTP/1.1\r\n\r\n",
				SHA1Str(cci->sha1name));
	}
	else if (cci->method == hc_peek) {
		/* PEEK request */
		request = malloc(MAX_URL_LEN);
		if (request == NULL) {
			NiceExit(-1, "request memory allocation failed\n");
		}
		req_len = sprintf(request,
				"WAPROX-PEEK http://waprox-%s HTTP/1.1\r\n\r\n",
				SHA1Str(cci->sha1name));
	}
	else {
		assert(FALSE);
	}

	g_NumCacheRequestSent++;
	SendData(ci, request, req_len);
	UpdateCurrentTime(&cache_conn->lastAccessTime);
}
/*-----------------------------------------------------------------*/
void ScheduleCacheRequest(CacheTransactionInfo* cache_info, char addTail)
{
	/* queue the request based on the priority */
	if (cache_info->method == hc_get || cache_info->method == hc_peek) {
		if (addTail) {
			TAILQ_INSERT_TAIL(&g_FirstClassRequestList,
					cache_info, firstList);
		}
		else {
			TAILQ_INSERT_HEAD(&g_FirstClassRequestList,
					cache_info, firstList);
		}
		g_NumPendingFirstTransactions++;
	}
	else if (cache_info->method == hc_put) {
		if (addTail) {
			TAILQ_INSERT_TAIL(&g_SecondClassRequestList,
					cache_info, secondList);
		}
		else {
			TAILQ_INSERT_TAIL(&g_SecondClassRequestList,
					cache_info, secondList);
		}
		g_NumPendingSecondTransactions++;
	}
	else {
		assert(FALSE);
	}
}
/*-----------------------------------------------------------------*/
void SendCacheRequest(hcMethod method, processState ps,
		u_char* sha1name, int length, char* pChunk, int dstPxyId,
		ConnectionInfo* sci, ChunkTreeInfo* cti, int index,
		void* pOpaque, int opaqueLen)
{		
	/* create cache transaction info */
	CacheTransactionInfo* cache_info = CreateCacheTransactionInfo(
			method, ps, dstPxyId, sha1name, length, pChunk,
			sci, cti, index, pOpaque, opaqueLen);
	
	/* queue the request info and issue it */
	ScheduleCacheRequest(cache_info, TRUE);
	IssuePendingCacheRequests();
}
/*-----------------------------------------------------------------*/
void PutDisk(u_char* digest, char* buffer, int buf_len)
{	
	if (GetTempDiskCache(digest) != NULL) {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("%s already in progress\n", SHA1Str(digest));
#endif
		return;
	}

	/* if put queue too long, discard it here  */
	if (g_NumPendingSecondTransactions > MAX_PUT_DISK_QUEUE) {
		g_TotalPutDiscarded += buf_len;
		return;
	}

	/* keep this chunk in temp mem cache until PUT operation finishes */
	PutTempDiskCache(digest, buffer, buf_len);

	/* issue a new WAPROX-PUT request */
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("queue put %s\n", SHA1Str(digest));
#endif
	SendCacheRequest(hc_put, ps_none, digest, buf_len,
			buffer, -1, NULL, NULL, 0, NULL, 0);

#if 0
	/* peek first, then put */
	SendCacheRequest(hc_peek, ps_putDisk, digest,
			buf_len, buffer, NULL, NULL, 0, NULL, 0);
	TRACE("send peek before put %s\n", SHA1Str(digest));
#endif
}
/*-----------------------------------------------------------------*/
CacheTransactionInfo* CreateCacheTransactionInfo(hcMethod method,
		processState ps, int dstPxyId, u_char* sha1name, int chunkLen,
		char* pChunk, ConnectionInfo* sci, ChunkTreeInfo* cti,
		int index, void* pOpaque, int opaqueLen)
{
	/* create CacheTransactionInfo and fill it */
	CacheTransactionInfo* cci = malloc(sizeof(CacheTransactionInfo));
	if (cci == NULL) {
		NiceExit(-1, "CacheTransactionInfo malloc failed\n");
	}

	cci->sourceCI = sci;
	cci->method = method;

	/* fill cid if user connection */
	if (sci != NULL) {
		if (sci->type == ct_user) {
			assert(ps == ps_nameTreeDelivery);
			UserConnectionInfo* uci = sci->stub;
			assert(uci != NULL);
			cci->cid.srcIP = uci->dip;
			cci->cid.srcPort = uci->dport;
			cci->cid.dstIP = uci->sip;
			cci->cid.dstPort = uci->sport;
		}
		else {
			assert(ps == ps_bodyRequest || ps == ps_peekRequest);
			memset(&cci->cid, 0, sizeof(ConnectionID));
		}
	}

	UpdateCurrentTime(&cci->sentTime);
	cci->ps = ps;
	cci->chunkLen = chunkLen;
	memcpy(cci->sha1name, sha1name, SHA1_LEN);
	cci->index = index;
	cci->pCTInfo = cti;
	cci->dstPxyId = dstPxyId;
	if (pChunk != NULL) {
		assert(cci->chunkLen > 0);
		cci->pChunk = malloc(cci->chunkLen);
		if (cci->pChunk == NULL) {
			NiceExit(-1, "chunk memory allocation failed\n");
		}
		memcpy(cci->pChunk, pChunk, chunkLen);
	}
	else {
		cci->pChunk = NULL;
	}

#ifdef _WAPROX_RESPONSE_FIFO
	cci->pOpaque = pOpaque;
	cci->opaqueLen = opaqueLen;
#else
	if (opaqueLen > 0) {
		assert(pOpaque != NULL);
		cci->pOpaque = malloc(opaqueLen);
		if (cci->pOpaque == NULL) {
			NiceExit(-1, "opaque memory allocation failed\n");
		}
		memcpy(cci->pOpaque, pOpaque, opaqueLen);
		cci->opaqueLen = opaqueLen;
	}
	else {
		cci->pOpaque = NULL;
		cci->opaqueLen = 0;
	}
#endif

	return cci;
}
/*-----------------------------------------------------------------*/
void DestroyCacheTransactionInfo(CacheTransactionInfo* cci)
{
	if (cci->pChunk != NULL) {
		free(cci->pChunk);
	}

	if (cci->pOpaque != NULL) {
		free(cci->pOpaque);
	}

	free(cci);
}
/*-----------------------------------------------------------------*/
void IssuePendingCacheRequests()
{
	CacheTransactionInfo* walk;
	CacheRequestList deadList;

	/* NOTE: strictly priority queue
	   always favor peek/get over put 
	   put can starve, but we don't care. */

	/* 1. always try first class request first */
	TAILQ_INIT(&deadList);
	TAILQ_FOREACH(walk, &g_FirstClassRequestList, firstList) {
		ConnectionInfo* ci = GetIdleCacheConn();
		if (ci != NULL) {
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("resume first class request: %d remains\n",
					g_NumPendingFirstTransactions);
#endif
			IssueCacheRequest(ci, walk);
			TAILQ_INSERT_TAIL(&deadList, walk, deadList);
			g_NumPendingFirstTransactions--;
			g_PdNumFirstRequests++;
			assert(g_NumPendingFirstTransactions >= 0);
		}
		else {
			break;
		}
	}

	while (!TAILQ_EMPTY(&deadList)) {
		walk = TAILQ_FIRST(&deadList);
		TAILQ_REMOVE(&deadList, walk, deadList);
		TAILQ_REMOVE(&g_FirstClassRequestList, walk, firstList);
	}

	/* 2. try second class request */
	TAILQ_INIT(&deadList);
	TAILQ_FOREACH(walk, &g_SecondClassRequestList, secondList) {
		ConnectionInfo* ci = GetIdleCacheConn();
		if (ci != NULL) {
#ifdef _WAPROX_DEBUG_OUTPUT
			TRACE("resume second class request: %d remains\n",
					g_NumPendingSecondTransactions);
#endif
			IssueCacheRequest(ci, walk);
			TAILQ_INSERT_TAIL(&deadList, walk, deadList);
			g_NumPendingSecondTransactions--;
			g_PdNumSecondRequests++;
			assert(g_NumPendingSecondTransactions >= 0);
		}
		else {
			break;
		}
	}

	while (!TAILQ_EMPTY(&deadList)) {
		walk = TAILQ_FIRST(&deadList);
		TAILQ_REMOVE(&deadList, walk, deadList);
		TAILQ_REMOVE(&g_SecondClassRequestList, walk, secondList);
	}
}
/*-----------------------------------------------------------------*/
int ResolveOneLevelDisk(ChunkTreeInfo* cti, int level,
		ConnectionInfo* target_ci)
{
	OneChunkName* names = cti->pNames;
	int i, newIdx;

	/* generate chunk name list to lookup */	
	/* TODO: check memory cache first for optimization */
	int* chunksToLookup = ResolveOneLevel(cti, level);
	int skip = 0;
	for (i = 0; i < cti->numChunkUnresolved; i++) {
		newIdx = chunksToLookup[i];
		SendCacheRequest(hc_get, ps_nameTreeDelivery,
				names[newIdx].sha1name,
				names[newIdx].length, NULL, -1,
				target_ci, cti, newIdx,
				NULL, 0);
	}

	cti->numChunkUnresolved -= skip;
	assert(cti->numChunkUnresolved >= 0);
	if (cti->numChunkUnresolved == 0) {
		/* all done this level */
		return TRUE;
	}
	else {
		/* check the next level */
		return FALSE;
	}
}
/*-----------------------------------------------------------------*/
