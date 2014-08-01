#ifndef __WAPROX_CONNECTION_H__
#define __WAPROX_CONNECTION_H__

#include "util.h"
#include "queue.h"
#include <event.h>
#include "mrc_tree.h"
#include "chunkcache.h"
#include "reassemble.h"
#include "disk.h"

/*
	- waprox maintains 6 types of connections
	1. sources who forward traffic into waprox
	   destinations who waprox makes connections with on behalf of clients
	2. peers who waprox exchanges chunk blocks with 
	3. a pptpd NAT server who waprox makes local connection with
	4. a cache server who provides PUT/GET interface
	5. a local helper process for DNS name lookup
	5. a local helper process for flushing chunk name hints to disk
*/

#define MAX_NUM_CI		(1024 * 5)
#define SOCKBUF_SIZE		(128 * 1024)
#if 0
#define MIN_RABIN_BUFSIZE	(g_ChunkSize * 3)
#define MAX_CHUNK_SIZE		(g_ChunkSize * 5) 
#endif
/* we always use the same threshold */
#define MIN_RABIN_BUFSIZE	(64 * 1024 * 3)
#define MAX_CHUNK_SIZE		(64 * 1024 * 5) 
#if 0
#define MAX_WINDOW_SIZE		MAX((MAX_CHUNK_SIZE * 5), SOCKBUF_SIZE)
#define RECV_BUFSIZE		MAX((MAX_CHUNK_SIZE * 2), SOCKBUF_SIZE)
#endif
#define MAX_WINDOW_SIZE		(60 * 1024 * 1024)
#if 0
#define RECV_BUFSIZE		(1024 * 1024)
#endif
#define RECV_BUFSIZE		(MIN_RABIN_BUFSIZE * 1.1)
#define MAX_CACHE_CONNS		(100)
#define MAX_DNS_CONNS		(20)

extern unsigned int g_ConnectionID;

typedef enum eventState {
	es_none,
	es_added,
	es_triggered
} eventState;

typedef struct myEvent {
	eventState esRead;
	eventState esWrite;
	struct event evRead;
	struct event evWrite;
	short int sctp_event;
} myEvent;
extern myEvent g_MyEventSet[MAX_NUM_CI];

/* pseudo and global connection table */
extern struct hashtable* g_PseudoConnMap;
extern struct hashtable* g_ConnMap;

typedef enum connectionType {
	ct_user,	/* both incoming and outgoing connections */
	ct_control,	/* peer proxy connections for name delivery */
	ct_chunk,	/* peer proxy connections for chunk request/response */
	ct_cache,	/* a cache server connection */
	ct_dns,		/* DNS helper process */
	ct_disk,	/* Disk helper process */
	ct_pptpd	/* pptpd NAT connection */
} connectionType;

typedef enum connectionStatus {
	cs_active,	/* read event is enabled */
	cs_block,	/* read event is disabled for flow control */
	cs_done,	/* to be closed */
	cs_finsent,	/* fin sent */
	cs_closed	/* closed */
} connectionStatus;

typedef enum sctpAssocState {
	sas_none,
	sas_initsent,
	sas_active,
	sas_blocked
} sctpAssocState;

/* speical cases: WABodyResponse and Reconstruction */
typedef struct MessageInfo {
	char* msg;
	int len;
	int streamID;
	TAILQ_ENTRY(MessageInfo) messageList;
} MessageInfo;
typedef TAILQ_HEAD(MessageList, MessageInfo) MessageList;

/* connection (socket) info */
struct ConnectionInfo {
	/* socket file descriptor */
	int fd;

	/* connection ID for debugging (not globally unique) */
	unsigned int connID;

	/* connection type: user, prox, pptpd */
	connectionType type; 

	/* connection status */
	connectionStatus status;

	/* error flag */
	char isAbnormal;

	/* writable flag */
	char isWritable;

	/* buffers */
	char* bufRead;
	int lenRead;
	int lenWrite;

	/* message lists (write only for now) */
	MessageList msgListWrite;
	int firstMsgOffset;

	/* for list */
	TAILQ_ENTRY(ConnectionInfo) ciList; /* for global */
	TAILQ_ENTRY(ConnectionInfo) ciList2; /* for proxy */
	TAILQ_ENTRY(ConnectionInfo) ciDead; /* for dead conn list */
	TAILQ_ENTRY(ConnectionInfo) ciFree; /* for removing item from list */

	/* pointer to type-specific data structure */
	void* stub;

	/* timestamp for debugging */
	struct timeval timestamp;
};

