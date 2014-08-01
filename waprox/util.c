#include "util.h"
#include <openssl/sha.h>
#include <arpa/inet.h>
#include <math.h>
#include <assert.h>
#include <netinet/tcp.h>

#ifdef _WAPROX_USE_SCTP
#include <netinet/sctp.h>
#endif

struct timeval g_ElapsedTimerStart;

/*-----------------------------------------------------------------*/
void Calc_SHA1Sig(const byte* buf, int32 num_bytes,
		u_char digest[SHA1_LEN])
{
	SHA_CTX sha;
	SHA1_Init(&sha);
	SHA1_Update(&sha, buf, num_bytes);
	SHA1_Final(digest, &sha);
}
/*-----------------------------------------------------------------*/
int VerifySHA1(const byte* buf, int32 num_bytes, u_char digest[SHA1_LEN])
{
#ifdef _WAPROX_INTEGRITY_CHECK
	u_char sha1name[SHA1_LEN];
	assert(num_bytes >= 0);
	Calc_SHA1Sig(buf, num_bytes, sha1name);
	if (memcmp(sha1name, digest, SHA1_LEN) == 0) {
		return TRUE;
	}
	else {
		return FALSE;
	}
#else
	return TRUE;
#endif
}
/*-----------------------------------------------------------------*/
char* SHA1Str(u_char* digest)
{
	static char sha1str[(SHA1_LEN * 2) + 1];
	int i;
	for (i = 0; i < SHA1_LEN; i++) {
		sprintf(sha1str + (i * 2), "%02x", digest[i]);
	}
	sha1str[(SHA1_LEN * 2)] = '\0';
	return sha1str;
}
/*-----------------------------------------------------------------*/
u_char* SHA1StrR(char* sha1str)
{
	static u_char digest[SHA1_LEN];
	int i;
	char hex[3];
	unsigned int d = 0;
	for (i = 0; i < SHA1_LEN; i++) {
		hex[0] = sha1str[i * 2];
		hex[1] = sha1str[(i * 2) + 1];
		hex[2] = '\0';
		sscanf(hex, "%x", &d);
		digest[i] = (u_char)d;
	}
	return digest;
}
/*-----------------------------------------------------------------*/
int SetSocketBufferSize(int sockfd, int buffersize)
{
	/* set socket send buffer size */
	if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF,
			&buffersize, sizeof(int)) == -1) {
		close(sockfd);
		TRACE("setsockopt SO_SNDBUF failed\n");
		return -1;
	}

	/* set socket receive buffer size */
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF,
			&buffersize, sizeof(int)) == -1) {
		close(sockfd);
		TRACE("setsockopt SO_RCVBUF failed\n");
		return -1;
	}
	
	/* success */
	return 0;
}
/*-----------------------------------------------------------------*/
int DisableNagle(int sockfd)
{
#ifdef _WAPROX_NODELAY
	int flag = 1;
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
				(char*)&flag, sizeof(int)) < 0) {
		close(sockfd);
		TRACE("failed TCP_NODELAY socket - %d\n", errno);
		return(-1);
	}
