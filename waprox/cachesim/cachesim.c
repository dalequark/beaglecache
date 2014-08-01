#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include "czipdef.h"
#include "debug.h"
#include "hashtable.h"
#include "util.h"
#include "queue.h"

HANDLE hdebugLog = NULL;

/* command line parameter */
unsigned long long g_MaxCountK = 0;
unsigned long long g_MaxSizeMB = 0;
unsigned int g_PrintInverval = 10000;
int g_CurrentParition = 0;
int g_MaxPartition = 0;
float g_LoadFactor = 1.0;

typedef struct CacheEntry {
	u_char* sha1name;
	u_int chunklen;
	TAILQ_ENTRY(CacheEntry) list; /* for LRU */
} CacheEntry;
typedef TAILQ_HEAD(CacheList, CacheEntry) CacheList;

typedef struct CacheInfo {
	struct hashtable* cacheTable;
	CacheList cacheList;

	/* hit/total stats */
	unsigned long long hitCount;
	unsigned long long hitBytes;
	unsigned long long totCount;
	unsigned long long totBytes;

	/* current cache usage */
	unsigned long long cacheCount;
	unsigned long long cacheBytes;

	/* cache capacity */
	unsigned long long maxCount;
	unsigned long long maxBytes;

	/* start time */
	time_t starttime;

	/* etc */
	unsigned long long chainLength;
} CacheInfo;

