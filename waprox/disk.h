#ifndef __WAPROX_DISK_H__
#define __WAPROX_DISK_H__

#include "connection.h"
#include "protocol.h"

#define MAX_PUT_DISK_QUEUE	(1024)

typedef enum processState {
	ps_none,
	ps_keepalive,
	ps_nameTreeDelivery,
	ps_bodyRequest,
	ps_peekRequest,
	ps_putDisk,
	ps_userData
} processState;

typedef enum hcMethod {
	hc_put,
	hc_get,
	hc_peek
} hcMethod;

/* cache transaction (req-resp) information */
struct CacheTransactionInfo {
	/* TODO: these two are tightly coupled! */
	ConnectionID cid;
	ConnectionInfo* sourceCI;
	ChunkTreeInfo* pCTInfo;

	u_char sha1name[SHA1_LEN];
	int chunkLen;
	char* pChunk;
	int dstPxyId;
	hcMethod method;
	processState ps;
	void* pOpaque;
	int opaqueLen;
	int index;
	struct timeval sentTime;
	TAILQ_ENTRY(CacheTransactionInfo) firstList;
	TAILQ_ENTRY(CacheTransactionInfo) secondList;
	TAILQ_ENTRY(CacheTransactionInfo) deadList;
};

typedef TAILQ_HEAD(CacheRequestList, CacheTransactionInfo) CacheRequestList;
extern CacheRequestList g_FirstClassRequestList;
extern CacheRequestList g_SecondClassRequestList;
extern int g_NumPendingFirstTransactions;
extern int g_NumPendingSecondTransactions;

void InitDiskCache();
struct CacheTransactionInfo* CreateCacheTransactionInfo(hcMethod method,
		processState ps, int dstPxyId, u_char* sha1name, int chunkLen, 
		char* pChunk, ConnectionInfo* sci, ChunkTreeInfo* cti,
		int index, void* pOpaque, int opaqueLen);
void DestroyCacheTransactionInfo(struct CacheTransactionInfo* cci);
void SendCacheRequest(hcMethod method, processState ps,
		u_char* sha1name, int length, char* pChunk, int dstPxyId,
		ConnectionInfo* sci, ChunkTreeInfo* cti, int index,
		void* pOpaque, int opaqueLen);
void IssuePendingCacheRequests();
void IssueCacheRequest(ConnectionInfo* ci, struct CacheTransactionInfo* cci);
void ScheduleCacheRequest(struct CacheTransactionInfo* cache_info,
		char addTail);
void PutDisk(u_char* digest, char* buffer, int buf_len);
void PutDone(u_char* sha1name);
int ResolveOneLevelDisk(ChunkTreeInfo* cti, int level,
		ConnectionInfo* target_ci);
void MakeNewCacheConnection(int i);
void CheckDiskCacheConnections();
int DestroyCacheConnections();
ConnectionInfo* GetIdleCacheConn();
char* GetHCMethodName(hcMethod method);

#endif /*__WAPROX_DISK_H__*/