#endif

	/* success */
	return 0;
}
/*-----------------------------------------------------------------*/
char* InetToStr(unsigned int ip)
{
	struct in_addr tmp;
	tmp.s_addr = htonl(ip);
	return inet_ntoa(tmp);
}
/*-----------------------------------------------------------------*/
void PrintInetAddress(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport)
{
	char* sip_str = strdup(InetToStr(sip));
	char* dip_str = strdup(InetToStr(dip));
	TRACE("Src[%s:%d], Dst[%s:%d]\n", sip_str, sport,
			dip_str, dport);
	free(sip_str);
	free(dip_str);
}
/*-----------------------------------------------------------------*/
void FillTimevalSec(struct timeval* tv, int sec)
{
	timerclear(tv);
	tv->tv_sec = sec;
}
/*-----------------------------------------------------------------*/
void FillTimevalMillisec(struct timeval* tv, int ms)
{
	timerclear(tv);
	tv->tv_sec = (ms / 1000);
	tv->tv_usec = (ms % 1000) * 1000;
}
/*-----------------------------------------------------------------*/
int GetTimeGapMS(struct timeval* start, struct timeval* end)
{
	struct timeval result;
	timersub(end, start, &result);
	int msec = result.tv_sec * 1000 + result.tv_usec / 1000;
	return msec;
}
/*-----------------------------------------------------------------*/
int GetTimeGapUS(struct timeval* start, struct timeval* end)
{
	struct timeval result;
	timersub(end, start, &result);
	int us = result.tv_sec * 1000000 + result.tv_usec;
	return us;
}
/*-----------------------------------------------------------------*/
int GetElapsedUS(struct timeval* start)
{
	struct timeval current;
	UpdateCurrentTime(&current);
	return GetTimeGapUS(start, &current);
}
/*-----------------------------------------------------------------*/
void UpdateCurrentTime(struct timeval* tv)
{
	#if 0
	if (gettimeofdayex(tv, NULL) != 0) {
	#endif
	if (gettimeofday(tv, NULL) != 0) {
		NiceExit(-1, "failed to run gettimeofday()\n");
	}
}
/*-----------------------------------------------------------------*/
void PrintElapsedTime(char* info, struct timeval* base)
{
	struct timeval current;
	static struct timeval prev;
	UpdateCurrentTime(&current);
	if (base != NULL) {
		TRACE("%s: %d ms elapsed (diff %d ms)\n", info,
				GetTimeGapMS(&prev, &current),
				GetTimeGapMS(base, &current));
	}
	else {
		TRACE("%s: %d ms elapsed\n", info,
				GetTimeGapMS(&prev, &current));
	}

	prev = current;
}
/*-----------------------------------------------------------------*/
#ifdef _WAPROX_USE_SCTP
int SetSctpNumStreams(int sock, int numStreams)
{
	/* set # of in/out streams */
	struct sctp_initmsg initmsg;
	unsigned int msglen = sizeof(initmsg);
	if (getsockopt(sock, SOL_SCTP, SCTP_INITMSG,
				&initmsg, &msglen) == -1) {
		close(sock);
		return(FALSE);
	}
	initmsg.sinit_num_ostreams = numStreams;
	initmsg.sinit_max_instreams = numStreams;
	if (setsockopt(sock, SOL_SCTP, SCTP_INITMSG,
				&initmsg, sizeof(initmsg)) == -1) {
		close(sock);
		return(FALSE);
	}
	return TRUE;
}
/*-----------------------------------------------------------------*/
int CreateSctpAcceptSocket(int portNum, int nonBlocking, int loopbackOnly,
		int numStreams)
{
	int doReuse = 1;
	struct linger doLinger;
	int sock;
	struct sockaddr_in sa;

	/* Create socket. */
	if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_SCTP)) == -1)
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

	/* set # of in/out streams */
	if (SetSctpNumStreams(sock, numStreams) == FALSE) {
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
	if (listen(sock, 1024) == -1) {
		close(sock);
		return(-1);
	}

	return(sock);
}
/*-----------------------------------------------------------------*/
int MakeSctpConnection(char *name, in_addr_t netAddr, int portNum,
		int nonBlocking, int numStreams)
{
	struct sockaddr_in saddr;
	int fd;

	if (name != NULL) {
		struct hostent *ent;
		if ((ent = gethostbyname(name)) == NULL) {
			if (hdebugLog)
				TRACE("failed in name lookup - %s\n", name);
			return(-1);
		}
		memcpy(&netAddr, ent->h_addr, sizeof(netAddr));
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
		if (hdebugLog)
			TRACE("failed creating socket - %d\n", errno);
		return(-1);
	}

	/* set # of in/out streams */
	if (SetSctpNumStreams(fd, numStreams) == FALSE) {
		close(fd);
		return(-1);
	}

	if (nonBlocking) {
		if (fcntl(fd, F_SETFL, O_NDELAY) < 0) {
			if (hdebugLog)
				TRACE("failed fcntl'ing socket - %d\n", errno);
			close(fd);
			return(-1);
		}
	}

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = netAddr;
	saddr.sin_port = htons(portNum);

	if (connect(fd, (struct sockaddr *) &saddr, 
				sizeof(struct sockaddr_in)) < 0) {
		if (errno == EINPROGRESS)
			return(fd);
		if (hdebugLog)
			TRACE("failed connecting socket - %d\n", errno);
		close(fd);
		return(-1);
	}

	return(fd);
}
/*-----------------------------------------------------------------*/
int DisableSctpNagle(int sockfd)
{
#ifdef _WAPROX_NODELAY
	int flag = 1;
	if (setsockopt(sockfd, SOL_SCTP, SCTP_NODELAY,
				(char*)&flag, sizeof(int)) < 0) {
		close(sockfd);
		TRACE("failed SCTP_NODELAY socket - %d\n", errno);
		return(-1);
	}
#endif

	/* success */
	return 0;
}
#endif
/*-----------------------------------------------------------------*/
int CreateSlave(SlaveFunc *func, void *token, int doTrace)
{
	/* creates a pipe, forks off a new process, closes the appropriate
	   ends, and returns the parent's fd back to the caller. The child
	   then waits for requests and processes them */
	int socks[2];
	int pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, socks) < 0) {
		NiceExit(-1, "failed on socketpair\n");
	}

	if ((pid = fork()) < 0) {
		NiceExit(-1, "failed on socketpair\n");
	}
	if (pid == 0) {
		/* we're in child */
		close(socks[0]);
		if (SetSocketBufferSize(socks[1], 128 * 1024) != 0) {
			NiceExit(-1, "SetSocketBufferSize failed\n");
		}

		func(socks[1], token);

		/* we should never get here */
		NiceExit(-1, "slave main returned\n");
	}
	if (doTrace) {
		TRACE("created slave %d\n", pid);
	}
	close(socks[1]);
	if (SetSocketBufferSize(socks[0], 128 * 1024) != 0) {
		NiceExit(-1, "SetSocketBufferSize failed\n");
	}

	return(socks[0]);
}
/*-----------------------------------------------------------------*/
