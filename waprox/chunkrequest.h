#ifndef __WAPROX_CHUNKREQUEST_H__
#define __WAPROX_CHUNKREQUEST_H__

#include "protocol.h"
#include "queue.h"
#include "reassemble.h"

typedef struct ChunkRequestInfo {
	char isRedirected;
	/* if not redirected: the destination proxy address
	   if redirected: the previous hop proxy address */
	in_addr_t pxyAddr;
	ConnectionID cid; /* invalid if redirected */
	int length;
	struct timeval sentTime;
	char isNetwork;
	int peerIdx;
	TAILQ_ENTRY(ChunkRequestInfo) reqList;
} ChunkRequestInfo;

typedef struct PeekRequestInfo {
	ConnectionID cid; 
	ReassembleInfo* ri;
	ChunkNameInfo* cni;
	struct timeval sentTime;
	TAILQ_ENTRY(PeekRequestInfo) reqList;
} PeekRequestInfo;

/* opaque state variable for disk cache */
typedef struct ChunkRequestOpaque {
	ConnectionID cid;
	WAOneRequest request;
	in_addr_t prevPxyAddr;
#ifdef _WAPROX_RESPONSE_FIFO
	TAILQ_ENTRY(ChunkRequestOpaque) list;
	int peerIdx;
	char* msg;
	int msg_len;
	int streamID;
#endif
} ChunkRequestOpaque;

typedef struct PeekRequestOpaque {
	ConnectionID cid;
	WAOnePeekRequest request;
} PeekRequestOpaque;

typedef struct PeekRequestOpaque2 {
	ConnectionID cid;
	ReassembleInfo* ri;
	ChunkNameInfo* cni;
} PeekRequestOpaque2;

extern struct hashtable* chunk_request_ht;
extern struct hashtable* peek_request_ht;
extern int g_NumChunkRequestsInFlight;
extern int g_NumPeekRequestsInFlight;

typedef TAILQ_HEAD(ChunkRequestList, ChunkRequestInfo) ChunkRequestList;
typedef TAILQ_HEAD(PeekRequestList, PeekRequestInfo) PeekRequestList;
#ifdef _WAPROX_RESPONSE_FIFO
typedef TAILQ_HEAD(ChunkResponseList, ChunkRequestOpaque) ChunkResponseList;
#endif

int CreateChunkRequestTable();
int DestroyChunkRequestTable();
int PutChunkRequest(u_char* sha1name, ConnectionID cid, char isRedirected,
		in_addr_t pxyAddr, int length, int peerIdx, char isNetwork);
int PutPeekRequest(ReassembleInfo* ri, ChunkNameInfo* cni, u_char* sha1name,
		ConnectionID cid);
ChunkRequestList* GetChunkRequestList(u_char* sha1name);
PeekRequestList* GetPeekRequestList(u_char* sha1name);
int DestroyChunkRequestList(u_char* sha1name);
int DestroyPeekRequestList(u_char* sha1name);

#endif /*__WAPROX_CHUNKREQUEST_H__*/
