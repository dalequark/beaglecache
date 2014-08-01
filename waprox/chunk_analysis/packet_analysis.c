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
#include "rabinfinger2.h"
#include "util.h"
#include "queue.h"

#define int_ntoa(x)	inet_ntoa(*((struct in_addr *)&x))

/* parameters */
int g_Verbose = FALSE;
int g_Offline = FALSE;

int g_InitTimer = FALSE;
struct timeval g_LastTimer;

/*-----------------------------------------------------------------*/
unsigned int finger_hash_from_key_fn(void* k)
{
	int i = 0;
	int hash = 0;
	u_int64* finger = k;
	for (i = 0; i < sizeof(u_int64); i++) {
		hash += *(((unsigned char*)finger) + i);
	}
	return hash;
}
/*-----------------------------------------------------------------*/
int finger_keys_equal_fn(void* key1, void* key2)
{
	#if 0
	u_int64* finger1 = key1;
	u_int64* finger2 = key2;
	return (*finger1 == *finger2);
	#endif
	return (memcmp(key1, key2, sizeof(u_int64)) == 0);
}
/*-----------------------------------------------------------------*/

/* fingerprint index table */
struct hashtable* g_FingerTable;

typedef struct FingerEntry {
	unsigned int pktId;
	short offset;
} FingerEntry;

/* packet cache */
#define MAX_PKT_SIZE		(1800)

typedef struct PacketEntry {
	unsigned short length;
	char data[MAX_PKT_SIZE];
} PacketEntry;

PacketEntry* g_PacketCache;
int g_CurPacketId = 0;
int g_NumPacketsInCache = 0;

/* main parameters */
unsigned int g_ChunkSize = 32;
unsigned int g_WindowSize = 64;
unsigned int g_MaxPackets = 65536;
unsigned int g_Overhead = 8;
char g_IgnoreHeader = FALSE;

/* misc. */
struct timeval g_StartTime;
struct timeval g_CurrentTime;
unsigned long long g_NumIPPackets = 0;
unsigned long long g_NumIPFragPackets = 0;
unsigned long long g_BytesIP = 0;
unsigned long long g_BytesIPFrag = 0;
char** g_filenames = NULL;
int g_NumFiles = 0;
unsigned long long g_NumPacketLoss = 0;
unsigned long long g_BytesLoss = 0;
unsigned long long g_BytesRedundant = 0;
unsigned long long g_BytesOverhead = 0;

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
char* adres(struct tuple4 addr);
static void doPacketChunking(u_char* data, int skblen);

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

	return;
}

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
			g_StartTime = nids_last_pcap_header->ts;
			g_CurrentTime = nids_last_pcap_header->ts;
			g_InitTimer = TRUE;
		}


		timersub(&g_CurrentTime, &g_LastTimer, &result);
		g_CurrentTime = nids_last_pcap_header->ts;
	}
}

static void my_ip_func(u_char* data, int skblen)
{
	struct ip *this_iphdr = (struct ip *)data;
	int iplen = ntohs(this_iphdr->ip_len);
	g_NumIPPackets++;
	g_BytesIP += iplen;

	if (g_IgnoreHeader) {
		/* exclude TCP/IP header */
		assert(skblen >= 20);
		doPacketChunking(data + 20, skblen - 20);
	}
	else {
		doPacketChunking(data, skblen);
	}
}

static RabinBoundary* doRabin(u_char* psrc, int buf_len,
		unsigned int* numBoundaries)
{
	int rabin_buffer_len = MIN(buf_len, MAX_PKT_SIZE);

	/* prepare rabin context */
	static RabinCtx rctx;
	static u_int min_mask = 0;
	static u_int max_mask = 0;
	static char init = FALSE;
	if (init == FALSE) {
		memset(&rctx, 0, sizeof(RabinCtx));
		InitRabinCtx(&rctx, g_ChunkSize, g_WindowSize, 0, 128*1024);
		init = TRUE;
		min_mask = GetMRCMask(g_ChunkSize);
		max_mask = 0;
	}

	/* prepare RabinBoundary */
	static RabinBoundary* rabinBoundaries = NULL;
	*numBoundaries = 0;
	if (rabinBoundaries == NULL) {
		rabinBoundaries = malloc(sizeof(RabinBoundary) * MAX_PKT_SIZE);
		if (rabinBoundaries == NULL) {
			fprintf(stderr, "RabinBoundary alloc failed\n");
			exit(0);
		}
	}

	/* detect all the RabinBoundaries */
	int rb_size = GetRabinChunkSizeEx(&rctx, (const u_char*)psrc,
			rabin_buffer_len, min_mask, max_mask, TRUE,
			rabinBoundaries, numBoundaries);
	#if 0
	fprintf(stderr, "%d bytes, min_mask: %d, max_mask: %d, rb_size: %d, boundaries: %d\n", rabin_buffer_len, min_mask, max_mask, rb_size, *numBoundaries);
	#endif
	assert(rb_size > 0);
	assert(rb_size <= rabin_buffer_len);
	assert(*numBoundaries > 0);
	assert(rabinBoundaries[*numBoundaries - 1].offset == rb_size);
	return rabinBoundaries;
}