/* user-specific connection information */
typedef struct UserConnectionInfo {
	/* forward or reverse */
	int isForward;

	/* HTTP proxy */
	char isHttpProxy;

	/* MRC context */
	/*MrcCtx mrc_ctx;*/
	RabinCtx rctx;
	RabinCtx rctx4put;

	/* TODO: handle overflow ex) reset message */
	unsigned int bytesTX;
	unsigned int bytesRX;

	/* flags used to determine if writing is done before closing */
	unsigned int closeBytes;

	unsigned int numChunks;

	/* cache hit/miss stat */
	unsigned int numHits;
	unsigned int numMisses;
	unsigned int hitBytes;
	unsigned int missBytes;

	/* from disk to network */
	unsigned int numMoves;	
	unsigned int moveBytes;

	/* chunck sequence number to handle out-of-order delivery */
	unsigned int seq_no;
	ReassembleList reassembleList;
	ReassembleList readyList;
	unsigned int pendingNames;
	unsigned int pendingBytes;

	/* TODO: maintain in/out buffer */
	ChunkBufferList chunkBufferList;
	unsigned int numChunkBuffers;

	/* flow control */
	unsigned int maxWindowSize;
	/* 1. sending side state */
	unsigned int numSent;
	/* 2. receiving side state */
	unsigned int lastAcked;
	unsigned int lastCumulativeAck;

	/* update last access time to check buffer timeout */
	struct timeval startTime;
	struct timeval lastAccessTime;

	/* source and destination IP/port */
	unsigned int sip;
	unsigned short sport;
	unsigned int dip;
	unsigned short dport;

	/* destination proxy */
	int dstPxyIdx;
	int nameFlowId;

	#if 0
	/* corresponding peer name connection */
	ConnectionInfo* nameCI;
	#endif

	/* control message list */
	MessageList controlMsgList;
} UserConnectionInfo;

/* control-specific connection information */
typedef struct ControlConnectionInfo {
	int peerIdx;
	int flowIdx;

	/* SCTP related */
	unsigned int assocID;
	sctpAssocState assocState;

	/* TODO: per user connection, we maintain:
	 1. name lists to send 
	 2. ack lists to send */
	 #if 0
	 struct hashtable* connmap;
	 #endif
} ControlConnectionInfo;

/* chunk-specific connection information */
typedef struct ChunkConnectionInfo {
	int peerIdx;
	int flowIdx;
	
	/* SCTP related */
	unsigned int assocID;
	sctpAssocState assocState;

	/* TODO: per user connection, we maintain:
	 1. chunk request lists to send 
	 2. chunk response lists to send */
	 #if 0
	 struct hashtable* connmap;
	 #endif
} ChunkConnectionInfo;

/* pptp-specific connection information */
typedef struct PptpConnectionInfo {
	/* TODO: */
} PptpConnectionInfo;

/* forward decl. */
typedef struct CacheTransactionInfo CacheTransactionInfo;
typedef struct DnsTransactionInfo DnsTransactionInfo;

/* cache-specific connection information */
typedef struct CacheConnectionInfo {
	struct timeval lastAccessTime;
	CacheTransactionInfo* pCurTrans;
	int cacheConnIdx;
} CacheConnectionInfo;

/* dns-specific connection information */
typedef struct DnsConnectionInfo {
	DnsTransactionInfo* pCurTrans;
} DnsConnectionInfo;

/* disk-specific connection information */
typedef struct DiskConnectionInfo {
	/* TODO: */
} DiskConnectionInfo;

/* connection key */
typedef struct ConnectionKey {
	unsigned int sip;
	unsigned short sport;
	unsigned int dip;
	unsigned short dport;
} ConnectionKey;

/* user connection list */
typedef TAILQ_HEAD(ConnectionList, ConnectionInfo) ConnectionList;
extern ConnectionList g_DeadConnList;
extern ConnectionList g_UserList;
extern int g_NumUserConns;
extern int g_NumDeadConns;

/* peer connection info */
extern int g_NumPeerConns;

/* master set */
extern ConnectionInfo* g_MasterCIsetByFD[MAX_NUM_CI];

/* pptpd NAT connection info */
extern ConnectionInfo* g_PptpdNatCI;

/* disk (chunk name hint) connection info */
extern ConnectionInfo* g_DiskHelperCI;

/* cache connection info */
extern ConnectionInfo* g_CacheConnections[MAX_CACHE_CONNS];
extern int g_NumCacheConns;

/* dns connection info */
extern ConnectionInfo* g_DnsHelperConnections[MAX_DNS_CONNS];
extern int g_NumDnsConns;

/* functions */
ConnectionInfo* CreateNewConnectionInfo(int newfd, connectionType type);
void CreateUserConnectionInfo(ConnectionInfo* ci, int isForward);
void CreateControlConnectionInfo(ConnectionInfo* ci);
void CreateChunkConnectionInfo(ConnectionInfo* ci);
void CreateCacheConnectionInfo(ConnectionInfo* ci, int index);
void CreateDnsConnectionInfo(ConnectionInfo* ci);
void DestroyConnectionInfo(ConnectionInfo* ci);
void DestroyUserConnectionInfo(ConnectionInfo* ci);
void DestroyControlConnectionInfo(ConnectionInfo* ci);
void DestroyChunkConnectionInfo(ConnectionInfo* ci);
void DestroyCacheConnectionInfo(ConnectionInfo* ci);
void DestroyDnsConnectionInfo(ConnectionInfo* ci);
int CreateConnectionTables();
int DestroyConnectionTables();
int PutConnection(struct hashtable* ht, ConnectionInfo* ci);
ConnectionInfo* SearchConnection(struct hashtable* ht,
		unsigned int sip, unsigned short sport, 
		unsigned int dip, unsigned short dport);
int RemoveConnection(struct hashtable* ht,
	unsigned int sip, unsigned short sport,
	unsigned int dip, unsigned short dport);
void PrintConnectionTableStats();
void InitConns();
void SetDead(ConnectionInfo* ci, char abnormal);

#endif /*__WAPROX_CONNECTION_H__*/
