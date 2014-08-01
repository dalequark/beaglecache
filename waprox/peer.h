#ifndef __WAPROX_PEER_H__
#define __WAPROX_PEER_H__

#include "util.h"
#include "queue.h"
#include "connection.h"

#define MAX_PEER_SIZE	(1024)
#define MAX_PER_PEER_CONNS	(10)

/* peer information */
typedef struct PeerInfo {
	int idx;
	char* name;
	char* ip;
	in_addr_t netAddr;
	char isAlive;
	char isLocalHost;
	time_t lastPingTime;
	unsigned int addrHash;
	unsigned int numRedirected;
	unsigned int bytesRedirected;

	/* monitor stat for ILS */
	unsigned int RTT;
	unsigned int networkPendingBytes;
	unsigned int diskPendingChunks;

	/* round-robin */
	unsigned int nameFlowID;
	unsigned int chunkFlowID;

	/* from client to server */
	/* chunk request/response connection */
	ConnectionInfo* chunkCI[MAX_PER_PEER_CONNS];
	/* name delivery connection */
	ConnectionInfo* nameCI[MAX_PER_PEER_CONNS];

#ifdef _WAPROX_USE_SCTP
	/* from server to client */
	ConnectionInfo* chunkCI2; /* chunk request/response connection */
	ConnectionInfo* nameCI2; /* name delivery connection */
#endif

	/* per peer user connection list for scheduling */
	ConnectionList userList;
	unsigned int numUsers;
} PeerInfo;
extern PeerInfo* g_PeerInfo;
extern int g_NumPeers;
extern int g_NumLivePeers;

/* save localhost info */
extern char g_HostName[];
extern PeerInfo* g_LocalInfo;
extern PeerInfo* g_FixedDestInfo;
extern int g_FirstPeerIdx;

/* functions */
void ParsePeerConf(char* buf);
void DestroyPeerList();
void UpdatePeerLiveness(PeerInfo* pi);
void ProcessPing(char* buffer, unsigned int buf_len);
int PickDestinationProxy(unsigned int sip, unsigned short sport, unsigned int dip, unsigned short dport);
int PickPeerToRedirect(u_char* sha1Name);
void SendPing(char isPing, char* serverName, PeerInfo* pi,
		struct timeval* timestamp);
void CheckNodeStatus();
void PrintPeerStats();
PeerInfo* GetPeerInfoByName(char* name);
PeerInfo* GetPeerInfoByAddr(in_addr_t addr);
PeerInfo* GetPeerInfoByAssoc(int assocID);
void SetPeerInfoByAssoc(int assocID, int peerID);
int GetPeerIdxFromFD(int fd);
int GetNextChunkFlowID(int peerIdx);
int GetNextControlFlowID(int peerIdx);
PeerInfo* AddUnknownPeerInfo(in_addr_t netAddr);

#endif /*__WAPROX_PEER_H__*/