static int expand(char left, u_char* data1, int len1, int offset1,
		u_char* data2, int len2, int offset2)
{
	assert(offset1 <= len1);
	assert(offset2 <= len2);
	assert(memcmp(data1 + offset1, data2 + offset2, g_WindowSize) == 0);

	int margin = 0;
	int tmp_offset1 = 0;
	int tmp_offset2 = 0;
	if (left) {
		/* expand left */
		tmp_offset1 = offset1 - 1;
		tmp_offset2 = offset2 - 1;
		while (tmp_offset1 >= 0 && tmp_offset2 >= 0) {
			if (data1[tmp_offset1] == data2[tmp_offset2]) {
				margin++;
				tmp_offset1--;
				tmp_offset2--;
			}
			else {
				break;
			}
		}
	}
	else {
		/* expand right */
		tmp_offset1 = offset1 + g_WindowSize;
		tmp_offset2 = offset2 + g_WindowSize;
		while (tmp_offset1 < len1 && tmp_offset2 < len2) {
			if (data1[tmp_offset1] == data2[tmp_offset2]) {
				margin++;
				tmp_offset1++;
				tmp_offset2++;
			}
			else {
				break;
			}
		}
	}

	return margin;
}

static void doPacketChunking(u_char* data, int skblen)
{
	if (skblen >= MAX_PKT_SIZE) {
		fprintf(stderr, "skip too large packet: %d bytes\n", skblen);
		exit(0);
	}

	if (skblen < g_WindowSize) {
		if (g_Verbose) {
			fprintf(stderr, "skip too small packet: %d bytes\n",
					skblen);
		}
		return;
	}

	#if 0
	fprintf(stderr, "### get %d bytes packet\n", skblen);
	#endif

	/* first check if we have to evict an old packet */
	unsigned int numBoundaries = 0;
	RabinBoundary* rbdrs = NULL;
	int i;
	int nextId = (g_CurPacketId + 1) % g_MaxPackets;
	if (g_PacketCache[nextId].length > 0) {
		/* valid entry: release its fingerprints */
		rbdrs = doRabin((u_char*)g_PacketCache[nextId].data,
				g_PacketCache[nextId].length, &numBoundaries);
		for (i = 0; i < numBoundaries; i++) {
			/* release finger index */
			FingerEntry* fe_val = hashtable_search(g_FingerTable,
					&rbdrs[i].finger);
			if (fe_val != NULL && fe_val->pktId == nextId) {
				fe_val = hashtable_remove(g_FingerTable,
						&rbdrs[i].finger);
				free(fe_val);
			}
		}

		/* invalidate the old packet */
		g_PacketCache[nextId].length = 0;
		g_NumPacketsInCache--;
		assert(g_NumPacketsInCache >= 0);
	}

	/* process a new packet, then update finger index */	
	rbdrs = doRabin(data, skblen, &numBoundaries);
	int lastOffset = 0;
	char redundancyTest[MAX_PKT_SIZE];
	memset(redundancyTest, 0, sizeof(redundancyTest));
	for (i = 0; i < numBoundaries; i++) {
		int offset = rbdrs[i].offset;
		int rb_size = offset - lastOffset;
		if (rb_size < g_WindowSize) {
			#if 0
			fprintf(stderr, "skip too small chunk: %d bytes\n",
					rb_size);
			#endif
			continue;
		}

		u_int64 finger = rbdrs[i].finger;
		int finger_offset = offset - g_WindowSize;
		assert(finger_offset >= 0);
		lastOffset = offset;

		/* update finger index */
		FingerEntry* fe_val = hashtable_search(g_FingerTable, &finger);
		if (fe_val != NULL) {
			/* find redundancy */
			if (fe_val->pktId == nextId) {
				#if 0
				fprintf(stderr, "self-referencing\n");
				#endif
				continue;
			}

			PacketEntry* pe = &g_PacketCache[fe_val->pktId];
			int left = expand(TRUE, (u_char*)pe->data, pe->length,
					fe_val->offset, data, skblen,
					finger_offset);
			int right = expand(FALSE, (u_char*)pe->data, pe->length,
					fe_val->offset, data, skblen,
					finger_offset);
			int start = finger_offset - left;
			assert(start >= 0);
			memset(redundancyTest + start, 1,
					left + g_WindowSize + right);
		}
		else {
			/* make key */
			u_int64* fe_key = malloc(sizeof(u_int64));
			if (fe_key == NULL) {
				fprintf(stderr, "finger key malloc failed\n");
				exit(0);
			}
			memcpy(fe_key, &finger, sizeof(u_int64));

			/* make value */
			fe_val = malloc(sizeof(FingerEntry));
			if (fe_val == NULL) {
				fprintf(stderr, "FingerEntry malloc failed\n");
				exit(0);
			}
			fe_val->pktId = nextId;
			fe_val->offset = finger_offset;

			/* insert it */
			if (hashtable_insert(g_FingerTable, fe_key, fe_val)
					== 0) {
				fprintf(stderr, "finger insert failed\n");
				exit(0);
			}

			assert(hashtable_search(g_FingerTable, &finger)
				!= NULL);
		}
	}

	/* find non-overlapping region */
	char lastFlag = redundancyTest[0];
	int total_count = 0;
	int region_count = 0;
	int overhead_count = 0;
	for (i = 0; i < skblen; i++) {
		if (lastFlag == redundancyTest[i]) {
			/* keep counting */
			region_count++;
		}
		else {
			/* flush the result */
			if (lastFlag == 1) {
				/* redundant region */
				overhead_count++;
			}
			total_count += region_count;

			/* reset the counter */
			region_count = 1;
		}

		lastFlag = redundancyTest[i];

		if (redundancyTest[i] == 1) {
			g_BytesRedundant++;
		}
	}

	/* flush the last result */
	if (lastFlag == 1) {
		/* redundant region */
		overhead_count++;
	}
	total_count += region_count;
	assert(total_count == skblen);

	/* give the overhead */
	g_BytesOverhead += overhead_count * g_Overhead;
	assert(overhead_count >= 0);

	/* update finger index so that it points to the most recent packet */
	for (i = 0; i < numBoundaries; i++) {
		/* update finger index */
		FingerEntry* fe_val = hashtable_search(g_FingerTable,
				&rbdrs[i].finger);
		if (fe_val != NULL) {
			int offset = rbdrs[i].offset;
			int rb_size = offset - lastOffset;
			if (rb_size < g_WindowSize) {
				continue;
			}

			int finger_offset = offset - g_WindowSize;
			assert(finger_offset >= 0);
			lastOffset = offset;

			#if 0
			fprintf(stderr, "update: from %d to %d\n",
					fe_val->pktId, nextId);
			#endif
			fe_val->pktId = nextId;
			fe_val->offset = finger_offset;
		}
	}

	/* update packet cache: FIFO */
	#if 0
	fprintf(stderr, "PktID: %d, %d bytes packet, %d fingers found!\n",
			nextId, skblen, numFingers);
	#endif
	g_PacketCache[nextId].length = skblen;
	assert(skblen > 0);
	assert(skblen < MAX_PKT_SIZE);
	memcpy(g_PacketCache[nextId].data, data, skblen);
	g_CurPacketId = nextId;
	g_NumPacketsInCache++;
	assert(g_NumPacketsInCache <= g_MaxPackets);
	float saving = 100.0 * (g_BytesRedundant - g_BytesOverhead)
		/ g_BytesIP;
	if (g_Verbose) {
		fprintf(stderr, "%lld: %d pkts %d fingers,"
				" %lld total bytes, %lld bytes redundant,"
				" %lld bytes overhead, saving: %.2f%%\n",
				g_NumIPPackets, g_NumPacketsInCache,
				hashtable_count(g_FingerTable),
				g_BytesIP, g_BytesRedundant,
				g_BytesOverhead, saving);
	}
}

