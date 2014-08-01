#ifndef __WAPROX_PROTOCOL_H__
#define __WAPROX_PROTOCOL_H__

#include "czipdef.h"
#include "mrc_tree.h"
#include "util.h"

/*---------------------------------------------------------------*/
#define MAGIC_WAHEADER		(0x57)

#define MAGIC_WAOPENCONNECTION	(0x70)
#define MAGIC_WANAMEDELIVERY	(0x71)
#define MAGIC_WACLOSECONNECTION	(0x72)
#define MAGIC_WANAMEACK		(0x73)
#define MAGIC_WARAWMESSAGE	(0x74)

#define MAGIC_WABODYREQUEST	(0x80)
#define MAGIC_WABODYRESPONSE	(0x81)
#define MAGIC_WAPEEKREQUEST	(0x82)
#define MAGIC_WAPEEKRESPONSE	(0x83)
#define MAGIC_WAPUTREQUEST	(0x84)
#define MAGIC_WAPUTRESPONSE	(0x85)

#define MAGIC_WAPING		(0x90)
/*---------------------------------------------------------------*/
#define TYPE_WAOPENCONNECTION	('O')
#define TYPE_WANAMEDELIVERY	('N')
#define TYPE_WACLOSECONNECTION	('C')
#define TYPE_WANAMEACK		('A')
#define TYPE_WARAWMESSAGE	('M')

#define TYPE_WABODYREQUEST	('Q')
#define TYPE_WABODYRESPONSE	('R')
#define TYPE_WAPEEKREQUEST	('E')
#define TYPE_WAPEEKRESPONSE	('K')
#define TYPE_WAPUTREQUEST	('U')
#define TYPE_WAPUTRESPONSE	('T')

#define TYPE_WAPING		('P')
/*---------------------------------------------------------------*/
#pragma pack(1)
/*-----------------------------------------------------------------
	<Control Flow>
	'O' for WAOpenConnection
	'N' for WANameDelivery
	'C' for WACloseConnection
	'A' for WANameAck
	'M' for WARawMessage
-----------------------------------------------------------------*/
typedef struct ConnectionID {
	unsigned int srcIP;
	unsigned short srcPort;
	unsigned int dstIP;
	unsigned short dstPort;
} ConnectionID;

typedef struct WAControlHeader {
	unsigned char magic;
	char msgType;
	unsigned int length; /* the length of a whole packet */
	ConnectionID connID;
} WAControlHeader;

typedef struct WAOpenConnection {
	WAControlHeader hdr;
	unsigned char magic;
} WAOpenConnection;

typedef struct WANameDelivery {
	WAControlHeader hdr;
	unsigned char magic;
	int numTrees;
	/* WAOneTree comes here */
} WANameDelivery;

typedef struct WAOneChunk {
	u_char sha1name[SHA1_LEN];
	unsigned int length;
	char hasContent;
} WAOneChunk;

typedef struct WAOneTree {
	char numLevels;
	int numNames;
	/* actual tree comes here */
} WAOneTree;

typedef struct WACloseConnection {
	WAControlHeader hdr;
	unsigned char magic;
	unsigned int bytesTX;
} WACloseConnection;

typedef struct WANameAck {
	WAControlHeader hdr;
	unsigned char magic;
	unsigned int ack;
} WANameAck;

typedef struct WARawMessage {
	WAControlHeader hdr;
	unsigned char magic;
	int msg_len;
	/* message comes here */
} WARawMessage;

/*-----------------------------------------------------------------
	<Chunk Flow>
	'Q' for WABodyRequest
	'R' for WABodyResponse
	'E' for WAPeekRequest
	'K' for WAPeekResponse
	'U' for WAPutRequest
	'T' for WAPutResponse
-----------------------------------------------------------------*/
typedef struct WAChunkHeader {
	unsigned char magic;
	char msgType;
	unsigned int length; /* the length of a whole packet */
} WAChunkHeader;

typedef struct WAOneRequest {
	in_addr_t orgPxyAddr;
	u_char sha1name[SHA1_LEN];
	unsigned int length;
} WAOneRequest;

typedef struct WABodyRequest {
	WAChunkHeader hdr;
	unsigned char magic;
	int numRequests;
	/* WAOneRequest comes here */
} WABodyRequest;

typedef struct WAOneResponse {
	u_char sha1name[SHA1_LEN];
	unsigned int length;
} WAOneResponse;

typedef struct WABodyResponse {
	WAChunkHeader hdr;
	unsigned char magic; 
	int numResponses;
	/* WAOneResponse comes here */
} WABodyResponse;

