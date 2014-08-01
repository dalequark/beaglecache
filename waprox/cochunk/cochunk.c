#include <assert.h>
#include <getopt.h>
#include <ctype.h>
#include "czipdef.h"
#include "rabinfinger2.h"
#include "debug.h"
#include "mrc.h"
#include "util.h"
#include "hashtable.h"
#include "hashtable_itr.h"
#include <GeoIP.h>
#include <GeoIPCity.h>
#include "GeoIP_internal.h"
char* geoiplookup2(GeoIP* gi,char *hostname,int i);

HANDLE hdebugLog = NULL;

/* paramters (with default values) */
unsigned int g_AvgMinChunkSize = 128;
unsigned int g_AvgMaxChunkSize = (64 * 1024);
unsigned int g_Degree = 8;
unsigned int g_RabinWindow = 16;
unsigned int g_AbsMinChunkSize = 30;
unsigned int g_ChunkOverhead = 20;

/* globals */
MrcCtx* g_MrcCtx = NULL;
struct hashtable* g_Cache = NULL;
unsigned long long g_Hits = 0;
unsigned long long g_HitBytes = 0;
unsigned long long g_TotBytes = 0;
unsigned long long g_CacheBytes = 0;
unsigned long long g_NumObjects = 0;
GeoIP* gi = NULL;
int geoipedition = 0;

typedef struct CacheKey {
	u_char sha1name[SHA1_LEN];
	/*
	u_int block_length;
	LIST_ENTRY(CacheKey) freeList;
	TAILQ_ENTRY(CacheKey) fifoList;
	*/
} CacheKey;

/* function declarations */
void getOptions(int argc, char** argv);
void ProcessOneDumpFile(char* filename);
int ProcessChunk(RabinCtx* rctx, char* psrc, int buf_len, char* country_code, char* clientIP);
int MyResolve(MrcTree* tree, char* country_code, char* clientIP);
unsigned int chunk_hash_from_key_fn_raw(void* k);
unsigned int chunk_hash_from_key_fn(void* k);
int chunk_keys_equal_fn_raw(void* key1, void* key2);
int chunk_keys_equal_fn(void* key1, void* key2);
void UpdateCache(u_char* name, int length);

