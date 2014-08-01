#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <nids.h>
#include <netdb.h>
#include <assert.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include "czipdef.h"
#include "hashtable.h"
#include "hashtable_itr.h"
#include "chunkcache.h"
#include "rabinfinger2.h"
#include "mrc_tree.h"
#include "config.h"
#include "debug.h"

/*#define BUFFER_SIZE	MAX(RECV_BUFSIZE, 128 * 1024)*/
#define BUFFER_SIZE	(256 * 1024 * 1)
#define MEMBUFFERSIZE	(BUFFER_SIZE / 2)

#define MAX_PACKET_QUEUE	(10)

/* per chunk overhead: name length (20 bytes) + content length (4 bytes) */
#define PER_CHUNK_OVERHEAD	(SHA1_LEN + sizeof(int))

#define int_ntoa(x)	inet_ntoa(*((struct in_addr *)&x))

/* parameters */
int g_Verbose = FALSE;
int g_MultiResolution = FALSE;
int g_Offline = FALSE;
int g_CompleteSessionOnly = TRUE;
int g_GenerateLogFiles = FALSE;
int g_DumpChunkBlocks = FALSE;
int g_WebObjectStat = FALSE;
int g_NameOnly = FALSE;
int g_UseLibnids = FALSE;
int g_HandleOOODP = FALSE;
int g_PrintCacheHit = 0;
int g_ChunkingMode = 2;
char g_MeasureHit = FALSE;
FILE* g_Outfile = NULL;
FILE* g_BlockFile = NULL;
FILE* g_SessionFile = NULL;

/* HTTP object map */
typedef struct HttpObjectKey {
	char* req_line;
	char* host;
} HttpObjectKey;

typedef struct HttpObjectValue {
	int totNumAccess;
	char isCacheable;
	int uniqueBytes;
	int totalBytes;
} HttpObjectValue;

struct hashtable* g_HttpObjectMap = NULL;

unsigned int httpObject_hash_from_key_fn(void* k)
{
	int i = 0;
	int hash = 0;
	HttpObjectKey* key = k;
	for (i = 0; i < strlen(key->req_line); i++) {
		hash += *(((unsigned char*)key->req_line) + i);
	}
	for (i = 0; i < strlen(key->host); i++) {
		hash += *(((unsigned char*)key->host) + i);
	}
	return hash;
}

int httpObject_keys_equal_fn(void* key1, void* key2)
{
	HttpObjectKey *k1, *k2;
	k1 = key1;
	k2 = key2;
	if (strlen(k1->host) == strlen(k2->host) &&
			strcmp(k1->host, k2->host) == 0 &&
			strlen(k1->req_line) == strlen(k2->req_line) &&
			strcmp(k1->req_line, k2->req_line) == 0) {
		return TRUE;
	}
	else {
		return FALSE;
	}
}

/* session map */
typedef struct PacketNode {
	u_int seq;
	u_int len;
	char* data;
	struct PacketNode* pNext;
} PacketNode;

typedef struct MyHalfStream {
	char first;
	char* buffer;
	int length;
	int total_bytes;
	int unique_bytes;
	struct timeval lastPacketTime;
	PacketNode* pNode;
	int packetq_len;
	RabinCtx rctx;
} MyHalfStream;

typedef struct SessionValue {
	HttpObjectKey* objKey;
	struct tuple4 addr;
	MyHalfStream client;
	MyHalfStream server;
	int fin_count;
	LIST_ENTRY(SessionValue) list;
} SessionValue;

typedef struct LossSessionValue {
	struct tuple4 addr;
	u_int lastSeq;
	int lastLen;
	MyHalfStream stream;
	char isClient;
	LIST_ENTRY(LossSessionValue) list;
	LIST_ENTRY(LossSessionValue) deadlist;
} LossSessionValue;

typedef LIST_HEAD(SessionList, SessionValue) SessionList;
SessionList g_SessionList = LIST_HEAD_INITIALIZER(g_SessionList);

typedef LIST_HEAD(LossSessionList, LossSessionValue) LossSessionList;
LossSessionList g_LossSessionList = LIST_HEAD_INITIALIZER(g_LossSessionList);
LossSessionList g_DeadLossSessionList = LIST_HEAD_INITIALIZER(g_DeadLossSessionList);

struct hashtable* g_SessionMap = NULL;
struct hashtable* g_LossSessionMap = NULL;

unsigned int session_hash_from_key_fn(void* k)
{
	int i = 0;
	int hash = 0;
	for (i = 0; i < sizeof(struct tuple4); i++) {
		hash += *(((unsigned char*)k) + i);
	}
	return hash;
}
int session_keys_equal_fn(void* key1, void* key2)
{
	return (memcmp(key1, key2, sizeof(struct tuple4)) == 0);
}

/* chunk map: reuse chunkcache.h from waprox */
typedef struct CacheValue2 {
	/* we don't need the actual block */
	int block_length;
	int numAccess;
} CacheValue2;

typedef struct ChunkMap {
	struct hashtable* map;
} ChunkMap;

ChunkMap g_ChunkMap;

/* index cache */
struct hashtable* g_IndexCacheMRC;

struct hashtable* g_IndexCacheUL;
unsigned int g_NumLargeCacheEntriesUL = 0;
unsigned int g_NumNormalCacheEntriesUL = 0;

struct hashtable* g_IndexCacheUS;
unsigned int g_NumSmallCacheEntriesUS = 0;
unsigned int g_NumNormalCacheEntriesUS = 0;
unsigned int g_NumUSNames = 0;

/* chunk index */
struct hashtable* g_IndexMRC;
struct hashtable* g_IndexUL;
struct hashtable* g_IndexUS;

/* basic cache entry */
typedef struct CacheEntryBasic {
	#if 0
	unsigned int diskBlockNo;
	unsigned short chunkLength;
	#endif
} CEB;

typedef struct CacheEntryPartial {
	u_char parent[SHA1_LEN]; 
	#if 0
	unsigned short offset;
	unsigned short length;
	#endif
} CER;

typedef struct CacheEntryComposite {
	u_char numChildren;
	u_char** children; /* children names */
} CEC;

/* MRC scheme */
typedef CEB CacheMRC;

/* Unique Large scheme */
typedef struct CacheUL {
	char isLargest;
	union {
		CEB basic;
		CER partial;
	};
} CacheUL;

/* Unique Small scheme */
typedef struct CacheUS {
	char isSmallest;
	union {
		CEB basic;
		CEC composite;
	};
} CacheUS;

int g_InitTimer = FALSE;
struct timeval g_LastTimer;
struct timeval g_LastBufferTimeout;
struct timeval g_LastLossSessionCheck;

/* misc. */
int g_NumBufferTimeout = 0;
struct timeval g_StartTime;
struct timeval g_CurrentTime;
unsigned long long g_NumIPPackets = 0;
unsigned long long g_NumIPFragPackets = 0;
unsigned long long g_NumUDPPackets = 0;
unsigned long long g_BytesIP = 0;
unsigned long long g_BytesIPFrag = 0;
unsigned long long g_BytesUDP = 0;
int g_NumSessions = 0;
unsigned long long g_NumLossSessions = 0;
char** g_filenames = NULL;
int g_NumFiles = 0;
unsigned long long g_NumPacketLoss = 0;
unsigned long long g_BytesTCP = 0;
unsigned long long g_BytesLoss = 0;
unsigned long long g_NumSessionDropped = 0;
unsigned long long g_NumLocalHits = 0;
unsigned long long g_NumLocalMisses = 0;
unsigned long long g_NumMissingChunkRequests = 0;
unsigned long long g_ActualTotalBytes = 0;
unsigned long long g_WaproxTotalBytes = 0;
unsigned long long g_ChunkCacheBytes = 0;
unsigned long long g_NumChunkTransactions = 0;
unsigned long long g_OverheadBytes = 0;
unsigned long long g_NumChunkWrites = 0;

/*
   TODO
   1. minimum byte threshold
   2. chunk stats by level
   3. dynamic level
 */

/* from nids/tcp.h */
struct skbuff {
	struct skbuff *next;
	struct skbuff *prev;

	void *data;
	u_int len;
	u_int truesize;
	u_int urg_ptr;

	char fin;
	char urg;
	u_int seq;
	u_int ack;
};

/* function decl. */
void CheckLossSession(char forceFlushing);
void AppendBuffer(MyHalfStream* stream, char* buffer, int length);
int ProcessRabinChunking(char isClient, MyHalfStream* stream, int force,
		HttpObjectKey* key);
int ProcessRabinChunkingRaw(RabinCtx* rctx, char * host, char* buffer,
		int buf_len, int force, int* uniqueBytes);
void PrintStats(ChunkMap* pMap, FILE* out);
void ProcessBufferTimeout(struct timeval* curTime);
char* adres(struct tuple4 addr);
int PutChunk(ChunkMap* pMap, u_char* digest, int rb_size, char* block);
SessionValue* CreateSession(struct tuple4* addr);
void DestroySession(struct tuple4* addr);
int ProcessOneRabinChunking3(RabinCtx* rctx, char* psrc, int buf_len);
int MyResolve(OneChunkName* pNames, int numNames,
		MRCPerLevelInfo* pli, int maxLevel);

/* update index */
void UpdateIndex(OneChunkName* pNames, int numNames, MRCPerLevelInfo* pli,
		int maxLevel);
void UpdateMRC(OneChunkName* pName);
void UpdateUL(OneChunkName* pName, u_char* parent);
void UpdateUS(OneChunkName* pNames, int numNames, MRCPerLevelInfo* pli,
		int lowLevel);
void MyFlushChildren(OneChunkName* pNames, int* stack, int sp, char isSmallest);

/* update cache index */
void UpdateIndexCache(u_char* name);
void UpdateIndexCacheMRC(u_char* name);
void UpdateIndexCacheUL(u_char* name, int recursion_level);
void UpdateIndexCacheUS(u_char* name, int recursion_level);

static char* GetLogFileName(const char* prefix)
{
	static char filename[1024];
	bzero(filename, sizeof(filename));
	if (g_MultiResolution == TRUE) {
		sprintf(filename, "%s_m%02d_%06d_%06d.txt", prefix,
				g_NumMRCWays, g_MinChunkSize, g_ChunkSize);
	}
	else {
		sprintf(filename, "%s_s_%06d.txt", prefix, g_ChunkSize);
	}
	return filename;
}

#if 0
static void my_nids_packetloss(struct ip* iph, struct tcphdr* tcph,
		struct skbuff* packet)
{
	if (packet->len <= 0) {
		return;
	}

