#include "chunkrequest.h"
#include "hashtable.h"
#include "hashtable_itr.h"
#include "chunkcache.h"
#include "peer.h"
#include "ils.h"
#include <assert.h>

/*-----------------------------------------------------------------*/
struct hashtable* chunk_request_ht = NULL;
struct hashtable* peek_request_ht = NULL;
int g_NumChunkRequestsInFlight = 0;
int g_NumPeekRequestsInFlight = 0;
/*-----------------------------------------------------------------*/
int CreateChunkRequestTable()
{
	chunk_request_ht = create_hashtable(65536,
			chunk_hash_from_key_fn_raw, chunk_keys_equal_fn_raw);
	if (chunk_request_ht == NULL) {
		NiceExit(-1, "chunk request hashtable creation failed\n");
		return FALSE;
	}

	peek_request_ht = create_hashtable(65536,
			chunk_hash_from_key_fn_raw, chunk_keys_equal_fn_raw);
	if (peek_request_ht == NULL) {
		NiceExit(-1, "peek request hashtable creation failed\n");
		return FALSE;
	}

	return TRUE;
}
/*-----------------------------------------------------------------*/
int DestroyChunkRequestTable()
{
	if (chunk_request_ht == NULL || peek_request_ht == NULL) {
		NiceExit(-1, "CreateChunkRequestTable first\n");
		return FALSE;
	}

	/* iterate the table and free the list */
	if (hashtable_count(chunk_request_ht) > 0) {
		struct hashtable_itr* itr = hashtable_iterator(
				chunk_request_ht);
		if (itr == NULL) {
			NiceExit(-1, "itr failed\n");
			return FALSE;
		}
		do {
			u_char* k = hashtable_iterator_key(itr);
			DestroyChunkRequestList(k);
		} while (hashtable_iterator_advance(itr));
	}
	hashtable_destroy(chunk_request_ht, 1);

	/* iterate the table and free the list */
	if (hashtable_count(peek_request_ht) > 0) {
		struct hashtable_itr* itr = hashtable_iterator(
				peek_request_ht);
		if (itr == NULL) {
			NiceExit(-1, "itr failed\n");
			return FALSE;
		}
		do {
			u_char* k = hashtable_iterator_key(itr);
			DestroyPeekRequestList(k);
		} while (hashtable_iterator_advance(itr));
	}
	hashtable_destroy(peek_request_ht, 1);
	
	return TRUE;
}
/*-----------------------------------------------------------------*/
int PutChunkRequest(u_char* sha1name, ConnectionID cid, char isRedirected,
		in_addr_t pxyAddr, int length, int peerIdx, char isNetwork)
{
	ChunkRequestList* list = GetChunkRequestList(sha1name);
	if (list == NULL) {
		/* create a key */
		u_char* key = malloc(sizeof(char) * SHA1_LEN);
		if (key == NULL) {
			NiceExit(-1, "sha1 key allocation failed\n");
		}
		memcpy(key, sha1name, SHA1_LEN);

		/* create an empty list */
		list = malloc(sizeof(ChunkRequestList));
		if (list == NULL) {
			NiceExit(-1, "list value allocation failed\n");
		}
		TAILQ_INIT(list);

		/* insert the pair into the table */
		if (hashtable_insert(chunk_request_ht, key, list) == 0) {
			NiceExit(-1, "hashtable_insert failed\n");
		}
	}
	assert(list != NULL);

	/* create a new entry */
	ChunkRequestInfo* cri = malloc(sizeof(ChunkRequestInfo));
	if (cri == NULL) {
		NiceExit(-1, "ChunkRequestInfo allocation failed\n");
	}

	/* fill the value of the entry */
	cri->isRedirected = isRedirected;
	cri->length = length;
	cri->isNetwork = isNetwork;
	cri->peerIdx = peerIdx;
	assert(peerIdx != -1);
	memcpy(&cri->cid, &cid, sizeof(ConnectionID));
	memcpy(&cri->pxyAddr, &pxyAddr, sizeof(in_addr_t));
	UpdateCurrentTime(&cri->sentTime);

	/* update load stat */
	if (isNetwork) {
		g_PeerInfo[peerIdx].networkPendingBytes += length +
			PerChunkResponseOverhead;
	}
	else {
		g_PeerInfo[peerIdx].diskPendingChunks++;
	}

	/* append the new info to the list */
	TAILQ_INSERT_TAIL(list, cri, reqList);
	g_NumChunkRequestsInFlight++;
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("# chunk requests in flight: %d\n", g_NumChunkRequestsInFlight);
#endif
	return TRUE;

	/* TODO: when timeout, retry */
}
/*-----------------------------------------------------------------*/
int PutPeekRequest(ReassembleInfo* ri, ChunkNameInfo* cni, u_char* sha1name,
		ConnectionID cid)
{
	PeekRequestList* list = GetPeekRequestList(sha1name);
	if (list == NULL) {
		/* create a key */
		u_char* key = malloc(sizeof(char) * SHA1_LEN);
		if (key == NULL) {
			NiceExit(-1, "sha1 key allocation failed\n");
		}
		memcpy(key, sha1name, SHA1_LEN);

		/* create an empty list */
		list = malloc(sizeof(PeekRequestList));
		if (list == NULL) {
			NiceExit(-1, "list value allocation failed\n");
		}
		TAILQ_INIT(list);

		/* insert the pair into the table */
		if (hashtable_insert(peek_request_ht, key, list) == 0) {
			NiceExit(-1, "hashtable_insert failed\n");
		}
	}
	assert(list != NULL);

	/* create a new entry */
	PeekRequestInfo* pri = malloc(sizeof(PeekRequestInfo));
	if (pri == NULL) {
		NiceExit(-1, "PeekRequestInfo allocation failed\n");
	}

	/* fill the value of the entry */
	assert(ri != NULL);
	pri->ri = ri;
	assert(cni != NULL);
	pri->cni = cni;
	memcpy(&pri->cid, &cid, sizeof(ConnectionID));
	UpdateCurrentTime(&pri->sentTime);

	/* append the new info to the list */
	TAILQ_INSERT_TAIL(list, pri , reqList);
	g_NumPeekRequestsInFlight++;
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("# peek requests in flight: %d\n", g_NumPeekRequestsInFlight);
#endif
	return TRUE;

	/* TODO: when timeout, retry */
}
/*-----------------------------------------------------------------*/
ChunkRequestList* GetChunkRequestList(u_char* sha1name)
{
	return hashtable_search(chunk_request_ht, sha1name);
}
/*-----------------------------------------------------------------*/
PeekRequestList* GetPeekRequestList(u_char* sha1name)
{
	return hashtable_search(peek_request_ht, sha1name);
}
/*-----------------------------------------------------------------*/
int DestroyChunkRequestList(u_char* sha1name)
{
	ChunkRequestList* list = hashtable_remove(chunk_request_ht, sha1name);
	if (list == NULL) {
		NiceExit(-1, "list not found\n");
		return FALSE;
	}

	ChunkRequestInfo* walk = NULL;
	while (!TAILQ_EMPTY(list)) {
		walk = TAILQ_FIRST(list);
		assert(walk != NULL);
		TAILQ_REMOVE(list, walk, reqList);
		free(walk);
		g_NumChunkRequestsInFlight--;
		assert(g_NumChunkRequestsInFlight >= 0);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("# chunk requests in flight: %d\n",
				g_NumChunkRequestsInFlight);
#endif
	}

	free(list);
	return TRUE;
}
/*-----------------------------------------------------------------*/
int DestroyPeekRequestList(u_char* sha1name)
{
	PeekRequestList* list = hashtable_remove(peek_request_ht, sha1name);
	if (list == NULL) {
		NiceExit(-1, "list not found\n");
		return FALSE;
	}

	PeekRequestInfo* walk = NULL;
	while (!TAILQ_EMPTY(list)) {
		walk = TAILQ_FIRST(list);
		TAILQ_REMOVE(list, walk, reqList);
		free(walk);
		g_NumPeekRequestsInFlight--;
		assert(g_NumPeekRequestsInFlight >= 0);
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("# peek requests in flight: %d\n",
				g_NumPeekRequestsInFlight);
#endif
	}

	free(list);
	return TRUE;
}
/*-----------------------------------------------------------------*/
