#include "config.h"
#include "util.h"
#include "peer.h"
#include <ctype.h>

/* parameter definitions and default values */
int g_PptpNatServerPort = 9980;
int g_UserListeningPort = 9981;
int g_NameListeningPort = 9982;
int g_ChunkListeningPort = 9983;
int g_ProxyPingPort = 9984;
int g_CacheServerPort = 33333;
int g_HttpProxyPort = 8080;
int g_MaxDnsConns = 5;
int g_MinChunkSize = 64; /* in bytes */
int g_ChunkSize = 1024; /* in bytes */
int g_NumMRCWays = 4;
int g_MaxCacheSize = 50; /* in MB */
int g_TimerInterval = 5; /* in sec */
int g_BufferTimeout = 500; /* in ms */
int g_MaxCacheConns = 3;
char g_MemCacheOnly = TRUE;
char g_NoCache = FALSE;
char g_ClientChunking = TRUE;
char g_ServerChunking = TRUE;
char g_UseFixedDest = FALSE;
char* g_FixedDestAddr = NULL;
int g_FixedDestPort = 80;
char g_SkipFirstChunk = FALSE;
int g_MaxPeerConns = 5;
int g_PutQueueSize = 10;
int g_WanLinkBW = 1000;
int g_WanLinkRTT = 1000;
int g_SeekLatency = 15;
char g_GenerateNameHint = TRUE;

/* command line options: override conf file options */
int o_minChunkSize = -1;
int o_maxChunkSize = -1;
int o_numWays = -1;
char o_disableChunking = FALSE;
char* o_localHost = NULL;
int o_maxPeerConns = -1;