typedef struct WAOnePeekRequest {
	u_char sha1name[SHA1_LEN];
} WAOnePeekRequest;

typedef struct WAPeekRequest {
	WAChunkHeader hdr;
	unsigned char magic;
	int numRequests;
	/* WAOnePeekRequest comes here */
} WAPeekRequest;

typedef struct WAOnePeekResponse {
	u_char sha1name[SHA1_LEN];
	char isHit;
} WAOnePeekResponse;

typedef struct WAPeekResponse {
	WAChunkHeader hdr;
	unsigned char magic; 
	int numResponses;
	/* WAOnePeekResponse comes here */
} WAPeekResponse;

typedef struct WAPutRequest {
	WAChunkHeader hdr;
	unsigned char magic;
	int numRequests;
} WAPutRequest;

typedef struct WAOnePutRequest {
	u_char sha1name[SHA1_LEN];
	unsigned int length;
} WAOnePutRequest;

typedef struct WAPutResponse {
	WAChunkHeader hdr;
	unsigned char magic; 
	int numResponses;
} WAPutResponse;

typedef struct WAOnePutResponse {
	u_char sha1name[SHA1_LEN];
} WAOnePutResponse;

/*-----------------------------------------------------------------
	<Monitoring Flow>
-----------------------------------------------------------------*/
/* NOTE: it should be less than one UDP packet size */
typedef struct WAPing {
	unsigned char magic;
	char isPing; /* 1 if Ping, 0 if Pong */
	in_addr_t netAddr;
	/*
		cpu load, RTT, memory, etc.
	*/
	struct timeval timestamp; /* for RTT */
	unsigned int networkPendingBytes;
	unsigned int diskPendingChunks;
} WAPing;

#pragma pack()

/* functions */
void PrintControlHeader(WAControlHeader* hdr);
void PrintChunkHeader(WAChunkHeader* hdr);
void FillControlHeader(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned dport, char* buffer,
		char type, unsigned int length);
void FillChunkHeader(char* buffer, char type, unsigned int length);
void BuildWAOpenConnection(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		char** buffer, unsigned int* len);
void BuildWANameTreeDelivery(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		char** buffer, unsigned int* len, int numNames,
		char* pTree, unsigned int lenTree);
void BuildWANameTreeDelivery2(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		char** buffer, unsigned int* len,
		int numNames, OneChunkName* pNames, char numLevels);
void BuildWANameTreeDelivery3(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		char** buffer, unsigned int* len, char* psrc,
		OneChunkName* pNames, CandidateList* candidateList,
		int numCandidates, int newmsg_len);
void BuildWABodyRequest(in_addr_t orgPxyAddr, char** buffer,
		unsigned int* len, u_char* sha1name, int length);
void BuildWABodyResponse(char** buffer, unsigned int* len,
		u_char* sha1name, char* block, unsigned int block_len);
void BuildWAPeekRequest(char** buffer, unsigned int* len, u_char* sha1name);
void BuildWAPeekResponse(char** buffer, unsigned int* len, u_char* sha1name,
		char isHit);
void BuildWAPutRequest(char** buffer, unsigned int* len, u_char* sha1name,
		char* block, unsigned int block_len);
void BuildWAPutResponse(char** buffer, unsigned int* len, u_char* sha1name);
void BuildWACloseConnection(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		unsigned int bytesTX, char** buffer, unsigned int* len);
void BuildWANameAck(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport, char** buffer,
		unsigned int* len, unsigned int advertisedWindow);
void BuildWAPing(char isPing, in_addr_t* serverAddr, struct timeval* timestamp,
		unsigned int networkBytes, unsigned int diskChunks,
		char** buffer, unsigned int* len);
void BuildWARawMessage(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport, char** buffer,
		unsigned int* len, char* message, unsigned int msg_len);
int VerifyControlHeader(char* buffer, unsigned int buf_len, char* msgType);
int VerifyChunkHeader(char* buffer, unsigned int buf_len, char* msgType);
int VerifyPing(char* buffer, unsigned int buf_len, char* msgType);
int MergeTwoNameDelivery(char** msg1, int* len1, char* msg2, int len2);
int MergeTwoBodyRequest(char** msg1, int* len1, char* msg2, int len2);
int MergeTwoBodyResponse(char** msg1, int* len1, char* msg2, int len2);
char IsSameMessageType(char* msg1, int len1, char* msg2, int len2);
int GetCandidateMessageLength(CandidateList* candidateList, int* outNumItems);

#endif /*__WAPROX_PROTOCOL_H__*/
