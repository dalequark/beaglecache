#include <features.h>
#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2)
# include <bits/types.h>
# undef __FD_SETSIZE
# define __FD_SETSIZE 8192
#endif 

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
char g_Verbose = FALSE;
int g_ServerPort = 8864;
char g_ServerMode = FALSE;
char* g_ServerName = "localhost";
int g_SessionSoFar = 0;
int g_SessionComplete = 0;
int g_SessionSkipped = 0;
int g_SessionTimedOut = 0;

/* session map */
typedef struct MyHalfStream {
	int length;
	char* buffer;
	struct timeval lastPacketTime;
} MyHalfStream;

typedef struct SessionValue {
	int ID;
	struct tuple4 addr;
	int fd;
	MyHalfStream client;
	MyHalfStream server;
	struct timeval startTime;
	struct timeval endTime;
	char isLoss;
	TAILQ_ENTRY(SessionValue) queue;
} SessionValue;

typedef TAILQ_HEAD(SessionQueue, SessionValue) SessionQueue;
SessionQueue g_SessionQueue;
static SessionValue* g_SessionArray[65536];

struct hashtable* g_SessionMap = NULL;

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
struct timeval g_LastPacketTime;

/* listen socket */
static int g_ServerFD = -1;

/* client state */
typedef enum ClientState {
	cs_init,
	cs_request_done
} ClientState;

/* client info */
typedef struct ClientInfo {
	int fd;
	ClientState state;
	SessionValue* sv;
	int objId;
	int numRead;
	int numWrite;
	struct timeval startTime;
} ClientInfo;

#define MAX_CLIENTS_FD		(8192)
static ClientInfo g_clientInfo[MAX_CLIENTS_FD];
static fd_set g_ReadFDSET, g_WriteFDSET, g_ExceptFDSET;
static int g_HighFD;
static int g_NumClients = 0;
static int g_MaxNumClients = MAX_CLIENTS_FD / 2;

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
unsigned long long g_NumSessions = 0;
unsigned long long g_NumLossSessions = 0;
char** g_filenames = NULL;
int g_NumFiles = 0;
unsigned long long g_NumPacketLoss = 0;
unsigned long long g_BytesTCP = 0;
unsigned long long g_BytesLoss = 0;
unsigned long long g_NumSessionDropped = 0;
unsigned int g_TotalRX;
unsigned int g_TotalTX;

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
SessionValue* MakeNewSessionValue(struct tuple4* addr, char isLoss);
void RunMain();
void InitEvents();
void HandleEvents();
void InitClientInfo(int index);
SessionValue* GetNextSessionValue();
void HandleRead(int index);
void HandleWrite(int index);
void CloseClientInfo(int index);
void FreeSessionValue(SessionValue* sv);
void PrepareClientInfo(int newfd);
void AttachClientInfo(int newfd);
#define MY_BUF_SIZE (512 * 1024)

void AppendBuffer(MyHalfStream* stream, char* buffer, int length)
{
	stream->buffer = realloc(stream->buffer, stream->length + length);
	memcpy(stream->buffer + stream->length, buffer, length);
	stream->length += length;
}

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

	SessionValue* value = hashtable_search(g_SessionMap, &addr);
	if (value == NULL) {
		/* create new session */
		value = MakeNewSessionValue(&addr, TRUE);
		g_NumLossSessions++;
	}
	else {
		/* store the data */
		if (addr.source > addr.dest) {
			/* client */
			AppendBuffer(&value->server, packet->data, packet->len);
		}
		else {
			/* server */
			AppendBuffer(&value->client, packet->data, packet->len);
		}
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
	if (nids_params.filename != NULL) {
		if (g_InitTimer == FALSE) {
			g_StartTime = nids_last_pcap_header->ts;
			g_CurrentTime = nids_last_pcap_header->ts;
			g_InitTimer = TRUE;
		}

		g_LastPacketTime = g_CurrentTime;
		g_CurrentTime = nids_last_pcap_header->ts;
	}
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
	fprintf(out, "TCP bytes: %lld conns: %d / %lld\n", g_BytesTCP, 
			hashtable_count(g_SessionMap), g_NumSessions);
	fprintf(out, "TCP Loss bytes: %lld conns: %lld pkts: %lld\n",
			g_BytesLoss, g_NumLossSessions, g_NumPacketLoss);
	fprintf(out, "----------------------------------------------\n");
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
	/* make a non-blocking connection */
	in_addr_t netAddr;
	bzero(&netAddr, sizeof(in_addr_t));
	if (g_Verbose) {
		fprintf(stderr, "connecting to %s:%d\n",
				g_ServerName, g_ServerPort);
	}
	int fd = MakeConnection(g_ServerName, netAddr, g_ServerPort, TRUE);
	if (fd == -1) {
		NiceExit(-1, "connection failed\n");
	}

	/* reuse addresses */
	int doReuse = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 
				&doReuse, sizeof(doReuse)) == -1) {
		NiceExit(-1, "setsockopt failed\n");
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

	if (fd != -1) {
		/* non-blocking */
		if (fcntl(fd, F_SETFL, O_NDELAY) < 0) {
			NiceExit(-1, "fcntl failed\n");
		}

		/* reuse addresses */
  		int doReuse = 1;
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 
					&doReuse, sizeof(doReuse)) == -1) {
			NiceExit(-1, "setsockopt failed\n");
		}
	}

	return fd;
}