	char saddr[20], daddr[20];
	strcpy(saddr, int_ntoa(iph->ip_src.s_addr));
	strcpy(daddr, int_ntoa(iph->ip_dst.s_addr));
	if (g_Verbose) {
		fprintf(stderr, "packet loss! from %s:%hu to  %s:%hu, "
				"len: %d, seq: %u\n",
				saddr, ntohs(tcph->th_sport),
				daddr, ntohs(tcph->th_dport), 
				packet->len, packet->seq);
	}

	g_NumPacketLoss++;
	g_BytesLoss += packet->len;

	if (g_CompleteSessionOnly == TRUE) {
		return;
	}

	struct tuple4 addr;
	addr.saddr = iph->ip_src.s_addr;
	addr.daddr = iph->ip_dst.s_addr;
	addr.source = ntohs(tcph->th_sport);
	addr.dest = ntohs(tcph->th_dport);

	SessionValue* svalue = hashtable_search(g_SessionMap, &addr);
	if (svalue != NULL) {
		NiceExit(-1, "loss session found in regular session!\n");
	}

	LossSessionValue* value = hashtable_search(g_LossSessionMap, &addr);
	if (value == NULL) {
		/* create new session */
		struct tuple4* key = malloc(sizeof(struct tuple4));
		if (key == NULL) {
			fprintf(stderr, "tuple4 key allocation failed\n");
			exit(1);
		}
		memcpy(key, &addr, sizeof(struct tuple4));

		value = malloc(sizeof(LossSessionValue));
		if (value == NULL) {
			fprintf(stderr, "loss session value allocation failed\n");
			exit(1);
		}
		memcpy(&value->addr, key, sizeof(struct tuple4));
		value->lastSeq = packet->seq;
		value->lastLen = packet->len;

		value->stream.buffer = malloc(BUFFER_SIZE);
		if (value->stream.buffer == NULL) {
			fprintf(stderr, "buffer allocation failed\n");
			exit(1);
		}
		value->stream.length = 0;
		value->stream.lastPacketTime = nids_last_pcap_header->ts;
		value->stream.unique_bytes = 0;
		value->stream.total_bytes = 0;

		/* differentiate client and server streams */
		if (addr.source >= addr.dest) {
			/* client */
			value->isClient = TRUE;
		}
		else {
			/* server */
			value->isClient = FALSE;
		}

		AppendBuffer(&value->stream, packet->data, packet->len);
		assert(value->stream.length == packet->len);
		ProcessRabinChunking(value->isClient, &value->stream, FALSE, NULL);

		if (!hashtable_insert(g_LossSessionMap, key, value)) {
			fprintf(stderr, "new loss session insertion failed\n");
			exit(1);
		}

		LIST_INSERT_HEAD(&g_LossSessionList, value, list);
		g_NumLossSessions++;
	}
	else {
		/* TODO: check if packets are delivered in order or not */
		if (value->lastSeq + value->lastLen == packet->seq) {
			/* continous packets */
			AppendBuffer(&value->stream, packet->data, packet->len);
			assert(value->stream.length > 0);
			/*fprintf(stderr, "continuous: %d bytes\n", value->stream.length);*/
			#if 0
			int i;
			fprintf(stderr, "\n-------------------------------\n");
			for (i = 0; i < value->stream.length; i++) {
				fprintf(stderr, "%c", value->stream.buffer[i]);
			}
			fprintf(stderr, "\n-------------------------------\n");
			#endif
			ProcessRabinChunking(value->isClient, &value->stream, FALSE, NULL);
		}
		else {
			/* there's a hole, force chunk processing */
			/*fprintf(stderr, "hole, we had %d bytes\n", value->stream.length);*/
			ProcessRabinChunking(value->isClient, &value->stream, TRUE, NULL);
			assert(value->stream.length == 0);
			AppendBuffer(&value->stream, packet->data, packet->len);
			assert(value->stream.length == packet->len);
		}

		/* update last seq */
		value->lastSeq = packet->seq;
		value->lastLen = packet->len;
		value->stream.lastPacketTime = nids_last_pcap_header->ts;
	}
}
#endif

static void my_nids_syslog(int type, int errnum, struct ip *iph, void *data)
{
	char saddr[20], daddr[20];
	switch (type) {
		case NIDS_WARN_IP:
			if (errnum != NIDS_WARN_IP_HDR) {
				strcpy(saddr, int_ntoa(iph->ip_src.s_addr));
				strcpy(daddr, int_ntoa(iph->ip_dst.s_addr));
				fprintf(stderr, "%s, packet (apparently) from %s to %s\n", nids_warnings[errnum], saddr, daddr);
			} else
				fprintf(stderr, "%s\n", nids_warnings[errnum]);
			break;

		case NIDS_WARN_TCP:
			strcpy(saddr, int_ntoa(iph->ip_src.s_addr));
			strcpy(daddr, int_ntoa(iph->ip_dst.s_addr));
			if (errnum != NIDS_WARN_TCP_HDR) {
				/*
				fprintf(stderr, "%s,from %s:%hu to  %s:%hu\n", nids_warnings[errnum], saddr, ntohs(((struct tcphdr *) data)->th_sport), daddr, ntohs(((struct tcphdr *) data)->th_dport));
				*/
			}
			else
				fprintf(stderr, "%s,from %s to %s\n", nids_warnings[errnum], saddr, daddr);
			break;

		case NIDS_WARN_SCAN:
			fprintf(stderr, "NIDS_WARN_SCAN?\n");
			exit(0);
			break;

		default:
			fprintf(stderr, "Unknown warning number ?\n");
	}
}

static void my_ip_frag_func(struct ip* a_packet, int len)
{
	/* TODO: implement Neil Spring's method */
	g_NumIPFragPackets++;
	g_BytesIPFrag += len;

	/* if offline analysis, check timer events here */
	struct timeval result;
	if (nids_params.filename != NULL) {
		assert(g_Offline == TRUE);
		if (g_InitTimer == FALSE) {
			g_LastTimer = nids_last_pcap_header->ts;
			g_LastBufferTimeout = nids_last_pcap_header->ts;
			g_LastLossSessionCheck = nids_last_pcap_header->ts;
			g_StartTime = nids_last_pcap_header->ts;
			g_CurrentTime = nids_last_pcap_header->ts;
			g_InitTimer = TRUE;
		}

		timersub(&g_CurrentTime, &g_LastBufferTimeout, &result);
		if ((result.tv_sec * 1000) + (result.tv_usec / 1000) >=
				g_BufferTimeout && g_BufferTimeout > 0) {
			/* buffer timeout */
			ProcessBufferTimeout(&nids_last_pcap_header->ts);
			g_LastBufferTimeout = nids_last_pcap_header->ts;
		}

		/* check loss session every 30 seconds */
		#define LOSS_SESSION_CHECK	(30)
		timersub(&g_CurrentTime, &g_LastLossSessionCheck, &result);
		if ((result.tv_sec * 1000) + (result.tv_usec / 1000) >=
				(LOSS_SESSION_CHECK * 1000)) {
			/* buffer timeout */
			CheckLossSession(FALSE);
			g_LastLossSessionCheck = nids_last_pcap_header->ts;
		}

		timersub(&g_CurrentTime, &g_LastTimer, &result);
		if (result.tv_sec >= g_TimerInterval) {
			/* timer */
			PrintStats(&g_ChunkMap, stderr);
			g_LastTimer = nids_last_pcap_header->ts;
		}

		g_CurrentTime = nids_last_pcap_header->ts;
	}
}

