#ifndef __WAPROX_SCTPGLUE_TREE_H__
#define __WAPROX_SCTPGLUE_TREE_H__

#include <sctp.h>
#include "connection.h"

#ifdef HAVE_SYS_POLL_H
	#include <sys/poll.h>
#else
	#define POLLIN     0x001
	#define POLLPRI    0x002
	#define POLLOUT    0x004
	#define POLLERR    0x008
#endif

/* in millisecond */
#define MY_SCTP_TIMER_INTERVAL	(10)
#define MY_SCTP_CONTROL_PORT	(7)
#define MY_SCTP_CHUNK_PORT	(8)
#define MY_SCTP_PROTO_ID	(1)
#define MY_MAX_STREAMS		(MAX_NUM_CI)
#define MY_SCTP_MAX_IN_STREAMS	(MY_MAX_STREAMS)
#define MY_SCTP_MAX_OUT_STREAMS	(MY_MAX_STREAMS)

typedef struct UlpData {
	connectionType type;
} UlpData;

extern unsigned short g_SCTP_ControlInstanceName;
extern unsigned short g_SCTP_ChunkInstanceName;
extern unsigned short g_SCTP_ClientInstanceName;

void SctpSendAll(ConnectionInfo* ci);
void SctpAssociatePeer(int peerIdx, connectionType type);
void WriteToSctpPeer(int peerIdx, connectionType type);
void SctpUserEventDispatchCB(int fd, short int event, short int* gotEvents,
		void* dummy);
void SctpReadCB(int fd, short int event, short int* gotEvents, void* dummy);
void SctpWriteCB(int fd, short int event, short int* gotEvents, void* dummy);
void SctpTimerCB(unsigned int id, void* param1, void* param2);
void SctpMain();
void SctpAddReadOnlyEvent(int fd, sctp_userCallback cb);
void SctpAddReadEvent(int fd, sctp_userCallback cb);
void SctpAddWriteEvent(int fd, sctp_userCallback cb);

/* user-level program (ULP) callbacks */
void MyDataArriveNotif(unsigned int assocID, unsigned short streamID,
		unsigned int length, unsigned short ssn, unsigned int tsn,
		unsigned int protoID, unsigned int unordered, void* ulpdata);
void MySendFailureNotif(unsigned int assocID, unsigned char *data,
		unsigned int length, unsigned int *context, void* ulpdata);
void MyNetworkStatusChangeNotif(unsigned int assocID, short destAddr,
		unsigned short newState, void* ulpdata);
void* MyCommunicationUpNotif(unsigned int assocID, int status,
		unsigned int numDestAddr, unsigned short numInStreams,
		unsigned short numOutStreams, int supportPRSCTP, void* ulpdata);
void MyCommunicationLostNotif(unsigned int assocID, unsigned short status,
		void* ulpdata);
void MyCommunicationErrorNotif(unsigned int assocID, unsigned short status,
		void* ulpdata);
void MyRestartNotif(unsigned int assocID, void* ulpdata);
void MyPeerShutdownReceivedNotif(unsigned int assocID, void* ulpdata);
void MyShutdownCompleteNotif(unsigned int assocID, void* ulpdata);
void MyQueueStatusChangeNotif(unsigned int assocID, int qtype, int qid,
		int qlength, void* ulpdata);
void MyAsconfStatusNotif(unsigned int assocID, unsigned int corrID,
		int result, void* opaque, void* ulpdata);

#endif /*__WAPROX_SCTPGLUE_TREE_H__*/
