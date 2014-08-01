#ifndef __WAPROX_MRC_TREE_H__
#define __WAPROX_MRC_TREE_H__

#include "czipdef.h"
#include "rabinfinger2.h"
#include "queue.h"
#include "reassemble.h"

/* this should be larger than the Rabin window size,
   otherwise MRC may not be deterministic */
#define WAPROX_MIN_CHUNK_SIZE	(30)
#define WAPROX_RABIN_WINDOW	(16)
#define WAPROX_MRC_MIN		(0)
#define WAPROX_MRC_MAX		(128*1024)

typedef enum chunkType {
	chunk_name,
	chunk_both,
	chunk_index,
	chunk_raw
} chunkType;

typedef struct PutMessageInfo {
	char* msg;
	int msg_len;
	int dstPxyId;
	TAILQ_ENTRY(PutMessageInfo) messageList;
} PutMessageInfo;
typedef TAILQ_HEAD(PutMessageList, PutMessageInfo) PutMessageList;
extern PutMessageList g_PutMessageList;
extern int g_NumPutMessages;
extern unsigned int g_CurPutQueueSize;
extern unsigned int g_TotalPutDiscarded;

typedef struct OneChunkName {
	/* mandatory info - sent over network */
	chunkType type;
	int length;
	u_char sha1name[SHA1_LEN];
	char* pChunk;
	char freeChunk;
	int rindex;

	/* auxiliary info - internal only */
	int index;
	int parentIdx;
	int level;

	/* can be either used as name hint or put hint */
	char isHit;

	/* if sha1 is computed or not */
	char isSha1Ready;

	/* is candidate or not */
	char isCandidate;

	/* offset for sorting */
	int offset;

	/* where this chunk is from? */
	char fromPeer;
} OneChunkName;

typedef struct MRCPerLevelInfo {
	int numNames;
	int firstIndex;
} MRCPerLevelInfo;

struct ChunkTreeInfo {
	/* chunk tree sequence number for reassemlby */
	int seq_no;

	/* resolve information */
	int curResolvingLevel;
	int numChunkUnresolved;
	char* chunkState;

	/* chunk tree root */
	int numNames;
	char numLevels;
	OneChunkName* pNames;

	/* per level info */
	MRCPerLevelInfo* perLevelInfo;

	/* the leaf level of MRC tree */
	int lowLevel;

	/* various stats */
	int numMissingChunks;
	int bytesMissingChunks;
	int numLocalHits;
	int bytesHits;
	int numLocalMisses;
};

typedef struct PerLevelStats {
	int bytesHits;
	int numHits;
} PerLevelStats;

typedef struct OneCandidate {
	int index;
	int offset;
	int length;
	char isHit;
	u_char sha1name[SHA1_LEN];
	TAILQ_ENTRY(OneCandidate) list;
	TAILQ_ENTRY(OneCandidate) deadList;
} OneCandidate;

typedef TAILQ_HEAD(CandidateList, OneCandidate) CandidateList;
	
extern int g_maxNames;
extern int g_maxLevel;
extern PerLevelStats* g_PerLevelStats;

typedef void (*ChunkCallBack)(u_char* digest, char* buffer, int buf_len);
typedef int (*LookupCallBack)(u_char* digest);

/* n-ary MRC tree */
#if 0
void GenerateMRC2(MrcCtx* mrc_ctx, int level, int chunkSize, char* buffer,
		int buf_len, int index, OneChunkName* names, int* numNames,
		ChunkCallBack cb);
void GenerateMultiResolutionChunks(char* buffer, int buf_len,
		int level, OneChunkName* names, int index,
		int* outNumNames, ChunkCallBack cb);
int GetRootChunkLevel(char* buffer, int buf_len);
#endif
void PrintChunkTree(OneChunkName* names);
#if 0
void PrintChunkTree2(OneChunkName* names, int numNames);
int GetTreeLevel(int index);
int GetParent(int index);
int GetFirstIndex(int level);
#endif
int GetTreeLevel2(OneChunkName* pNames, int index);
int GetParent2(OneChunkName* pNames, int index);
int GetFirstIndex2(struct ChunkTreeInfo* cti, int level);
int CompareChunkIndex(const void *p1, const void *p2);
void InitMRCParams();
void InitOneChunk(OneChunkName* name);
#if 0
OneChunkName* CreateNewMRCTree(OneChunkName* names, int numNames);
OneChunkName* MakePreMRCTree(MrcCtx* mrc_ctx, char* buffer, int buf_len,
		int numLevels, int* outNumNames, ChunkCallBack cb);
