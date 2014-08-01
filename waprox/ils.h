#ifndef __WAPROX_ILS_H__
#define __WAPROX_ILS_H__

#include "reassemble.h"

#define MAX_ILS_QUEUE	(65536)

typedef struct ILSQueue {
	ChunkNameInfo* q[MAX_ILS_QUEUE];
	unsigned int qlen;
	unsigned int totBytes;
	float latency;
} ILSQueue;

typedef enum ChunkRequestType {
	cr_local,
	cr_peer,
	cr_origin
} ChunkRequestType;

extern int PerChunkResponseOverhead;

void ILSDump(ILSQueue* queue);
void ILSReset(ILSQueue* queue);
void ILSUpdateDiskLatency(ILSQueue* queue, unsigned int pendingChunks);
void ILSUpdateNetworkLatency(ILSQueue* queue, unsigned int pendingBytes);
void ILSEnqueue(ILSQueue* queue, ChunkNameInfo* cni, int overhead);
ChunkNameInfo* ILSDequeue(ILSQueue* queue);
void InitILS();
int FindBestSchedule();
int PopulateILSQueue();
int IntelligentLoadShedding();
void SendLocalChunkRequest(ChunkNameInfo* cni, int dstPxyIdx);
void SendRemoteChunkRequest(ChunkNameInfo* cni, int peerIdx, char isNetwork);
ChunkRequestType SendChunkRequest(ChunkNameInfo* cni, char forceNetwork);

#endif /*__WAPROX_ILS_H__*/
