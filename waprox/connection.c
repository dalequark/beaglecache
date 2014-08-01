#include "connection.h"
#include "hashtable.h"
#include <assert.h>
#include "peer.h"
#include "log.h"
#include "chunkcache.h"
#include "config.h"
#include "disk.h"
#ifdef _WAPROX_USE_SCTP
	#include <sctp.h>
	#include "sctpglue.h"
#endif
#include "waprox.h"

/* user connection list */
unsigned int g_ConnectionID = 0;
ConnectionList g_UserList;

/* dead connection list */
ConnectionList g_DeadConnList;

/* pptpd NAT connection info */
ConnectionInfo* g_PptpdNatCI = NULL;

/* connection table */
#define CONNECTIONMAP_SIZE	(65536)

/* pseudo and global connection table */
struct hashtable* g_PseudoConnMap;
struct hashtable* g_ConnMap;
int g_NumUserConns = 0;
int g_NumDeadConns = 0;
int g_NumPeerConns = 0;
int g_NumCacheConns = 0;
int g_NumDnsConns = 0;

/* master ci set */
ConnectionInfo* g_MasterCIsetByFD[MAX_NUM_CI];
myEvent g_MyEventSet[MAX_NUM_CI];

/* cache connections */
ConnectionInfo* g_CacheConnections[MAX_CACHE_CONNS];

/* dns connections */
ConnectionInfo* g_DnsHelperConnections[MAX_DNS_CONNS];

/* disk connections */
ConnectionInfo* g_DiskHelperCI = NULL;