/* function decl. */
void getOptions(int argc, char** argv);
unsigned int chunk_hash_from_key_fn_raw(void* k);
unsigned int chunk_hash_from_key_fn(void* k);
int chunk_keys_equal_fn_raw(void* key1, void* key2);
int chunk_keys_equal_fn(void* key1, void* key2);
void PrintCacheInfo(CacheInfo* cacheInfo);
void SimulateCache(CacheInfo* cacheInfo, u_char* sha1name, u_int chunklen);
void SimulateMain(FILE* fp, CacheInfo* cacheInfo);
int NeedEviction(CacheInfo* cacheInfo);
/*-----------------------------------------------------------------*/
unsigned int chunk_hash_from_key_fn_raw(void* k)
{
#define _rotl(Val, Bits) ((((Val)<<(Bits)) | (((Val) & MASK)>>(32 - (Bits)))) & MASK)
	int i = 0;
	unsigned int hash = 0;
	unsigned char* sha1name = k;
	for (i = 0; i < SHA1_LEN; i++) {
    		hash += (_rotl(hash, 19) + sha1name[i]);
	}
	return hash;
}
/*-----------------------------------------------------------------*/
unsigned int chunk_hash_from_key_fn(void* k)
{
	return chunk_hash_from_key_fn_raw(k);
}
/*-----------------------------------------------------------------*/
int chunk_keys_equal_fn_raw(void* key1, void* key2)
{
	return (memcmp(key1, key2, SHA1_LEN) == 0);
}
/*-----------------------------------------------------------------*/
int chunk_keys_equal_fn(void* key1, void* key2)
{
	return chunk_keys_equal_fn_raw(key1, key2);
}
/*-----------------------------------------------------------------*/
void PrintCacheInfo(CacheInfo* cacheInfo)
{
	int elapsed = time(NULL) - cacheInfo->starttime;
	float hratio = 0;
	if (cacheInfo->totCount > 0)
		hratio = 1.0 * cacheInfo->hitCount / cacheInfo->totCount * 100;
	float bhratio = 0;
	if (cacheInfo->totBytes > 0)
		bhratio = 1.0 * cacheInfo->hitBytes / cacheInfo->totBytes * 100;
	float speed = 0;
	if (cacheInfo->totCount > 0)
		speed = 1.0 * cacheInfo->totCount / elapsed;
	float avgChainLength = 0;
	if (cacheInfo->totCount > 0)
		avgChainLength = 1.0 * cacheInfo->chainLength / cacheInfo->totCount;
	printf("HitRatio: %llu / %llu / %.2f ByteHitRatio: %llu / %llu / %.2f "
			"CurrentUsage: %llu / %llu (MaxUsage: %llu / %llu) "
			"%d seconds elapsed, %.2f objects/sec (AvgChainLength: %.2f)\n",
			cacheInfo->hitCount, cacheInfo->totCount, hratio,
			cacheInfo->hitBytes, cacheInfo->totBytes, bhratio,
			cacheInfo->cacheCount, cacheInfo->cacheBytes,
			cacheInfo->maxCount, cacheInfo->maxBytes,
			elapsed, speed, avgChainLength);
	fflush(stdout);
	fflush(stderr);
}
/*-----------------------------------------------------------------*/
int NeedEviction(CacheInfo* cacheInfo)
{
	/* maxCount of 0 means no limit */
	if (cacheInfo->maxCount > 0 && cacheInfo->cacheCount > cacheInfo->maxCount) {
		return TRUE;
	}

	/* maxBytes of 0 means no limit */
	if (cacheInfo->maxBytes > 0 && cacheInfo->cacheBytes > cacheInfo->maxBytes) {
		return TRUE;
	}

	/* everything seems fine */
	return FALSE;
}
/*-----------------------------------------------------------------*/
void SimulateCache(CacheInfo* cacheInfo, u_char* sha1name, u_int chunklen)
{
	//printf("%s %d\n", SHA1Str(sha1name), chunklen);
	unsigned int chainLength = 0;
	CacheEntry* entry = hashtable_search2(cacheInfo->cacheTable, sha1name, &chainLength);
	cacheInfo->chainLength += chainLength;
	//CacheEntry* entry = hashtable_search(cacheInfo->cacheTable, sha1name);
	if (entry != NULL) {
		/* cache hit: move this entry to the end of cache list */
		if (entry->chunklen != chunklen) {
			printf("some error %s: %d vs %d\n",
					sha1name, entry->chunklen, chunklen);
			fflush(stdout);
			fflush(stderr);
			return;
		}
		assert(entry->chunklen == chunklen);
		TAILQ_REMOVE(&(cacheInfo->cacheList), entry, list);
		TAILQ_INSERT_TAIL(&(cacheInfo->cacheList), entry, list);

		/* update hit stats */
		cacheInfo->hitBytes += entry->chunklen;
		cacheInfo->hitCount++;
	}
	else {
		/* cache miss: insert this entry at the end of cache list */
		u_char* key = malloc(sizeof(u_char) * SHA1_LEN);
		if (key == NULL) {
			NiceExit(-1, "key malloc() failed");
		}
		memcpy(key, sha1name, SHA1_LEN);

		entry = malloc(sizeof(CacheEntry));
		if (entry == NULL) {
			NiceExit(-1, "CacheEntry malloc() failed");
		}
		entry->sha1name = key;
		entry->chunklen = chunklen;
		hashtable_insert(cacheInfo->cacheTable, key, entry);
		TAILQ_INSERT_TAIL(&(cacheInfo->cacheList), entry, list);

		/* update usage stats */
		cacheInfo->cacheBytes += chunklen;
		cacheInfo->cacheCount++;

		/* evict the first one if needed */
		while (NeedEviction(cacheInfo) == TRUE) {
			/* remove from the list */
			CacheEntry* victim = TAILQ_FIRST(&cacheInfo->cacheList);
			TAILQ_REMOVE(&(cacheInfo->cacheList), victim, list);

			/* update usage stats */
			cacheInfo->cacheCount--;
			cacheInfo->cacheBytes -= victim->chunklen;
			assert(cacheInfo->cacheCount >= 0);
			assert(cacheInfo->cacheBytes >= 0);

			/* remove from the table */
			CacheEntry* val = hashtable_remove(cacheInfo->cacheTable, victim->sha1name);
			assert(val != NULL);
			free(val);
		}
	}

	/* update total stats always */
	cacheInfo->totBytes += chunklen;
	cacheInfo->totCount++;

	/* print stats */
	if (cacheInfo->totCount % g_PrintInverval == 0) {
		PrintCacheInfo(cacheInfo);
	}
}
/*-----------------------------------------------------------------*/
void getOptions(int argc, char** argv)
{
	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "c:b:t:l:a:p:"))
			!= -1) {
		switch (c)
		{
			case 'c': /* max cache entries (K) */
				g_MaxCountK = atoll(optarg);
				break;
			case 'b': /* max cache bytes (MB) */
				g_MaxSizeMB = atoll(optarg);
				break;
			case 't': /* stat print interval */
				g_PrintInverval = atoi(optarg);
				break;
			case 'a': /* current partition assignment */
				g_CurrentParition = atoi(optarg);
				break;
			case 'p': /* max partition */
				g_MaxPartition = atoi(optarg);
				break;
			case 'l': /* load factor */
				g_LoadFactor = atof(optarg);
				break;
			case '?':
				if (optopt == 'c' || optopt == 'b' || optopt == 'a'
						|| optopt == 'p' || optopt == 't' || optopt == 'l') {
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				}
				else if (isprint(optopt)) {
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				}
				else {
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				}
				NiceExit(-1, "");
			default:
				break;
		}
	}
}
/*-----------------------------------------------------------------*/
void SimulateMain(FILE* fp, CacheInfo* cacheInfo)
{
	/* simulate cache */
	while (!feof(fp)) {
		char linebuf[1024];
		if (fgets(linebuf, sizeof(linebuf), fp) == NULL) {
			if (feof(fp)) {
				/* EOF */
				break;
			}
			else {
				NiceExit(-1, "can't get a line");
			}
		}

		u_char* sha1name = SHA1StrR(linebuf);
		u_int chunklen = atoi(linebuf + (SHA1_LEN * 2) + 1);
		if (g_MaxPartition > 0 && ((int)(sha1name[SHA1_LEN - 1]) % g_MaxPartition) != g_CurrentParition) {
			/* ignore if not mine */
			continue;
		}

		SimulateCache(cacheInfo, sha1name, chunklen);
	}

	/* final stats */
	PrintCacheInfo(cacheInfo);
}
/*-----------------------------------------------------------------*/
int main(int argc, char** argv)
{
	getOptions(argc, argv);
	/*
	printf("sizeof(CacheEntry): %d\n", sizeof(CacheEntry));
	printf("sizeof(u_char) * SHA1_LEN: %d\n", sizeof(u_char) * SHA1_LEN);
	*/

	/* create cache index */
	CacheInfo cacheInfo;
	memset(&cacheInfo, 0, sizeof(cacheInfo));
	cacheInfo.maxCount = 1000 * g_MaxCountK;
	cacheInfo.maxBytes = 1024 * 1024 * g_MaxSizeMB;
	cacheInfo.cacheTable = create_hashtable(
			65536 * g_LoadFactor,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (cacheInfo.cacheTable == NULL) {
		NiceExit(-1, "cache creation failed");
	}
	TAILQ_INIT(&cacheInfo.cacheList);
	cacheInfo.starttime = time(NULL);

	if (optind >= argc) {
		/* from STDIN */
		SimulateMain(stdin, &cacheInfo);
	}
	else {
		int i = 0;
		for (i = optind; i < argc; i++) {
			/* open file */
			printf("filename: %s\n", argv[i]);
			fflush(stdout);
			fflush(stderr);
			FILE* fp = fopen(argv[i], "rb");
			if (fp == NULL) {
				NiceExit(-1, "file open failed");
			}

			SimulateMain(fp, &cacheInfo);

			/* close file */
			fclose(fp);
		}
	}

	printf("all done!\n");

	/* destroy cache index */
	hashtable_destroy(cacheInfo.cacheTable, TRUE);

	return 0;
}
/*-----------------------------------------------------------------*/
