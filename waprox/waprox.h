#ifndef __WAPROX_H__
#define __WAPROX_H__

#include "connection.h"
#include "protocol.h"
#include "mrc_tree.h"
#include "disk.h"
#include "chunkrequest.h"

/* statistics */
extern unsigned int g_BytesUserTX;
extern unsigned int g_BytesUserRX;
extern unsigned int g_BytesPeerTX;
extern unsigned int g_BytesPeerRX;
extern unsigned int g_BytesPptpTX;
extern unsigned int g_BytesPptpRX;
extern unsigned int g_BytesCacheTX;
extern unsigned int g_BytesCacheRX;
extern unsigned int g_BytesDnsTX;
extern unsigned int g_BytesDnsRX;
extern unsigned int g_Hits;
extern unsigned int g_Misses;
extern unsigned int g_RareMisses;
extern unsigned int g_MissRedirected;
extern unsigned int g_NumChunkRequests;
extern unsigned int g_NumTotUserConns;

/* debug */
extern unsigned int g_NumBlocked;
extern unsigned int g_NumCacheRequestSent;
extern unsigned int g_NumCacheResponseReceived;
extern unsigned int g_NumChunkRequestSent;
extern unsigned int g_NumCacheResponseReceived;
extern unsigned int g_NumPeekRequestSent;
extern unsigned int g_NumPeekResponseReceived;
extern unsigned int g_NumPutRequestSent;
extern unsigned int g_NumPutResponseReceived;
extern unsigned int g_NumPptpRequestSent;
extern unsigned int g_NumPptpResponseReceived;
extern unsigned int g_NumPutTempHits;

/* performance debug */
extern unsigned int g_PdNumNameTrees;
extern unsigned int g_PdPeakNumNameTrees;
extern unsigned int g_PdNumChunkRequests;
extern unsigned int g_PdPeakNumChunkRequests;
extern unsigned int g_PdNumFirstRequests;
extern unsigned int g_PdPeakNumFirstRequests;
extern unsigned int g_PdNumSecondRequests;
extern unsigned int g_PdPeakNumSecondRequests;

/* listening sockets */
extern int g_acceptSocketUser;
extern int g_acceptSocketName;
extern int g_acceptSocketChunk;
extern int g_acceptSocketPing;
extern int g_acceptSocketHttp;

/* some globals */
extern char g_TryReconstruction;
extern char g_NewPeekUpdate;

/* core functions */
void CloseConnection(ConnectionInfo* ci);
void WriteToNamePeer(int peerIdx, int flowId, char* msg, int len, int streamID);
void WriteToChunkPeer(int peerIdx, char* msg, int len, int streamID);
int SendData(ConnectionInfo* ci, char* msg, int msg_len);
int SendDataPrivate(ConnectionInfo* ci, char* msg, int msg_len, int streamID);
void SendNATRequest(ConnectionInfo* ci);
void SendWANameAck(ConnectionInfo* ci, unsigned int ack);
void SendWACloseConnection(ConnectionInfo* ci, unsigned int bytesTX);
int ProcessReadBuffer(ConnectionInfo* ci);
int ProcessOpenConnection(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessNameTreeDelivery(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessNameTreeDelivery2(ConnectionInfo* ci, char* buffer, int buf_len);
void PeekOneChunk(WAOneChunk* pChunk, ReassembleInfo* ri, ChunkNameInfo* cni,
		ConnectionID cid, int streamID, int dstPxyIdx);
void PostProcessNameTreeDelivery(CacheTransactionInfo* cci, char isHit,
		char* block, int block_length);
void PostProcessNameTreeDelivery2(CacheTransactionInfo* cci, char isHit,
		char* block, int block_length);
void LastProcessNameTreeDelivery(ConnectionInfo* ci, ChunkTreeInfo* cti);
#if 0
void LastProcessNameTreeDelivery2(ReassembleInfo* ri);
#endif
int ProcessBodyRequest(ConnectionInfo* ci, char* buffer, int buf_len);
void PostProcessBodyRequest(ConnectionInfo* ci, char isHit,
		ChunkRequestOpaque* opaque, char* block);
void ProcessOneChunkRequest(ChunkRequestInfo* cri, u_char* sha1name,
		char* block, int blength, int streamID);
int ProcessBodyResponse(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessPeekRequest(ConnectionInfo* ci, char* buffer, int buf_len);
void PostProcessPeekRequest(ConnectionInfo* ci, char isHit,
		PeekRequestOpaque* opaque);
void ProcessOnePeekRequest(PeekRequestInfo* pri, u_char* sha1name, char isHit);
int ProcessPeekResponse(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessPutRequest(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessPutResponse(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessNameAck(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessCloseConnection(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessRawMessage(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessOneRabinChunking(ConnectionInfo* ci, char* psrc,
		int buf_len, char** msg, unsigned int* msg_len);
int ProcessOneRabinChunking2(ConnectionInfo* ci, char* psrc,
		int buf_len, char** msg, unsigned int* msg_len);
int ProcessUserData(ConnectionInfo* ci, int forceProcess);
int ProcessControlData(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessChunkData(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessNATResponse(char* buffer, int buf_len);
int ProcessCacheResponse(ConnectionInfo* ci, char* buffer, int buf_len);
int ProcessDnsResponse(ConnectionInfo* ci, char* buffer, int buf_len);
void PostProcessCachePUT(CacheTransactionInfo* cci);
void PostProcessCacheGET(CacheTransactionInfo* cci, char isHit,
		char* block, int block_length);
void PostProcessCachePEEK(CacheTransactionInfo* cci, char isHit);
void ProcessExceptionalCacheResponse(ConnectionInfo* ci, char* buffer,
		int buf_len);
void PrintProxStat();
void AdjustReadBuffer(ConnectionInfo* ci, int num_processed);
ConnectionInfo* GetDestinationConnection(struct hashtable* ht,
		unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport);
void InitializeProxy(int debug, char* proxconf, char* peerconf);
void CheckTimerEvents();
void InitUserConnection(ConnectionInfo* ci, char isHttpProxy);
void OpenUserConnection(ConnectionInfo* ci);
void SetPersistReadEvent(int fd);
int ScheduleNameDelivery();
int CleanUpClosedConnections();
void ProcessPutMessageQueue();
int QueueControlData(UserConnectionInfo* uci, char* msg, int msg_len,
		int streamID);
void UpdatePeekResult(ReassembleInfo* ri, ChunkNameInfo* cni, char isHit,
		char hasContent);
void DistributePutRequest(u_char* sha1name, char* block, int length,
		int dstPxyId);
void CheckPeek(ReassembleInfo* ri);
int WaproxMain(int debug, char* proxconf, char* peerconf);

/* callback functions */
void SocketAcceptCB(int fd, short event, void *arg);
void TimerCB(int fd, short event, void *arg);
void PingCB(int fd, short event, void *arg);
void BufferTimeoutCB(int fd, short event, void *arg);
void EventReadCB(int fd, short event, void *arg);
void Write(ConnectionInfo* ci, char eventTriggered);
int ActualWrite(ConnectionInfo* ci, char* bufSend,
		unsigned int lenSend, int streamID);
void PostWrite(ConnectionInfo* ci, unsigned int bytesSent, char eventTriggered);

#endif /*__WAPROX_H__*/