/* implement custom TCP reassembly */
void my_process_tcp(u_char* data, int skblen)
{
	struct ip *this_iphdr = (struct ip *)data;
	struct tcphdr *this_tcphdr = (struct tcphdr *)(data +
			4 * this_iphdr->ip_hl);
	int iplen = ntohs(this_iphdr->ip_len);
	char* payload = (char *) (this_tcphdr) + 4 * this_tcphdr->th_off;

	/* ip length check */
	if ((unsigned)iplen < 4 * this_iphdr->ip_hl + sizeof(struct tcphdr)) {
		TRACE("ip length error, discard\n");
		return;
	} 

	/* data length check */
	int datalen = iplen - 4 * this_iphdr->ip_hl - 4 * this_tcphdr->th_off;
	if (datalen < 0) {
		TRACE("data length error, discard\n");
		return;
	} 

	struct tuple4 addr, raddr;
	addr.saddr = this_iphdr->ip_src.s_addr;
	addr.daddr = this_iphdr->ip_dst.s_addr;
	addr.source = ntohs(this_tcphdr->th_sport);
	addr.dest = ntohs(this_tcphdr->th_dport);
	raddr.saddr = addr.daddr;
	raddr.daddr = addr.saddr;
	raddr.source = addr.dest;
	raddr.dest = addr.source;
	char saddr[20], daddr[20];
	strcpy(saddr, int_ntoa(this_iphdr->ip_src.s_addr));
	strcpy(daddr, int_ntoa(this_iphdr->ip_dst.s_addr));
	char isClient = TRUE;
	SessionValue* ssn = hashtable_search(g_SessionMap, &addr);
	if (ssn != NULL) {
		/* session found */
		isClient = TRUE;
	}
	else {
		/* try reverse direction */
		ssn = hashtable_search(g_SessionMap, &raddr);
		if (ssn != NULL) {
			/* session found */
			isClient = FALSE;
		}
		else {
			/* a new session! */
			isClient = TRUE;
			if (!(this_tcphdr->th_flags & TH_SYN)) {
				/* garbage packet? */
				return;
			}
			ssn = CreateSession(&addr);
			if (g_Verbose) {
				TRACE("new session (%s:%u to %s:%u),"
						" len: %d / %d\n",
						saddr, addr.source,
						daddr, addr.dest,
						ssn->client.total_bytes,
						ssn->server.total_bytes);
			}
		}
	}

	assert(ssn != NULL);
	MyHalfStream* myhlf = NULL;
	if (isClient) {
		myhlf = &ssn->client;
	}
	else {
		myhlf = &ssn->server;
	}

	if (g_HandleOOODP) {
		u_int seq = ntohl(this_tcphdr->th_seq);
		/* create a new packet node */
		PacketNode* pNode = malloc(sizeof(PacketNode));
		if (pNode == NULL) {
			NiceExit(-1, "PacketNode malloc failed\n");
		}
		pNode->seq = seq;
		pNode->len = datalen;
		pNode->data = malloc(datalen);
		pNode->pNext = NULL;
		if (pNode->data == NULL) {
			NiceExit(-1, "packet data malloc failed\n");
		}
		memcpy(pNode->data, payload, datalen);

		if (myhlf->pNode == NULL) {
			/* empty */
			/*TRACE("empty\n");*/
			myhlf->pNode = pNode;
			myhlf->packetq_len++;
		}
		else {
			PacketNode* pCur = myhlf->pNode;
			PacketNode* pPrev = NULL;
			char done = FALSE;
			while (pCur != NULL) {
				if (pNode->seq == pCur->seq) {
					/* duplicate, ignore */
					free(pNode->data);
					free(pNode);
					done = TRUE;
					break;
				}
				else if (pNode->seq < pCur->seq) {
					/*TRACE("insert!\n");*/
					/* insert here before */
					pNode->pNext = pCur;
					if (pPrev == NULL) {
						/* change head */
						myhlf->pNode = pNode;
					}
					else {
						/* change non-head */
						pPrev->pNext = pNode;
					}
					myhlf->packetq_len++;
					done = TRUE;
					break;
				}
				else if (pNode->seq > pCur->seq) {
					/* see next one */
					pPrev = pCur;
					pCur = pCur->pNext;
				}
				else {
					assert(FALSE);
				}
			}

			if (!done) {
				/* insert at the end */
				/*TRACE("insert at end\n");*/
				assert(pPrev != NULL);
				assert(pCur == NULL);
				pPrev->pNext = pNode;
				pNode->pNext = NULL;
				myhlf->packetq_len++;
			}
		}

		/*TRACE("qlen: %d\n", myhlf->packetq_len);*/
		assert(myhlf->packetq_len <= MAX_PACKET_QUEUE);

		/* check the packet queue */
		if (myhlf->packetq_len >= MAX_PACKET_QUEUE) {
			/* fill the buffer first */
			PacketNode* pNode = myhlf->pNode;
			PacketNode* pPrev = NULL;
			while (pNode != NULL) {
				PacketNode* pNext = pNode->pNext;
				if (pPrev != NULL) {
					/*
					   TRACE("%u <= %u\n", pPrev->seq, pNode->seq);
					 */
					assert(pPrev->seq <= pNode->seq);
					int gap = pNode->seq - pPrev->seq - pPrev->len;
					if (gap != 0) {
						/*
						   TRACE("gap: %d bytes\n", gap);
						 */
					}
					g_BytesLoss += gap;
				}

				AppendBuffer(myhlf, pNode->data, pNode->len);
				g_BytesTCP += pNode->len;
				pPrev = pNode;
				pNode = pNext;
			}

			/* free all */
			pNode = myhlf->pNode;
			while (pNode != NULL) {
				PacketNode* pNext = pNode->pNext;
				free(pNode->data);
				free(pNode);
				myhlf->packetq_len--;
				assert(myhlf->packetq_len >= 0);
				pNode = pNext;
			}

			/* everything should be flushed */
			assert(myhlf->packetq_len == 0);
			myhlf->pNode = NULL;
		}
	}
	else {
		AppendBuffer(myhlf, payload, datalen);
		g_BytesTCP += datalen;
	}

	/* processing chunk for this half */
	if (myhlf->length >= MEMBUFFERSIZE) {
		ProcessRabinChunking(isClient, myhlf, FALSE, ssn->objKey);
	}

	if (this_tcphdr->th_flags & TH_FIN || this_tcphdr->th_flags & TH_RST) {
		/* FIN/RST: close session */
		/*TRACE("FIN|RST\n");*/
		ssn->fin_count++;
		if (ssn->fin_count >= 2) {
			if (g_Verbose) {
				TRACE("close session (%s:%u to %s:%u),"
						" len: %d / %d\n",
						saddr, addr.source,
						daddr,addr.dest,
						ssn->client.total_bytes,
						ssn->server.total_bytes);
			}
			DestroySession(&addr);
		}
	}
}

static void my_ip_func(u_char* data, int skblen)
{
	struct ip *this_iphdr = (struct ip *)data;
	int iplen = ntohs(this_iphdr->ip_len);
	g_NumIPPackets++;
	g_BytesIP += iplen;
	switch (((struct ip *) data)->ip_p) {
		case IPPROTO_TCP:
			if (!g_UseLibnids) {
				my_process_tcp(data, skblen);
			}
			break;
		default:
			/* ignore other pakcets */
			break;
	}
}

static void my_udp_func(struct ip* a_packet, int len)
{
	g_NumUDPPackets++;
	g_BytesUDP += len;
}
	
static void my_no_mem(char* message)
{
	fprintf(stderr, "%s: we're running out of memory!\n", message);
	exit(0);
}

void PrintStats(ChunkMap* pMap, FILE* out)
{
	float saving = 0, saving2 = 0;
	if (g_ActualTotalBytes > 0) {
		saving = 100 - ((float)g_WaproxTotalBytes
				/ g_ActualTotalBytes * 100);
		saving2 = 100 - ((float)(g_WaproxTotalBytes + g_OverheadBytes)
				/ g_ActualTotalBytes * 100);
	}

	float avgChunkSize = 0;
	if (hashtable_count(pMap->map) > 0) {
		avgChunkSize = (float)g_ChunkCacheBytes
				/ hashtable_count(pMap->map);
	}

	struct timeval result;
	timersub(&g_CurrentTime, &g_StartTime, &result);
	fprintf(out, "[%d secs]\n", (int)result.tv_sec);
	fprintf(out, "IP bytes: %lld / %lld pkts: %lld / %lld\n",
			g_BytesIP, g_BytesIPFrag,
			g_NumIPPackets, g_NumIPFragPackets);
	fprintf(out, "UDP bytes: %lld pkts : %lld\n", 
			g_BytesUDP, g_NumUDPPackets);
	fprintf(out, "TCP bytes: %lld conns: %d / %d\n", g_BytesTCP, 
			hashtable_count(g_SessionMap), g_NumSessions);
	fprintf(out,	"TCP Loss bytes: %lld conns: %lld pkts: %lld\n",
			g_BytesLoss, g_NumLossSessions, g_NumPacketLoss);
	fprintf(out, "# chunk transactions: %lld\n", g_NumChunkTransactions);
	fprintf(out, "Chunk bytes: %lld # chunks: %d Avg chunk size: %.2f\n",
			g_ChunkCacheBytes, hashtable_count(pMap->map),
			avgChunkSize);
	fprintf(out, "# chunk requests: %lld bytes: %lld "
			"# local lookups: %lld %lld # write: %lld\n",
			g_NumMissingChunkRequests, g_WaproxTotalBytes,
			g_NumLocalHits, g_NumLocalMisses, g_NumChunkWrites);
	fprintf(out, "Per level hits:");
	int i;
	for (i = 0; i < g_maxLevel; i++) {
		fprintf(out, " %d", g_PerLevelStats[i].numHits);
	}
	fprintf(out, "\n");
	fprintf(out, "Per level bytes:");
	for (i = 0; i < g_maxLevel; i++) {
		fprintf(out, " %d", g_PerLevelStats[i].bytesHits);
	}
	fprintf(out, "\n");

	fprintf(out, "Saving: %lld (%lld) / %lld %.2f (%.2f) %%\n",
			g_WaproxTotalBytes + g_OverheadBytes,
			g_WaproxTotalBytes, g_ActualTotalBytes,
			saving2, saving);
	fprintf(out, "SUMMARY %lld %lld %lld %.2f %.2f %lld %d %.2f "
			"%lld %lld %lld\n",
			g_OverheadBytes, g_WaproxTotalBytes,
			g_ActualTotalBytes, saving2, saving,
			g_ChunkCacheBytes, hashtable_count(pMap->map),
			avgChunkSize, g_NumMissingChunkRequests,
			g_NumLocalHits, g_NumLocalMisses);
	fprintf(out, "INDEX MRC: %d UL: %d US: %d\n",
			hashtable_count(g_IndexMRC),
			hashtable_count(g_IndexUL),
			hashtable_count(g_IndexUS));
	fprintf(out, "INDEX-CACHE MRC: %d UL: %d (%d/%d) US: %d (%d/%d/%d)\n",
			hashtable_count(g_IndexCacheMRC),
			hashtable_count(g_IndexCacheUL),
			g_NumLargeCacheEntriesUL, g_NumNormalCacheEntriesUL,
			hashtable_count(g_IndexCacheUS),
			g_NumSmallCacheEntriesUS, g_NumNormalCacheEntriesUS,
			g_NumUSNames);
	fprintf(out, "----------------------------------------------\n");
}

static int MyLookupCallBack(u_char* digest)
{
	CacheValue2* value = hashtable_search(g_ChunkMap.map, digest);
	if (value != NULL) {
		if (g_MeasureHit) {
			TRACE("HIT: %d\n", value->block_length);
		}
		return TRUE;
	}
	else {
		return FALSE;
	}
}

void PutChunkCallBack(u_char* digest, char* buffer, int buf_len)
{
	PutChunk(&g_ChunkMap, digest, buf_len, buffer);
	g_NumChunkWrites++;
}

int ProcessChunkTree(ChunkMap* pMap, ChunkTreeInfo* cti, char* block)
{
	assert(g_MultiResolution == TRUE);

	/* 1. first resolve all the chunks: hit or miss */
	ResolveMRCTreeInMemory(cti, MyLookupCallBack);

	/* 2. based on the policy, generate missing chunk lists */
	/*SamplePolicy(cti, block);*/
	int i, index = 0;
	OneChunkName* names = cti->pNames;
	int numNames = 0;
	int* resultArray = MissingChunkPolicy(cti, &numNames);
	int bytesMissingChunks = 0;
	int numMissingChunks = 0;
	for (i = 0; i < numNames; i++) {
		index = resultArray[i];
		if (cti->chunkState[index] == FALSE) {
			numMissingChunks++;
			bytesMissingChunks += names[index].length;
		}
	}
	assert(cti->bytesMissingChunks <= bytesMissingChunks);
	assert(cti->numMissingChunks <= numMissingChunks);
	/*
	cti->bytesMissingChunks = bytesMissingChunks;
	cti->numMissingChunks = numMissingChunks;
	*/

	/* update stat here */
	g_NumLocalHits += cti->numLocalHits;
	g_NumLocalMisses += cti->numLocalMisses;
	g_WaproxTotalBytes += cti->bytesMissingChunks;
	g_NumMissingChunkRequests += cti->numMissingChunks;
	g_OverheadBytes += PER_CHUNK_OVERHEAD * cti->numMissingChunks;	

	/* 3. put the missing chunks in the cache for the future use */
	PutMissingChunks(cti, block, names[0].length, PutChunkCallBack);
	return cti->bytesMissingChunks;
}

