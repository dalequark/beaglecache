#ifndef __WAPROX_UTIL_H__
#define __WAPROX_UTIL_H__

/*
	util.h: stole useful code from here and there
*/

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "debug.h"
#include "czipdef.h"

extern struct timeval g_ElapsedTimerStart;

/* communication */
int SetSocketBufferSize(int sockfd, int buffersize);
int DisableNagle(int sockfd);
char* InetToStr(unsigned int ip);
void PrintInetAddress(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport);
int MakeSctpConnection(char *name, in_addr_t netAddr, int portNum,
		int nonBlocking, int numStreams);
int CreateSctpAcceptSocket(int portNum, int nonBlocking, int loopbackOnly,
		int numStreams);
int DisableSctpNagle(int sockfd);
int SetSctpNumStreams(int sock, int numStreams);

/* SHA1 hash */
void Calc_SHA1Sig(const byte* buf, int32 num_bytes, u_char digest[SHA1_LEN]);
char* SHA1Str(u_char* digest);
u_char* SHA1StrR(char* sha1str);
int VerifySHA1(const byte* buf, int32 num_bytes, u_char digest[SHA1_LEN]);

/* etc. */
void FillTimevalSec(struct timeval* tv, int sec);
void FillTimevalMillisec(struct timeval* tv, int ms);
void UpdateCurrentTime(struct timeval* tv);
int GetTimeGapMS(struct timeval* start, struct timeval* end);
int GetTimeGapUS(struct timeval* start, struct timeval* end);
void PrintElapsedTime(char* info, struct timeval* base);
int GetElapsedUS(struct timeval* start);
typedef void SlaveFunc(int fd, void* token);
int CreateSlave(SlaveFunc *func, void *token, int doTrace);

#endif /*__WAPROX_UTIL_H__*/
