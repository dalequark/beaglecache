#ifndef __WAPROX_REASSEMBLE_H__
#define __WAPROX_REASSEMBLE_H__

#include "czipdef.h"
#include "queue.h"

/* forward decl. */
typedef struct ConnectionInfo ConnectionInfo;
typedef struct ReassembleInfo ReassembleInfo;
typedef struct ChunkTreeInfo ChunkTreeInfo;

typedef struct ChunkNameInfo {
	char isHit;
	char inMemory;
	int length;
	int peerIdx;
	char isNetwork;
	u_char sha1name[SHA1_LEN];
	ReassembleInfo* ri; /* parent pointer */
	TAILQ_ENTRY(ChunkNameInfo) nameList;
} ChunkNameInfo;
typedef TAILQ_HEAD(ChunkNameList, ChunkNameInfo) ChunkNameList; 

struct ReassembleInfo {
	unsigned int seq_no;
	unsigned int numNames;
	unsigned int numPeekResolved;
	char getSent;
	ConnectionInfo* source_ci;

	/* TODO: we want to eliminate cti eventually */
	ChunkTreeInfo* cti;
	ChunkNameList nameList;
	TAILQ_ENTRY(ReassembleInfo) reassembleList;
	TAILQ_ENTRY(ReassembleInfo) readyList;
};
typedef TAILQ_HEAD(ReassembleList, ReassembleInfo) ReassembleList;

ReassembleInfo* CreateNewReassembleInfo(ConnectionInfo* source_ci,
		unsigned int seq_no, ChunkTreeInfo* cti);
ReassembleInfo* GetReassembleInfo(ReassembleList* rl, unsigned int seq_no);
void DestroyReassembleList(ReassembleList* rl);
void DestroyReassembleInfo(ReassembleInfo* ri);
void ReconstructOriginalData(ConnectionInfo* target_ci);
int ReconstructOneChunkTree(ConnectionInfo* target_ci,
		ReassembleInfo* ri);
ChunkNameInfo* AppendChunkName(ConnectionInfo* ci, ReassembleInfo* ri,
		u_char* sha1name, unsigned int length, char isHit);

#endif /*__WAPROX_REASSEMBLE_H__*/