static void my_no_mem(char* message)
{
	fprintf(stderr, "%s: we're running out of memory!\n", message);
	exit(0);
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

void usage(char* prog)
{
	fprintf(stderr, "Usage: %s [options] packet_trace_files\n", prog);
	fprintf(stderr, "\t-i: interface\n");
	fprintf(stderr, "\t-e: filter expression\n");
	fprintf(stderr, "\t-m: use multi-process\n");
	fprintf(stderr, "\t-v: verbose output\n");
	fprintf(stderr, "\t-h: show this usage\n");
	fprintf(stderr, "\t-c: average chunk size in bytes, default=32\n");
	fprintf(stderr, "\t-w: Rabin window size in bytes, default=64\n");
	fprintf(stderr, "\t-p: cache size in # packets, default=65536\n");
	fprintf(stderr, "\t-o: overhead size in bytes, default=8\n");
}

void getOptions(int argc, char** argv)
{
	int c;

	opterr = 0;
	while ((c = getopt(argc, argv, "i:tmvhc:w:p:o:")) != -1) {
		switch (c)
		{
			case 'i': /* interface */
				nids_params.device = optarg;
				g_Offline = FALSE;
				break;
			case 'e': /* filter expression */
				nids_params.pcap_filter = optarg;
				break;
			case 'm': /* use multi-process */
				nids_params.multiproc = 1;
				break;
			case 'v': /* verbose output */
				g_Verbose = TRUE;
				break;
			case 'h': /* show usage */
				usage(argv[0]);
				exit(0);
			case 'c': /* chunksize (bytes) */
				g_ChunkSize = atoi(optarg);
				break;
			case 'w': /* Rabin window size (bytes) */
				g_WindowSize = atoi(optarg);
				break;
			case 'p': /* packet cache size (packets) */
				g_MaxPackets = atoi(optarg);
				break;
			case 'o': /* overhead (bytes) */
				g_Overhead = atoi(optarg);
				break;
			case 't': /* ignore TCP/IP header */
				g_IgnoreHeader = TRUE;
				break;
			case '?':
				if (optopt == 'i'
						|| optopt == 'c'
						|| optopt == 'o'
						|| optopt == 'w'
						|| optopt == 'p') {
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

	/* register pakcetloss function */
	nids_params.packetloss = my_nids_packetloss;

	g_NumFiles = argc - optind;
	g_filenames = &argv[optind];
	if (g_NumFiles > 0) {
		/* offline mode */
		g_Offline = TRUE;
	}
}

void CheckTimerEvents()
{
}

void loop()
{
	int fd = nids_getfd();
	fd_set rset;
	UpdateCurrentTime(&g_StartTime);
	UpdateCurrentTime(&g_LastTimer);
	g_InitTimer = TRUE;
	for (;;)
	{
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 50 * 1000;
		FD_ZERO(&rset);
		FD_SET(fd, &rset);
		UpdateCurrentTime(&g_CurrentTime);
		CheckTimerEvents();
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

	/* register ip frag call back */
	nids_register_ip_frag(my_ip_frag_func);

	/* register ip call back */
	nids_register_ip(my_ip_func);
}

int main(int argc, char** argv)
{
	/* set libnids parameters */
	getOptions(argc, argv);

	/* create finger table */
	g_FingerTable = create_hashtable(65536, finger_hash_from_key_fn,
			finger_keys_equal_fn);
	if (g_FingerTable == NULL) {
		fprintf(stderr, "finger table creation failed\n");
		exit(1);
	}

	/* create packet cache */
	g_PacketCache = malloc(sizeof(PacketEntry) * g_MaxPackets);
	if (g_PacketCache == NULL) {
		fprintf(stderr, "packet cache creation failed #1\n");
		exit(1);
	}
	int i;
	for (i = 0; i < g_MaxPackets; i++) {
		/* initialize packet cache */
		g_PacketCache[i].length = 0;
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
		}
	}

	fprintf(stderr, "[Final Result]\n");
	fprintf(stderr, "%llu pkts, %llu bytes\n", g_NumIPPackets, g_BytesIP);
	fprintf(stderr, "chunksize: %d windowsize: %d"
			" cachesize: %d overhead: %d, ignoreheader: %d\n",
			g_ChunkSize, g_WindowSize,
			g_MaxPackets, g_Overhead, g_IgnoreHeader);
	fprintf(stderr, "Bytes: %llu total %llu redundant %llu overhead\n",
			g_BytesIP, g_BytesRedundant, g_BytesOverhead);
	fprintf(stderr, "Cache: %d packets %d fingerprints\n",
			g_NumPacketsInCache, hashtable_count(g_FingerTable));
	float saving = 100.0 * (g_BytesRedundant - g_BytesOverhead) / g_BytesIP;
	fprintf(stderr, "saving: %.02f %%\n", saving);
	return 0;
}
