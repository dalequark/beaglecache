#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#define __FAVOR_BSD
#include <netinet/tcp.h>
#include "hashtable.h"
#include "our_syslog.h"
#include "pptpwaprox.h"

/* 	TODO: 
	- dynamically load IP/Port value: where to put conf file?
	- remove client connection entry when connection closes 
	(should check RST/FIN packet)
	- postpone making connection until waprox finishes making connection
	(should check SYN packet)
	- optimize checksum calculation (remove unnecessary copy)
*/

/* Note: all key/value are network byte order */
typedef struct ClientKey {
	unsigned long ip;
	unsigned short port;
} ClientKey;

typedef struct ClientValue {
	unsigned long org_ip;
	unsigned short org_port;
	struct timeval lastAccessTime;
	int sawFIN;
} ClientValue;

static unsigned int client_hash(void* key)
{
	ClientKey* k = (ClientKey*)key;
	return k->ip + k->port;
}

static int client_comp(void* key1, void* key2)
{
	ClientKey* k1 = (ClientKey*)key1;
	ClientKey* k2 = (ClientKey*)key2;

	if (k1->ip == k2->ip && k1->port == k2->port)
		return 1;
	else
		return 0;
}

static struct hashtable* client_table;
static unsigned long localIP;
static unsigned short waproxPort;
static int init;

static char nat_buffer[1024 * 8];
static int nat_buflen;

