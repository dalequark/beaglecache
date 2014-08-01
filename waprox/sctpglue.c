#include "sctpglue.h"
#include "waprox.h"
#include "config.h"
#include "peer.h"
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

unsigned short g_SCTP_ControlInstanceName = -1;
unsigned short g_SCTP_ChunkInstanceName = -1;
unsigned short g_SCTP_ClientInstanceName = -1;
static unsigned int g_StreamID = 0;

/*-----------------------------------------------------------------*/
void SctpAddReadOnlyEvent(int fd, sctp_userCallback cb)
{
	/* unregister callback first */
	sctp_unregisterUserCallback(fd);

	/* register read-only callback */
	short event = POLLIN | POLLPRI;
	if (sctp_registerUserCallback(fd, cb, NULL, event) < 0) {
		TRACE("fd=%d\n", fd);
		NiceExit(-1, "sctp register read callback failed\n");
	}

	/* remember current event flag */
	g_MyEventSet[fd].sctp_event = event;
	/*TRACE("[%d] read set\n", fd);*/
}
/*-----------------------------------------------------------------*/
void SctpAddReadEvent(int fd, sctp_userCallback cb)
{
	/* unregister callback first */
	sctp_unregisterUserCallback(fd);

	/* add read event and register callback */
	short event = g_MyEventSet[fd].sctp_event;
	event |= (POLLIN | POLLPRI);
	if (sctp_registerUserCallback(fd, cb, NULL, event) < 0) {
		NiceExit(-1, "sctp register write callback failed\n");
	}

	/* remember current event flag */
	g_MyEventSet[fd].sctp_event = event;
	/*TRACE("[%d] read added set\n", fd);*/
}
/*-----------------------------------------------------------------*/
void SctpAddWriteEvent(int fd, sctp_userCallback cb)
{
	/* unregister callback first */
	sctp_unregisterUserCallback(fd);

	/* register write callback */
	short event = g_MyEventSet[fd].sctp_event;
	event |= POLLOUT;
	if (sctp_registerUserCallback(fd, cb, NULL, event) < 0) {
		NiceExit(-1, "sctp register write callback failed\n");
	}

	/* remember current event flag */
	g_MyEventSet[fd].sctp_event = event;
	/*TRACE("[%d] write set\n", fd);*/
}
/*-----------------------------------------------------------------*/
void SctpAcceptCB(int fd, short int event, short int* gotEvents, void* dummy)
{
	/* process accept */
	assert(fd > 0);
	assert(event & (POLLPRI | POLLIN));
	if (fd == g_acceptSocketUser) {
		/* user */
		SocketAcceptCB(fd, 0, NULL);
	}
	else if (fd == g_acceptSocketName) {
		/* name */
		SocketAcceptCB(fd, 0, NULL);
	}
	else if (fd == g_acceptSocketChunk) {
		/* chunk */
		SocketAcceptCB(fd, 0, NULL);
	}
	else if (fd == g_acceptSocketPing) {
		/* ping */
		PingCB(fd, 0, NULL);
	}
	else {
		assert(FALSE);
	}
}
/*-----------------------------------------------------------------*/
void SctpUserEventDispatchCB(int fd, short int event, short int* gotEvents,
		void* dummy)
{
	assert(fd >= 0);
	assert(g_MasterCIsetByFD[fd] != NULL);
	if (event & POLLERR) {
		TRACE("error! fd=%d\n", fd);
	}

	if (event & (POLLIN | POLLPRI)) {
		SctpReadCB(fd, event, gotEvents, dummy);
	}

	if (event & (POLLOUT)) {
		SctpWriteCB(fd, event, gotEvents, dummy);
	}
}
/*-----------------------------------------------------------------*/
void SctpReadCB(int fd, short int event, short int* gotEvents, void* dummy)
{
	assert(fd >= 0);
	assert(g_MasterCIsetByFD[fd] != NULL);
	assert(event & (POLLIN | POLLPRI));

	/* unregister callback first */
	sctp_unregisterUserCallback(fd);

	EventReadCB(fd, 0, g_MasterCIsetByFD[fd]);

	if (g_MasterCIsetByFD[fd] == NULL) {
		/* CI destoryed for some reason */
		return;
	}

	if (g_MasterCIsetByFD[fd]->status != cs_block
			&& g_MasterCIsetByFD[fd]->lenRead > 0) {
		/* process existing buffer */
		ProcessReadBuffer(g_MasterCIsetByFD[fd]);
	}

	if (g_MasterCIsetByFD[fd] == NULL) {
		/* CI destoryed for some reason */
		return;
	}

	/* register read callback */
	if (g_MasterCIsetByFD[fd]->status == cs_active
			&& g_MasterCIsetByFD[fd]->lenRead < RECV_BUFSIZE) {
		/* flow control: keep the read event set */
		SctpAddReadEvent(fd, &SctpUserEventDispatchCB);
	}
	else {
		/* TODO: who re-enables READ event? */
	}
}
/*-----------------------------------------------------------------*/
void SctpWriteCB(int fd, short int event, short int* gotEvents, void* dummy)
{
	assert(fd >= 0);
	assert(g_MasterCIsetByFD[fd] != NULL);
	assert(event & POLLOUT);
	Write(g_MasterCIsetByFD[fd], TRUE);

	/* note: connection may be destroyed after Write() */
	if (g_MasterCIsetByFD[fd] != NULL) {
		if (g_MasterCIsetByFD[fd]->lenWrite == 0) {
			/* nothing to send: remove write event */
			/*
			TRACE("[%d] %d bytes written done\n", fd, ci->lenWrite);
			*/
			SctpAddReadOnlyEvent(fd, &SctpUserEventDispatchCB);
		}
		else {
			/* we still have stuff to write.
			   keep the write event set */
		}
	}
}
/*-----------------------------------------------------------------*/
void SctpTimerCB(unsigned int id, void* param1, void* param2)
{
	/* reserve next time event */
	sctp_startTimer(0, MY_SCTP_TIMER_INTERVAL * 1000, &SctpTimerCB,
			NULL, NULL);
	CheckTimerEvents();
}
/*-----------------------------------------------------------------*/
void MyDataArriveNotif(unsigned int assocID, unsigned short streamID,
		unsigned int length, unsigned short ssn, unsigned int tsn,
		unsigned int protoID, unsigned int unordered, void* ulpdata)
{

	unsigned short instID = 0;
	int result = sctp_getInstanceID(assocID, &instID);
	if (result != 0) {
		NiceExit(-1, "sctp_getInstanceID failed\n");
	}
#ifdef _WAPROX_DEBUG_OUTPUT
	TRACE("got data: %d bytes, instID: %d, assocID: %d, streamID: %d\n",
			length, instID, assocID, streamID);
#endif
	char buffer[SOCKBUF_SIZE];
	unsigned int buflen = sizeof(buffer);
	unsigned short mySSN = 0;
	unsigned int myTSN = 0;
	assert(protoID == MY_SCTP_PROTO_ID);
	assert(unordered == SCTP_ORDERED_DELIVERY);
	result = sctp_receive(assocID, streamID, (unsigned char*)buffer,
			&buflen, &mySSN, &myTSN, SCTP_MSG_DEFAULT);
	if (result != SCTP_SUCCESS) {
		TRACE("error code: %d\n", result);
		NiceExit(-1, "sctp_receive() failed\n");
	}

	/* feed data to corresponding CI */
	PeerInfo* pi = GetPeerInfoByAssoc(assocID);
	assert(pi != NULL);
	ConnectionInfo* ci = NULL;
	if (instID == g_SCTP_ControlInstanceName) {
		ci = pi->nameCI2;
	}
	else if (instID == g_SCTP_ChunkInstanceName) {
		ci = pi->chunkCI2;
	}
	else {
		assert(FALSE);
	}

	/* process it */
	assert(ci != NULL);
	assert(ci->lenRead + buflen < RECV_BUFSIZE);
	memcpy(ci->bufRead + ci->lenRead, buffer, buflen);
	ci->lenRead += buflen;
	ProcessReadBuffer(ci);
}
/*-----------------------------------------------------------------*/
void MySendFailureNotif(unsigned int assocID, unsigned char *data,
		unsigned int length, unsigned int *context, void* ulpdata)
{
	TRACE("got callback!\n");
}
/*-----------------------------------------------------------------*/
void MyNetworkStatusChangeNotif(unsigned int assocID, short destAddr,
		unsigned short newState, void* ulpdata)
{
	TRACE("got callback!\n");
}
/*-----------------------------------------------------------------*/
void* MyCommunicationUpNotif(unsigned int assocID, int status,
		unsigned int numDestAddr, unsigned short numInStreams,
		unsigned short numOutStreams, int supportPRSCTP, void* ulpdata)
{
	/* get where it's from */
	SCTP_PathStatus pstatus;
	if (sctp_getPathStatus(assocID, sctp_getPrimary(assocID),
			&pstatus) != 0) {
		NiceExit(-1, "sctp_getPathStatus failed\n");
	}
	TRACE("from: %s\n", pstatus.destinationAddress);

	/* remember assoc <-> CI mapping here */
	in_addr_t addr;
	if (inet_aton((char*)pstatus.destinationAddress,
			(struct in_addr*)&addr) == 0) {
		NiceExit(-1, "inet_aton failed\n");
	}
	PeerInfo* pi = GetPeerInfoByAddr(addr);
	assert(pi != NULL);
	SetPeerInfoByAssoc(assocID, pi->idx);

	/* get the instance ID */
	unsigned short instID = 0;
	if (sctp_getInstanceID(assocID, &instID) != 0) {
		NiceExit(-1, "sctp_getInstanceID failed\n");
	}

	if (instID == g_SCTP_ClientInstanceName) {
		/* client: change the state */
		TRACE("client, instID: %d, assocID: %d\n", instID, assocID);
		UlpData* mydata = ulpdata;
		assert(mydata != NULL);
		if (mydata->type == ct_control) {
			ControlConnectionInfo* nci = pi->nameCI->stub;
			nci->assocState = sas_active;
			SctpSendAll(pi->nameCI);
		}
		else if (mydata->type == ct_chunk) {
			ChunkConnectionInfo* cci = pi->chunkCI->stub;
			cci->assocState = sas_active;
			SctpSendAll(pi->chunkCI);
		}
		else {
			assert(FALSE);
		}
	}
	else if (instID == g_SCTP_ControlInstanceName) {
		/* server: create ControlConnectionInfo here */
		TRACE("server, instID: %d, assocID: %d\n", instID, assocID);
		ConnectionInfo* ci = CreateNewConnectionInfo(-1, ct_control);
		CreateControlConnectionInfo(ci);
		ControlConnectionInfo* nci = ci->stub;
		nci->peerIdx = pi->idx;
		nci->assocState = sas_active;
		pi->nameCI2 = ci;
	}
	else if (instID == g_SCTP_ChunkInstanceName) {
		/* server: create ChunkConnectionInfo here */
		TRACE("server, instID: %d, assocID: %d\n", instID, assocID);
		ConnectionInfo* ci = CreateNewConnectionInfo(-1, ct_chunk);
		CreateChunkConnectionInfo(ci);
		ChunkConnectionInfo* cci = ci->stub;
		cci->peerIdx = pi->idx;
		cci->assocState = sas_active;
		pi->chunkCI2 = ci;
	}
	else {
		assert(FALSE);
	}

	return ulpdata;
}
/*-----------------------------------------------------------------*/
void MyCommunicationLostNotif(unsigned int assocID, unsigned short status,
		void* ulpdata)
{
	TRACE("got callback!\n");
}
/*-----------------------------------------------------------------*/
void MyCommunicationErrorNotif(unsigned int assocID, unsigned short status,
		void* ulpdata)
{
	TRACE("got callback!\n");
}
/*-----------------------------------------------------------------*/
void MyRestartNotif(unsigned int assocID, void* ulpdata)
{
	TRACE("got callback!\n");
}
/*-----------------------------------------------------------------*/
void MyPeerShutdownReceivedNotif(unsigned int assocID, void* ulpdata)
{
	TRACE("got callback!\n");
}
/*-----------------------------------------------------------------*/
void MyShutdownCompleteNotif(unsigned int assocID, void* ulpdata)
{
	TRACE("got callback!\n");
}
/*-----------------------------------------------------------------*/
void MyQueueStatusChangeNotif(unsigned int assocID, int qtype, int qid,
		int qlength, void* ulpdata)
{
	TRACE("got callback!\n");
	/* TODO: resume sending queued chunks */
}
/*-----------------------------------------------------------------*/
void MyAsconfStatusNotif(unsigned int assocID, unsigned int corrID,
		int result, void* opaque, void* ulpdata)
{
	TRACE("got callback!\n");
}
/*-----------------------------------------------------------------*/
void SctpSendAll(ConnectionInfo* ci)
{
	/* send them all now */
	if (ci == NULL) {
		/* not instantiated yet */
		return;
	}

	/* get assoc ID first */
	unsigned int assocID = 0;
	sctpAssocState state = sas_none;
	if (ci->type == ct_control) {
		ControlConnectionInfo* nci = ci->stub;
		assocID = nci->assocID;
		state = nci->assocState;
	}
	else if (ci->type == ct_chunk) {
		ChunkConnectionInfo* cci = ci->stub;
		assocID = cci->assocID;
		state = cci->assocState;
	}
	else {
		assert(FALSE);
	}

	if (state != sas_active) {
		return;
	}

	assert(assocID > 0);
	char stop = FALSE;
	int result = 0;
	int bytesSent = 0;
	MessageInfo* walk = NULL;
	while (!TAILQ_EMPTY(&(ci->msgListWrite)) && stop == FALSE) {
		TAILQ_FOREACH(walk, &(ci->msgListWrite), messageList) {
			/* debug */
			char msgType;
			if (ci->type == ct_control) {
				assert(VerifyControlHeader(walk->msg,
						walk->len, &msgType) == 1);
			}
			else if (ci->type == ct_chunk) {
				assert(VerifyChunkHeader(walk->msg,
						walk->len, &msgType) == 1);
			}
			else {
				assert(FALSE);
			}

			/* send it */
			int streamID = g_StreamID;
			result = sctp_send(assocID, streamID,
					(unsigned char*)walk->msg, walk->len,
					MY_SCTP_PROTO_ID,
					SCTP_USE_PRIMARY,
					SCTP_NO_CONTEXT,
					SCTP_INFINITE_LIFETIME,
					SCTP_ORDERED_DELIVERY,
					SCTP_BUNDLING_ENABLED);
			g_StreamID = (g_StreamID + 1) % MY_MAX_STREAMS;
			if (result == SCTP_SUCCESS) {
#ifdef _WAPROX_DEBUG_OUTPUT
				TRACE("type: %d, assocID: %d, streamID: %d, "
						"%d bytes sctp_sent\n",
						ci->type, assocID,
						streamID, walk->len);
#endif
				/* remove the sent message from the list */
				TAILQ_REMOVE(&(ci->msgListWrite), walk,
						messageList);
				bytesSent += walk->len;
				free(walk->msg);
				free(walk);
				break;
			}
			else if (result == SCTP_QUEUE_EXCEEDED) {
				TRACE("sctp queue full: try next time\n");	
				stop = TRUE;
				break;
			}
			else {
				TRACE("assocID: %d, error code: %d\n",
						assocID, result);
				NiceExit(-1, "sctp_send() failed\n");
			}
		}
	}
}
/*-----------------------------------------------------------------*/
void SctpAssociatePeer(int peerIdx, connectionType type)
{
	PeerInfo* pi = &g_PeerInfo[peerIdx];
	assert(pi != NULL);

	/* create CI first */
	ConnectionInfo* ci = NULL;
	sctpAssocState state = sas_none;
	int remote_port = 0;
	if (type == ct_control) {
		remote_port = MY_SCTP_CONTROL_PORT;
		if (pi->nameCI == NULL) {
			pi->nameCI = CreateNewConnectionInfo(
					-1, ct_control);
			CreateControlConnectionInfo(pi->nameCI);
			ControlConnectionInfo* nci = pi->nameCI->stub;
			nci->peerIdx = peerIdx;
		}
		else {
			ControlConnectionInfo* nci = pi->nameCI->stub;
			assert(nci->peerIdx == peerIdx);
		}
		ci = pi->nameCI;
		ControlConnectionInfo* nci = ci->stub;
		state = nci->assocState;
	}
	else if (type == ct_chunk) {
		remote_port = MY_SCTP_CHUNK_PORT;
		if (pi->chunkCI == NULL) {
			pi->chunkCI = CreateNewConnectionInfo(
					-1, ct_chunk);
			CreateChunkConnectionInfo(pi->chunkCI);
			ChunkConnectionInfo* cci = pi->chunkCI->stub;
			cci->peerIdx = peerIdx;
		}
		else {
			ChunkConnectionInfo* cci = pi->chunkCI->stub;
			assert(cci->peerIdx == peerIdx);
		}
		ci = pi->chunkCI;
		ChunkConnectionInfo* cci = ci->stub;
		state = cci->assocState;
	}
	else {
		assert(FALSE);
	}

	/* associate it */
	assert(ci != NULL);
	if (state == sas_none) {
		/* we use the CLIENT instance */
		assert(remote_port != 0);
		UlpData* ulpData = malloc(sizeof(UlpData));
		if (ulpData == NULL) {
			NiceExit(-1, "UlpData alloc failed\n");
		}
		ulpData->type = type;

		unsigned int assocID = sctp_associate(
				g_SCTP_ClientInstanceName,
				MY_SCTP_MAX_OUT_STREAMS,
				(unsigned char*)pi->ip, remote_port, ulpData);
		if (assocID == 0) {
			NiceExit(-1, "sctp_assoc failed\n");
		}
		TRACE("try associate to %s:%d, InstanceName: %d, assocID: %d\n",
				g_PeerInfo[peerIdx].ip, remote_port,
				g_SCTP_ClientInstanceName, assocID);

		if (type == ct_control) {
			ControlConnectionInfo* nci = ci->stub;
			nci->assocID = assocID;
			nci->assocState = sas_initsent;
		}
		else if (type == ct_chunk) {
			ChunkConnectionInfo* cci = ci->stub;
			cci->assocID = assocID;
			cci->assocState = sas_initsent;
		}
		else {
			assert(FALSE);
		}
	}
}
/*-----------------------------------------------------------------*/
void WriteToSctpPeer(int peerIdx, connectionType type)
{
	PeerInfo* pi = &g_PeerInfo[peerIdx];
	assert(pi != NULL);

	if (type == ct_control) {
		ControlConnectionInfo* nci = pi->nameCI->stub;
		if (nci->assocState == sas_active) {
			SctpSendAll(pi->nameCI);
		}
		else {
			TRACE("name assoc not ready yet\n");
		}
	}
	else if (type == ct_chunk) {
		ChunkConnectionInfo* cci = pi->chunkCI->stub;
		if (cci->assocState == sas_active) {
			SctpSendAll(pi->chunkCI);
		}
		else {
			TRACE("chunk assoc not ready yet\n");
		}
	}
	else {
		assert(FALSE);
	}
}
/*-----------------------------------------------------------------*/
void SctpMain()
{
	/* initalize the SCTP library */
	struct SCTP_ulp_Callbacks ulp_cb;
	ulp_cb.dataArriveNotif = &MyDataArriveNotif;
	ulp_cb.sendFailureNotif = &MySendFailureNotif;
	ulp_cb.networkStatusChangeNotif = &MyNetworkStatusChangeNotif;
	ulp_cb.communicationUpNotif = &MyCommunicationUpNotif;
	ulp_cb.communicationLostNotif = &MyCommunicationLostNotif;
	ulp_cb.communicationErrorNotif = &MyCommunicationErrorNotif;
	ulp_cb.restartNotif = &MyRestartNotif;
	ulp_cb.peerShutdownReceivedNotif = &MyPeerShutdownReceivedNotif;
	ulp_cb.shutdownCompleteNotif = &MyShutdownCompleteNotif;
	ulp_cb.queueStatusChangeNotif = &MyQueueStatusChangeNotif;
	ulp_cb.asconfStatusNotif = &MyAsconfStatusNotif;

	SCTP_LibraryParameters params;
	sctp_getLibraryParameters(&params);
	params.sendOotbAborts = 0;
	params.checksumAlgorithm = SCTP_CHECKSUM_ALGORITHM_CRC32C;
	params.supportPRSCTP = 0;
	params.supportADDIP = 0;
	sctp_setLibraryParameters(&params);

	/* register SCTP server instances */
	unsigned char localAddrList[SCTP_MAX_NUM_ADDRESSES][SCTP_MAX_IP_LEN];
	bzero(localAddrList[0], SCTP_MAX_IP_LEN);
	strcpy((char*)localAddrList[0], "0.0.0.0");
	g_SCTP_ControlInstanceName = sctp_registerInstance(
			MY_SCTP_CONTROL_PORT,
			MY_SCTP_MAX_IN_STREAMS, MY_SCTP_MAX_OUT_STREAMS,
			1, localAddrList, ulp_cb);
	if (g_SCTP_ControlInstanceName <= 0) {
		TRACE("error code: %d\n", g_SCTP_ControlInstanceName);
		NiceExit(-1, "sctp_registerInstance failed\n");
	}
	TRACE("SCTP Control Instance Name: %d\n", g_SCTP_ControlInstanceName);

	g_SCTP_ChunkInstanceName = sctp_registerInstance(
			MY_SCTP_CHUNK_PORT,
			MY_SCTP_MAX_IN_STREAMS, MY_SCTP_MAX_OUT_STREAMS,
			1, localAddrList, ulp_cb);
	if (g_SCTP_ChunkInstanceName <= 0) {
		TRACE("error code: %d\n", g_SCTP_ChunkInstanceName);
		NiceExit(-1, "sctp_registerInstance failed\n");
	}
	TRACE("SCTP Chunk Instance Name: %d\n", g_SCTP_ChunkInstanceName);

	/* register SCTP client instance */
	g_SCTP_ClientInstanceName = sctp_registerInstance(0,
			MY_SCTP_MAX_IN_STREAMS, MY_SCTP_MAX_OUT_STREAMS,
			1, localAddrList, ulp_cb);
	if (g_SCTP_ClientInstanceName <= 0) {
		TRACE("error code: %d\n", g_SCTP_ControlInstanceName);
		NiceExit(-1, "sctp_registerInstance failed\n");
	}
	TRACE("SCTP Client Instance Name: %d\n", g_SCTP_ClientInstanceName);

	/* add listening events at the beginning */
	SctpAddReadOnlyEvent(g_acceptSocketUser, &SctpAcceptCB);
	SctpAddReadOnlyEvent(g_acceptSocketName, &SctpAcceptCB);
	SctpAddReadOnlyEvent(g_acceptSocketChunk, &SctpAcceptCB);
	SctpAddReadOnlyEvent(g_acceptSocketPing, &SctpAcceptCB);

	/* add timer event: 10ms interval */
	sctp_startTimer(0, MY_SCTP_TIMER_INTERVAL * 1000, &SctpTimerCB,
			NULL, NULL);

	/* run */
	unsigned int ver = sctp_getLibraryVersion();
	TRACE("start running (using sctplib %d.%d)...\n",
			(ver >> 16), (ver & 0x0f));
	while (sctp_eventLoop() >= 0) ;

	/* never reach here */
}
/*-----------------------------------------------------------------*/