/*-----------------------------------------------------------------*/
void InitConns()
{
	TAILQ_INIT(&g_UserList);
	TAILQ_INIT(&g_DeadConnList);
	g_PseudoConnMap = NULL;
	g_ConnMap = NULL;
	g_NumUserConns = 0;
	g_NumPeerConns = 0;
	g_NumCacheConns = 0;
	g_NumDnsConns = 0;

	int i;
	for (i = 0; i < MAX_NUM_CI; i++) {
		g_MasterCIsetByFD[i] = NULL;
	}

	for (i = 0; i < MAX_CACHE_CONNS; i++) {
		g_CacheConnections[i] = NULL;
	}
}
/*-----------------------------------------------------------------*/
static unsigned int conn_hash_from_key_fn(void* k)
{
	ConnectionKey* key = (ConnectionKey*)k;
	int i = 0;
	int hash = 0;
	/* for ip */
	for (i = 0; i < sizeof(unsigned int); i++) {
		hash += *(((unsigned char*)&(key->sip)) + i);
		hash += *(((unsigned char*)&(key->dip)) + i);
	}
	/* for port */
	for (i = 0; i < sizeof(unsigned short); i++) {
		hash += *(((unsigned char*)&(key->sport)) + i);
		hash += *(((unsigned char*)&(key->dport)) + i);
	}
	return hash;
}
/*-----------------------------------------------------------------*/
static int conn_keys_equal_fn(void* key1, void* key2)
{
	ConnectionKey* k1 = (ConnectionKey*)key1;
	ConnectionKey* k2 = (ConnectionKey*)key2;
	if (k1->sip == k2->sip && k1->sport == k2->sport &&
		k1->dip == k2->dip && k1->dport == k2->dport)
		return 1;
	return 0;
}
/*-----------------------------------------------------------------*/
int CreateConnectionTables()
{
	if (g_PseudoConnMap != NULL || g_ConnMap != NULL) {
		NiceExit(-1, "connection map already created\n");
		return FALSE;
	}

	g_PseudoConnMap = create_hashtable(CONNECTIONMAP_SIZE,
			conn_hash_from_key_fn, conn_keys_equal_fn);
	if (g_PseudoConnMap == NULL) {
		NiceExit(-1, "pseudo connection map creation failed\n");
		return FALSE;
	}

	g_ConnMap = create_hashtable(CONNECTIONMAP_SIZE,
			conn_hash_from_key_fn, conn_keys_equal_fn);
	if (g_ConnMap == NULL) {
		NiceExit(-1, "global connection map creation failed\n");
		return FALSE;
	}

	return TRUE;
}
/*-----------------------------------------------------------------*/
int DestroyConnectionTables()
{
	if (g_PseudoConnMap == NULL || g_ConnMap == NULL) {
		NiceExit(-1, "CreatePseudoConnectionTable() first\n");
		return FALSE;
	}

	hashtable_destroy(g_PseudoConnMap, 1);
	hashtable_destroy(g_ConnMap, 1);
	return TRUE;
}
/*-----------------------------------------------------------------*/
int PutConnection(struct hashtable* ht, ConnectionInfo* ci)
{
	/* create a key */
	ConnectionKey* key = (ConnectionKey*)malloc(sizeof(ConnectionKey));
	if (key == NULL) {
		NiceExit(-1, "key allocation failed\n");
	}

	assert(ci->type == ct_user);
	UserConnectionInfo* uci = ci->stub;
	key->sip = uci->sip;
	key->sport = uci->sport;
	key->dip = uci->dip;
	key->dport = uci->dport;

	/* put the new entry in ht */
	if (hashtable_insert(ht, key, ci) == 0) {
		NiceExit(-1, "hashtable_insert failed\n");
		return FALSE;
	}
	else {
		return TRUE;
	}
}
/*-----------------------------------------------------------------*/
ConnectionInfo* SearchConnection(struct hashtable* ht,
		unsigned int sip, unsigned short sport, 
		unsigned int dip, unsigned short dport)
{
	ConnectionKey key;
	key.sip = sip;
	key.sport = sport;
	key.dip = dip;
	key.dport = dport;
	return hashtable_search(ht, &key);
}
/*-----------------------------------------------------------------*/
int RemoveConnection(struct hashtable* ht,
	unsigned int sip, unsigned short sport,
	unsigned int dip, unsigned short dport)
{
	ConnectionKey key;
	key.sip = sip;
	key.sport = sport;
	key.dip = dip;
	key.dport = dport;

	/* delete */
	if (hashtable_remove(ht, &key) == NULL) {
		/*
		NiceExit(-1, "hashtable_remove: not found\n");
		*/
		return FALSE;
	}

	return TRUE;
}
/*-----------------------------------------------------------------*/
void PrintConnectionTableStats()
{
	fprintf(stderr, "-# of User Connections: %d\n", g_NumUserConns);
	fprintf(stderr, "-# of Peer Connections: %d\n", g_NumPeerConns);
	fprintf(stderr, "-# of Cache Connections: %d\n", g_NumCacheConns);
	fprintf(stderr, "-# of DNS Connections: %d\n", g_NumDnsConns);
}
/*-----------------------------------------------------------------*/
ConnectionInfo* CreateNewConnectionInfo(int newfd, connectionType type)
{
	/* create a new connection info */
	ConnectionInfo *ci = malloc(sizeof(ConnectionInfo));
	if (ci == NULL) {
		NiceExit(-1, "ConnectionInfo memory allocation failed\n");
	}

	/* create a read buffer for this connection */
	ci->bufRead = malloc(RECV_BUFSIZE);
	if (ci->bufRead == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}
	ci->lenRead = 0;

	/* init message queue */
	TAILQ_INIT(&(ci->msgListWrite));
	ci->firstMsgOffset = 0;
	ci->lenWrite = 0;

	/* initialize fields */
	ci->fd = newfd;
	ci->connID = g_ConnectionID++;
	ci->type = type;
	ci->stub = NULL;
	ci->status = cs_active;
	ci->isWritable = TRUE;
	ci->isAbnormal = FALSE;

	if (newfd != -1) {
		/* make it non-blocking */
		if (fcntl(newfd, F_SETFL, O_NDELAY) < 0) {
			char buf[1024];
			memset(buf, 0, sizeof(buf));
			sprintf(buf, "failed fcntl'ing socket - %d\n", errno);
			NiceExit(-1, buf);
		}

		/* increase socket buffer size */
		if (SetSocketBufferSize(newfd, SOCKBUF_SIZE) != 0) {
			NiceExit(-1, "SetSocketBufferSize failed\n");
		}

		/* add this to the master set */
		assert(g_MasterCIsetByFD[newfd] == NULL);
		g_MasterCIsetByFD[newfd] = ci;
		assert(g_MyEventSet[newfd].esRead == es_none);
		assert(g_MyEventSet[newfd].esWrite == es_none);

#ifdef _WAPROX_USE_SCTP
		/* register SCTP read callback */
		g_MyEventSet[newfd].sctp_event = POLLIN | POLLPRI | POLLERR;
		SctpAddReadOnlyEvent(newfd, &SctpUserEventDispatchCB);
#endif
	}

	TRACE("new connection id: %d\n", ci->connID);
	return ci;
}
/*-----------------------------------------------------------------*/
void CreateUserConnectionInfo(ConnectionInfo* ci, int isForward)
{
	/* create UserConnectionInfo and fill it */
	assert(ci->type == ct_user);
	ci->stub = malloc(sizeof(UserConnectionInfo));
	if (ci->stub == NULL) {
		NiceExit(-1, "UserConnectionInfo memory allocation failed\n");
	}

	UserConnectionInfo* uci = ci->stub;

	/* initialize fields */
	uci->numChunks = 0;
	uci->isForward = isForward;
	uci->isHttpProxy = FALSE;
	uci->bytesTX = 0;
	uci->bytesRX = 0;
	uci->closeBytes = 0;
	uci->numHits = 0;
	uci->numMisses = 0;
	uci->hitBytes = 0;
	uci->missBytes = 0;
	uci->numMoves = 0;
	uci->moveBytes = 0;
	uci->numSent = 0;
	uci->lastAcked = 0;
	uci->lastCumulativeAck = 0;
	uci->dstPxyIdx = -1;
	uci->nameFlowId = -1;
	#if 0
	uci->nameCI = NULL;
	#endif
	uci->pendingNames = 0;
	uci->pendingBytes = 0;
	uci->numChunkBuffers = 0;
	uci->seq_no = 0;
	TAILQ_INIT(&(uci->chunkBufferList));
	TAILQ_INIT(&(uci->reassembleList));
	TAILQ_INIT(&(uci->readyList));
	TAILQ_INIT(&(uci->controlMsgList));

	/* init MRC context */
	memset(&(uci->rctx), 0, sizeof(RabinCtx));
	InitRabinCtx(&(uci->rctx), g_MinChunkSize, WAPROX_RABIN_WINDOW,
			WAPROX_MRC_MIN, WAPROX_MRC_MAX);
	memset(&(uci->rctx4put), 0, sizeof(RabinCtx));
	InitRabinCtx(&(uci->rctx4put), g_MinChunkSize, WAPROX_RABIN_WINDOW,
			WAPROX_MRC_MIN, WAPROX_MRC_MAX);

	/* set max window size */
	uci->maxWindowSize = MAX_WINDOW_SIZE;

	/* record the current time */
	UpdateCurrentTime(&(uci->startTime));
	UpdateCurrentTime(&(uci->lastAccessTime));

	/* bezero ip/port info */
	uci->sip = 0;
	uci->sport = 0;
	uci->dip = 0;
	uci->dport = 0;

	/* add it to the user connection list */
	TAILQ_INSERT_TAIL(&g_UserList, ci, ciList);
	g_NumUserConns++;
	g_NumTotUserConns++;
	TRACE("[%d] new user connection\n", ci->connID);
}
/*-----------------------------------------------------------------*/
void CreateControlConnectionInfo(ConnectionInfo* ci)
{
	/* create ControlConnectionInfo and fill it */
	assert(ci->type == ct_control);
	ci->stub = malloc(sizeof(ControlConnectionInfo));
	if (ci->stub == NULL) {
		NiceExit(-1, "ControlConnectionInfo allocation failed\n");
	}

	ControlConnectionInfo* nci = ci->stub;
	nci->peerIdx = -1;
	nci->flowIdx = -1;
	nci->assocID = -1;
	nci->assocState = sas_none;
	g_NumPeerConns++;

	/* create connection table */
	#if 0
	nci->connmap = create_hashtable(CONNECTIONMAP_SIZE,
		conn_hash_from_key_fn, conn_keys_equal_fn);
	if (nci->connmap  == NULL) {
		NiceExit(-1, "connection table creation failed\n");
	}
	#endif
}
/*-----------------------------------------------------------------*/
void CreateChunkConnectionInfo(ConnectionInfo* ci)
{
	/* create ChunkConnectionInfo and fill it */
	assert(ci->type == ct_chunk);
	ci->stub = malloc(sizeof(ChunkConnectionInfo));
	if (ci->stub == NULL) {
		NiceExit(-1, "ChunkConnectionInfo memory allocation failed\n");
	}

	ChunkConnectionInfo* cci = ci->stub;
	cci->peerIdx = -1;
	cci->flowIdx = -1;
	cci->assocID = -1;
	cci->assocState = sas_none;
	g_NumPeerConns++;

	#if 0
	/* create connection table */
	cci->connmap = create_hashtable(CONNECTIONMAP_SIZE,
		conn_hash_from_key_fn, conn_keys_equal_fn);
	if (cci->connmap  == NULL) {
		NiceExit(-1, "connection table creation failed\n");
	}
	#endif
}
/*-----------------------------------------------------------------*/
void CreateCacheConnectionInfo(ConnectionInfo* ci, int index)
{
	/* create CacheConnectionInfo and fill it */
	assert(ci->type == ct_cache);
	ci->stub = malloc(sizeof(CacheConnectionInfo));
	if (ci->stub == NULL) {
		NiceExit(-1, "CacheConnectionInfo memory allocation failed\n");
	}

	CacheConnectionInfo* cci = ci->stub;
	cci->pCurTrans = NULL;
	cci->cacheConnIdx = index;
	assert(g_CacheConnections[index] == ci);
	g_NumCacheConns++;
	UpdateCurrentTime(&(cci->lastAccessTime));
}
/*-----------------------------------------------------------------*/
void CreateDnsConnectionInfo(ConnectionInfo* ci)
{
	/* create DnsConnectionInfo and fill it */
	assert(ci->type == ct_dns);
	ci->stub = malloc(sizeof(DnsConnectionInfo));
	if (ci->stub == NULL) {
		NiceExit(-1, "DnsConnectionInfo memory allocation failed\n");
	}

	DnsConnectionInfo* dci = ci->stub;
	dci->pCurTrans = NULL;
	g_NumDnsConns++;
	assert(g_NumDnsConns < MAX_DNS_CONNS);
}
/*-----------------------------------------------------------------*/
void DestroyConnectionInfo(ConnectionInfo* ci)
{
	if (ci->type == ct_user) {
		DestroyUserConnectionInfo(ci);
	}
	else if (ci->type == ct_control) {
		DestroyControlConnectionInfo(ci);
	}
	else if (ci->type == ct_chunk) {
		DestroyChunkConnectionInfo(ci);
	}
	else if (ci->type == ct_pptpd) {
		NiceExit(-1, "this shouldn't be happened\n");
	}
	else if (ci->type == ct_cache) {
		DestroyCacheConnectionInfo(ci);
	}
	else if (ci->type == ct_dns) {
		DestroyDnsConnectionInfo(ci);
	}
	else if (ci->type == ct_disk) {
		NiceExit(-1, "this shouldn't be happened\n");
	}
	else {
		assert(FALSE);
	}

	/* delete from the master set */
	assert(g_MasterCIsetByFD[ci->fd] == ci);
	g_MasterCIsetByFD[ci->fd] = NULL;
	if (g_MyEventSet[ci->fd].esRead != es_none) {
		if (event_del(&g_MyEventSet[ci->fd].evRead) != 0) {
			NiceExit(-1, "event_del faield\n");
		}
		g_MyEventSet[ci->fd].esRead = es_none;
	}
	if (g_MyEventSet[ci->fd].esWrite != es_none) {
		if (event_del(&g_MyEventSet[ci->fd].evWrite) != 0) {
			NiceExit(-1, "event_del faield\n");
		}
		g_MyEventSet[ci->fd].esWrite = es_none;
	}

	if (ci->fd != -1) {
		if (ci->type == ct_user) {
#ifdef _WAPROX_USE_SCTP
			/* unregister SCTP callbacks */
			sctp_unregisterUserCallback(ci->fd);
#endif
		}

		if (close(ci->fd) != 0) {
			perror("close");
			NiceExit(-1, "close failure\n");
		}
	}

	/* destroy write message list */
	while (!TAILQ_EMPTY(&(ci->msgListWrite))) {
		MessageInfo* mi = TAILQ_FIRST(&(ci->msgListWrite));
		TAILQ_REMOVE(&(ci->msgListWrite), mi, messageList);
		free(mi->msg);
		free(mi);
	}

#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("[%d] %d us taken closing\n", ci->connID,
			GetElapsedUS(&ci->timestamp));