/*-----------------------------------------------------------------*/
void Initialize()
{
	if (client_table == NULL) {
		/* create client hash table first */
		client_table = create_hashtable(65536, client_hash, client_comp);
		if (client_table == NULL) {
			syslog(LOG_ERR, "WAPROX: create_hashtable() failed");
			return;
		}
	}

	if (localIP == 0) {
		localIP = inet_addr(PPTP_LOCAL_IP);
	}

	if (waproxPort == 0) {
		waproxPort = htons(WAPROX_USER_PORT);
	}

	init = 1;
}
/*-----------------------------------------------------------------*/
/* IP Family checksum routine (from UNP) */ 
unsigned short in_cksum(unsigned short *ptr,int nbytes) 
{
	register long sum; /* assumes long == 32 bits */ 
	u_short oddbyte; 
	register u_short answer; /* assumes u_short == 16 bits */ 

	/* Our algorithm is simple, using a 32-bit accumulator (sum), 
	 * we add sequential 16-bit words to it, and at the end, fold back 
	 * all the carry bits from the top 16 bits into the lower 16 bits. */
	sum = 0; 

	while (nbytes > 1) { 
		sum += *ptr++; 
		nbytes -= 2;
	} /* mop up an odd byte, if necessary */ 

	if (nbytes == 1) { 
		oddbyte = 0; /* make sure top half is zero */ 
		*((u_char *) &oddbyte) = *(u_char *)ptr; /* one byte only */ 
		sum += oddbyte;
	} /* Add back carry outs from top 16 bits to low 16 bits. */ 
	sum = (sum >> 16) + (sum & 0xffff); /* add high-16 to low-16 */ 
	sum += (sum >> 16); /* add carry */ 
	answer = ~sum; /* ones-complement, then truncate to 16 bits */ 
	return(answer); 
}
/*-----------------------------------------------------------------*/
void DumpTcpIpHeader(char* buffer, int reverseDirection)
{
	struct ip* iph = (struct ip*)(buffer + 1);
	struct tcphdr* tcph = (struct tcphdr*)(buffer + 1 + sizeof(struct ip));
	char* src_ip = strdup(inet_ntoa(iph->ip_src));
	char* dst_ip = strdup(inet_ntoa(iph->ip_dst));
	/*
	syslog(LOG_INFO, "WAPROX: (%c) S[%s:%d], D[%s:%d]",
			reverseDirection ? 'R' : 'F',
			src_ip, ntohs(tcph->th_sport),
			dst_ip, ntohs(tcph->th_dport));
	*/

	/* clean up */
	if (src_ip != NULL)
		free(src_ip);
	if (dst_ip != NULL)
		free(dst_ip);
}
/*-----------------------------------------------------------------*/
void CalculateChecksum(char* buffer)
{
	struct ip* iph = (struct ip*)(buffer + 1);
	struct tcphdr* tcph = (struct tcphdr*)(buffer + 1 + iph->ip_hl * 4);

	/* TCP/IP checksum recalculate */
	struct pseudo_header{ /* For TCP header checksum */ 
		unsigned int source_address; 
		unsigned int dest_address; 
		unsigned char placeholder; 
		unsigned char protocol; 
		unsigned short tcp_length; 
		struct tcphdr tcp; 
		char payload[2048];
	}  pseudo_header;

	/* subtract IP header length */
	unsigned short tcp_len = ntohs(iph->ip_len) - iph->ip_hl * 4;

	/* Psuedo-headers needed for TCP hdr checksum 
	   (they do not change and do not need to be in the loop) */ 
	pseudo_header.source_address = iph->ip_src.s_addr;
	pseudo_header.dest_address = iph->ip_dst.s_addr; 
	pseudo_header.placeholder = 0; 
	pseudo_header.protocol = IPPROTO_TCP;
	pseudo_header.tcp_length = htons(tcp_len);

	/* make checksums zero: we will recalculate */
	iph->ip_sum = 0;
	tcph->th_sum = 0;

	/* IP header checksum */
	iph->ip_sum = in_cksum((unsigned short*)iph, iph->ip_hl * 4);

	/* Setup TCP for checksum */
	bcopy((char*)tcph, (char*)&pseudo_header.tcp, 20);
	bcopy((char*)tcph + 20, (char*)&pseudo_header.payload, tcp_len - 20);

	if (tcp_len % 2) {
		/* odd TCP length: padding */
		pseudo_header.payload[tcp_len] = 0;
	}

	/* TCP header checksum */
	tcph->th_sum = in_cksum((unsigned short*)&pseudo_header, 12 + tcp_len);
}
/*-----------------------------------------------------------------*/
void MangleTcpIpHeader(char* buffer, int reverseDirection)
{
	if (!init) {
		Initialize();
	}

	/* check if it has IP header or not */
	if ((buffer[1] & 0xF0) != 0x40) {
		/* if not, ignore */
		return;
	}

	struct ip* iph = (struct ip*)(buffer + 1);
	struct tcphdr* tcph = (struct tcphdr*)(buffer + 1 + sizeof(struct ip));

	/* for now, we only consider TCP sessions */
	if (iph->ip_p != IPPROTO_TCP) {
		return;
	}

	/* TCP/IP header rewrite */
	ClientKey key;
	ClientValue* value = NULL;
	if (reverseDirection) {
		key.ip = ntohl(iph->ip_dst.s_addr);
		key.port = ntohs(tcph->th_dport);
		value = hashtable_search(client_table, &key);
		if (value != NULL) {
			/* restore the original header */
			iph->ip_src.s_addr = htonl(value->org_ip);
			tcph->th_sport = htons(value->org_port);
			CalculateChecksum(buffer);
		}
		else {
			/* this should not be happened */
			syslog(LOG_ERR, "WAPROX: packet from proxy to client without NAT entry");
			return;
		}
	}
	else {
		key.ip = ntohl(iph->ip_src.s_addr);
		key.port = ntohs(tcph->th_sport);
		value = hashtable_search(client_table, &key);
		if (value == NULL) {
			/* new NAT entry: should be SYN packet */
			if (!(tcph->th_flags & TH_SYN)) {
				syslog(LOG_ERR, "WAPROX: SYN packet should come first");
				return;
			}

			/* create key */
			ClientKey* newkey = (ClientKey*)malloc(sizeof(ClientKey));
			if (newkey == NULL) {
				syslog(LOG_ERR, "WAPROX: ClientKey creation failed");
				return;
			}
			newkey->ip = key.ip;
			newkey->port = key.port;

			/* create value */
			value = (ClientValue*)malloc(sizeof(ClientValue));
			if (value == NULL) {
				syslog(LOG_ERR, "WAPROX: ClientValue creation failed");
				free(newkey);
				return;
			}
			value->org_ip = ntohl(iph->ip_dst.s_addr);
			value->org_port = ntohs(tcph->th_dport);
			gettimeofday(&(value->lastAccessTime), NULL);
			value->sawFIN = 0;

			/* store the original destination in the table */
			if (hashtable_insert(client_table, newkey, value) == 0) {
				syslog(LOG_ERR, "WAPROX: hashtable_insert() failed");
				free(newkey);
				free(value);
				return;
			}

			/*syslog(LOG_INFO, "WAPROX: new NAT entry made");*/
		}
		else {
			/* NAT entry exists */
			if (value->org_ip != ntohl(iph->ip_dst.s_addr) && value->org_port != ntohs(tcph->th_dport)) {
				syslog(LOG_ERR, "WAPROX: NAT entry mismatch. we overwrite");
				value->org_ip = ntohl(iph->ip_dst.s_addr);
				value->org_port = ntohs(tcph->th_dport);
				value->sawFIN = 0;
				return;
			}
		}

		/* rewrite the header to waprox */
		iph->ip_dst.s_addr = localIP;
		tcph->th_dport = waproxPort;
		CalculateChecksum(buffer);
	}

	/* common routine: update time, check FIN packet */
	gettimeofday(&(value->lastAccessTime), NULL);
	if (tcph->th_flags & TH_FIN) {
		/* FIN packet */
		/*syslog(LOG_INFO, "WAPROX: saw FIN packet");*/
		value->sawFIN = 1;
	}

	DumpTcpIpHeader(buffer, reverseDirection);
}
/*-----------------------------------------------------------------*/
int CreatePrivateAcceptSocketEx(int portNum, int nonBlocking, int loopbackOnly)
{
	int doReuse = 1;
	struct linger doLinger;
	int sock;
	struct sockaddr_in sa;

	/* Create socket. */
	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
		return(-1);

	/* don't linger on close */
	doLinger.l_onoff = doLinger.l_linger = 0;
	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, 
				&doLinger, sizeof(doLinger)) == -1) {
		close(sock);
		return(-1);
	}

	/* reuse addresses */
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
				&doReuse, sizeof(doReuse)) == -1) {
		close(sock);
		return(-1);
	}

	if (nonBlocking) {
		/* make listen socket nonblocking */
		if (fcntl(sock, F_SETFL, O_NDELAY) == -1) {
			close(sock);
			return(-1);
		}
	}

	/* set up info for binding listen */
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = (loopbackOnly) ? htonl(INADDR_LOOPBACK) 
		: htonl(INADDR_ANY);
	sa.sin_port = htons(portNum);

	/* bind the sock */
	if (bind(sock, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
		close(sock);
		return(-1);
	}

	/* start listening */
	if (listen(sock, 32) == -1) {
		close(sock);
		return(-1);
	}

	return(sock);
}
/*-----------------------------------------------------------------*/
int CreatePrivateAcceptSocket(int portNum, int nonBlocking)
{
	return CreatePrivateAcceptSocketEx(portNum, nonBlocking, 0);
}
/*-----------------------------------------------------------------*/
int ProcessNATQuery(int waprox_fd)
{
	/* read the request from the waprox_fd */
	int numRead = read(waprox_fd, nat_buffer + nat_buflen, sizeof(nat_buffer) - nat_buflen);
	if (numRead == -1) {
		syslog(LOG_ERR, "WAPROX: NAT Request read error");
		return -1;
	}
	else if (numRead == 0) {
		syslog(LOG_ERR, "WAPROX: NAT Request EOF");
		return -1;
	}
	else {
		nat_buflen += numRead;
		/*syslog(LOG_INFO, "WAPROX: NAT Request read %d bytes", nat_buflen);*/
	}

	int num_processed = 0;
	NATRequest request;
	NATResponse response;
	while (nat_buflen >= sizeof(NATRequest)) {
		/* read one request */
		memcpy(&request, nat_buffer + num_processed, sizeof(NATRequest));
		nat_buflen -= sizeof(NATRequest);
		num_processed += sizeof(NATRequest);

		/* lookup NAT table to find the original destination IP/Port */
		ClientKey key;
		key.ip = ntohl(request.ip);
		key.port = ntohs(request.port);
		struct in_addr tmp;
		tmp.s_addr = htonl(key.ip);
		/*syslog(LOG_INFO, "WAPROX: NAT Request %s:%d", inet_ntoa(tmp), key.port);*/
		ClientValue* value = hashtable_search(client_table, &key);
		if (value == NULL) {
			syslog(LOG_ERR, "WAPROX: NAT entry not found");
			return -1;
		}
		else {
			response.ip = htonl(key.ip);
			response.port = htons(key.port);
			response.org_ip = htonl(value->org_ip);
			response.org_port = htons(value->org_port);
			/*syslog(LOG_INFO, "WAPROX: NAT entry found");*/
		}

		struct in_addr tmp_addr;
		tmp_addr.s_addr = response.ip;
		char* sip_str = strdup(inet_ntoa(tmp_addr));
		tmp_addr.s_addr = response.org_ip;
		char* dip_str = inet_ntoa(tmp_addr);
		/*syslog(LOG_INFO, "WAPROX: NAT Response Src[%s:%d], Dst[%s:%d]\n", sip_str, ntohs(response.port), dip_str, ntohs(response.org_port));*/
		free(sip_str);

		/* write the response to the waprox_fd */
		int numWrite = write(waprox_fd, &response, sizeof(NATResponse));
		if (numWrite != sizeof(NATResponse)) {
			/* TODO: need buffering? */
			syslog(LOG_ERR, "WAPROX: NAT Response write fail: %d/%d bytes", numWrite, sizeof(NATResponse));
			return -1;
		}
		else {
			/*syslog(LOG_INFO, "WAPROX: NAT Response write success");*/
		}
	}

	/* adjust buffer */
	if (num_processed > 0) {
		memmove(nat_buffer, nat_buffer + num_processed, nat_buflen);
	}

	return 0;
}
/*-----------------------------------------------------------------*/
void RemoveIdleEntry()
{
	/* periodically called to save memory */
	/* TODO: iterate all entries and remove idle entires (if any) */
}
/*-----------------------------------------------------------------*/
