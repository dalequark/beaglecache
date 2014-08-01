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
#include <errno.h>
#include <fcntl.h>
#include "queue.h"
#include "czipdef.h"
#include "hashtable.h"
#include "hashtable_itr.h"
#include "gettimeofdayex.h"
#include "applib.h"
#include "util.h"

#define int_ntoa(x)	inet_ntoa(*((struct in_addr *)&x))

HANDLE hdebugLog = NULL;

/* parameters */
char g_CompleteSessionOnly = TRUE;
int g_TimerInterval = 5;
char g_Verbose = FALSE;
int g_ServerPort = 8864;
char g_ServerMode = FALSE;
char* g_ServerName = "localhost";
char g_OneConnection = FALSE;
char g_PacketSpacing = FALSE;

/* session map */
typedef struct MyHalfStream {
	int length;
	struct timeval lastPacketTime;
} MyHalfStream;

typedef struct SessionValue {
	struct tuple4 addr;
	int fd;
	MyHalfStream client;
	MyHalfStream server;
	struct timeval startTime;
	struct timeval endTime;
	LIST_ENTRY(SessionValue) list;
} SessionValue;

typedef struct LossSessionValue {
	struct tuple4 addr;
	u_int lastSeq;
	int lastLen;
	MyHalfStream stream;
	LIST_ENTRY(LossSessionValue) list;
} LossSessionValue;

typedef LIST_HEAD(SessionList, SessionValue) SessionList;
SessionList g_SessionList = LIST_HEAD_INITIALIZER(g_SessionList);

typedef LIST_HEAD(LossSessionList, LossSessionValue) LossSessionList;
LossSessionList g_LossSessionList = LIST_HEAD_INITIALIZER(g_LossSessionList);

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

int g_InitTimer = FALSE;
struct timeval g_LastTimer;
struct timeval g_LastPacketTime;

/* listen socket */
static int g_ServerFD = -1;
static int g_OneFD = -1;

/* misc. */
struct timeval g_StartTime;
struct timeval g_StartTimeWall;
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
void PrintStats(FILE* out);
char* adres(struct tuple4 addr);

static void my_nids_packetloss(struct ip* iph, struct tcphdr* tcph,
		struct skbuff* packet)
{
	char saddr[20], daddr[20];
	strcpy(saddr, int_ntoa(iph->ip_src.s_addr));
	strcpy(daddr, int_ntoa(iph->ip_dst.s_addr));
	/*
	fprintf(stderr, "packet loss! from %s:%hu to  %s:%hu, "
			"len: %d, seq: %u\n",
			saddr, ntohs(tcph->th_sport),
			daddr, ntohs(tcph->th_dport), 
			packet->len, packet->seq);
	*/

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

	LossSessionValue* value = hashtable_search(g_LossSessionMap, &addr);
	if (value == NULL) {
		/* create new session */
		struct tuple4* key = malloc(sizeof(struct tuple4));
		if (key == NULL) {
			NiceExit(-1, "tuple4 key allocation failed\n");
		}
		memcpy(key, &addr, sizeof(struct tuple4));

		value = malloc(sizeof(LossSessionValue));
		if (value == NULL) {
			NiceExit(-1, "loss session value allocation failed\n");
		}
		memcpy(&value->addr, key, sizeof(struct tuple4));
		value->lastSeq = packet->seq;
		value->lastLen = packet->len;
		value->stream.length = 0;
		assert(value->stream.length == packet->len);

		if (!hashtable_insert(g_LossSessionMap, key, value)) {
			NiceExit(-1, "new loss session insertion failed\n");
		}

		LIST_INSERT_HEAD(&g_LossSessionList, value, list);
		g_NumLossSessions++;
	}
	else {
		/* TODO: check if packets are delivered in order or not */
		if (value->lastSeq + value->lastLen == packet->seq) {
			/* continous packets */
			assert(value->stream.length > 0);
		}
		else {
			/* there's a hole, force chunk processing */
			assert(value->stream.length == 0);
			assert(value->stream.length == packet->len);
		}

		/* update last seq */
		value->lastSeq = packet->seq;
		value->lastLen = packet->len;
	}
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
			NiceExit(-1, "NIDS_WARN_SCAN?\n");
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
		if (g_InitTimer == FALSE) {
			g_LastTimer = nids_last_pcap_header->ts;
			g_StartTime = nids_last_pcap_header->ts;
			g_CurrentTime = nids_last_pcap_header->ts;
			g_InitTimer = TRUE;
		}