#endif

	if (ci->bufRead != NULL) {
		free(ci->bufRead);
	}
	free(ci);
}
/*-----------------------------------------------------------------*/
void DestroyUserConnectionInfo(ConnectionInfo* ci)
{
	assert(ci->type == ct_user);

	/* remove it from the connmap */
	UserConnectionInfo* uci = ci->stub;
	#if 0
	ControlConnectionInfo* nci = uci->nameCI->stub;
	RemoveConnection(nci->connmap, uci->sip, uci->sport,
			uci->dip, uci->dport);
	#endif
	RemoveConnection(g_ConnMap, uci->sip, uci->sport,
			uci->dip, uci->dport);
	RemoveConnection(g_PseudoConnMap, uci->sip, uci->sport,
			uci->dip, uci->dport);

	/* destroy reassmeble list */
	DestroyReassembleList(&(uci->reassembleList));

	/* destroy the chunk buffer list */
	/* don't update name hint
	   if the connection is abnoramlly terminated */
	int numDestroyed = DestroyChunkBufferList(&(uci->chunkBufferList),
			0, !ci->isAbnormal);
	uci->numChunkBuffers -= numDestroyed;
	assert(uci->numChunkBuffers == 0);

	/* remove from the global user list */
	TAILQ_REMOVE(&g_UserList, ci, ciList);
	g_NumUserConns--;
	assert(g_NumUserConns >= 0);

	/* remove from the proxy user list */
	if (uci->dstPxyIdx != -1) {
		TAILQ_REMOVE(&g_PeerInfo[uci->dstPxyIdx].userList, ci, ciList2);
		g_PeerInfo[uci->dstPxyIdx].numUsers--;
		assert(g_PeerInfo[uci->dstPxyIdx].numUsers >= 0);
	}
	assert(TAILQ_EMPTY(&(uci->controlMsgList)) == TRUE);

	if (ci->status == cs_block) {
		g_NumBlocked--;
		assert(g_NumBlocked >= 0);
	}

	/* log user session info */
	UpdateCurrentTime(&(uci->lastAccessTime));
	LogSessionInfo(uci);
	free(uci);

#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("[%d] destroyed\n", ci->connID);
#endif
}
/*-----------------------------------------------------------------*/
void DestroyControlConnectionInfo(ConnectionInfo* ci)
{
	assert(ci->type == ct_control);
	/* make PeerInfo's ci NULL */
	ControlConnectionInfo* nci = ci->stub;
	if (nci->peerIdx != -1 && nci->flowIdx != -1) {
		g_PeerInfo[nci->peerIdx].nameCI[nci->flowIdx] = NULL;
	}
	g_NumPeerConns--;
	assert(g_NumPeerConns >= 0);

	/* destroy connection table */
	#if 0
	assert(nci->connmap != NULL);
	hashtable_destroy(nci->connmap, 1);
	#endif
	free(nci);
}
/*-----------------------------------------------------------------*/
void DestroyChunkConnectionInfo(ConnectionInfo* ci)
{
	assert(ci->type == ct_chunk);
	/* make PeerInfo's ci NULL */
	ChunkConnectionInfo* cci = ci->stub;
	if (cci->peerIdx != -1 && cci->flowIdx != -1) {
		assert(cci->flowIdx != -1);
		g_PeerInfo[cci->peerIdx].chunkCI[cci->flowIdx] = NULL;
	}
	g_NumPeerConns--;
	assert(g_NumPeerConns >= 0);

	#if 0
	/* destroy connection table */
	assert(cci->connmap != NULL);
	hashtable_destroy(cci->connmap, 1);
	#endif
	free(cci);
}
/*-----------------------------------------------------------------*/
void DestroyCacheConnectionInfo(ConnectionInfo* ci)
{
	assert(ci->type == ct_cache);
	CacheConnectionInfo* cci = ci->stub;
	CacheTransactionInfo* cache_tinfo = cci->pCurTrans;

	g_CacheConnections[cci->cacheConnIdx] = NULL;
	g_NumCacheConns--;
	assert(g_NumCacheConns >= 0);
	if (cache_tinfo != NULL) {
		/* oops */
		TRACE("[%d] oops! - let's try this again: %s\n",
				ci->fd, SHA1Str(cache_tinfo->sha1name));

		/* retry this: add at head */
		ScheduleCacheRequest(cache_tinfo, FALSE);
		IssuePendingCacheRequests();
		#if 0
		/* regard this as cache miss */
		PostProcessCacheGET(cache_tinfo, FALSE, NULL,
				cache_tinfo->chunkLen);
		#endif
		
		#if 0
		/* add to the head of the queue */
		TAILQ_INSERT_HEAD(&g_CacheRequestList, cci->pCurTrans, reqList);

		/* allocate this request to existing one */
		IssuePendingRequests();
		#endif

		#if 0
		/* connect right away in this case */
		CheckDiskCacheConnections();
		#endif
	}
	else {
		/* just allocate one */
		IssuePendingCacheRequests();
	}

	free(cci);
	TRACE("cache connection close[%d], total: %d\n",
			ci->fd, g_NumCacheConns);
}
/*-----------------------------------------------------------------*/
void DestroyDnsConnectionInfo(ConnectionInfo* ci)
{
	NiceExit(-1, "stub\n");
}
/*-----------------------------------------------------------------*/
void SetDead(ConnectionInfo* ci, char abnormal)
{
	assert(ci != NULL);
	assert(g_MasterCIsetByFD[ci->fd] == ci);
	if (ci->status == cs_closed) {
		/* we've already set this dead before */
		return;
	}
	ci->status = cs_closed;
	ci->isAbnormal = abnormal;
	if (ci->type == ct_user && ci->isAbnormal) {
		/* if user connection is abnormally dead */
		/* 1. remove all pending control messages */
		UserConnectionInfo* uci = ci->stub;
		MessageInfo* mi = NULL;
		assert(uci != NULL);
		while (!TAILQ_EMPTY(&(uci->controlMsgList))) {
			mi = TAILQ_FIRST(&(uci->controlMsgList));
			assert(mi != NULL);
			TAILQ_REMOVE(&(uci->controlMsgList), mi,
					messageList);
			if (mi->msg != NULL) {
				free(mi->msg);
			}
			free(mi);
		}

		/* 2. inform the other side */
		SendWACloseConnection(ci, 0);
	}

	TAILQ_INSERT_TAIL(&g_DeadConnList, ci, ciDead);
	UpdateCurrentTime(&ci->timestamp);
	g_NumDeadConns++;
	assert(g_NumDeadConns < MAX_NUM_CI);
}
/*-----------------------------------------------------------------*/
