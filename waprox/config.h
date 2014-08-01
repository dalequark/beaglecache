#ifndef __WAPROX_CONFIG_H__
#define __WAPROX_CONFIG_H__

#include <sys/time.h>
#include "queue.h"
#include "connection.h"

#define PROXCONF	"waprox.conf"
#define PEERCONF	"peer.conf"

/* parameters */
extern int g_PptpNatServerPort;
extern int g_UserListeningPort;
extern int g_NameListeningPort;
extern int g_ChunkListeningPort;
extern int g_ProxyPingPort;
extern int g_CacheServerPort;
extern int g_HttpProxyPort;
extern int g_MaxDnsConns;
extern int g_MinChunkSize;
extern int g_ChunkSize;
extern int g_NumMRCWays;
extern int g_MaxCacheSize;
extern int g_TimerInterval;
extern int g_BufferTimeout;
extern int g_MaxCacheConns;
extern char g_MemCacheOnly;
extern char g_NoCache;
extern char g_ClientChunking;
extern char g_ServerChunking;
extern char g_UseFixedDest;
extern char* g_FixedDestAddr;
extern int g_FixedDestPort;
extern char g_SkipFirstChunk;
extern int g_MaxPeerConns;
extern int g_PutQueueSize;
extern int g_WanLinkBW;
extern int g_WanLinkRTT;
extern int g_SeekLatency;
extern char g_GenerateNameHint;

/* from command line options */
extern int o_minChunkSize;
extern int o_maxChunkSize;
extern int o_numWays;
extern char o_disableChunking;
extern char* o_localHost;
extern int o_maxPeerConns;

typedef enum confType {
	type_proxconf, type_peerconf
} confType;
/* parse conf file */
void ReadConf(confType type, const char* filename);

#endif /*__WAPROX_CONFIG_H__*/