int PutChunk(ChunkMap* pMap, u_char* digest, int rb_size, char* block)
{
	CacheValue2* value = hashtable_search(pMap->map, digest);
	if (value == NULL) {
		/* miss */
		/*fprintf(stderr, "miss, %d bytes\n", rb_size);*/
		CacheKey* key = NewKey(digest, rb_size);
		value = malloc(sizeof(CacheValue2));
		if (value == NULL) {
			fprintf(stderr, "chunk allocation failed\n");
			exit(1);
		}
		value->block_length = rb_size;
		value->numAccess = 1;
		if (!hashtable_insert(pMap->map, key, value)) {
			fprintf(stderr, "new chunk insertion failed\n");
			exit(1);
		}
		g_ChunkCacheBytes += rb_size;

		/* store the chunk in the file system */
		if (g_BlockFile != NULL) {
			assert(g_DumpChunkBlocks == TRUE);
			/* write chunk name first */
			fprintf(g_BlockFile, "\n%s\n", SHA1Str(digest));
			while (TRUE) {

				/* write chunk block */
				int numWrite = fwrite(block, sizeof(u_char),
						rb_size, g_BlockFile);
				if (numWrite == 0) {
					fprintf(stderr, "chunk write failed\n");
					exit(1);
				}
				rb_size -= numWrite;
				if (rb_size == 0) {
					/* we're done */
					break;
				}
			}
		}

		return FALSE;
	}
	else {
		/* hit */
		/*fprintf(stderr, "hit, %d bytes\n", rb_size);*/
		assert(value->block_length == rb_size);
		value->numAccess++;
		return TRUE;
	}
}

int ProcessOneChunk(ChunkMap* pMap, u_char* digest, int rb_size, char* block)
{
	assert(g_MultiResolution == FALSE);
	g_OverheadBytes += PER_CHUNK_OVERHEAD;
	if (PutChunk(pMap, digest, rb_size, block) == FALSE) {
		/* cache miss: should fetch the missing chunk from peers */
		g_NumLocalMisses++;
		g_WaproxTotalBytes += rb_size;
		g_NumMissingChunkRequests++;
		g_OverheadBytes += PER_CHUNK_OVERHEAD;
		g_NumChunkWrites++;
		return rb_size;
	}
	else {
		/* cache hit: do nothing */
		g_NumLocalHits++;
		if (g_MeasureHit) {
			TRACE("HIT: %d\n", rb_size);
		}
		return 0;
	}
}

int ProcessRabinChunkingRaw(RabinCtx* rctx, char * host, char* buffer,
		int buf_len, int force, int* uniqueBytes)
{
	int num_processed = 0;
	//u_char digest[SHA1_LEN];
	int rb_size = 0;
	*uniqueBytes = 0;

	while (buf_len > 0) {
		/* decide if process or not */
		/*if (force == FALSE && buf_len < g_ChunkSize * 5) {*/
		if (force == FALSE && buf_len < BUFFER_SIZE / 2) {
			/* wait until we get data more */
			break;
		}

		rb_size = ProcessOneRabinChunking3(rctx, buffer, buf_len);
		#if 0
		/* apply rabin's fingerprinting here */
		rb_size = GetRabinChunkSizeMrcCtx(mrc_ctx, (u_char*)buffer,
				buf_len, g_ChunkSize, FALSE);
		Calc_SHA1Sig((u_char*)buffer, rb_size, digest);

		/* store this chunk in the cache */
		if (g_NameOnly == FALSE) {
			g_NumChunkTransactions++;
			if (g_MultiResolution == FALSE) {
				*uniqueBytes += ProcessOneChunk(&g_ChunkMap,
						digest, rb_size, buffer);
			}
			else {
				/* generate multi-level chunk names */
				int numNames = 0;
				OneChunkName* pNames = MakePreMRCTree(mrc_ctx,
						buffer, rb_size, 0, &numNames,
						NULL);
				assert(numNames > 0);
				assert(numNames < g_maxNames);
				g_OverheadBytes += PER_CHUNK_OVERHEAD
					* numNames;
				ChunkTreeInfo* cti = CreateNewChunkTreeInfo(
						pNames, numNames);
				InitChunkTreeInfo(cti, pNames, numNames, 0);
				*uniqueBytes += ProcessChunkTree(&g_ChunkMap,
						cti, buffer);
				DestroyChunkTreeInfo(cti);
			}
		}
		else {
			assert(g_Outfile != NULL);
			#if 0
			fprintf(g_Outfile, "%s %d %d %s\n", SHA1Str(digest),
					rb_size, (int)g_CurrentTime.tv_sec,
					host);
			#endif
			fprintf(g_Outfile, "%s %d\n", SHA1Str(digest), rb_size);
		}
		#endif

		/* advance */
		buf_len -= rb_size;
		num_processed += rb_size;
		buffer += rb_size;
	}

	if (force == TRUE) {
		assert(buf_len == 0);
	}

	return num_processed;
}

int ProcessRabinChunking(char isClient, MyHalfStream* stream, int force,
		HttpObjectKey* key)
{
	int uniqueBytes = 0;
	int num_processed = 0;
	char* host = NULL;
	if (key != NULL) {
		host = key->host;
	}

	if ((g_ChunkingMode == 2 /* both */) ||
			(g_ChunkingMode == 0 /* client */ && isClient) ||
			(g_ChunkingMode == 1 /* server */ && !isClient)) {
		num_processed = ProcessRabinChunkingRaw(&(stream->rctx),
				host, stream->buffer, stream->length,
				force, &uniqueBytes);
	}
	else {
		/* drain everything */
		uniqueBytes = stream->length;
		num_processed = stream->length;
	}

	if (key != NULL) {
		HttpObjectValue* obj = hashtable_search(g_HttpObjectMap, key);
		assert(obj != NULL);
		obj->uniqueBytes += uniqueBytes;
		obj->totalBytes += num_processed;
	}
	stream->total_bytes += num_processed;
	stream->unique_bytes += uniqueBytes;
	//fprintf(stderr, "uniq: %d, total: %d\n", uniqueBytes, num_processed);
	g_ActualTotalBytes += num_processed;

	/* adjust buffer */
	if (num_processed > 0) {
		memmove(stream->buffer, stream->buffer + num_processed,
				stream->length - num_processed);
		stream->length -= num_processed;
		assert(stream->length >= 0);
	}

	return num_processed;
}

void AppendBuffer(MyHalfStream* stream, char* buffer, int length)
{
	assert(stream->length + length < BUFFER_SIZE);
	memcpy(stream->buffer + stream->length, buffer, length);
	stream->length += length;
}

void DestroyAllSession()
{
	while (hashtable_count(g_SessionMap) > 0) {
		SessionValue* ssn = LIST_FIRST(&g_SessionList);
		DestroySession(&(ssn->addr));
	}		
}

void CheckLossSession(char forceFlushing)
{
	LossSessionValue* walk;
	struct timeval cur_time, result;
	UpdateCurrentTime(&cur_time);
	LIST_FOREACH(walk, &g_LossSessionList, list) {
		timersub(&cur_time, &walk->stream.lastPacketTime, &result);
		if (forceFlushing == TRUE) {
			ProcessRabinChunking(walk->isClient, &walk->stream, TRUE, NULL);
		}

		#define LOSS_SESSION_TIMEOUT (300)
		if (result.tv_sec > LOSS_SESSION_TIMEOUT) {
			ProcessRabinChunking(walk->isClient, &walk->stream, TRUE, NULL);

			/* delete the session */
			LIST_INSERT_HEAD(&g_DeadLossSessionList, walk, deadlist);
		}
	}

	while(!LIST_EMPTY(&g_DeadLossSessionList)) {
		walk = LIST_FIRST(&g_DeadLossSessionList);
		LossSessionValue* value = hashtable_remove(g_LossSessionMap,
				&walk->addr);
		if (value == NULL) {
			fprintf(stderr, "loss session remove failed\n");
			exit(1);
		}

		static int LossID;
		fprintf(stderr, "LossID: %d Bytes: %d / %d\n", LossID++,
				value->stream.unique_bytes, value->stream.total_bytes);

		/* clean up */
		LIST_REMOVE(walk, list);
		LIST_REMOVE(walk, deadlist);
		free(walk->stream.buffer);
		free(walk);
	}
}

void ProcessBufferTimeout(struct timeval* curTime)
{
	/* check every session for buffer timeout */
	if (g_BufferTimeout == 0) {
		/* we don't flush at all */
		return;
	}

	SessionValue* walk;
	struct timeval result;
	LIST_FOREACH(walk, &g_SessionList, list) {
		/* check client buffer */
		timersub(curTime, &walk->client.lastPacketTime, &result);
		if ((result.tv_sec * 1000) + (result.tv_usec / 1000) >=
				g_BufferTimeout && walk->client.length > 0) {
			/* flush server buffer */
			ProcessRabinChunking(FALSE, &walk->client, TRUE,
					walk->objKey);
			g_NumBufferTimeout++;
		}

		/* check server buffer */
		timersub(curTime, &walk->server.lastPacketTime, &result);
		if ((result.tv_sec * 1000) + (result.tv_usec / 1000) >=
				g_BufferTimeout && walk->server.length > 0) {
			/* flush client buffer */
			ProcessRabinChunking(TRUE, &walk->server, TRUE,
					walk->objKey);
			g_NumBufferTimeout++;
		}
	}
}

void CheckTimerEvents2()
{
	/* this is only valid for online mode */
	assert(nids_params.filename == NULL && g_Offline == FALSE);
	struct timeval cur_time, result;
	UpdateCurrentTime(&cur_time);
	timersub(&cur_time, &g_LastTimer, &result);
	if (result.tv_sec >= g_TimerInterval) {
		/* timer */
		PrintStats(&g_ChunkMap, stderr);
		UpdateCurrentTime(&g_LastTimer);
	}
	
	timersub(&cur_time, &g_LastBufferTimeout, &result);
	if ((result.tv_sec * 1000) + (result.tv_usec / 1000) >=
			g_BufferTimeout && g_BufferTimeout > 0) {
		/* buffer timeout */
		ProcessBufferTimeout(&cur_time);
		UpdateCurrentTime(&g_LastBufferTimeout);
	}
}