		timersub(&g_CurrentTime, &g_LastTimer, &result);
		if (result.tv_sec >= g_TimerInterval) {
			/* timer */
			PrintStats(stderr);
			g_LastTimer = nids_last_pcap_header->ts;
		}

		g_LastPacketTime = g_CurrentTime;
		g_CurrentTime = nids_last_pcap_header->ts;
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

void PrintStats(FILE* out)
{
	struct timeval result1, result2, curTime;
	timersub(&g_CurrentTime, &g_StartTime, &result1);
	UpdateCurrentTime(&curTime);
	timersub(&curTime, &g_StartTimeWall, &result2);
	fprintf(out, "----------------------------------------------\n");
	fprintf(out, "[%d secs, %d wall secs]\n",
			(int)result1.tv_sec, (int)result2.tv_sec);
	fprintf(out, "IP bytes: %lld / %lld pkts: %lld / %lld\n",
			g_BytesIP, g_BytesIPFrag,
			g_NumIPPackets, g_NumIPFragPackets);
	fprintf(out, "UDP bytes: %lld pkts : %lld\n", 
			g_BytesUDP, g_NumUDPPackets);
	fprintf(out, "TCP bytes: %lld conns: %d / %d\n", g_BytesTCP, 
			hashtable_count(g_SessionMap), g_NumSessions);
	fprintf(out, "TCP Loss bytes: %lld conns: %lld pkts: %lld\n",
			g_BytesLoss, g_NumLossSessions, g_NumPacketLoss);
	fprintf(out, "----------------------------------------------\n");
}

void CheckTimerEvents()
{
	/* this is only valid for online mode */
	assert(nids_params.filename == NULL);
	struct timeval cur_time, result;
	UpdateCurrentTime(&cur_time);
	timersub(&cur_time, &g_LastTimer, &result);
	if (result.tv_sec >= g_TimerInterval) {
		/* timer */
		PrintStats(stderr);
		UpdateCurrentTime(&g_LastTimer);
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

int MakeDataConnection()
{
	/* make a blocking connection */
	in_addr_t netAddr;
	bzero(&netAddr, sizeof(in_addr_t));
	if (g_Verbose) {
		fprintf(stderr, "connecting to %s:%d\n",
				g_ServerName, g_ServerPort);
	}
	int fd = MakeConnection(g_ServerName, netAddr, g_ServerPort, FALSE);
	int numRetries = 0;
	while (fd == -1) {
		numRetries++;
		if (numRetries > 10) {
			fprintf(stderr, "connection failed\n");
			return -1;
		}
		if (g_Verbose) {
			fprintf(stderr, "retrying...\n");
		}
		fd = MakeConnection(g_ServerName, netAddr, g_ServerPort, FALSE);
	}
	if (g_Verbose) {
		fprintf(stderr, "connected %d\n", fd);
	}
	return fd;
}

int AcceptDataConnection()
{
	assert(g_ServerFD != -1);
	struct sockaddr sa;
	socklen_t len = sizeof(struct sockaddr);
	int fd = accept(g_ServerFD, &sa, &len);
	if (g_Verbose) {
		fprintf(stderr, "accepted %d\n", fd);
	}
	return fd;
}

void CloseDataConnection(SessionValue* value)
{
	if (value->fd != -1 && g_OneConnection == FALSE) {
		if (g_Verbose) {
			fprintf(stderr, "close %d\n", value->fd);
		}
		UpdateCurrentTime(&value->endTime);
		close(value->fd);
		int data = value->server.length + value->client.length;
		int gap = GetTimeGapMS(&value->startTime, &value->endTime);
		float BW = 0;
		if (data != 0 && gap != 0) {
			BW = (data * 8) / (gap / 1000.0) / 1024;
		}
		fprintf(stderr, "FD: %02d TX: %d RX: %d "
				"Time: %d Kbps: %.2f Total: %lld\n",
				value->fd, value->server.length,
				value->client.length, gap, BW, g_BytesTCP);
		value->fd = -1;
	}
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
		struct tuple4* key = malloc(sizeof(struct tuple4));
		if (key == NULL) {
			NiceExit(-1, "tuple4 key allocation failed\n");
		}
		memcpy(key, &a_tcp->addr, sizeof(struct tuple4));
		SessionValue* value = malloc(sizeof(SessionValue));
		if (value == NULL) {
			NiceExit(-1, "session value allocation failed\n");
		}
		memcpy(&value->addr, &a_tcp->addr, sizeof(struct tuple4));
		value->client.lastPacketTime = nids_last_pcap_header->ts;
		value->server.lastPacketTime = nids_last_pcap_header->ts;
		value->client.length = 0;
		value->server.length = 0;
		value->fd = -1;

		/* TODO: consider session direction when create */
		if (g_ServerMode == FALSE) {
			/* client side */
			if (g_OneConnection == TRUE) {
				if (g_OneFD == -1) {
					g_OneFD = MakeDataConnection();
				}
				value->fd = g_OneFD;
			}
			else {
				value->fd = MakeDataConnection();
			}
		}
		else {
			/* server accepts a new connection */
			if (g_OneConnection == TRUE) {
				if (g_OneFD == -1) {
					g_OneFD = AcceptDataConnection();
				}
				value->fd = g_OneFD;
			}
			else {
				value->fd = AcceptDataConnection();
			}
		}

		assert(value->fd != -1);
		if (!hashtable_insert(g_SessionMap, key, value)) {
			NiceExit(-1, "new session insertion failed\n");
		}
		LIST_INSERT_HEAD(&g_SessionList, value, list);
		g_NumSessions++;
		UpdateCurrentTime(&value->startTime);
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

		/* delete the session */
		SessionValue* value = hashtable_remove(g_SessionMap,
				&a_tcp->addr);
		if (value == NULL) {
			NiceExit(-1, "session remove failed\n");
		}

		CloseDataConnection(value);

		/* clean up */
		LIST_REMOVE(value, list);
		free(value);
		return;
	}
	else if (a_tcp->nids_state == NIDS_DATA)
	{
		// new data has arrived; gotta determine in what direction
		// and if it's urgent or not

		struct half_stream *hlf;
		MyHalfStream *myhlf;
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
			NiceExit(-1, "session value search failed\n");
		}

		int curDir = -1;
		if (a_tcp->client.count_new) {
			/* new data for client */
			hlf = &a_tcp->client; 
			myhlf = &value->client;
			strcat(buf, "(<-)"); 
			curDir = 0;
		}
		else {
			/* new data for server */
			hlf = &a_tcp->server;
			myhlf = &value->server;
			strcat(buf, "(->)");
			curDir = 1;
		}
		myhlf->length += hlf->count_new;

		/* check the time gap */
		msec = GetTimeGapMS(&myhlf->lastPacketTime,
				&(nids_last_pcap_header->ts));
		if (g_Verbose) {
			fprintf(stderr,"%s, gap: %d ms, %d bytes\n",
					buf, msec, hlf->count_new);
		}

		/* update last packet arrival time */
		myhlf->lastPacketTime = nids_last_pcap_header->ts;

		/* mimic packet spacing */
		if (g_PacketSpacing) {
			msec = GetTimeGapMS(&g_LastPacketTime,
					&(nids_last_pcap_header->ts));
			usleep(msec);
			if (g_Verbose) {
				fprintf(stderr, "packet spacing %d ms\n", msec);
			}
		}

		int offset = 0;
		int ret = 0;
		if (value->fd == -1) {
			return;
		}

		assert(curDir != -1);
		while (offset != hlf->count_new) {
			assert(offset >= 0);
			assert(hlf->count_new - offset > 0);
			if ((g_ServerMode == FALSE && curDir == 1) ||
					(g_ServerMode == TRUE && curDir ==0)) {
				/* send */
				ret = send(value->fd, hlf->data + offset,
						hlf->count_new - offset, 0);
			}
			else {
				/* recv */
				char rcvBuf[2048];
				assert(hlf->count_new < sizeof(rcvBuf));
				ret = recv(value->fd, rcvBuf,
						hlf->count_new - offset, 0);
			}

			if (ret == 0) {
				fprintf(stderr, "peer closed\n");
				CloseDataConnection(value);
				return;
			}
			else if (ret == -1) {
				perror("send/recv");
				CloseDataConnection(value);
				if (g_OneConnection) {
					NiceExit(-1, "terminate\n");
				}
				return;
			}
			else {
				offset += ret;
			}
		}
		/*
		fprintf(stderr, "%d bytes processed (%lld total)\n",
				offset, g_BytesTCP);
		*/
	}
	else {
		NiceExit(-1, "should not reach here\n");
	}
}

void printUsage(char* program)
{
	fprintf(stderr, "%s [-options] [tcpdump trace file...]\n", program);
	fprintf(stderr, "\t-e: pcap filter expression, default=NULL\n");
	fprintf(stderr, "\t-n: # maximum hosts, default=65536\n");
	fprintf(stderr, "\t-s: # maximum tcp stream, default=65536\n");
	fprintf(stderr, "\t-p: server port, default=8864\n");
	fprintf(stderr, "\t-a: target server name, default=localhost\n");
	fprintf(stderr, "\t-t: print stats interval in sec, default=1\n");
	fprintf(stderr, "\t-d: use server mode, default=no\n");
	fprintf(stderr, "\t-v: verbose output, default=no\n");
	fprintf(stderr, "\t-1: use one connection, default=no\n");
	fprintf(stderr, "\t-m: use multiprocess, default=no\n");
	fprintf(stderr, "\t-g: analyze complete sessions only, default=yes\n");
	fprintf(stderr, "\t-h: show all the options\n");
}

void getOptions(int argc, char** argv)
{
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

	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "e:n:s:p:a:t:1umdhvg")) != -1) {
		switch (c)
		{
			case 'e': /* filter expression */
				nids_params.pcap_filter = optarg;
				break;
			case 'n': /* num maximum hosts */
				nids_params.n_hosts = atoi(optarg);
				break;
			case 's': /* num maximum tcp stream */
				nids_params.n_tcp_streams = atoi(optarg);
				break;
			case 'p': /* server port */
				g_ServerPort = atoi(optarg);
				break;
			case 'a': /* server port */
				g_ServerName = optarg;
				break;
			case 't': /* print stats timer interval */
				g_TimerInterval = atoi(optarg);
				break;
			case '1': /* use one data connection */
				g_OneConnection = TRUE;
				break;
			case 'u': /* use packet spacing */
				g_PacketSpacing = TRUE;
				break;
			case 'm': /* use multi-process */
				nids_params.multiproc = 1;
				break;
			case 'd': /* use server mode */
				g_ServerMode = TRUE;
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
			case '?':
				if (optopt == 'e' || optopt == 'n'
						|| optopt == 's'
						|| optopt == 'p'
						|| optopt == 'a'
						|| optopt == 't') {
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
	
	g_NumFiles = argc - optind;
	g_filenames = &argv[optind];
}

void initLibnids()
{
	/* init libnids */
	if (!nids_init()) {
		fprintf(stderr, "%s\n", nids_errbuf);
		exit(1);
	}

	/* disable checksum for all packets */
	struct nids_chksum_ctl ctl;
	ctl.netaddr = inet_addr("0.0.0.0");
	ctl.mask = inet_addr("0.0.0.0");
	ctl.action = NIDS_DONT_CHKSUM;
	nids_register_chksum_ctl(&ctl, 1);

	/* register tcp call back */
	nids_register_tcp(tcp_callback);

	/* register udp call back */
	nids_register_udp(my_udp_func);

	/* register ip frag call back */
	nids_register_ip_frag(my_ip_frag_func);

	/* register ip call back */
	//nids_register_ip(my_ip_func);

	UpdateCurrentTime(&g_StartTimeWall);
}

int main(int argc, char** argv)
{
	/* set libnids parameters */
	getOptions(argc, argv);

	/* create session map */
	g_SessionMap = create_hashtable(65536,
			session_hash_from_key_fn,
			session_keys_equal_fn);
	if (g_SessionMap == NULL) {
		NiceExit(-1, "session map creation failed\n");
	}

	g_LossSessionMap = create_hashtable(65536,
			session_hash_from_key_fn,
			session_keys_equal_fn);
	if (g_LossSessionMap == NULL) {
		NiceExit(-1, "loss session map creation failed\n");
	}

	/* create a server socket */
	if (g_ServerMode == TRUE) {
		g_ServerFD = CreatePrivateAcceptSocket(g_ServerPort, FALSE);
		if (g_ServerFD == -1) {
			NiceExit(-1, "Server socket creation failed\n");
		}
	}

	/* start running */
	int i;
	for (i = 0; i < g_NumFiles; i++) {
		/* set input trace file */
		nids_params.filename = g_filenames[i];

		/* offline: read from captured files */
		initLibnids();
		nids_run();
		PrintStats(stderr);
	}

	return 0;
}