/*-----------------------------------------------------------------*/
char GetTrueFalse(const char* optionString)
{
	if (optionString[0] == 'T' || optionString[0] == 't') {
		/* True, TRUE or true, whatever */
		return TRUE;
	}
	else if (optionString[0] == 'F' || optionString[0] == 'f') {
		return FALSE;
	}
	else {
		char errMessage[2048];
		sprintf(errMessage, "%s should be True or False\n",
				optionString);
		NiceExit(-1, errMessage);
		return FALSE;
	}
}
/*-----------------------------------------------------------------*/
void ParseProxConf(char* buf)
{
	const char* delim = "\n\t ";

	/* read parameter name first */
	char* name = strtok(buf, delim);
	name = strdup(name);
	if (name == NULL) {
		char errMessage[2048];
		sprintf(errMessage, "syntax error in %s file: %s\n",
				PROXCONF, buf);
		NiceExit(-1, errMessage);
	}

	/* read parameter value next */
	const char* value = strtok(NULL, delim);
	if (value == NULL) {
		char errMessage[2048];
		sprintf(errMessage, "syntax error in %s file: %s\n",
				PROXCONF, buf);
		NiceExit(-1, errMessage);
	}
	TRACE("%s: %s\n", name, value);

	if (strcmp(name, "UserPort") == 0) {
		g_UserListeningPort = atoi(value);
	}
	else if (strcmp(name, "PptpPort") == 0) {
		g_PptpNatServerPort = atoi(value);
	}
	else if (strcmp(name, "NamePort") == 0) {
		g_NameListeningPort = atoi(value);
	}
	else if (strcmp(name, "ChunkPort") == 0) {
		g_ChunkListeningPort = atoi(value);
	}
	else if (strcmp(name, "PingPort") == 0) {
		g_ProxyPingPort = atoi(value);
	}
	else if (strcmp(name, "CachePort") == 0) {
		g_CacheServerPort = atoi(value);
	}
	else if (strcmp(name, "HttpPort") == 0) {
		g_HttpProxyPort = atoi(value);
	}
	else if (strcmp(name, "MaxDnsConns") == 0) {
		g_MaxDnsConns = atoi(value);
	}
	else if (strcmp(name, "MaxCacheConns") == 0) {
		g_MaxCacheConns = atoi(value);
	}
	else if (strcmp(name, "MinChunkSize") == 0) {
		g_MinChunkSize = atoi(value);
		if (o_minChunkSize != -1) {
			/* use command line option */
			g_MinChunkSize = o_minChunkSize;
			TRACE("\toverride to use %d\n", g_MinChunkSize);
		}
	}
	else if (strcmp(name, "ChunkSize") == 0) {
		g_ChunkSize = atoi(value);
		if (o_maxChunkSize != -1) {
			/* use command line option */
			g_ChunkSize = o_maxChunkSize;
			TRACE("\toverride to use %d\n", g_ChunkSize);
		}
	}
	else if (strcmp(name, "NumMRCWays") == 0) {
		g_NumMRCWays = atoi(value);
		if (o_numWays != -1) {
			/* use command line option */
			g_NumMRCWays = o_numWays;
			TRACE("\toverride to use %d\n", g_NumMRCWays);
		}
	}
	else if (strcmp(name, "MaxCacheSize") == 0) {
		g_MaxCacheSize = atoi(value);
	}
	else if (strcmp(name, "TimerInterval") == 0) {
		g_TimerInterval = atoi(value);
	}
	else if (strcmp(name, "BufferTimeout") == 0) {
		g_BufferTimeout = atoi(value);
	}
	else if (strcmp(name, "NoCache") == 0) {
		g_NoCache = GetTrueFalse(value);
	}
	else if (strcmp(name, "MemCacheOnly") == 0) {
		g_MemCacheOnly = GetTrueFalse(value);
	}
	else if (strcmp(name, "ClientChunking") == 0) {
		g_ClientChunking = GetTrueFalse(value);
		if (o_disableChunking == TRUE) {
			g_ClientChunking = FALSE;
			TRACE("\toverride to disable\n");
		}
	}
	else if (strcmp(name, "ServerChunking") == 0) {
		g_ServerChunking = GetTrueFalse(value);
		if (o_disableChunking == TRUE) {
			g_ServerChunking = FALSE;
			TRACE("\toverride to disable\n");
		}
	}
	else if (strcmp(name, "UseFixedDest") == 0) {
		g_UseFixedDest = GetTrueFalse(value);
	}
	else if (strcmp(name, "FixedDestAddr") == 0) {
		g_FixedDestAddr = strdup(value);
	}
	else if (strcmp(name, "FixedDestPort") == 0) {
		g_FixedDestPort = atoi(value);
	}
	else if (strcmp(name, "SkipFirstChunk") == 0) {
		g_SkipFirstChunk = GetTrueFalse(value);
	}
	else if (strcmp(name, "MaxPeerConns") == 0) {
		g_MaxPeerConns = atoi(value);
		if (o_maxPeerConns != -1) {
			/* use command line option */
			g_MaxPeerConns = o_maxPeerConns;
			TRACE("\toverride to use %d\n", g_MaxPeerConns);
		}

	}
	else if (strcmp(name, "PutQueueSize") == 0) {
		g_PutQueueSize = atoi(value);
	}
	else if (strcmp(name, "WanLinkBW") == 0) {
		g_WanLinkBW = atoi(value);
	}
	else if (strcmp(name, "WanLinkRTT") == 0) {
		g_WanLinkRTT = atoi(value);
	}
	else if (strcmp(name, "SeekLatency") == 0) {
		g_SeekLatency = atoi(value);
	}
	else {
		char errMessage[2048];
		sprintf(errMessage, "unknown parameter in %s file: %s\n",
				PROXCONF, buf);
		NiceExit(-1, errMessage);
	}

	free(name);
}
/*-----------------------------------------------------------------*/
void ReadConf(confType type, const char* filename)
{
	char buf[1024];
	FILE* fp = fopen(filename, "r");
	if (fp == NULL) {
		char errMessage[2048];
		sprintf(errMessage, "Cannot open %s file\n", filename);
		NiceExit(-1, errMessage);
	}

	/* read one line at a time */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (buf[0] == '#') {
			/* ignore comment */
			continue;
		}
		else if (isspace(buf[0])) {
			/* ignore white space */
			continue;
		}
		else {
			/* fetch the parameter */
			if (type == type_proxconf) {
				ParseProxConf(buf);
			}
			else if (type == type_peerconf) {
				ParsePeerConf(buf);
			}
			else {
				char errMessage[2048];
				sprintf(errMessage, "cannot parse %s file\n",
						filename);
				NiceExit(-1, errMessage);
			}
		}
	}

	if (fclose(fp) != 0) {
		char errMessage[2048];
		sprintf(errMessage, "Cannot close %s file\n", filename);
		NiceExit(-1, errMessage);
	}
}
/*-----------------------------------------------------------------*/