void ParseHeaderLine(char* buffer, int buf_len, SessionValue* session,
		char isRequest)
{
	#define READ_BUFFER_SIZE (1024 * 4)
	static char* LINE_DELIMETERS[] = {"\r\n", "\n\r", "\r", "\n"};
	char* line_end = NULL;
	int i;
	char isFirstLine = TRUE;
	char stop = FALSE;
	char first_line[READ_BUFFER_SIZE];
	char host_line[READ_BUFFER_SIZE];
	char pragma_line[READ_BUFFER_SIZE];
	first_line[0] = '\0';
	host_line[0] = '\0';
	pragma_line[0] = '\0';
	
	while (!stop) {
		stop = TRUE;
		for (i = 0; i < sizeof(LINE_DELIMETERS) / sizeof(char*);
				i++) {
			line_end = strstr(buffer, LINE_DELIMETERS[i]);
			if (line_end != NULL) {
				/* find a new line */
				int offset = line_end - buffer;
				if (isFirstLine == TRUE) {
					memcpy(first_line, buffer, offset);
					first_line[offset] = '\0';
					isFirstLine = FALSE;
				}
				else if (strncasecmp(buffer, "HOST:", 5) == 0) {
					memcpy(host_line, buffer, offset);
					host_line[offset] = '\0';
				}
				else if (strncasecmp(buffer, "PRAGMA:", 7)
						== 0) {
					memcpy(pragma_line, buffer, offset);
					pragma_line[offset] = '\0';
				}

				offset += strlen(LINE_DELIMETERS[i]);
				buffer += offset;
				buf_len -= offset;
				stop = FALSE;
				break;
			}
		}
	}

	if (session->objKey == NULL) {
		assert(isRequest == TRUE && strlen(first_line) > 0
			&& strlen(host_line) > 0);
		/* client request */
		HttpObjectKey* key = malloc(sizeof(HttpObjectKey));
		if (key == NULL) {
			fprintf(stderr, "HttpObjectKey allocation failed\n");
			exit(1);
		}

		/* request line */
		key->req_line = malloc(strlen(first_line) + 1);
		if (key->req_line == NULL) {
			fprintf(stderr, "req_line allocation failed\n");
			exit(1);
		}
		memcpy(key->req_line, first_line, strlen(first_line) + 1);
		//fprintf(stderr, "Request: %s\n", key->req_line);

		/* host line */
		char* host = strstr(host_line, " ");
		assert(host != NULL);
		key->host = malloc(strlen(host + 1) + 1);
		if (key->host == NULL) {
			fprintf(stderr, "host allocation failed\n");
			exit(1);
		}
		memcpy(key->host, host + 1, strlen(host + 1) + 1);
		//fprintf(stderr, "Host: %s\n", key->host);

		session->objKey = key;

		#if 0
		char URI[READ_BUFFER_SIZE];
		char* host = strstr(host_line, " ");
		assert(host != NULL);
		char* path = strstr(first_line, " ");
		assert(path != NULL);
		char* ver = strstr(path + 1, " ");
		ver[0] = '\0';
		memcpy(URI, host + 1, strlen(host + 1));
		memcpy(URI + strlen(host + 1), path + 1, strlen(path + 1));
		URI[strlen(URI)] = '\0';
		fprintf(stderr, "%s\n", URI);
		#endif
	}
	assert(session->objKey != NULL);

	HttpObjectValue* obj = hashtable_search(g_HttpObjectMap,
			session->objKey);
	if (obj == NULL) {
		/* new object */
		obj = malloc(sizeof(HttpObjectValue));
		obj->totNumAccess = 1;
		obj->isCacheable = TRUE;
		obj->uniqueBytes = 0;
		obj->totalBytes = 0;
		if (obj == NULL) {
			fprintf(stderr, "Http object allocation failed\n");
			exit(1);
		}

		if (!hashtable_insert(g_HttpObjectMap, session->objKey, obj)) {
			fprintf(stderr, "new Http object insertion failed\n");
			exit(1);
		}
	}
	else {
		obj->totNumAccess++;
		//fprintf(stderr, "existing object: %d\n", obj->totNumAccess);
	}
	assert(obj != NULL);

	if (strlen(pragma_line) > 0) {
		/* this should be the response */
		//assert(isRequest == FALSE);
		if (strstr(pragma_line, "no-cache") != NULL ||
			strstr(pragma_line, "private") != NULL) {
			obj->isCacheable = FALSE;
		}
	}
}

/* struct tuple4 contains addresses and port numbers of the TCP connections
   the following auxiliary function produces a string looking like
   10.0.0.1,1024,10.0.0.2,23 */
char* adres(struct tuple4 addr)
{
	static char buf[256];
	strcpy(buf, int_ntoa(addr.saddr));
	sprintf(buf + strlen(buf), ",%i,", addr.source);
	strcat(buf, int_ntoa(addr.daddr));
	sprintf(buf + strlen(buf), ",%i", addr.dest);
	return buf;
}

void DestroySession(struct tuple4* addr)
{
	/* delete the session */
	SessionValue* value = hashtable_remove(g_SessionMap, addr);
	if (value == NULL) {
		struct tuple4 raddr;
		raddr.saddr = addr->daddr;
		raddr.daddr = addr->saddr;
		raddr.source = addr->dest;
		raddr.dest = addr->source;
		value = hashtable_remove(g_SessionMap, &raddr);
		if (value == NULL) {
			fprintf(stderr, "session remove failed\n");
			exit(1);
		}
	}
	assert(value != NULL);

	/* flush buffer */
	ProcessRabinChunking(FALSE, &value->client, TRUE, value->objKey);
	ProcessRabinChunking(TRUE, &value->server, TRUE, value->objKey);

	/* log per-session stat */
	static int ID = 0;
	if (g_SessionFile != NULL) {
		HttpObjectValue* obj = NULL;
		if (value->objKey != NULL) {
			obj = hashtable_search(g_HttpObjectMap, value->objKey);
			assert(obj != NULL);
		}
		char isCacheable = '-';
		if (obj != NULL) {
			isCacheable = obj->isCacheable ? 'T' : 'F';
		}
		fprintf(g_SessionFile, "ID: %d TX: %d / %d RX: %d / %d Cacheable: %c\n",
				ID++, value->server.unique_bytes,
				value->server.total_bytes,
				value->client.unique_bytes,
				value->client.total_bytes,
				isCacheable);
	}

	/* clean up */
	LIST_REMOVE(value, list);
	free(value->client.buffer);
	free(value->server.buffer);
	free(value);
}

SessionValue* CreateSession(struct tuple4* addr)
{
	/* create new session */
	struct tuple4* key = malloc(sizeof(struct tuple4));
	if (key == NULL) {
		fprintf(stderr, "tuple4 key allocation failed\n");
		exit(1);
	}
	memcpy(key, addr, sizeof(struct tuple4));
	SessionValue* value = malloc(sizeof(SessionValue));
	if (value == NULL) {
		fprintf(stderr, "session value allocation failed\n");
		exit(1);
	}
	memcpy(&value->addr, addr, sizeof(struct tuple4));
	value->client.lastPacketTime = nids_last_pcap_header->ts;
	value->server.lastPacketTime = nids_last_pcap_header->ts;
	value->client.first = TRUE;
	value->server.first = TRUE;
	value->objKey = NULL;
	value->fin_count = 0;
	value->client.pNode = NULL;
	value->client.packetq_len = 0;
	value->server.pNode = NULL;
	value->server.packetq_len = 0;
	InitRabinCtx(&(value->client.rctx), g_MinChunkSize,
			WAPROX_RABIN_WINDOW, WAPROX_MRC_MIN,
			WAPROX_MRC_MAX);
	InitRabinCtx(&(value->server.rctx), g_MinChunkSize,
			WAPROX_RABIN_WINDOW, WAPROX_MRC_MIN,
			WAPROX_MRC_MAX);

	value->client.buffer = malloc(BUFFER_SIZE);
	if (value->client.buffer == NULL) {
		fprintf(stderr, "client buffer allocation failed\n");
		exit(1);
	}
	value->client.length = 0;
	value->client.total_bytes = 0;
	value->client.unique_bytes = 0;
	value->server.buffer = malloc(BUFFER_SIZE);
	if (value->server.buffer == NULL) {
		fprintf(stderr, "server buffer allocation failed\n");
		exit(1);
	}
	value->server.length = 0;
	value->server.total_bytes = 0;
	value->server.unique_bytes = 0;

	if (!hashtable_insert(g_SessionMap, key, value)) {
		fprintf(stderr, "new session insertion failed\n");
		exit(1);
	}

	LIST_INSERT_HEAD(&g_SessionList, value, list);
	g_NumSessions++;
	if (g_NumSessions >= g_PrintCacheHit && !g_MeasureHit) {
		/* cache warm: start to measure hit chunks */
		g_MeasureHit = TRUE;

		/* reset counter */
		g_NumLocalHits = 0;
		g_NumLocalMisses = 0;
		g_NumMissingChunkRequests = 0;
		g_ActualTotalBytes = 0;
		g_WaproxTotalBytes = 0;
		g_ChunkCacheBytes = 0;
		g_NumChunkTransactions = 0;
		g_OverheadBytes = 0;
		g_NumChunkWrites = 0;

		int i;
		for (i = 0; i < g_maxLevel; i++) {
			g_PerLevelStats[i].numHits = 0;
			g_PerLevelStats[i].bytesHits = 0;
		}
	}
	return value;
}

