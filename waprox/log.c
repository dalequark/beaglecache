#include <assert.h>
#include "log.h"
#include "config.h"

HANDLE hdebugLog = NULL;
HANDLE hexLog = NULL;
HANDLE hstatLog = NULL;

/*-----------------------------------------------------------------*/
void LogInit(int debug)
{
	if (debug == TRUE) {
		/* use stderr instead */
		return;
	}

	const char *debugFile = "dbg_waprox";
	const char *exlogFile = "exlog_waprox";
	const char *statlogFile = "stat_waprox";

	hdebugLog = CreateLogFHandle(debugFile, TRUE);
	if (hdebugLog == NULL) {
		hdebugLog = CreateLogFHandle("dbg_waprox", TRUE);
	}
	if (hdebugLog == NULL || (OpenLogF(hdebugLog) < 0)) {
		NiceExit(-1, "opening debug log failed");
	}

	hexLog = CreateLogFHandle(exlogFile, FALSE);
	if (hexLog == NULL || (OpenLogF(hexLog) < 0)) {
		NiceExit(-1, "extended log file creation failure");
	}

	hstatLog = CreateLogFHandle(statlogFile, FALSE);
	if (hstatLog == NULL || (OpenLogF(hstatLog) < 0)) {
		NiceExit(-1, "stat log file creation failure");
	}
}
/*-----------------------------------------------------------------*/
void LogSessionInfo(UserConnectionInfo* uci)
{
	char logData[8096];
	char* sname = strdup(InetToStr(uci->sip));
	char* dname = strdup(InetToStr(uci->dip));

	float RXsaving = 0;
	if (uci->missBytes + uci->hitBytes != uci->bytesRX) {
		TRACE("connection may be terminated abnormaly\n");
	}

	if (uci->bytesRX != 0) {
		RXsaving = (float)uci->hitBytes / uci->bytesRX * 100;
	}
	snprintf(logData, sizeof(logData), "%c %09d.%03d %09d.%03d "
			"%s:%d %s:%d | %d %d | %d %d %d | %d %d %d | %.2f\n",
			uci->isForward ? 'F' : 'R',
			(int)uci->startTime.tv_sec,
			(int)(uci->startTime.tv_usec / 1000),
			(int)uci->lastAccessTime.tv_sec,
			(int)(uci->lastAccessTime.tv_usec / 1000),
			sname, uci->sport, dname, uci->dport,
			uci->bytesTX, uci->bytesRX,
			uci->numHits, uci->numMisses, uci->numMoves,
			uci->hitBytes, uci->missBytes, uci->moveBytes,
			RXsaving);
	free(sname);
	free(dname);

	WriteLog(hexLog, logData, strlen(logData), TRUE);
	TRACE("%s", logData);
}
/*-----------------------------------------------------------------*/
