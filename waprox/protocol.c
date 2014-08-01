#include "protocol.h"
#include "util.h"
#include <arpa/inet.h>
#include <assert.h>

/*-----------------------------------------------------------------*/
void PrintControlHeader(WAControlHeader* hdr)
{
	fprintf(stderr, "[WAControlHeader]\n");
	fprintf(stderr, "magic: %02X\n", hdr->magic);
	fprintf(stderr, "msgType: %c\n", hdr->msgType);
	PrintInetAddress(ntohl(hdr->connID.srcIP),
			ntohs(hdr->connID.srcPort),
			ntohl(hdr->connID.dstIP),
			ntohs(hdr->connID.dstPort));
	fprintf(stderr, "length: %d\n", ntohl(hdr->length));
}
/*-----------------------------------------------------------------*/
void PrintChunkHeader(WAChunkHeader* hdr)
{
	fprintf(stderr, "[WAChunkHeader]\n");
	fprintf(stderr, "magic: %02X\n", hdr->magic);
	fprintf(stderr, "msgType: %c\n", hdr->msgType);
	fprintf(stderr, "length: %d\n", ntohl(hdr->length));
}
/*-----------------------------------------------------------------*/
void FillControlHeader(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned dport, char* buffer,
		char type, unsigned int length)
{
	assert(sip != 0 && sport != 0 && dip != 0 && dport != 0);
	WAControlHeader* msg = (WAControlHeader*)buffer;
	msg->magic = MAGIC_WAHEADER;
	msg->msgType = type;
	msg->length = htonl(length);

	/* fill up connection ID */
	msg->connID.srcIP = htonl(sip);
	msg->connID.srcPort = htons(sport);
	msg->connID.dstIP = htonl(dip);
	msg->connID.dstPort = htons(dport);
}
/*-----------------------------------------------------------------*/
void FillChunkHeader(char* buffer, char type, unsigned int length)
{
	WAChunkHeader* msg = (WAChunkHeader*)buffer;
	msg->magic = MAGIC_WAHEADER;
	msg->msgType = type;
	msg->length = htonl(length);
}
/*-----------------------------------------------------------------*/
void BuildWAOpenConnection(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		char** buffer, unsigned int* len)
{
	*buffer = malloc(sizeof(WAOpenConnection));
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	WAOpenConnection* msg = (WAOpenConnection*)(*buffer);
	msg->magic = MAGIC_WAOPENCONNECTION;
	*len = sizeof(WAOpenConnection);
	FillControlHeader(sip, sport, dip, dport, *buffer,
			TYPE_WAOPENCONNECTION, *len);
}
/*-----------------------------------------------------------------*/
void BuildWANameTreeDelivery(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		char** buffer, unsigned int* len, int numNames,
		char* pTree, unsigned int lenTree)
{
	assert(lenTree > 0);
	assert(pTree != NULL);
	assert(*buffer == NULL);
	assert(*len == 0);
	*buffer = malloc(sizeof(WANameDelivery) + sizeof(WAOneTree)
			+ lenTree);
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WANameDelivery */
	WANameDelivery* msg = (WANameDelivery*)(*buffer);
	msg->magic = MAGIC_WANAMEDELIVERY;
	msg->numTrees = htonl(1);
	WAOneTree* tree = (WAOneTree*)(*buffer + sizeof(WANameDelivery));
	tree->numNames = htonl(numNames);
	memcpy(*buffer + sizeof(WANameDelivery) + sizeof(WAOneTree),
			pTree, lenTree);
	*len = sizeof(WANameDelivery) + sizeof(WAOneTree) + lenTree;

	/* fill up header */
	FillControlHeader(sip, sport, dip, dport, *buffer,
			TYPE_WANAMEDELIVERY, *len);
}
/*-----------------------------------------------------------------*/
void BuildWANameTreeDelivery2(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		char** buffer, unsigned int* len,
		int numNames, OneChunkName* pNames, char numLevels)
{
	assert(pNames != NULL);
	assert(*buffer == NULL);
	assert(*len == 0);
	
	/* allocate buffer enough here */
	int lenTree = GetAcutalTreeSize(pNames, numNames);
	lenTree += sizeof(WANameDelivery) + sizeof(WAOneTree);
	*buffer = malloc(lenTree);
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WANameDelivery */
	WANameDelivery* msg = (WANameDelivery*)(*buffer);
	msg->magic = MAGIC_WANAMEDELIVERY;
	msg->numTrees = htonl(1);
	WAOneTree* tree = (WAOneTree*)(*buffer + sizeof(WANameDelivery));
	tree->numNames = htonl(numNames);
	tree->numLevels = numLevels;
	if (numNames == 1) {
		assert(numLevels == 1);
	}

	int i;
	int actualLen = sizeof(WANameDelivery) + sizeof(WAOneTree);
	for (i = 0; i < numNames; i++) {
		/* put type first */
		CopyToMessage(*buffer, &actualLen, &(pNames[i].type),
				sizeof(pNames[i].type));

		if (pNames[i].type == chunk_name) {
			/* put length, name */
			CopyToMessage(*buffer, &actualLen, &(pNames[i].length),
					sizeof(pNames[i].length));
			CopyToMessage(*buffer, &actualLen, pNames[i].sha1name,
					SHA1_LEN);
		}
		else if (pNames[i].type == chunk_both) {
			/* put length, name, content */
			CopyToMessage(*buffer, &actualLen, &(pNames[i].length),
					sizeof(pNames[i].length));
			CopyToMessage(*buffer, &actualLen, pNames[i].sha1name,
					SHA1_LEN);
		}
		else if (pNames[i].type == chunk_raw) {
			/* put length, content */
			CopyToMessage(*buffer, &actualLen, &(pNames[i].length),
					sizeof(pNames[i].length));
			assert(pNames[i].pChunk != NULL);
			CopyToMessage(*buffer, &actualLen, pNames[i].pChunk,
					sizeof(pNames[i].length));
		}
		else if (pNames[i].type == chunk_index) {
			/* put rindex */
			CopyToMessage(*buffer, &actualLen, &(pNames[i].rindex),
					sizeof(pNames[i].rindex));
		}
		else {
			assert(FALSE);
		}
		assert(actualLen <= lenTree);
	}
	assert(actualLen == lenTree);
	*len = actualLen;

	/* fill up header */
	FillControlHeader(sip, sport, dip, dport, *buffer,
			TYPE_WANAMEDELIVERY, *len);
}
/*-----------------------------------------------------------------*/
void BuildWANameTreeDelivery3(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		char** buffer, unsigned int* len, char* psrc,
		OneChunkName* pNames, CandidateList* candidateList,
		int numCandidates, int newmsg_len)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	
	/* allocate buffer enough here */
	*buffer = malloc(sizeof(WANameDelivery) + sizeof(WAOneTree) +
			newmsg_len);
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WANameDelivery */
	WANameDelivery* msg = (WANameDelivery*)(*buffer);
	msg->magic = MAGIC_WANAMEDELIVERY;
	msg->numTrees = htonl(1);
	WAOneTree* tree = (WAOneTree*)(*buffer + sizeof(WANameDelivery));
	tree->numNames = htonl(numCandidates);
	tree->numLevels = 1;
	int actualLen = sizeof(WANameDelivery) + sizeof(WAOneTree);
	int offset = 0;
	OneCandidate* walk;
	TAILQ_FOREACH(walk, candidateList, list) {
		/* put name, length, and cache hit/miss */
		assert(offset == walk->offset);
		WAOneChunk* oneChunk = (WAOneChunk*)(*buffer + actualLen);
		memcpy(oneChunk->sha1name, pNames[walk->index].sha1name,
				SHA1_LEN);
		oneChunk->length = htonl(walk->length);
		oneChunk->hasContent = !walk->isHit;
		actualLen += sizeof(WAOneChunk);

		if ((walk->isHit == FALSE) || (walk->length
				<= WAPROX_MIN_CHUNK_SIZE)) {
			/* MISS: this is a new one, or too small chunk.
			   put the content as well */
			memcpy(*buffer + actualLen, psrc + offset,
					walk->length);
			actualLen += walk->length;
			assert(VerifySHA1((const byte*)psrc + offset,
						walk->length,
						pNames[walk->index].sha1name)
					== TRUE);
			oneChunk->hasContent = TRUE;
		}
		offset += walk->length;
	}

	*len = actualLen;

	/* fill up header */
	FillControlHeader(sip, sport, dip, dport, *buffer,
			TYPE_WANAMEDELIVERY, *len);
}
/*-----------------------------------------------------------------*/
void BuildWABodyRequest(in_addr_t orgPxyAddr, char** buffer,
		unsigned int* len, u_char* sha1name, int length)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	*buffer = malloc(sizeof(WABodyRequest) + sizeof(WAOneRequest));
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WABodyRequest */
	WABodyRequest* msg = (WABodyRequest*)(*buffer);
	msg->magic = MAGIC_WABODYREQUEST;
	msg->numRequests = htonl(1);
	WAOneRequest* req = (WAOneRequest*)(*buffer + sizeof(WABodyRequest));
	memcpy(&req->orgPxyAddr, &orgPxyAddr, sizeof(in_addr_t));
	memcpy(req->sha1name, sha1name, SHA1_LEN);
	req->length = htonl(length);

	/* fill up header */
	*len = sizeof(WABodyRequest) + sizeof(WAOneRequest);
	FillChunkHeader(*buffer, TYPE_WABODYREQUEST, *len);
}
/*-----------------------------------------------------------------*/
void BuildWABodyResponse(char** buffer, unsigned int* len,
		u_char* sha1name, char* block, unsigned int block_len)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	assert(block_len > 0);
	*buffer = malloc(sizeof(WABodyResponse) +
			sizeof(WAOneResponse) + block_len);
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WABodyResponse */
	WABodyResponse* msg = (WABodyResponse*)(*buffer);
	msg->magic = MAGIC_WABODYRESPONSE;
	msg->numResponses = htonl(1);

	/* fill up WAOneResponse */
	WAOneResponse* resp = (WAOneResponse*)(*buffer +
			sizeof(WABodyResponse));
	memcpy(resp->sha1name, sha1name, SHA1_LEN);
	resp->length = htonl(block_len);
	memcpy(*buffer + sizeof(WABodyResponse) + sizeof(WAOneResponse),
			block, block_len);

	/* note: we still include block_len */
	*len = sizeof(WABodyResponse) + sizeof(WAOneResponse) + block_len;

	/* fill up header */
	FillChunkHeader(*buffer, TYPE_WABODYRESPONSE, *len);
}
/*-----------------------------------------------------------------*/
void BuildWAPeekRequest(char** buffer, unsigned int* len, u_char* sha1name)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	*buffer = malloc(sizeof(WAPeekRequest) + sizeof(WAOnePeekRequest));
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WAPeekRequest */
	WAPeekRequest* msg = (WAPeekRequest*)(*buffer);
	msg->magic = MAGIC_WAPEEKREQUEST;
	msg->numRequests = htonl(1);
	WAOnePeekRequest* req = (WAOnePeekRequest*)(*buffer +
			sizeof(WAPeekRequest));
	memcpy(req->sha1name, sha1name, SHA1_LEN);

	/* fill up header */
	*len = sizeof(WAPeekRequest) + sizeof(WAOnePeekRequest);
	FillChunkHeader(*buffer, TYPE_WAPEEKREQUEST, *len);
}
/*-----------------------------------------------------------------*/
void BuildWAPeekResponse(char** buffer, unsigned int* len, u_char* sha1name,
		char isHit)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	*buffer = malloc(sizeof(WAPeekResponse) + sizeof(WAOnePeekResponse));
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WAPeekResponse */
	WAPeekResponse* msg = (WAPeekResponse*)(*buffer);
	msg->magic = MAGIC_WAPEEKRESPONSE;
	msg->numResponses = htonl(1);

	/* fill up WAOnePeekResponse */
	WAOnePeekResponse* resp = (WAOnePeekResponse*)(*buffer +
			sizeof(WAPeekResponse));
	memcpy(resp->sha1name, sha1name, SHA1_LEN);
	resp->isHit = isHit;

	*len = sizeof(WABodyResponse) + sizeof(WAOneResponse);

	/* fill up header */
	FillChunkHeader(*buffer, TYPE_WAPEEKRESPONSE, *len);
}
/*-----------------------------------------------------------------*/
void BuildWAPutRequest(char** buffer, unsigned int* len, u_char* sha1name,
		char* block, unsigned int block_len)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	assert(block_len > 0);
	*buffer = malloc(sizeof(WAPutRequest) +
			sizeof(WAOnePutRequest) + block_len);
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WAPutRequest */
	WAPutRequest* msg = (WAPutRequest*)(*buffer);
	msg->magic = MAGIC_WAPUTREQUEST;
	msg->numRequests = htonl(1);

	/* fill up WAOnePutRequest*/
	WAOnePutRequest* req = (WAOnePutRequest*)(*buffer +
			sizeof(WAPutRequest));
	memcpy(req->sha1name, sha1name, SHA1_LEN);
	req->length = htonl(block_len);
	memcpy(*buffer + sizeof(WAPutRequest) + sizeof(WAOnePutRequest),
			block, block_len);

	/* note: we still include block_len */
	*len = sizeof(WAPutRequest) + sizeof(WAOnePutRequest) + block_len;

	/* fill up header */
	FillChunkHeader(*buffer, TYPE_WAPUTREQUEST, *len);
}
/*-----------------------------------------------------------------*/
void BuildWAPutResponse(char** buffer, unsigned int* len, u_char* sha1name)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	*buffer = malloc(sizeof(WAPutResponse) + sizeof(WAOnePutResponse));
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WAPutResponse */
	WAPutResponse* msg = (WAPutResponse*)(*buffer);
	msg->magic = MAGIC_WAPUTRESPONSE;
	msg->numResponses = htonl(1);

	/* fill up WAOnePutResponse */
	WAOnePutResponse* resp = (WAOnePutResponse*)(*buffer +
			sizeof(WAPutResponse));
	memcpy(resp->sha1name, sha1name, SHA1_LEN);

	*len = sizeof(WAPutResponse) + sizeof(WAOnePutResponse);

	/* fill up header */
	FillChunkHeader(*buffer, TYPE_WAPUTRESPONSE, *len);
}
/*-----------------------------------------------------------------*/
void BuildWANameAck(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport, char** buffer,
		unsigned int* len, unsigned int ack)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	*buffer = malloc(sizeof(WANameAck));
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WANameAck */
	WANameAck* msg = (WANameAck*)(*buffer);
	msg->magic = MAGIC_WANAMEACK;
	msg->ack = htonl(ack);
	*len = sizeof(WANameAck);

	/* fill up header */
	FillControlHeader(sip, sport, dip, dport, *buffer,
			TYPE_WANAMEACK, *len);
}
/*-----------------------------------------------------------------*/
void BuildWACloseConnection(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport,
		unsigned int bytesTX, char** buffer, unsigned int* len)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	*buffer = malloc(sizeof(WACloseConnection));
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	WACloseConnection* msg = (WACloseConnection*)(*buffer);
	msg->magic = MAGIC_WACLOSECONNECTION;
	msg->bytesTX = htonl(bytesTX);
	*len = sizeof(WACloseConnection);
	FillControlHeader(sip, sport, dip, dport, *buffer,
			TYPE_WACLOSECONNECTION, *len);
}
/*-----------------------------------------------------------------*/
void BuildWAPing(char isPing, in_addr_t* serverAddr, struct timeval* timestamp,
		unsigned int networkBytes, unsigned int diskChunks,
		char** buffer, unsigned int* len)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	assert(isPing == 1 || isPing == 0);
	*buffer = malloc(sizeof(WAPing));
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}
	
	WAPing* msg = (WAPing*)(*buffer);
	msg->magic = MAGIC_WAPING;
	msg->isPing = isPing;
	memcpy(&msg->netAddr, serverAddr, sizeof(in_addr_t));
	memcpy(&msg->timestamp, timestamp, sizeof(struct timeval));
	msg->networkPendingBytes = htonl(networkBytes);
	msg->diskPendingChunks = htonl(diskChunks);
	*len = sizeof(WAPing);
}
/*-----------------------------------------------------------------*/
void BuildWARawMessage(unsigned int sip, unsigned short sport,
		unsigned int dip, unsigned short dport, char** buffer,
		unsigned int* len, char* message, unsigned int msg_len)
{
	assert(*buffer == NULL);
	assert(*len == 0);
	assert(msg_len > 0);
	*buffer = malloc(sizeof(WARawMessage) + msg_len);
	if (*buffer == NULL) {
		NiceExit(-1, "buffer memory allocation failed\n");
	}

	/* fill up WARawMessage */
	WARawMessage* msg = (WARawMessage*)(*buffer);
	msg->magic = MAGIC_WARAWMESSAGE;
	msg->msg_len = htonl(msg_len);
	memcpy(*buffer + sizeof(WARawMessage), message, msg_len);
	*len = sizeof(WARawMessage) + msg_len;

	/* fill up header */
	FillControlHeader(sip, sport, dip, dport, *buffer,
			TYPE_WARAWMESSAGE, *len);
}
/*-----------------------------------------------------------------*/
char IsSameMessageType(char* msg1, int len1, char* msg2, int len2)
{
	WAChunkHeader* hdr1 = (WAChunkHeader*)msg1;
	assert(ntohl(hdr1->length) == len1);
	WAChunkHeader* hdr2 = (WAChunkHeader*)msg2;
	assert(ntohl(hdr2->length) == len2);
	if ((memcmp(hdr1, hdr2, sizeof(WAChunkHeader)) == 0)
			&& (hdr1->magic == hdr2->magic)) {
		return TRUE;
	}
	else {
		return FALSE;
	}
}
/*-----------------------------------------------------------------*/
int MergeTwoNameDelivery(char** msg1, int* len1, char* msg2, int len2)
{
	if (IsSameMessageType(*msg1, *len1, msg2, len2) == TRUE) {
		/* we can merge it */
		WANameDelivery* name1 = (WANameDelivery*)*msg1;
		WANameDelivery* name2 = (WANameDelivery*)msg2;
		assert(ntohl(name1->numTrees) >= 1);
		assert(ntohl(name2->numTrees) >= 1);
		int bytesToAdd = len2 - sizeof(WANameDelivery);
		assert(bytesToAdd > 0);
		*msg1 = realloc(*msg1, *len1 + bytesToAdd);
		if (msg1 == NULL) {
			NiceExit(-1, "realloc failed\n");
		}
		name1 = (WANameDelivery*)*msg1;
		memcpy(*msg1 + *len1, msg2 + sizeof(WANameDelivery),
				bytesToAdd);
		name1->numTrees = htonl(ntohl(name1->numTrees)
				+ ntohl(name2->numTrees));
		name1->hdr.length = htonl(*len1 + bytesToAdd);
		*len1 += bytesToAdd;
		return bytesToAdd;
	}
	else {
		return 0;
	}
}
/*-----------------------------------------------------------------*/
int MergeTwoBodyRequest(char** msg1, int* len1, char* msg2, int len2)
{
	if (IsSameMessageType(*msg1, *len1, msg2, len2) == TRUE) {
		/* we can merge it */
		WABodyRequest* req1 = (WABodyRequest*)*msg1;
		WABodyRequest* req2 = (WABodyRequest*)msg2;
		assert(ntohl(req1->numRequests) >= 1);
		assert(ntohl(req2->numRequests) >= 1);
		int bytesToAdd = len2 - sizeof(WABodyRequest);
		assert(bytesToAdd > 0);
		*msg1 = realloc(*msg1, *len1 + bytesToAdd);
		if (msg1 == NULL) {
			NiceExit(-1, "realloc failed\n");
		}
		req1 = (WABodyRequest*)*msg1;
		memcpy(*msg1 + *len1, msg2 + sizeof(WABodyRequest),
				bytesToAdd);
		req1->numRequests = htonl(ntohl(req1->numRequests)
				+ ntohl(req2->numRequests));
		req1->hdr.length = htonl(*len1 + bytesToAdd);
		*len1 += bytesToAdd;
		return bytesToAdd;
	}
	else {
		return 0;
	}
}
/*-----------------------------------------------------------------*/
int MergeTwoBodyResponse(char** msg1, int* len1, char* msg2, int len2)
{
	if (IsSameMessageType(*msg1, *len1, msg2, len2) == TRUE) {
		/* we can merge it */
		WABodyResponse* resp1 = (WABodyResponse*)*msg1;
		WABodyResponse* resp2 = (WABodyResponse*)msg2;
		assert(ntohl(resp1->numResponses) >= 1);
		assert(ntohl(resp2->numResponses) >= 1);
		int bytesToAdd = len2 - sizeof(WABodyResponse);
		assert(bytesToAdd > 0);
		*msg1 = realloc(*msg1, *len1 + bytesToAdd);
		if (msg1 == NULL) {
			NiceExit(-1, "realloc failed\n");
		}
		resp1 = (WABodyResponse*)*msg1;
		memcpy(*msg1 + *len1, msg2 + sizeof(WABodyResponse),
				bytesToAdd);
		resp1->numResponses = htonl(ntohl(resp1->numResponses)
				+ ntohl(resp2->numResponses));
		resp1->hdr.length = htonl(*len1 + bytesToAdd);
		*len1 += bytesToAdd;
		return bytesToAdd;
	}
	else {
		return 0;
	}
}
/*-----------------------------------------------------------------*/
int VerifyControlHeader(char* buffer, unsigned int buf_len, char* msgType)
{
	if (buf_len < sizeof(WAControlHeader)) {
		return 0;
	}

	WAControlHeader* hdr = (WAControlHeader*)buffer;
	if (hdr->magic != MAGIC_WAHEADER) {
		TRACE("invalid WAControlHeader magic: %d bytes\n", buf_len);
		PrintControlHeader(hdr);
		return -1;
	}

	if (buf_len < ntohl(hdr->length)) {
		return 0;
	}

	if (hdr->msgType == TYPE_WAOPENCONNECTION) {
		*msgType = TYPE_WAOPENCONNECTION;
		return 1;
	}
	else if (hdr->msgType == TYPE_WANAMEDELIVERY) {
		*msgType = TYPE_WANAMEDELIVERY;
		return 1;
	}
	else if (hdr->msgType == TYPE_WACLOSECONNECTION) {
		*msgType = TYPE_WACLOSECONNECTION;
		return 1;
	}
	else if (hdr->msgType == TYPE_WANAMEACK) {
		*msgType = TYPE_WANAMEACK;
		return 1;
	}
	else if (hdr->msgType == TYPE_WARAWMESSAGE) {
		*msgType = TYPE_WARAWMESSAGE;
		return 1;
	}
	else {
		TRACE("protocol error: unknown message type\n");
		return -1;
	}
}
/*-----------------------------------------------------------------*/
int VerifyChunkHeader(char* buffer, unsigned int buf_len, char* msgType)
{
	if (buf_len < sizeof(WAChunkHeader)) {
		return 0;
	}

	WAChunkHeader* hdr = (WAChunkHeader*)buffer;
	if (hdr->magic != MAGIC_WAHEADER) {
		TRACE("invalid WAChunkHeader magic: %d bytes\n", buf_len);
		PrintChunkHeader(hdr);
		return -1;
	}

	if (buf_len < ntohl(hdr->length)) {
		return 0;
	}

	if (hdr->msgType == TYPE_WABODYREQUEST) {
		*msgType = TYPE_WABODYREQUEST;
		return 1;
	}
	else if (hdr->msgType == TYPE_WABODYRESPONSE) {
		*msgType = TYPE_WABODYRESPONSE;
		return 1;
	}
	else if (hdr->msgType == TYPE_WAPEEKREQUEST) {
		*msgType = TYPE_WAPEEKREQUEST;
		return 1;
	}
	else if (hdr->msgType == TYPE_WAPEEKRESPONSE) {
		*msgType = TYPE_WAPEEKRESPONSE;
		return 1;
	}
	else if (hdr->msgType == TYPE_WAPUTREQUEST) {
		*msgType = TYPE_WAPUTREQUEST;
		return 1;
	}
	else if (hdr->msgType == TYPE_WAPUTRESPONSE) {
		*msgType = TYPE_WAPUTRESPONSE;
		return 1;
	}
	else {
		TRACE("protocol error: unknown message type\n");
		return -1;
	}
}
/*-----------------------------------------------------------------*/
int VerifyPing(char* buffer, unsigned int buf_len, char* msgType)
{
	if (buf_len < sizeof(WAPing)) {
		return 0;
	}

	WAPing* msg = (WAPing*)buffer;
	if (msg->magic != MAGIC_WAPING) {
		return -1;
	}

	if (!(msg->isPing == 1 || msg->isPing == 0)) {
		return -1;
	}

	*msgType = TYPE_WAPING;
	return 1;
}
/*-----------------------------------------------------------------*/
int GetCandidateMessageLength(CandidateList* candidateList, int* outNumItems)
{
	/* calculate the total message length */
	OneCandidate* walk;
	int offset = 0;
	int newmsg_len = 0;
	int items = 0;
	TAILQ_FOREACH(walk, candidateList, list) {
		assert(offset == walk->offset);
		offset += walk->length;
		newmsg_len += sizeof(WAOneChunk);
		if ((walk->isHit == FALSE) || (walk->length
				<= WAPROX_MIN_CHUNK_SIZE)) {
			newmsg_len += walk->length;
		}
		items++;
	}
	*outNumItems = items;
	return newmsg_len;
}
/*-----------------------------------------------------------------*/