void tcp_callback(struct tcp_stream *a_tcp, void ** this_time_not_needed)
{
	g_BytesTCP += a_tcp->client.count_new;
	g_BytesTCP += a_tcp->server.count_new;

	char buf[1024];
	strcpy(buf, adres(a_tcp->addr)); // we put conn params into buf
	if (a_tcp->nids_state == NIDS_JUST_EST) {
		// connection described by a_tcp is established
		// here we decide, if we wish to follow this stream
		// sample condition: if (a_tcp->addr.dest!=23) return;
		// in this simple app we follow each stream, so..
		a_tcp->client.collect++; // we want data received by a client
		a_tcp->server.collect++; // and by a server, too
		a_tcp->server.collect_urg++; 
		a_tcp->client.collect_urg++; 
		if (g_Verbose) {
			fprintf(stderr, "%s established\n", buf);
		}

		/* create new session */
		CreateSession(&a_tcp->addr);
		return;
	}
	else if (a_tcp->nids_state == NIDS_CLOSE
			|| a_tcp->nids_state == NIDS_RESET
			|| a_tcp->nids_state == NIDS_TIMED_OUT
			|| a_tcp->nids_state == NIDS_EXITING) {
		// connection has been closed normally
		if (g_Verbose) {
			fprintf(stderr, "%s closing\n", buf);
		}

		DestroySession(&a_tcp->addr);
		return;
	}
	else if (a_tcp->nids_state == NIDS_DATA)
	{
		// new data has arrived; gotta determine in what direction
		// and if it's urgent or not

		struct half_stream *hlf;
		MyHalfStream *myhlf, *myhlf2;
		struct timeval result;
		int msec;
		if (a_tcp->server.count_new_urg) {
			// new byte of urgent data has arrived 
			strcat(buf,"(urgent->)");
			buf[strlen(buf)+1]=0;
			buf[strlen(buf)]=a_tcp->server.urgdata;
			write(1,buf,strlen(buf));
			return;
		}
		if (a_tcp->client.count_new_urg) {
			// new byte of urgent data has arrived 
			strcat(buf,"(urgent<-)");
			buf[strlen(buf)+1]=0;
			buf[strlen(buf)]=a_tcp->client.urgdata;
			write(1,buf,strlen(buf));
			return;
		}

		SessionValue* value = hashtable_search(g_SessionMap,
				&a_tcp->addr);
		if (value == NULL) {
			fprintf(stderr, "session value search failed\n");
			exit(1);
		}

		char isClient = FALSE;
		if (a_tcp->client.count_new) {
			/* new data for client */
			hlf = &a_tcp->client; 
			myhlf = &value->client;
			myhlf2 = &value->server;
			strcat(buf, "(<-)"); 
			isClient = FALSE;
		}
		else {
			/* new data for server */
			hlf = &a_tcp->server;
			myhlf = &value->server;
			myhlf2 = &value->client;
			strcat(buf, "(->)");
			isClient = TRUE;
		}

		if (myhlf->first == TRUE && g_WebObjectStat == TRUE) {
			/* extract request line and host */
			ParseHeaderLine(hlf->data, hlf->count_new, value,
					a_tcp->server.count_new ? TRUE : FALSE);
			myhlf->first = FALSE;
		}

		/* copy the data */
		AppendBuffer(myhlf, hlf->data, hlf->count_new);

		/* check the time gap */
		timersub(&(nids_last_pcap_header->ts),
				&myhlf->lastPacketTime, &result);
		msec = result.tv_sec * 1000 + result.tv_usec / 1000;
		assert(msec >= 0);
		if (g_Verbose) {
			fprintf(stderr,"%s, gap: %d ms, %d bytes\n",
					buf, msec, hlf->count_new);
		}

		/* update last packet arrival time */
		myhlf->lastPacketTime = nids_last_pcap_header->ts;

		/* force processing the other half first */
		if (myhlf2->length > 0) {
			ProcessRabinChunking(!isClient, myhlf2, TRUE,
					value->objKey);
		}

		/* processing chunk for this half */
		ProcessRabinChunking(isClient, myhlf, FALSE, value->objKey);
		return;
	}
	else {
		fprintf(stderr, "should not reach here\n");
		exit(1);
	}
}

void printUsage(char* program)
{
	fprintf(stderr, "%s [-options] [tcpdump trace file...]\n", program);
	fprintf(stderr, "\t-k: chunking mode: client(0) server(1), both(2), default=2\n");
	fprintf(stderr, "\t-i: device interface, default=NULL\n");
	fprintf(stderr, "\t-u: minimum chunk size in bytes, default=128\n");
	fprintf(stderr, "\t-c: chunk size in bytes, default=1024\n");
	fprintf(stderr, "\t-b: buffer timeout in msec, no timeout if 0, default=50\n");
	fprintf(stderr, "\t-e: pcap filter expression, default=NULL\n");
	fprintf(stderr, "\t-o: chunk name output filename, default=NULL\n");
	fprintf(stderr, "\t-n: # maximum hosts, default=65536\n");
	fprintf(stderr, "\t-s: # maximum tcp stream, default=65536\n");
	fprintf(stderr, "\t-t: print stats interval in sec, default=1\n");
	fprintf(stderr, "\t-v: verbose output, default=no\n");
	fprintf(stderr, "\t-m: use multiprocess, default=no\n");
	fprintf(stderr, "\t-r: N-way multi-resolution chuncking\n");
	fprintf(stderr, "\t-g: analyze incomplete sessions as well\n");
	fprintf(stderr, "\t-f: generate various log files\n");
	fprintf(stderr, "\t-d: dump chunk blocks\n");
	fprintf(stderr, "\t-w: generate per Web object stat file\n");
	fprintf(stderr, "\t-l: use libnids tcp callback\n");
	fprintf(stderr, "\t-a: handle out-of-order-delivery packets\n");
	fprintf(stderr, "\t-p: print cache hit chunks after p sessions, default=0\n");
	fprintf(stderr, "\t-h: show all the options\n");
}

void printParameters()
{
	fprintf(stderr, "Buffer Timeout (ms): %d\n", g_BufferTimeout);
	fprintf(stderr, "Chunk Size (bytes): %d\n", g_ChunkSize);
	fprintf(stderr, "filename: %s\n", nids_params.filename);
	if (g_MultiResolution) {
		fprintf(stderr, "max level: %d\n", g_maxLevel);
		fprintf(stderr, "max names: %d\n", g_maxNames);
	}
}

void getOptions(int argc, char** argv)
{
	int c;

	opterr = 0;
	while ((c = getopt(argc, argv, "k:i:u:c:b:e:n:s:t:o:r:p:mvagfdwlh"))
			!= -1) {
		switch (c)
		{
			case 'k': /* chunking mode: client/server/both */
				g_ChunkingMode = atoi(optarg);
				break;
			case 'i': /* interface */
				nids_params.device = optarg;
				g_Offline = FALSE;
				break;
			case 'u': /* min chunksize (bytes) */
				g_MinChunkSize = atoi(optarg);
				break;
			case 'c': /* chunksize (bytes) */
				g_ChunkSize = atoi(optarg);
				break;
			case 'b': /* buffer timeout (ms) */
				g_BufferTimeout = atoi(optarg);
				break;
			case 'e': /* filter expression */
				nids_params.pcap_filter = optarg;
				break;
			case 'n': /* num maximum hosts */
				nids_params.n_hosts = atoi(optarg);
				break;
			case 's': /* num maximum tcp stream */
				nids_params.n_tcp_streams = atoi(optarg);
				break;
			case 't': /* print stats timer interval */
				g_TimerInterval = atoi(optarg);
				break;
			case 'm': /* use multi-process */
				nids_params.multiproc = 1;
				break;
			case 'r': /* multi-resolution */
				g_MultiResolution = TRUE;
				g_NumMRCWays = atoi(optarg);
				break;
			case 'h': /* show usage */
				printUsage(argv[0]);
				exit(1);
			case 'v': /* verbose output */
				g_Verbose = TRUE;
				break;
			case 'g': /* include dropped session */
				g_CompleteSessionOnly = FALSE;
				break;
			case 'f': /* generate log files */
				g_GenerateLogFiles = TRUE;
				break;
			case 'd': /* dump chunk blocks */
				g_DumpChunkBlocks = TRUE;
				break;
			case 'w': /* generate per Web oject stat file */
				g_WebObjectStat = TRUE;
				break;
			case 'l': /* use libnids tcp call back */
				g_UseLibnids = TRUE;
				break;
			case 'a': /* handle out-of-order-delivery packets */
				g_HandleOOODP = TRUE;
				break;
			case 'p': /* print cache hit chunks after p sessions */
				g_PrintCacheHit = atoi(optarg);
				break;
			case 'o': /* chunk name only */
				g_NameOnly = TRUE;
				g_Outfile = fopen(optarg, "w");
				if (g_Outfile == NULL) {
					fprintf(stderr, "outfile %s open failed\n", optarg);
					exit(0);
				}
				break;
			case '?':
				if (optopt == 'f' || optopt == 'i'
						|| optopt == 'u'
						|| optopt == 'r'
						|| optopt == 'c'
						|| optopt == 'e'
						|| optopt == 'p'
						|| optopt == 'o'
						|| optopt == 'b') {
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				}
				else if (isprint(optopt)) {
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				}
				else {
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				}
				exit(1);
			default:
				break;
		}
	}

	/* register syslog function */
	nids_params.syslog = my_nids_syslog;

	/* turn off port scanning */
	nids_params.scan_num_hosts = 0;

	/* register no_mem function */
	nids_params.no_mem = my_no_mem;

	/* increase the default values */
	nids_params.n_hosts = 65536;
	nids_params.n_tcp_streams = 65536;

	#if 0
	if (g_UseLibnids) {
		/* register pakcetloss function */
		nids_params.packetloss = my_nids_packetloss;
	}
	#endif

	g_NumFiles = argc - optind;
	g_filenames = &argv[optind];
	if (g_NumFiles > 0) {
		/* offline mode */
		g_Offline = TRUE;
	}

	char* filename = GetLogFileName("session");
	g_SessionFile = fopen(filename, "w");
	if (g_SessionFile == NULL) {
		fprintf(stderr, "outfile %s open failed\n", filename);
		exit(0);
	}

	InitMRCParams();
}

void loop()
{
	int fd = nids_getfd();
	fd_set rset;
	UpdateCurrentTime(&g_StartTime);
	UpdateCurrentTime(&g_LastTimer);
	UpdateCurrentTime(&g_LastBufferTimeout);
	UpdateCurrentTime(&g_LastLossSessionCheck);
	g_InitTimer = TRUE;
	for (;;)
	{
		struct timeval tv;
		tv.tv_sec = g_BufferTimeout / 1000;
		tv.tv_usec = (g_BufferTimeout % 1000) * 1000;
		FD_ZERO(&rset);
		FD_SET(fd, &rset);
		UpdateCurrentTime(&g_CurrentTime);
		CheckTimerEvents2();
		if (select(fd + 1, &rset, 0, 0, &tv)) {
			if (FD_ISSET(fd, &rset)) {
				if (!nids_next()) {
					fprintf(stderr, "%s\n", nids_errbuf);
					break;
				}
			}
		}
	}
}

