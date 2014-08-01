#ifndef __WAPROX_CHUNKCACHE_H__
#define __WAPROX_CHUNKCACHE_H__

#include "util.h"
#include "queue.h"
#include "mrc_tree.h"

typedef struct CacheKey {
	u_char sha1name[SHA1_LEN];
	u_int block_length;
	LIST_ENTRY(CacheKey) freeList;
	TAILQ_ENTRY(CacheKey) fifoList;
} CacheKey;

typedef struct ChunkBuffer {
	OneChunkName* pMRC;
	CandidateList* chunkList;
	u_int numNames;
	#if 0
	char* buffer;
	#endif
	u_int seq_no;
	u_int length;
	int lowLevel;
	TAILQ_ENTRY(ChunkBuffer) bufferList;
} ChunkBuffer;

typedef TAILQ_HEAD(ChunkBufferList, ChunkBuffer) ChunkBufferList;

typedef struct CacheValue {
	char* pBlock;
	u_int block_length;
	int ref_count;
} CacheValue;

extern int g_NumKeys;
extern int g_CacheSize;
extern struct hashtable* send_hint_ht;
extern struct hashtable* disk_hint_ht;
extern struct hashtable* put_tmp_ht;
extern struct hashtable* tmp_ht;
extern struct hashtable* mem_ht;

unsigned int chunk_hash_from_key_fn_raw(void* k);
unsigned int chunk_hash_from_key_fn(void* k);
int chunk_keys_equal_fn_raw(void* key1, void* key2);
int chunk_keys_equal_fn(void* key1, void* key2);

int CreateCache(int maxCacheSize);
int DestroyCache();
CacheKey* NewKey(u_char* sha1name, unsigned int block_length);
void PrintCacheStat();

/* memory cache */
void PutMemory(u_char* sha1name, char* buffer, int buf_len);
CacheValue* GetMemory(u_char* sha1name);

/* temp memory cache for reconstruction, etc. */
void RefPutChunkCallback(u_char* sha1name, char* buffer, int buf_len);
CacheValue* SearchCache(u_char* sha1name);
void RefFlushChunk(u_char* sha1name);

/* temp disk cache where we keep chunks until PUT complete */
void PutTempDiskCache(u_char* sha1name, char* buffer, int buf_len);
CacheValue* GetTempDiskCache(u_char* sha1name);
void DeleteTempDiskCache(u_char* sha1name);

/* send name hint */
void PutSendHintNoDisk(u_char* sha1name, int block_length);
void PutSendHint(u_char* sha1name, int block_length);
int GetSendHint(u_char* sha1name);

/* disk name hint */
void PutDiskHint(u_char* sha1name, int block_length);
int GetDiskHint(u_char* sha1name);

ChunkBuffer* CreateNewChunkBuffer(char* pBlock, int length,
		unsigned int seq_no, OneChunkName* pMRC, int numNames);
ChunkBuffer* CreateNewChunkBuffer2(char* pBlock, int length,
		unsigned int seq_no, OneChunkName* pMRC, int numNames,
		CandidateList* list, int lowLevel);
void DestroyChunkBuffer(ChunkBuffer* pBuffer);
void DestroyChunkBuffer2(ChunkBuffer* pBuffer, char updateHint);
int MemoryCacheCallBack(u_char* sha1name);
int DestroyChunkBufferList(ChunkBufferList* cbl, int ack, char updateHint);
void EmptyTempMemCache();

#endif /*__WAPROX_CHUNKCACHE_H__*/
