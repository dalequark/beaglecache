#include "chunkcache.h"
#include "hashtable.h"
#include "hashtable_itr.h"
#include "disk.h"
#include "config.h"
#include "waprox.h"
#include "diskhelper.h"
#include <assert.h>

int g_NumKeys = 0;
int g_CacheSize = 0;
static int _maxCacheSize = 0;
struct hashtable* send_hint_ht = NULL;
struct hashtable* disk_hint_ht = NULL;
struct hashtable* put_tmp_ht = NULL;
struct hashtable* tmp_ht = NULL;
struct hashtable* mem_ht = NULL;
typedef TAILQ_HEAD(CacheKeyFIFO, CacheKey) FifoQueue;
FifoQueue g_CacheFifo;
FifoQueue g_NameFifo;
FifoQueue g_DiskFifo;

/* function decl. */
void _RemoveHT(struct hashtable* ht, u_char* sha1name);
CacheKey* _PutHT(struct hashtable* ht, u_char* sha1name,
		char* block_buffer, unsigned int block_length);
/*-----------------------------------------------------------------*/
unsigned int chunk_hash_from_key_fn_raw(void* k)
{
	int i = 0;
	int hash = 0;
	unsigned char* sha1name = k;
	for (i = 0; i < SHA1_LEN; i++) {
		hash += *(((unsigned char*)sha1name) + i);
	}
	return hash;
}
/*-----------------------------------------------------------------*/
unsigned int chunk_hash_from_key_fn(void* k)
{
	return chunk_hash_from_key_fn_raw(((CacheKey*)k)->sha1name);
}
/*-----------------------------------------------------------------*/
int chunk_keys_equal_fn_raw(void* key1, void* key2)
{
	return (memcmp(key1, key2, SHA1_LEN) == 0);
}
/*-----------------------------------------------------------------*/
int chunk_keys_equal_fn(void* key1, void* key2)
{
	return chunk_keys_equal_fn_raw(((CacheKey*)key1)->sha1name,
			((CacheKey*)key2)->sha1name);
}
/*-----------------------------------------------------------------*/
int CreateCache(int maxCacheSize)
{
	/* create hashtable for send hint */
	send_hint_ht = create_hashtable(65536, chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (send_hint_ht == NULL) {
		NiceExit(-1, "send hint hashtable creation failed\n");
		return FALSE;
	}

	/* create hashtable for disk hint */
	disk_hint_ht = create_hashtable(65536, chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (disk_hint_ht == NULL) {
		NiceExit(-1, "disk hint hashtable creation failed\n");
		return FALSE;
	}

	/* create hashtable for put temp cache */
	put_tmp_ht = create_hashtable(65536, chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (put_tmp_ht == NULL) {
		NiceExit(-1, "put temp hashtable creation failed\n");
		return FALSE;
	}

	/* create hashtable for temp cache */
	tmp_ht = create_hashtable(65536, chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (tmp_ht == NULL) {
		NiceExit(-1, "temp hashtable creation failed\n");
		return FALSE;
	}

	/* create hashtable for memory cache */
	mem_ht = create_hashtable(65536, chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (mem_ht == NULL) {
		NiceExit(-1, "memory hashtable creation failed\n");
		return FALSE;
	}

	/* set the max size for memory cache */
	_maxCacheSize = maxCacheSize;
	
	/* init FIFO */
	TAILQ_INIT(&g_CacheFifo);
	TAILQ_INIT(&g_NameFifo);
	TAILQ_INIT(&g_DiskFifo);

	if (g_GenerateNameHint) {
		/* preload send hint if possible */
		PreloadHints();
	}

	return TRUE;
}
/*-----------------------------------------------------------------*/
int DestroyCache()
{
	if (send_hint_ht == NULL || disk_hint_ht == NULL ||
			put_tmp_ht == NULL || tmp_ht == NULL ||
			mem_ht == NULL) {
		NiceExit(-1, "CreateCache first\n");
		return FALSE;
	}

	hashtable_destroy(send_hint_ht, 1);
	hashtable_destroy(disk_hint_ht, 1);
	hashtable_destroy(put_tmp_ht, 1);
	hashtable_destroy(tmp_ht, 1);
	hashtable_destroy(mem_ht, 1);
	return TRUE;
}
/*-----------------------------------------------------------------*/
void CheckCache(int neededSize)
{
	if (g_CacheSize + neededSize <= _maxCacheSize) {
		/* we don't have to */
		return; 
	}

	/* if exceed the maximum capacity, evict the oldest one (FIFO) */
	LIST_HEAD(, CacheKey) freeList = LIST_HEAD_INITIALIZER(freeList);
	int numVictims = 0;
	int targetSize = neededSize * 2;
	assert(targetSize >= 0);
	CacheKey* walk = NULL;
	TAILQ_FOREACH(walk, &g_CacheFifo, fifoList) {
		if (targetSize <= 0) {
			break;
		}

		/* pick the first element */
		CacheValue* value = GetMemory(walk->sha1name);
		targetSize -= value->block_length;
		numVictims++;
		LIST_INSERT_HEAD(&freeList, walk, freeList);
	}

	/* clean up the free entries */
	while ((walk = LIST_FIRST(&freeList)) != NULL) {
		LIST_REMOVE(walk, freeList);
		TAILQ_REMOVE(&g_CacheFifo, walk, fifoList);

		/* cache size update */
		g_NumKeys--;
		assert(g_NumKeys >= 0);
		g_CacheSize -= (sizeof(CacheKey) + walk->block_length);
		assert(g_CacheSize >= 0);

		/* remove it */
		_RemoveHT(mem_ht, walk->sha1name);
	}
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("%d chunks evicted, %d remains, %d bytes\n",
			numVictims, hashtable_count(mem_ht), g_CacheSize);
#endif
}
/*-----------------------------------------------------------------*/
CacheKey* NewKey(u_char* sha1name, unsigned int block_length)
{
	CacheKey* key = (CacheKey*)malloc(sizeof(CacheKey));
	if (key == NULL) {
		NiceExit(-1, "key allocation failed\n");
	}
	memcpy(key->sha1name, sha1name, SHA1_LEN);
	key->block_length = block_length;

	return key;
}
/*-----------------------------------------------------------------*/
CacheKey* _PutHT(struct hashtable* ht, u_char* sha1name,
		char* block_buffer, unsigned int block_length)
{
	/* create a key */
	CacheKey* key = NewKey(sha1name, block_length);

	/* create a value */
	CacheValue* value = (CacheValue*)malloc(sizeof(CacheValue));
	if (value == NULL) {
		NiceExit(-1, "value allocation failed\n");
	}
	value->block_length = block_length;
	value->ref_count = 1;

	/* create a block for the cache value */
	value->pBlock = (char*)malloc(value->block_length);
	if (value->pBlock == NULL) {
		NiceExit(-1, "block allocation failed\n");
	}
	memcpy(value->pBlock, block_buffer, block_length);

	/* this must not be in the hashtable */
	assert(hashtable_search(ht, key) == NULL);

	/* store block into the hashtable */
	if (hashtable_insert(ht, key, value) == 0) {
		NiceExit(-1, "hashtable_insert failed\n");
	}

	return key;
}
/*-----------------------------------------------------------------*/
void PutSendHintNoDisk(u_char* sha1name, int block_length)
{
#define MAX_NAMES	(1024 * 1024)
	if (hashtable_count(send_hint_ht) >= MAX_NAMES) {
		CacheKey* victim = TAILQ_FIRST(&g_NameFifo);
		TAILQ_REMOVE(&g_NameFifo, victim, fifoList);
		int* v_value = hashtable_remove(send_hint_ht, victim->sha1name);
		assert(v_value != NULL);
		free(v_value);
	}

	/* create a key */
	CacheKey* key = NewKey(sha1name, block_length);

	/* create a value */
	int* value = (int*)malloc(sizeof(int));
	if (value == NULL) {
		NiceExit(-1, "value allocation failed\n");
	}
	*value = 0;

	/* this must not be in the hashtable */
	assert(hashtable_search(send_hint_ht, key) == NULL);

	/* store block into the hashtable */
	if (hashtable_insert(send_hint_ht, key, value) == 0) {
		NiceExit(-1, "hashtable_insert failed\n");
	}

	/* append it to the end of the fifo */
	TAILQ_INSERT_TAIL(&g_NameFifo, key, fifoList);
}
/*-----------------------------------------------------------------*/
void PutSendHint(u_char* sha1name, int block_length)
{
	PutSendHintNoDisk(sha1name, block_length);

	/* write this name to a file */
	char* info = malloc(SHA1_LEN + sizeof(unsigned int));
	if (info == NULL) {
		NiceExit(-1, "hint alloc failed\n");
	}
	memcpy(info, sha1name, SHA1_LEN);
	memcpy(info + SHA1_LEN, (char*)&block_length,
			sizeof(unsigned int));
	if (g_GenerateNameHint == TRUE) {
		SendData(g_DiskHelperCI, info,
				SHA1_LEN + sizeof(unsigned int));
	}
}
/*-----------------------------------------------------------------*/
void PutDiskHint(u_char* sha1name, int block_length)
{
	if (hashtable_count(disk_hint_ht) >= MAX_NAMES) {
		CacheKey* victim = TAILQ_FIRST(&g_DiskFifo);
		TAILQ_REMOVE(&g_DiskFifo, victim, fifoList);
		int* v_value = hashtable_remove(disk_hint_ht, victim->sha1name);
		assert(v_value != NULL);
		free(v_value);
	}

	/* create a key */
	CacheKey* key = NewKey(sha1name, block_length);

	/* create a value */
	int* value = (int*)malloc(sizeof(int));
	if (value == NULL) {
		NiceExit(-1, "value allocation failed\n");
	}
	*value = 0;

	/* this must not be in the hashtable */
	assert(hashtable_search(disk_hint_ht, key) == NULL);

	/* store block into the hashtable */
	if (hashtable_insert(disk_hint_ht, key, value) == 0) {
		NiceExit(-1, "hashtable_insert failed\n");
	}

	/* append it to the end of the fifo */
	TAILQ_INSERT_TAIL(&g_DiskFifo, key, fifoList);
}
/*-----------------------------------------------------------------*/
void PutMemory(u_char* sha1name, char* block_buffer, int block_length)
{
	if (GetMemory(sha1name) != NULL) {
		return;
	}

	/* check if we have room for a new entry, if no then make room */
	int new_size = sizeof(CacheKey) + sizeof(CacheValue) + block_length;
	CheckCache(new_size);

	/* append it to the end of the fifo */
	CacheKey* key = _PutHT(mem_ht, sha1name, block_buffer, block_length);
	TAILQ_INSERT_TAIL(&g_CacheFifo, key, fifoList);

	/* cache size update */
	g_NumKeys++;
	g_CacheSize += new_size;
}
/*-----------------------------------------------------------------*/
CacheValue* GetMemory(u_char* sha1name)
{
	/* TODO: use CacheKey* instead of u_char* */
	return hashtable_search(mem_ht, sha1name);
}
/*-----------------------------------------------------------------*/
int GetSendHint(u_char* sha1name)
{
	/* TODO: use CacheKey* instead of u_char* */
	if (hashtable_search(send_hint_ht, sha1name) != NULL) {
		return TRUE;
	}
	else {
		return FALSE;
	}
}
/*-----------------------------------------------------------------*/
int GetDiskHint(u_char* sha1name)
{
	/* TODO: use CacheKey* instead of u_char* */
	if (hashtable_search(disk_hint_ht, sha1name) != NULL) {
		return TRUE;
	}
	else {
		return FALSE;
	}
}
/*-----------------------------------------------------------------*/
CacheValue* SearchCache(u_char* sha1name)
{
	/* TODO: use CacheKey* instead of u_char* */
	return hashtable_search(tmp_ht, sha1name);
}
/*-----------------------------------------------------------------*/
void _RemoveHT(struct hashtable* ht, u_char* sha1name)
{
	/* TODO: use CacheKey* instead of u_char* */
	CacheValue* value = hashtable_remove(ht, sha1name);
	/* note: key is freed in the function above */
	assert(value != NULL);
	free(value->pBlock);
	free(value);
}
/*-----------------------------------------------------------------*/
void PrintCacheStat()
{
	fprintf(stderr, "-Max Size: %d\n", _maxCacheSize);
	fprintf(stderr, "-Cur Size: %d\n", g_CacheSize);
	fprintf(stderr, "-# of Keys: %d\n", g_NumKeys);
}
/*-----------------------------------------------------------------*/
ChunkBuffer* CreateNewChunkBuffer(char* pBlock, int length,
		unsigned int seq_no, OneChunkName* pMRC, int numNames)
{
	ChunkBuffer* newChunk = malloc(sizeof(ChunkBuffer));
	if (newChunk == NULL) {
		NiceExit(-1, "ChunkBuffer allocation failed\n");
	}

	/* copy buffer content info */
	assert(length > 0);
	assert(seq_no >= 0);
	#if 0
	newChunk->buffer = malloc(sizeof(char) * length);
	if (newChunk->buffer == NULL) {
		NiceExit(-1, "buffer allocation failed\n");
	}
	memcpy(newChunk->buffer, pBlock, length);
	#endif
	newChunk->length = length;
	newChunk->seq_no = seq_no;

	/* copy MRC tree */
	assert(numNames > 0);
	assert(numNames <= g_maxNames);
	newChunk->pMRC = malloc(sizeof(OneChunkName) * numNames);
	if (newChunk->pMRC == NULL) {
		NiceExit(-1, "MRC allocation failed\n");
	}
	memcpy(newChunk->pMRC, pMRC, sizeof(OneChunkName) * numNames);
	newChunk->numNames = numNames;
	newChunk->chunkList = NULL;
	newChunk->lowLevel = -1;
	return newChunk;
}
/*-----------------------------------------------------------------*/
ChunkBuffer* CreateNewChunkBuffer2(char* pBlock, int length,
		unsigned int seq_no, OneChunkName* pMRC, int numNames,
		CandidateList* list, int lowLevel)
{
	ChunkBuffer* newChunk = CreateNewChunkBuffer(pBlock, length, seq_no,
			pMRC, numNames);
	assert(newChunk != NULL);

	newChunk->lowLevel = lowLevel;

	/* set the candidate chunk list */
	newChunk->chunkList = list;

	/* put candidate chunks in its temp memory cache */
	PutTempCacheCandidateList(list, pBlock, length);

	return newChunk;
}
/*-----------------------------------------------------------------*/
void RefFlushChunk(u_char* sha1name)
{
	CacheValue* pValue = SearchCache(sha1name);
	if (pValue == NULL) {
		assert(FALSE);
		return;
	}
	assert(pValue->block_length > 0);
	pValue->ref_count--;
	assert(pValue->ref_count >= 0);
#ifdef _WAPROX_DEBUG_OUTPUT
	int length = pValue->block_length;
#endif
	if (pValue->ref_count == 0) {
		/* if nobody references it, remove from memory */
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("remove %s, %d bytes, %d items remain\n",
				SHA1Str(sha1name), length,
				hashtable_count(tmp_ht));
#endif
		_RemoveHT(tmp_ht, sha1name);
	}
	else {
#ifdef _WAPROX_DEBUG_OUTPUT
		TRACE("decrement ref %s, %d bytes, ref %d, %d items remain\n",
				SHA1Str(sha1name), length, pValue->ref_count,
				hashtable_count(tmp_ht));
#endif
	}
}
/*-----------------------------------------------------------------*/
void PutTempDiskCache(u_char* sha1name, char* buffer, int buf_len)
{
	assert(VerifySHA1((const byte*)buffer, buf_len, sha1name));
	if (buf_len == 0) {
		return;
	}

	CacheValue* value = GetTempDiskCache(sha1name);
	if (value == NULL) {
		assert(buf_len <= MAX_CHUNK_SIZE);
		_PutHT(put_tmp_ht, sha1name, buffer, buf_len);
	}
	else {
		NiceExit(-1, "perhaps consistency error?\n");
	}
}
/*-----------------------------------------------------------------*/
CacheValue* GetTempDiskCache(u_char* sha1name)
{
	/* TODO: use CacheKey* instead of u_char* */
	return hashtable_search(put_tmp_ht, sha1name);
}
/*-----------------------------------------------------------------*/
void DeleteTempDiskCache(u_char* sha1name)
{
	CacheValue* pValue = GetTempDiskCache(sha1name);
	if (pValue == NULL) {
		assert(FALSE);
		return;
	}
	assert(pValue->block_length > 0);
	_RemoveHT(put_tmp_ht, sha1name);
}
/*-----------------------------------------------------------------*/
void DestroyChunkBuffer(ChunkBuffer* cb)
{
	assert(cb->lowLevel == -1);
	int i;
	for (i = 0; i < cb->numNames; i++) {
		/* we don't automatically put it into disk */
		if (SearchCache(cb->pMRC[i].sha1name) != NULL) {
			RefFlushChunk(cb->pMRC[i].sha1name);
		}

#ifndef _WAPROX_NO_NAME_HINT
		/* hint */
		if (GetSendHint(cb->pMRC[i].sha1name) == FALSE) {
			PutSendHint(cb->pMRC[i].sha1name, cb->pMRC[i].length);
		}
#endif
	}
	free(cb->pMRC);
	#if 0
	free(cb->buffer);
	#endif
	free(cb);
}
/*-----------------------------------------------------------------*/
void DestroyChunkBuffer2(ChunkBuffer* cb, char updateHint)
{
	/* flush candidate chunks from temp cache */
	assert(cb->lowLevel >= 0);
	OneCandidate* walk = NULL;
	TAILQ_FOREACH(walk, cb->chunkList, list) {
		RefFlushChunk(walk->sha1name);
	}

	/* free candidate list */
	FreeCandidateList(cb->chunkList);

	if (updateHint) {
#ifndef _WAPROX_NO_NAME_HINT
		int i;
		for (i = 0; i < cb->numNames; i++) {
			/* hint */
#if 0
			if (GetSendHint(cb->pMRC[i].sha1name) == FALSE) {
				PutSendHint(cb->pMRC[i].sha1name,
						cb->pMRC[i].length);
			}
#endif
			if (cb->pMRC[i].level > cb->lowLevel) {
				/* we stop here */
				break;
			}

			if (cb->pMRC[i].isHit == FALSE) {
				/* put only missing chunk names */
				assert(cb->pMRC[i].isSha1Ready == TRUE);
				if (GetSendHint(cb->pMRC[i].sha1name)
						== FALSE) {
#ifdef _WAPROX_DEBUG_OUTPUT
					TRACE("put name %s, %d bytes\n",
							SHA1Str(cb->pMRC[i].
								sha1name),
							cb->pMRC[i].length);
#endif
					PutSendHint(cb->pMRC[i].sha1name,
							cb->pMRC[i].length);
				}
			}
#endif
		}
	}

	free(cb->pMRC);
	free(cb);
}
/*-----------------------------------------------------------------*/
void RefPutChunkCallback(u_char* sha1name, char* buffer, int buf_len)
{
	/* put it in the *TEMPORARY* memory cache */
	assert(VerifySHA1((const byte*)buffer, buf_len, sha1name));
	if (buf_len == 0) {
		return;
	}

	CacheValue* value = SearchCache(sha1name);
	if (value == NULL) {
		assert(buf_len <= MAX_CHUNK_SIZE);
		_PutHT(tmp_ht, sha1name, buffer, buf_len);
		value = SearchCache(sha1name);
	}
	else {
		assert(value->ref_count >= 1);
		value->ref_count++;
	}
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("%s added, %d bytes, %d ref, tot %d items\n",
			SHA1Str(sha1name), value->block_length,
			value->ref_count, hashtable_count(tmp_ht));
#endif
}
/*-----------------------------------------------------------------*/
int MemoryCacheCallBack(u_char* sha1name)
{
	CacheValue* value = GetMemory(sha1name);
	if (value != NULL) {
		/* bring the block from mem_ht to tmp_ht */
		RefPutChunkCallback(sha1name, value->pBlock,
				value->block_length);
		return TRUE;
	}
	else {
		return FALSE;
	}
}
/*-----------------------------------------------------------------*/
int DestroyChunkBufferList(ChunkBufferList* cbl, int ack, char updateHint)
{
	int numDestroyed = 0;
	while (!TAILQ_EMPTY(cbl)) {
		ChunkBuffer* cb = TAILQ_FIRST(cbl);
		if (ack == 0 || (cb->seq_no + cb->length <= ack)) {
			TAILQ_REMOVE(cbl, cb, bufferList);
#ifdef _WAPROX_FULL_MRC_TREE
			DestroyChunkBuffer(cb);
#else
			DestroyChunkBuffer2(cb, updateHint);
#endif
			numDestroyed++;	
		}
		else {
			break;
		}
	}

	return numDestroyed;
}
/*-----------------------------------------------------------------*/
void EmptyTempMemCache()
{
	if (hashtable_count(tmp_ht) == 0) {
		return;
	}

	int numItems = 0;
	struct hashtable_itr* itr = hashtable_iterator(tmp_ht);
	do {
		/* free value only, (key will be freed automatically) */
		CacheValue* value = hashtable_iterator_value(itr);
		assert(value != NULL);
		free(value->pBlock);
		free(value);
		numItems++;
	} while (hashtable_iterator_remove(itr));
	free(itr);

	TRACE("%d chunks free()d from temp cache\n", numItems);
}
/*-----------------------------------------------------------------*/