void initLibnids()
{
	/* init libnids */
	if (!nids_init()) {
		fprintf(stderr,"%s\n",nids_errbuf);
		exit(1);
	}

	/* disable checksum for all packets */
	struct nids_chksum_ctl ctl;
	ctl.netaddr = inet_addr("0.0.0.0");
	ctl.mask = inet_addr("0.0.0.0");
	ctl.action = NIDS_DONT_CHKSUM;
	nids_register_chksum_ctl(&ctl, 1);

	if (g_UseLibnids) {
		/* register tcp call back */
		nids_register_tcp(tcp_callback);

		/* register udp call back */
		nids_register_udp(my_udp_func);
	}

	/* register ip frag call back */
	nids_register_ip_frag(my_ip_frag_func);

	/* register ip call back */
	nids_register_ip(my_ip_func);

	if (g_DumpChunkBlocks == TRUE) {
		char* filename = GetLogFileName("block");
		g_BlockFile = fopen(filename, "w");
		if (g_BlockFile == NULL) {
			fprintf(stderr, "outfile %s open failed\n", filename);
			exit(1);
		}
	}

	printParameters();
}

void GenerateLogFiles()
{
	/* print object stats */
	char* filename = GetLogFileName("object");
	FILE* objFile = fopen(filename, "w");
	if (objFile == NULL) {
		fprintf(stderr, "outfile %s open failed\n", filename);
		exit(0);
	}

	unsigned long long cacheableTotalBytes = 0;
	unsigned long long cacheableUniqueBytes = 0;
	unsigned long long uncacheableTotalBytes = 0;
	unsigned long long uncacheableUniqueBytes = 0;
	struct hashtable_itr* itr = NULL;
	if (hashtable_count(g_HttpObjectMap) > 0) {
		itr = hashtable_iterator(g_HttpObjectMap);
		do {
			HttpObjectKey* k = hashtable_iterator_key(itr);
			HttpObjectValue* v = hashtable_iterator_value(itr);
			fprintf(objFile, "\"%s\" \"%s\" %d %d %d %d\n",
					k->host, k->req_line,
					v->totNumAccess, v->isCacheable ? 1 : 0,
					v->uniqueBytes, v->totalBytes);
			if (v->isCacheable == TRUE) {
				/* cacheable */
				cacheableTotalBytes += v->totalBytes;
				cacheableUniqueBytes += v->uniqueBytes;
			}
			else {
				/* uncacheable */
				uncacheableTotalBytes += v->totalBytes;
				uncacheableUniqueBytes += v->uniqueBytes;
			}
		} while (hashtable_iterator_advance(itr));
	}
	free(itr);
	fclose(objFile);

	/* print summary stats */
	filename = GetLogFileName("summary");
	FILE* summaryFile = fopen(filename, "w");
	if (summaryFile == NULL) {
		fprintf(stderr, "outfile %s open failed\n", filename);
		exit(0);
	}
	PrintStats(&g_ChunkMap, summaryFile);
	fprintf(summaryFile, "CACHE_SUMMARY %lld %lld %lld %lld\n",
			cacheableUniqueBytes, cacheableTotalBytes,
			uncacheableUniqueBytes, uncacheableTotalBytes);
	fclose(summaryFile);

	/* print chunk access frequency */
	filename = GetLogFileName("chunk");
	FILE* chunkFile = fopen(filename, "w");
	if (chunkFile == NULL) {
		fprintf(stderr, "outfile %s open failed\n", filename);
		exit(0);
	}

	if (hashtable_count(g_ChunkMap.map) > 0) {
		itr = hashtable_iterator(g_ChunkMap.map);
		do {
			CacheValue2* v = hashtable_iterator_value(itr);
			fprintf(chunkFile, "%d %d\n", v->block_length,
					v->numAccess);
		} while (hashtable_iterator_advance(itr));
	}
	free(itr);
	fclose(chunkFile);
}