SessionValue* MakeNewSessionValue(struct tuple4* addr, char isLoss)
{
	assert(hashtable_search(g_SessionMap, addr) == NULL);
	static int ID = 0;
	struct tuple4* key = malloc(sizeof(struct tuple4));
	if (key == NULL) {
		NiceExit(-1, "tuple4 key allocation failed\n");
	}
	memcpy(key, addr, sizeof(struct tuple4));

	SessionValue* value = malloc(sizeof(SessionValue));
	if (value == NULL) {
		NiceExit(-1, "session value allocation failed\n");
	}
	memcpy(&value->addr, addr, sizeof(struct tuple4));
	value->client.lastPacketTime = nids_last_pcap_header->ts;
	value->server.lastPacketTime = nids_last_pcap_header->ts;
	value->client.buffer = NULL;
	value->server.buffer = NULL;
	value->client.length = 0;
	value->server.length = 0;
	value->fd = -1;
	value->ID = ID++;
	value->isLoss = isLoss;
	UpdateCurrentTime(&value->startTime);
	if (!hashtable_insert(g_SessionMap, key, value)) {
		NiceExit(-1, "new loss session insertion failed\n");
	}
	TAILQ_INSERT_TAIL(&g_SessionQueue, value, queue);
	g_SessionArray[g_NumSessions++] = value;
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
		MakeNewSessionValue(&a_tcp->addr, FALSE);
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

		fprintf(stderr, "session %d load done\n", value->ID);

		return;
	}
	else if (a_tcp->nids_state == NIDS_DATA)
	{
		// new data has arrived; gotta determine in what direction
		// and if it's urgent or not

		struct half_stream *hlf;
		MyHalfStream *myhlf;
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

		/* store the data */
		AppendBuffer(myhlf, hlf->data, hlf->count_new);

		/* update last packet arrival time */
		myhlf->lastPacketTime = nids_last_pcap_header->ts;
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
	fprintf(stderr, "\t-d: use server mode, default=no\n");
	fprintf(stderr, "\t-v: verbose output, default=no\n");
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
	while ((c = getopt(argc, argv, "e:n:s:p:a:c:f:t:dhvg")) != -1) {
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
						|| optopt == 'c'
						|| optopt == 'f'
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

	/* register ip frag call back */
	nids_register_ip_frag(my_ip_frag_func);

	/* register ip call back */
	//nids_register_ip(my_ip_func);

	UpdateCurrentTime(&g_StartTimeWall);
}

void InitClientInfo(int index)
{
	assert(index >= 0 && index < MAX_CLIENTS_FD);
	g_clientInfo[index].fd = 0;
	g_clientInfo[index].state = cs_init;
	g_clientInfo[index].numRead = 0;
	g_clientInfo[index].numWrite = 0;
	g_clientInfo[index].sv = NULL;
	g_clientInfo[index].objId = 0;
}

SessionValue* GetNextSessionValue()
{
	SessionValue* value = g_SessionArray[g_SessionSoFar++];
	assert(value != NULL);
	TRACE("# current sessions: %d\n", g_SessionSoFar);
	return value;
}

void FreeSessionValue(SessionValue* sv)
{
	if (sv->client.buffer != NULL) {
		free(sv->client.buffer);
	}
	if (sv->server.buffer != NULL) {
		free(sv->server.buffer);
	}
	free(sv);
}

void CloseClientInfo(int index)
{
	/* we're done */
	struct timeval curtime, result;
	UpdateCurrentTime(&curtime);
	timersub(&curtime, &g_clientInfo[index].startTime, &result);
	int us = result.tv_sec * 1000000 + result.tv_usec;
	TRACE("STAT ID: %d %d bytes read, %d bytes write, %d us\n",
			g_clientInfo[index].sv->ID, g_clientInfo[index].numRead,
			g_clientInfo[index].numWrite, us);

	close(g_clientInfo[index].fd);
	InitClientInfo(index);
	g_NumClients--;
	assert(g_NumClients >= 0 && g_NumClients <= g_MaxNumClients);
}

void HandleRead(int index)
{
	assert(index > 0);
	assert(g_clientInfo[index].fd == index);

	/* read from the fd */
	char buffer[MY_BUF_SIZE];
	memset(buffer, 0, sizeof(buffer));
	while (TRUE) {
		int numRead = read(g_clientInfo[index].fd,
				buffer, sizeof(buffer));
		if (numRead > 0) {
			/* read success */
			g_clientInfo[index].numRead += numRead;
			if (g_Verbose) {
				fprintf(stderr, "fd=%d, %d bytes rcvd\n",
						index, numRead);
			}
			g_TotalRX += numRead;
		}
		else if (numRead == 0) {
			/* EOF - close */
			if (g_Verbose) {
				fprintf(stderr, "fd: %d, EOF\n",
						g_clientInfo[index].fd);
			}
			CloseClientInfo(index);
			g_SessionComplete++;
			return;
		}
		else {
			if (errno == EAGAIN || errno == EINTR) {
				/* try next time */
				break;
			}
			else {
				TRACE("%s, skip this\n", strerror(errno));
				g_SessionSkipped++;
				CloseClientInfo(index);
				return;
				//NiceExit(-1, "read error\n");
			}
		}
	}

	/* write response right away */
	if (strchr(buffer, '\r') != NULL) {
		g_clientInfo[index].state = cs_request_done;
		g_clientInfo[index].objId = atoi(buffer);
		g_clientInfo[index].sv = g_SessionArray[g_clientInfo[index].objId];
		TRACE("fd: %d, request: %s", index, buffer);
		HandleWrite(index);
	}
}

void HandleWrite(int index)
{
	assert(index > 0);
	assert(g_clientInfo[index].fd == index);

	/* write to the fd */
	SessionValue* sv = g_clientInfo[index].sv;
	assert(sv != NULL);

	char* buffer = NULL;
	int buflen = 0;
	buffer = sv->client.buffer;
	buflen = sv->client.length;

	int offset = g_clientInfo[index].numWrite;
	while (TRUE) {
		assert((buflen - offset) >= 0);
		int numWrite = write(g_clientInfo[index].fd,
				buffer + offset, buflen - offset);
		if (numWrite > 0) {
			/* write success */
			TRACE("fd=%d, %d bytes sent\n", index, numWrite);
			g_clientInfo[index].numWrite += numWrite;
			offset += numWrite;
			g_TotalTX += numWrite;
		}
		else if (numWrite == 0) {
			break;
		}
		else {
			if (errno == EAGAIN) {
				/* try next time */
				break;
			}
			else {
				fprintf(stderr, "fd=%d, %s\n",
						g_clientInfo[index].fd,
						strerror(errno));
				NiceExit(-1, "read error\n");
			}
		}
	}

	if (g_clientInfo[index].numWrite == sv->client.length) {
		/* we're done */
		TRACE("fd=%d, response sent\n", index);
		g_SessionComplete++;
		CloseClientInfo(index);
	}
}

void PrepareClientInfo(int newfd)
{
	assert(newfd > 0);
	InitClientInfo(newfd);
	g_clientInfo[newfd].fd = newfd;
	g_clientInfo[newfd].sv = NULL;
	UpdateCurrentTime(&g_clientInfo[newfd].startTime);
	g_NumClients++;
	TRACE("# clients: %d\n", g_NumClients);
	assert(g_NumClients >= 0 && g_NumClients <= g_MaxNumClients);
}

void HandleEvents()
{
	/* accept incoming connection */
	if (FD_ISSET(g_ServerFD, &g_ReadFDSET)) {
		int newfd = AcceptDataConnection();
		PrepareClientInfo(newfd);
	}

	/* check error for the server socket */
	if (FD_ISSET(g_ServerFD, &g_ExceptFDSET)) {
		NiceExit(-1, "server fd exception\n");
	}

	int i = 0;
	for (i = 0; i < MAX_CLIENTS_FD; i++) {
		if (FD_ISSET(g_clientInfo[i].fd, &g_ReadFDSET)) {
			HandleRead(i);
		}

		if (FD_ISSET(g_clientInfo[i].fd, &g_WriteFDSET)) {
			HandleWrite(i);
		}

		if (FD_ISSET(g_clientInfo[i].fd, &g_ExceptFDSET)) {
			/* except */
			NiceExit(-1, "client fd exception\n");
		}
	}
}

void InitEvents()
{
	/* init fd set */
	FD_ZERO(&g_ReadFDSET);
	FD_ZERO(&g_WriteFDSET);
	FD_ZERO(&g_ExceptFDSET);
	g_HighFD = 0;

	/* add server */
	assert(g_ServerFD != -1);
	FD_SET(g_ServerFD, &g_ReadFDSET);
	FD_SET(g_ServerFD, &g_ExceptFDSET);
	g_HighFD = g_ServerFD;

	/* add clients */
	int i;
	for (i = 0; i < MAX_CLIENTS_FD; i++) {
		if (g_clientInfo[i].fd != 0) {
			/* set the highest fd */
			if (g_clientInfo[i].fd > g_HighFD) {
				g_HighFD = g_clientInfo[i].fd;
			}

			/* set read and except event always */
			FD_SET(g_clientInfo[i].fd, &g_ReadFDSET);
			FD_SET(g_clientInfo[i].fd, &g_ExceptFDSET);

			/* set write event for sending response */
			if (g_clientInfo[i].state == cs_request_done) {
				FD_SET(g_clientInfo[i].fd, &g_WriteFDSET);
			}
		}
	}
}

void RunMain()
{
	/* init some variables */
	int numReady = 0;
	int i = 0;
	for (i = 0; i < MAX_CLIENTS_FD; i++) {
		InitClientInfo(i);
	}

	/* create server socket */
	g_ServerFD = CreatePrivateAcceptSocket(g_ServerPort, TRUE);
	if (g_ServerFD == -1) {
		NiceExit(-1, "Server socket creation failed\n");
	}

	/* main event loop */
	fprintf(stderr, "### main loop start!\n");
	struct timeval lastActivity;
	UpdateCurrentTime(&lastActivity);
	while (TRUE) {
		/* prepare read/write/except fdsets */
		InitEvents();
		
		/* block on select() */
		struct timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		numReady = select(g_HighFD + 1, &g_ReadFDSET, &g_WriteFDSET,
				&g_ExceptFDSET, &timeout);
		TRACE("select() returns: %d\n", numReady);
		if (numReady < 0) {
			NiceExit(-1, "select() returns negative\n");
		}
		else if (numReady == 0) {
			fprintf(stderr, "timeout now %d clients, %d processed, %d skipped, %d timedout\n", g_NumClients, g_SessionComplete, g_SessionSkipped, g_SessionTimedOut);
			struct timeval curTime;
			UpdateCurrentTime(&curTime);
			if (curTime.tv_sec - lastActivity.tv_sec > 300) {
				/* no activity for 5 min? */
				fprintf(stderr, "too much idle time (300 sec)\n");
				goto exit;
			}
			
			#if 0
			for (i = 0; i < g_MaxNumClients; i++) {
				if (g_clientInfo[i].fd != 0) {
					TRACE("ID %d, %d read, %d write\n", g_clientInfo[i].sv->ID, g_clientInfo[i].numRead, g_clientInfo[i].numWrite);
				}
			}
			#endif
		}
		else {
			/* handle ready events */	
			UpdateCurrentTime(&lastActivity);
			HandleEvents();
		}
	}

exit:
	fprintf(stderr, "timeout now %d clients, %d processed, %d skipped, %d timedout\n", g_NumClients, g_SessionComplete, g_SessionSkipped, g_SessionTimedOut);
}

int main(int argc, char** argv)
{
	TAILQ_INIT(&g_SessionQueue);

	/* set libnids parameters */
	getOptions(argc, argv);

	/* create session map */
	g_SessionMap = create_hashtable(65536,
			session_hash_from_key_fn,
			session_keys_equal_fn);
	if (g_SessionMap == NULL) {
		NiceExit(-1, "session map creation failed\n");
	}

	/* load session info into memory */
	int i;
	for (i = 0; i < g_NumFiles; i++) {
		/* set input trace file */
		nids_params.filename = g_filenames[i];

		/* offline: read from captured files */
		initLibnids();
		nids_run();
		PrintStats(stderr);
	}
	fprintf(stderr, "### session load complete\n");

	/* run main event-based client/server */
	RunMain();

	return 0;
}