#endif
struct ChunkTreeInfo* CreateNewChunkTreeInfo(int numNames, char numLevels);
void InitChunkTreeInfo(struct ChunkTreeInfo* pCTInfo, OneChunkName* names,
		int numNames, int seq_no);
void DestroyChunkTreeInfo(struct ChunkTreeInfo* cti);
int* ResolveOneLevel(struct ChunkTreeInfo* cti, int level);
void UpdateLookupResult(struct ChunkTreeInfo* cti, char isHit, int newIdx);
void ResolveMRCTreeInMemory(struct ChunkTreeInfo* cti, LookupCallBack cb);
void GetChunkNameList(int level, char* states, int index,
		int* resultArray, int* resultIdx, char maxLevel);
void GetChunkNameList2(struct ChunkTreeInfo* cti, int* resultArray,
		int* resultIdx);
int* MissingChunkPolicy(struct ChunkTreeInfo* cti, int* outNumEntries);
#if 0
void SamplePolicy(struct ChunkTreeInfo* cti, char* block);
#endif
void PrintChunkTreeCache(struct ChunkTreeInfo* cti);
void PutMissingChunks(struct ChunkTreeInfo* cti, char* buffer, int buf_len,
		ChunkCallBack cb);
void QueuePutMessage(RabinCtx* rctx, char* buffer, int buf_len, int dstPxyId,
		int seq_no, ReassembleInfo* ri);
void CopyToMessage(char* buffer, int* buflen, void* msg, int msglen);
void CopyFromMessage(void* msg, int msglen, void* buffer, int* offset);
int GetAcutalTreeSize(OneChunkName* pNames, int numNames);
struct ChunkTreeInfo* DecodeOneTree(char* buffer, unsigned int buf_len,
			int seq, int* numConsumed);
struct ChunkTreeInfo* DecodeOneTree2(char* buffer, unsigned int buf_len,
			int seq, int* numConsumed);
CandidateList* GetCandidateList(OneChunkName* pNames, MRCPerLevelInfo* pli,
		int maxLevel);
void UpdateCandidateList(OneChunkName* pNames, CandidateList* chunkList,
		int index, char isHit, int offset, int length);
void PutTempCacheCandidateList(CandidateList* chunkList, char* buffer,
		int buflen);
void DumpCandidateList(CandidateList* candidateList);
void FreeCandidateList(CandidateList* candidateList);
OneChunkName* DoOneMRC(RabinCtx* rctx, char* psrc, int buf_len,
		char putTempCache, int* outNumNames, int* outRb_size,
		int* outMaxLevel, MRCPerLevelInfo** pli);
void MakeTree(MRCPerLevelInfo* levInfo, int maxLevel, OneChunkName* pNames,
		int numNames);
OneChunkName* DoPreMRC(RabinCtx* rctx, char* psrc, int buf_len,
		int* outNumNames, int* outRb_size, int* outMaxLevel,
		MRCPerLevelInfo** outPli);
void MRCCalcSHA1(OneChunkName* pNames, int numNames, char putTempCache);
void CheckNameHint(OneChunkName* cur, char isSender);
int ResolveWithHint(OneChunkName* pNames, int numNames, char* buffer,
		int buf_len, MRCPerLevelInfo* pli, int maxLevel, char isSender);
void FlushChildren(OneChunkName* pNames, int* stack, int sp);
CandidateList* GetCandidateList2(OneChunkName* pNames, int numNames,
		MRCPerLevelInfo* pli, int maxLevel);
int CompareOneChunkName(const void *p1, const void *p2);
void ActualPutMissingChunks(char* buffer, int buf_len, int dstPxyId);
	
#endif /*__WAPROX_MRC_TREE_H__*/