int main(int argc, char** argv)
{
	/* set libnids parameters */
	getOptions(argc, argv);

	/* create chunk map */
	g_ChunkMap.map = create_hashtable(65536,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (g_ChunkMap.map == NULL) {
		fprintf(stderr, "chunk map creation failed\n");
		exit(1);
	}

	/* create index cache */
	g_IndexCacheMRC = create_hashtable(65536,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (g_IndexCacheMRC == NULL) {
		fprintf(stderr, "index cache MRC creation failed\n");
		exit(1);
	}
	g_IndexCacheUS = create_hashtable(65536,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (g_IndexCacheUS == NULL) {
		fprintf(stderr, "index cache US creation failed\n");
		exit(1);
	}
	g_IndexCacheUL = create_hashtable(65536,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (g_IndexCacheUL == NULL) {
		fprintf(stderr, "index cache UL creation failed\n");
		exit(1);
	}
	g_IndexMRC = create_hashtable(65536,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (g_IndexMRC == NULL) {
		fprintf(stderr, "index MRC creation failed\n");
		exit(1);
	}
	g_IndexUS = create_hashtable(65536,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (g_IndexUS == NULL) {
		fprintf(stderr, "index US creation failed\n");
		exit(1);
	}
	g_IndexUL = create_hashtable(65536,
			chunk_hash_from_key_fn,
			chunk_keys_equal_fn);
	if (g_IndexUL == NULL) {
		fprintf(stderr, "index UL creation failed\n");
		exit(1);
	}

	g_ActualTotalBytes = 0;
	g_WaproxTotalBytes = 0;
	g_ChunkCacheBytes = 0;

	/* create the chunk cache */
	#if 0
	g_GenerateNameHint = FALSE;
	if (!CreateCache(g_MaxCacheSize * 1024 * 1024)) {
		NiceExit(-1, "cache table creation failed\n");
	}
	#endif

	/* create session map */
	g_SessionMap = create_hashtable(65536,
			session_hash_from_key_fn,
			session_keys_equal_fn);
	if (g_SessionMap == NULL) {
		fprintf(stderr, "session map creation failed\n");
		exit(1);
	}

	g_LossSessionMap = create_hashtable(65536,
			session_hash_from_key_fn,
			session_keys_equal_fn);
	if (g_LossSessionMap == NULL) {
		fprintf(stderr, "loss session map creation failed\n");
		exit(1);
	}

	/* create HTTP object map */
	g_HttpObjectMap = create_hashtable(65536,
			httpObject_hash_from_key_fn,
			httpObject_keys_equal_fn);
	if (g_HttpObjectMap == NULL) {
		fprintf(stderr, "HTTP object map creation failed\n");
		exit(1);
	}

	/* start running */
	if (g_Offline == FALSE) {
		/* online: read from network interface */
		initLibnids();
		loop();
	}
	else {
		int i;
		for (i = 0; i < g_NumFiles; i++) {
			/* set input trace file */
			nids_params.filename = g_filenames[i];

			/* offline: read from captured files */
			initLibnids();
			nids_run();
			DestroyAllSession();
			CheckLossSession(TRUE);
			PrintStats(&g_ChunkMap, stderr);
		}
	}

	if (g_Outfile != NULL) {
		fclose(g_Outfile);
	}

	if (g_BlockFile != NULL) {
		fclose(g_BlockFile);
	}

	if (g_SessionFile != NULL) {
		fclose(g_SessionFile);
	}

	if (g_GenerateLogFiles == TRUE) {
		GenerateLogFiles();
	}

	return 0;
}

/*-----------------------------------------------------------------*/
int ProcessOneRabinChunking3(RabinCtx* rctx, char* psrc, int buf_len)
{
	/* get MRC first */
	int numNames = 0;
	int rb_size = 0;
	int maxLevel = 0;
	MRCPerLevelInfo* pli = NULL;
	OneChunkName* pNames = DoPreMRC(rctx, psrc, buf_len,
			&numNames, &rb_size, &maxLevel, &pli);
	assert(numNames > 0);
	assert(rb_size <= buf_len);

	/* make tree */
	MakeTree(pli, maxLevel, pNames, numNames);

	/* resolve tree */
	int lowLevel = MyResolve(pNames, numNames, pli, maxLevel);
	assert(lowLevel < maxLevel);
	
	/* find candidate chunks */
	CandidateList* candidateList = GetCandidateList2(pNames, numNames,
			pli, lowLevel);

	/* update index cache */
	assert(candidateList != NULL);
	OneCandidate* walk = NULL;
	TAILQ_FOREACH(walk, candidateList, list) {
		if (walk->isHit) {
			UpdateIndexCache(walk->sha1name);
			g_NumLocalHits++;
		}
		else {
			g_WaproxTotalBytes += walk->length;
		}

		g_ActualTotalBytes += walk->length;
	}

	/* update index */
	UpdateIndex(pNames, numNames, pli, maxLevel);

	return rb_size;
}
/*-----------------------------------------------------------------*/
int MyResolve(OneChunkName* pNames, int numNames,
		MRCPerLevelInfo* pli, int maxLevel)
{
	/* start from root, check name hint */
	int i;
	int lowLevel = 0;
	for (i = 0; i < maxLevel; i++) {
		int numHits = 0;
		int numChunks = pli[i].numNames;
		int newIdx = pli[i].firstIndex;
		int j;
		int offset = 0;
		lowLevel = i;
		for (j = 0; j < numChunks; j++) {
			assert(newIdx < numNames);
			OneChunkName* cur = &pNames[newIdx];
			assert(offset == cur->offset);
			Calc_SHA1Sig((const byte*)cur->pChunk, cur->length,
					cur->sha1name);
			cur->isSha1Ready = TRUE;
			if (hashtable_search(g_IndexMRC, cur->sha1name)
					!= NULL) {
				cur->isHit = TRUE;
			}
			else {
				cur->isHit = FALSE;
			}

			newIdx++;
			offset += cur->length;
			if (cur->isHit == TRUE) {
				numHits++;
			}
		}

		#if 0
		if (numHits == numChunks) {
			/* everything is hit, this level */
			break;
		}
		#endif
	}
	return lowLevel;
}
/*-----------------------------------------------------------------*/
void UpdateMRC(OneChunkName* pName)
{
	if (hashtable_search(g_IndexMRC, pName->sha1name) != NULL) {
		/* we already have it */
		return;
	}

	CacheKey* key = NewKey(pName->sha1name, pName->length);
	CacheMRC* value = malloc(sizeof(CacheMRC));
	if (value == NULL) {
		fprintf(stderr, "CacheMRC allocation failed\n");
		exit(1);
	}
	#if 0
	value->diskBlockNo = 0;
	value->chunkLength = pName->length;
	#endif
	if (!hashtable_insert(g_IndexMRC, key, value)) {
		fprintf(stderr, "CacheMRC insertion failed\n");
		exit(1);
	}
}
/*-----------------------------------------------------------------*/
void UpdateUL(OneChunkName* pName, u_char* parent)
{
	if (hashtable_search(g_IndexUL, pName->sha1name) != NULL) {
		/* we already have it */
		return;
	}

	CacheKey* key = NewKey(pName->sha1name, pName->length);
	CacheUL* value = malloc(sizeof(CacheUL));
	if (value == NULL) {
		fprintf(stderr, "CacheUL allocation failed\n");
		exit(1);
	}

	if (parent == NULL) {
		value->isLargest = TRUE;
		#if 0
		value->basic.diskBlockNo = 0;
		value->basic.chunkLength = pName->length;
		#endif
	}
	else {
		value->isLargest = FALSE;
		memcpy(value->partial.parent, parent, SHA1_LEN);
		#if 0
		value->partial.offset = 0;
		value->partial.length = 0;
		#endif
	}
	if (!hashtable_insert(g_IndexUL, key, value)) {
		fprintf(stderr, "CacheUL insertion failed\n");
		exit(1);
	}
}
/*-----------------------------------------------------------------*/
void UpdateIndex(OneChunkName* pNames, int numNames, MRCPerLevelInfo* pli,
		int maxLevel)
{
	/* start from root, check name hint */
	int i;
	int lowLevel = 0;
	for (i = 0; i < maxLevel; i++) {
		int numHits = 0;
		int numChunks = pli[i].numNames;
		int newIdx = pli[i].firstIndex;
		int j;
		int offset = 0;
		lowLevel = i;
		for (j = 0; j < numChunks; j++) {
			assert(newIdx < numNames);
			OneChunkName* cur = &pNames[newIdx];
			assert(offset == cur->offset);

			/* 1. update MRC always */
			UpdateMRC(cur);

			/* 2. update UniqueLarge */
			if (newIdx == 0) {
				/* update the largest entry */
				UpdateUL(cur, NULL);
			}
			else {
				/* update normal entry: give the parent */
				UpdateUL(cur, pNames[0].sha1name);
			}

			newIdx++;
			offset += cur->length;
			if (cur->isHit == TRUE) {
				numHits++;
			}
		}
	}

	/* 3. update UniqueSmall */
	UpdateUS(pNames, numNames, pli, maxLevel - 1);
}
/*-----------------------------------------------------------------*/
void UpdateIndexCache(u_char* name)
{
	UpdateIndexCacheMRC(name);
	UpdateIndexCacheUL(name, 0);
	UpdateIndexCacheUS(name, 0);
}
/*-----------------------------------------------------------------*/
void UpdateIndexCacheMRC(u_char* name)
{
	if (hashtable_search(g_IndexCacheMRC, name) != NULL) {
		/* already loaded */
		return;
	}

	CacheMRC* value = hashtable_search(g_IndexMRC, name);
	if (value == NULL) {
		fprintf(stderr, "CacheMRC not found\n");
		exit(1);
	}

	CacheKey* key = NewKey(name, 0);
	CacheMRC* newvalue = malloc(sizeof(CacheMRC));
	if (newvalue == NULL) {
		fprintf(stderr, "CacheMRC allocation failed\n");
		exit(1);
	}
	memcpy(newvalue, value, sizeof(CacheMRC));
	if (!hashtable_insert(g_IndexCacheMRC, key, newvalue)) {
		fprintf(stderr, "CacheMRC insertion failed\n");
		exit(1);
	}
}
/*-----------------------------------------------------------------*/
void UpdateIndexCacheUL(u_char* name, int recursion_level)
{
	if (recursion_level > g_maxLevel) {
		fprintf(stderr, "CacheUS recursion!\n");
		exit(1);
	}

	CacheUL* value = hashtable_search(g_IndexUL, name);
	if (value == NULL) {
		fprintf(stderr, "CacheUL not found\n");
		exit(1);
	}

	if (hashtable_search(g_IndexCacheUL, name) == NULL) {
		/* load this entry */
		CacheKey* key = NewKey(name, 0);
		CacheUL* newvalue = malloc(sizeof(CacheUL));
		if (newvalue == NULL) {
			fprintf(stderr, "CacheUL allocation failed\n");
			exit(1);
		}
		memcpy(newvalue, value, sizeof(CacheUL));
		if (!hashtable_insert(g_IndexCacheUL, key, newvalue)) {
			fprintf(stderr, "CacheUL insertion failed\n");
			exit(1);
		}

		if (value->isLargest) {
			g_NumLargeCacheEntriesUL++;
		}
		else {
			g_NumNormalCacheEntriesUL++;
		}
	}

	if (!value->isLargest) {
		/* load its parent as well */
		UpdateIndexCacheUL(value->partial.parent, recursion_level + 1);
	}
}
/*-----------------------------------------------------------------*/
void UpdateIndexCacheUS(u_char* name, int recursion_level)
{
	if (recursion_level > g_maxLevel) {
		fprintf(stderr, "CacheUS recursion!\n");
		exit(1);
	}

	CacheUS* value = hashtable_search(g_IndexUS, name);
	if (value == NULL) {
		fprintf(stderr, "CacheUS not found: %s\n",
				SHA1Str(name));
		exit(1);
	}

	if (hashtable_search(g_IndexCacheUS, name) == NULL) {
		/* load this entry */
		CacheKey* key = NewKey(name, 0);
		CacheUS* newvalue = malloc(sizeof(CacheUS));
		if (newvalue == NULL) {
			fprintf(stderr, "CacheUS allocation failed\n");
			exit(1);
		}
		memcpy(newvalue, value, sizeof(CacheUS));
		if (!hashtable_insert(g_IndexCacheUS, key, newvalue)) {
			fprintf(stderr, "CacheUS insertion failed\n");
			exit(1);
		}

		if (value->isSmallest) {
			g_NumSmallCacheEntriesUS++;
		}
		else {
			g_NumNormalCacheEntriesUS++;
			g_NumUSNames += value->composite.numChildren;
		}
	}

	if (!value->isSmallest) {
		/* load its children as well */
		int i;
		for (i = 0; i < value->composite.numChildren; i++) {
			UpdateIndexCacheUS(value->composite.children[i],
					recursion_level + 1);
		}
	}
}
/*-----------------------------------------------------------------*/
void UpdateUS(OneChunkName* pNames, int numNames, MRCPerLevelInfo* pli,
		int lowLevel)
{
	/* find candidate chunks from bottom to top*/
	assert(lowLevel >= 0);
	int i;
	//int base = pli[lowLevel].firstIndex;
	int numChunks = pli[lowLevel].numNames;
	assert(numChunks <= numNames);
	if (g_NumMRCWays == 1) {
		/* SRC, done */
		assert(lowLevel == 0);
	}
	else {
		/* prepare stack */
		static int* stack = NULL;
		if (stack == NULL) {
			stack = malloc(sizeof(int) * g_maxNames);
			if (stack == NULL) {
				NiceExit(-1, "malloc failed\n");
			}
		}

		/* MRC, check chunks bottom up */
		for (i = lowLevel; i >= 0; i--) {
			int numChunks = pli[i].numNames;
			int base = pli[i].firstIndex;
			int j;
			int sp = 0;
			//fprintf(stderr, "level: %d\n", i);
			for (j = 0; j < numChunks; j++) {
				int child = base + j;
				int parent = GetParent2(pNames, child);
				assert(child < numNames);
				assert(parent < numNames);
				assert(parent <= child);

				/* put child in stack */
				stack[sp++] = child;
				assert(sp < g_maxNames);
				/* TRACE("add child %d\n", child); */

				/* check stack */
				if (j == numChunks - 1) {
					/* last chunk, flush */
					MyFlushChildren(pNames, stack, sp,
							i == lowLevel);
					sp = 0;
				}
				else {
					assert((j + 1) <= (numChunks - 1));
					int nextParent = GetParent2(pNames,
							child + 1);
					if (nextParent != parent) {
						/* last child, flush */
						MyFlushChildren(pNames, stack,
								sp,
								i == lowLevel);
						sp = 0;
					}
				}
			}
		}
	}
}
/*-----------------------------------------------------------------*/
void MyFlushChildren(OneChunkName* pNames, int* stack, int sp, char isSmallest)
{
	assert(sp >= 1);
	int i;
	int parent = GetParent2(pNames, stack[0]);

	/* update the smallest chunks */
	if (isSmallest == TRUE) {
		for (i = 0; i < sp; i++) {
			int child = stack[i];
			assert(parent == GetParent2(pNames, child));

			if (hashtable_search(g_IndexUS, pNames[child].sha1name)
					!= NULL) {
				/* we already have it */
				continue;
			}

			CacheKey* key2 = NewKey(pNames[child].sha1name, 0);
			CacheUS* value2 = malloc(sizeof(CacheUS));
			if (value2 == NULL) {
				fprintf(stderr, "CacheUS allocation failed\n");
				exit(1);
			}
			value2->isSmallest = TRUE;
			#if 0
			value2->basic.diskBlockNo = 0;
			value2->basic.chunkLength = pNames[child].length;
			#endif
			if (!hashtable_insert(g_IndexUS, key2, value2)) {
				fprintf(stderr, "CacheUS insertion failed\n");
				exit(1);
			}
			#if 0
			fprintf(stderr, "children: %d, %d bytes\n",
					child, pNames[child].length);
			#endif
		}
	}

	if (hashtable_search(g_IndexUS, pNames[parent].sha1name) != NULL) {
		/* already loaded */
		return;
	}
	
	/* update composite */
	CacheKey* key = NewKey(pNames[parent].sha1name, 0);
	CacheUS* value = malloc(sizeof(CacheUS));
	if (value == NULL) {
		fprintf(stderr, "CacheUS allocation failed\n");
		exit(1);
	}

	value->isSmallest = FALSE;
	value->composite.numChildren = sp;
	value->composite.children = malloc(sizeof(u_char*) * sp);
	if (value->composite.children == NULL) {
		fprintf(stderr, "children allocation failed\n");
		exit(1);
	}

	for (i = 0; i < sp; i++) {
		int child = stack[i];
		assert(parent == GetParent2(pNames, child));
		value->composite.children[i] = malloc(SHA1_LEN);
		if (value->composite.children[i] == NULL) {
			fprintf(stderr, "children %d allocation failed\n", i);
			exit(1);
		}
		memcpy(value->composite.children[i],
				pNames[stack[i]].sha1name, SHA1_LEN);
	}

	if (!hashtable_insert(g_IndexUS, key, value)) {
		fprintf(stderr, "new CacheUS insertion failed\n");
		exit(1);
	}
	//fprintf(stderr, "parent idx: %d\n", parent);
}
/*-----------------------------------------------------------------*/