/*-----------------------------------------------------------------*/
char *trimwhitespace(char *str)
{
	char *end;

	// Trim leading space
	while(isspace(*str)) str++;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace(*end)) end--;

	// Write new null terminator
	*(end+1) = 0;

	return str;
}
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
void ProcessOneDumpFile(char* filename)
{
	FILE* fp = NULL;
	if (filename == NULL) {
		/* read from stdin */
		fp = stdin;
	}
	else {
		/* read from file */
		fp = fopen(filename, "rb");
	}

	if (fp == NULL) {
		NiceExit(-1, "dump file open failed");
	}

	/* feed one object by one */
	while (!feof(fp)) {
		/* read the first line */
		char linebuf[256 * 1024];
		if (fgets(linebuf, (int)sizeof(linebuf), fp) == NULL) {
			if (feof(fp)) {
				/* done */
				break;
			}
			else {
				NiceExit(-1, "can't get a line");
			}
		}
		//TRACE("line: %s", linebuf);
		char* firstline = strdup(linebuf);

		/* parse the first line */
		const char* signature = "!@#FULL_LOG_START!@#";
		char header[64];
		char clientIP[128];
		float requestTime = 0;
		int serviceTime = 0;
		int objectSize = 0;
		int partialSize = 0;
		char url[1024];
		char type[1024];
		memset(url, 0, sizeof(url));
		memset(clientIP, 0, sizeof(clientIP));
		memset(header, 0, sizeof(header));
		memset(type, 0, sizeof(type));
		sscanf(linebuf, "%s %s %f %d %d %d %s",
				header, clientIP, &requestTime, &serviceTime,
				&partialSize, &objectSize, url);

		char* country_code = geoiplookup2(gi, clientIP, geoipedition);

		/* integrity check */
		if (strncmp(header, signature, strlen(signature)) != 0) {
			NiceExit(-1, "error parsing line: no signature found");
		}

		/* process the request header */
		while (TRUE) {
			if (fgets(linebuf, (int)sizeof(linebuf), fp) == NULL) {
				if (feof(fp)) {
					/* done */
					break;
				}
				else {
					NiceExit(-1, "can't get a line");
				}
			}
			if (strlen(linebuf) <= 2) {
				break;
			}
		}

		/* process the response header */
		#if 0
		char nocache = FALSE;
		#endif
		char* typeline = NULL;
		int contentLen = 0;
		if (objectSize > 0) {
			int headerlines = 0;
			while (TRUE) {
				if (fgets(linebuf, (int)sizeof(linebuf), fp) == NULL) {
					if (feof(fp)) {
						/* done */
						break;
					}
					else {
						NiceExit(-1, "can't get a line");
					}
				}
				headerlines++;
				objectSize -= strlen(linebuf);
				if (strlen(linebuf) <= 2) {
					//TRACE("skip response header: %d lines\n", headerlines);
					break;
				}
				else {
					#if 0
					/* check Pragma */
					if (strncasecmp(linebuf, "Pragma:", 7) == 0) {
						if (strcasestr(linebuf, "no-cache") != NULL ||
								strcasestr(linebuf, "private") != NULL) {
							nocache = TRUE;
						}
					}
					#endif
					if (strncasecmp(linebuf, "Content-Type:", 13) == 0) {
						/* content type header exists */
						typeline = strdup(linebuf + 13);
					}
					if (strncasecmp(linebuf, "Content-Length:", 15) == 0) {
						/* content type header exists */
						contentLen = atoi(linebuf + 15);
					}
				}
			}
		}

		firstline = trimwhitespace(firstline);
		if (typeline != NULL) {
			char* newtypeline = trimwhitespace(typeline);
			printf("%s %d %s \"%s\"\n", firstline, contentLen, country_code, newtypeline);
			free(typeline);
		}
		else {
			printf("%s %d %s \"\"\n", firstline, contentLen, country_code);
		}

		/* create Rabin context */
		RabinCtx* rctx = CreateRabinContext(g_MrcCtx);
		assert(rctx != NULL);

		/* drain object body */
		char buffer[g_AvgMaxChunkSize * 3];
		int offset = 0;
		while (objectSize > 0) {
			int numReads = 0;
			if (objectSize >= (sizeof(buffer) - offset)) {
				numReads = fread(buffer + offset, 1, (sizeof(buffer) - offset), fp);
			}
			else {
				numReads = fread(buffer + offset, 1, objectSize, fp);
			}

			if (numReads > 0) {
				objectSize -= numReads;
				offset += numReads;
			}
			else {
				NiceExit(-1, "fread error?");
			}

			/* process chunk */
			int numProcessed = ProcessChunk(rctx, buffer, offset, country_code, clientIP);
			assert(numProcessed <= offset);
			assert(numProcessed > 0);
			//TRACE("%d processed, %d given\n", numProcessed, offset);

			/* adjust buffer */
			memmove(buffer, buffer + numProcessed, offset - numProcessed);
			offset -= numProcessed;
		}

		/* drain remaining chunks */
		while (offset > 0) {
			int numProcessed = ProcessChunk(rctx, buffer, offset, country_code, clientIP);
			assert(numProcessed <= offset);
			assert(numProcessed > 0);
			memmove(buffer, buffer + numProcessed, offset - numProcessed);
			offset -= numProcessed;
		}

		/* destroy Rabin context */
		DestroyRabinContext(rctx);
		free(firstline);
		g_NumObjects++;

		#if 0
		float idealsaving = 0;
		float actualsaving = 0;
		if (g_TotBytes > 0) {
			idealsaving = (float)g_HitBytes * 100 / g_TotBytes;
			actualsaving = (float)(g_HitBytes - g_Hits * g_ChunkOverhead) * 100 / g_TotBytes;
		}

		TRACE("Objects: %lld HITS: %lld chunks %lld KB, TOT: %d chunks %lld KB, "
				"Saving: %.2f %% ideal %.2f %% actual\n",
				g_NumObjects, g_Hits, g_HitBytes/1024,
				hashtable_count(g_Cache), g_TotBytes/1024,
				idealsaving, actualsaving);
		#endif
	}

	fclose(fp);
}
/*-----------------------------------------------------------------*/
void UpdateCache(u_char* name, int length)
{
	if (hashtable_search(g_Cache, name) != NULL) {
		/* already in the cache */
		return;
	}

	/* create a key */
	CacheKey* key = malloc(sizeof(CacheKey));
	if (key == NULL) {
		NiceExit(-1, "CacheKey malloc() failed");
	}
	memcpy(key->sha1name, name, SHA1_LEN);
	//key->block_length = length;

	/* create a val */
	char* val = malloc(sizeof(char));
	if (val == NULL) {
		NiceExit(-1, "char malloc() failed");
	}
	*val = 0;

	/* insert into the cache */
	if (!hashtable_insert(g_Cache, key, val)) {
		NiceExit(-1, "hashtable_insert() failed");
	}
	g_CacheBytes += length;
}
/*-----------------------------------------------------------------*/
void UpdateCacheAll(MrcTree* tree)
{
	/* put all chunks in the tree into cache */
	int i;
	for (i = 0; i < tree->numNames; i++) {
		UpdateCache(tree->pNames[i].sha1name, tree->pNames[i].length);
	}
}
/*-----------------------------------------------------------------*/
int ProcessChunk(RabinCtx* rctx, char* psrc, int buf_len, char* country_code, char* clientIP)
{
	/* get MRC first */
	MrcTree* tree = PerformPreMRC(g_MrcCtx, rctx, psrc, buf_len, NULL);
	int num_processed = tree->pNames[0].length;
	/*
	TRACE("%d chunks, %d processed out of %d bytes\n",
			tree->numNames, num_processed, buf_len);
	*/

	/* resolve tree */
	int lowLevel = MyResolve(tree, country_code, clientIP);
	assert(lowLevel < tree->maxlevel);
	
	#if 0
	/* find candidate chunks */
	CandidateList* candidateList = GetCandidateList3(tree, lowLevel);

	/* update index cache */
	assert(candidateList != NULL);
	OneCandidate* walk = NULL;
	int candi_count = 0;
	int candi_bytes = 0;
	TAILQ_FOREACH(walk, candidateList, list) {
		candi_count++;
		candi_bytes += walk->length;
		//TRACE("%s %d\n", SHA1Str(walk->sha1name), walk->length);
		printf("%s %d\n", SHA1Str(walk->sha1name), walk->length);
		if (walk->isHit) {
			g_HitBytes += walk->length;
			g_Hits++;
		}
		else {
		}

		g_TotBytes += walk->length;
	}
	//TRACE("candidate list: %d chunks, %d bytes\n", candi_count, candi_bytes);

	#ifdef CACHE_SIMUL
	/* update index */
	UpdateCacheAll(tree);
	#endif

	/* free some stuff */
	FreeCandidateList(candidateList);
	#endif
	DestroyMrcTree(tree);

	return num_processed;
}
/*-----------------------------------------------------------------*/
int MyResolve(MrcTree* tree, char* country_code, char* clientIP)
{
	/* start from root, check name hint */
	int i;
	int lowLevel = 0;
	int curChunkSize = g_AvgMaxChunkSize;
	for (i = 0; i < tree->maxlevel; i++) {
		int numHits = 0;
		int numChunks = tree->pli[i].numNames;
		int newIdx = tree->pli[i].firstIndex;
		int j;
		int offset = 0;
		lowLevel = i;
		for (j = 0; j < numChunks; j++) {
			assert(newIdx < tree->numNames);
			OneChunkName* cur = &tree->pNames[newIdx];
			assert(offset == cur->offset);
			Calc_SHA1Sig((const byte*)cur->pChunk, cur->length,
					cur->sha1name);
			cur->isSha1Ready = TRUE;

			/* print all chunk size */
			printf("%s %s C%d %s %d\n", clientIP, country_code, curChunkSize, SHA1Str(cur->sha1name), cur->length);

			#ifdef CACHE_SIMUL
			if (hashtable_search(g_Cache, cur->sha1name)
					!= NULL) {
				cur->isHit = TRUE;
			}
			else {
				cur->isHit = FALSE;
			}
			#endif

			newIdx++;
			offset += cur->length;
			if (cur->isHit == TRUE) {
				numHits++;
			}
		}

		curChunkSize /= g_Degree;

		#if 0
		/* optimization */
		if (numHits == numChunks) {
			/* everything is hit, this level */
			break;
		}
		#endif
	}
	return lowLevel;
}
/*-----------------------------------------------------------------*/
void getOptions(int argc, char** argv)
{
	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "d:s:l:v"))
			!= -1) {
		switch (c)
		{
			case 'd': /* MRC degree */
				g_Degree = atoi(optarg);
				break;
			case 's': /* min chunksize (bytes) */
				g_AvgMinChunkSize = atoi(optarg);
				break;
			case 'l': /* max chunksize (bytes) */
				g_AvgMaxChunkSize = atoi(optarg);
				break;
			case 'v': /* verbose output */
				//g_Verbose = TRUE;
				break;
			case '?':
				if (optopt == 'd' || optopt == 's' || optopt == 'l') {
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
char* geoiplookup2(GeoIP* gi,char *hostname,int i)
{
	GeoIPRecord * gir;
	if (GEOIP_CITY_EDITION_REV1 == i) {
		gir = GeoIP_record_by_name(gi, hostname);
		if (NULL == gir) {
			return "--";
		} else {
			return gir->country_code;
		}
	}
	return NULL;
}
/*-----------------------------------------------------------------*/
int main(int argc, char** argv)
{
	getOptions(argc, argv);

	/* init geoip */
	char* custom_file = "GeoIPCity.dat";
	GeoIP_setup_custom_directory(".");
	_GeoIP_setup_dbfilename();
	gi = GeoIP_open(custom_file, GEOIP_STANDARD);
	if (NULL == gi) {
		NiceExit(-1, "cannot init GeoIP DB");
	} 
	geoipedition = GeoIP_database_edition(gi);

	/* create MRC context */
	g_MrcCtx = CreateMrcCtx(g_Degree,
			g_AvgMinChunkSize, g_AvgMaxChunkSize,
			g_AbsMinChunkSize, g_AvgMaxChunkSize * 2,
			g_RabinWindow);
	assert(g_MrcCtx != NULL);

	/* create cache index */
	#ifdef CACHE_SIMUL
	g_Cache = create_hashtable(65536,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (g_Cache == NULL) {
		NiceExit(-1, "cache creation failed");
	}
	#endif

	if ((argc - optind) <= 0) {
		/* process from STDIN */
		ProcessOneDumpFile(NULL);
	}
	else {
		/* process one file at a time */
		int i = 0;
		for (i = optind; i < argc; i++) {
			TRACE("filename: %s\n", argv[i]);
			ProcessOneDumpFile(argv[i]);
		}
	}

	/* cleaning up */
	DestroyMrcCtx(g_MrcCtx);
	#ifdef CACHE_SIMUL
	hashtable_destroy(g_Cache, TRUE);
	#endif
	GeoIP_delete(gi);

	return 0;
}
/*-----------------------------------------------------------------*/
