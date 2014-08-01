#include "peer.h"
#include "config.h"
#include "protocol.h"
#include "hashtable.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <time.h>

PeerInfo* g_PeerInfo = NULL;
int g_NumPeers = 0;
int g_NumLivePeers = 0;
int g_FirstPeerIdx = -1;

/* current node's hostname */
char g_HostName[1024];
static in_addr_t localAddr[10];
static int numAddrs = 0;
PeerInfo* g_LocalInfo = NULL;
PeerInfo* g_FixedDestInfo = NULL;

#define PING_TIMEOUT	(g_TimerInterval * 3)

/* returns PeerInfo given server name */
static struct hashtable* peerNameMap = NULL;

/* returns PeerInfo given server addr */
static struct hashtable* peerAddrMap = NULL;

/* returns PeerInfo given assoc ID */
static PeerInfo* peerAssocMap[MAX_PEER_SIZE];

/* server weight for R-HRW */
typedef struct ServerWeight {
	int index;
	unsigned int weight;
} ServerWeight;
ServerWeight* g_Weights = NULL;
int RhrwPickPeer(char *chunkName, ServerWeight *weights);

/*-----------------------------------------------------------------*/
unsigned int ChunkHashString(const char* name, unsigned int hash)
{
	int i;
	int len;
	if (name == NULL)
		return 0;

	len = SHA1_LEN;
	for (i = 0; i < len; i ++)
		hash += (_rotl(hash, 19) + name[i]);

	return hash;
}
/*-----------------------------------------------------------------*/
static unsigned int hash_from_key_fn_name(void* k)
{
	char* name = k;
	int i, hash = 0;
	for (i = 0; i < strlen(name); i++) {
		hash += (unsigned char)name[i];
	}
	return hash;
}
/*-----------------------------------------------------------------*/
static int keys_equal_fn_name(void* key1, void* key2)
{
	char* k1 = (char*)key1;
	char* k2 = (char*)key2;
	if (strlen(k1) == strlen(k2) && memcmp(k1, k2, strlen(k1)) == 0) {
		return 1;
	}
	else {
		return 0;
	}
}
/*-----------------------------------------------------------------*/
static unsigned int hash_from_key_fn_addr(void* k)
{
	char* name = k;
	int i, hash = 0;
	for (i = 0; i < sizeof(in_addr_t); i++) {
		hash += (unsigned char)name[i];
	}
	return hash;
}
/*-----------------------------------------------------------------*/
static int keys_equal_fn_addr(void* key1, void* key2)
{
	if (memcmp(key1, key2, sizeof(in_addr_t)) == 0) {
		return 1;
	}
	else {
		return 0;
	}
}
/*-----------------------------------------------------------------*/
void CreatePeerInfo()
{
	/* init randome seed */
	srand(timeex(NULL));

	/* create array for peer information */
	g_PeerInfo = calloc(sizeof(PeerInfo), MAX_PEER_SIZE);
	if (g_PeerInfo == NULL) {
		NiceExit(-1, "Peer Info array memory allocation failed\n");
	}

	/* create server weight for R-HRW */
	g_Weights = calloc(sizeof(ServerWeight), MAX_PEER_SIZE);
	if (g_Weights == NULL) {
		NiceExit(-1, "Server Weight array memory allocation failed\n");
	}

	/* create convert tables */
	assert(peerNameMap == NULL);
	peerNameMap = create_hashtable(MAX_PEER_SIZE,
			hash_from_key_fn_name, keys_equal_fn_name);
	if (peerNameMap == NULL) {
		NiceExit(-1, "peerNameMap creation failed\n");
	}

	assert(peerAddrMap == NULL);
	peerAddrMap = create_hashtable(MAX_PEER_SIZE,
			hash_from_key_fn_addr, keys_equal_fn_addr);
	if (peerAddrMap == NULL) {
		NiceExit(-1, "peerAddrMap creation failed\n");
	}

	/* prepare fixed destination address here */
	if (g_UseFixedDest) {
		assert(g_FixedDestPort != 0);
		g_FixedDestInfo = malloc(sizeof(PeerInfo));
		if (g_FixedDestInfo == NULL) {
			NiceExit(-1, "g_FixedDestInfo malloc failed\n");
		}
		struct hostent* ent = gethostbyname(g_FixedDestAddr);
		struct in_addr tmp;
		if (ent == NULL) {
			TRACE("failed in name lookup - %s\n", g_FixedDestAddr);
			NiceExit(-1, "exit\n");
		}
		else {
			memcpy(&(g_FixedDestInfo->netAddr), ent->h_addr,
					sizeof(in_addr_t));
			tmp.s_addr = g_FixedDestInfo->netAddr;
			TRACE("Fixed Dest %s: %s\n", g_FixedDestAddr,
					inet_ntoa(tmp));
		}
	}
}
/*-----------------------------------------------------------------*/
char CheckLocalHost(char* name, in_addr_t* addr)
{
	/* get current node's hostname first */
	if (strlen(g_HostName) == 0) {
		if (o_localHost != NULL) {
			memcpy(g_HostName, o_localHost, strlen(o_localHost));
		}
		else {
			if (gethostname(g_HostName, sizeof(g_HostName)) != 0) {
				NiceExit(-1, "cannot get this hostname\n");
			}
		}
		TRACE("Host Name: %s\n", g_HostName);

		/* name look up */
		struct hostent* ent = gethostbyname(g_HostName);
		if (ent == NULL) {
			NiceExit(-1, "failed in local address lookup\n");
		}

		while (TRUE) {
			if (ent->h_addr_list[numAddrs] == NULL) {
				break;
			}
			memcpy(&localAddr[numAddrs],
					ent->h_addr_list[numAddrs],
					sizeof(in_addr_t));
			struct in_addr tmp;
			tmp.s_addr = localAddr[numAddrs];
			TRACE("Local address %d: %s\n", numAddrs,
					inet_ntoa(tmp));
			numAddrs++;
			if (numAddrs >= sizeof(localAddr)) {
				NiceExit(-1, "too many local interfaces\n");
			}
		}
	}

	if (strncmp(name, g_HostName, strlen(g_HostName)) == 0) {
		/* name matches */
		return TRUE;
	}

	int i;
	for (i = 0; i < numAddrs; i++) {
		if (memcmp(&localAddr[i], addr, sizeof(in_addr_t)) == 0) {
			/* address matches */
			return TRUE;
		}
	}

	/* nothing matches */
	return FALSE;
}
/*-----------------------------------------------------------------*/
PeerInfo* AddUnknownPeerInfo(in_addr_t netAddr)
{
	/* add peer info to array and table */
	int idx = g_NumPeers;
	g_PeerInfo[idx].idx = g_NumPeers;
	g_PeerInfo[idx].name = NULL;
	g_PeerInfo[idx].addrHash = 0;
	g_PeerInfo[idx].ip = NULL;
	TAILQ_INIT(&g_PeerInfo[idx].userList);
	memcpy(&g_PeerInfo[idx].netAddr, &netAddr, sizeof(in_addr_t));
	g_PeerInfo[idx].isAlive = TRUE;
	if (hashtable_insert(peerAddrMap, &g_PeerInfo[idx].netAddr,
				&g_PeerInfo[idx]) == 0) {
		NiceExit(-1, "hashtable_insert failure\n");
	}
	g_PeerInfo[idx].isLocalHost = FALSE;
	g_NumPeers++;
	assert(g_NumPeers <= MAX_PEER_SIZE);
	return &g_PeerInfo[idx];
}
/*-----------------------------------------------------------------*/
void ParsePeerConf(char* buf)
{
	if (g_PeerInfo == NULL) {
		CreatePeerInfo();
	}

	/* trim */
	if (buf[strlen(buf) - 1] == '\n') {
		buf[strlen(buf) - 1] = '\0';
	}

	/* add peer info to array and table */
	g_PeerInfo[g_NumPeers].idx = g_NumPeers;
	g_PeerInfo[g_NumPeers].name = strdup(buf);
	g_PeerInfo[g_NumPeers].addrHash = HashString(buf, 0, FALSE, FALSE);
	g_PeerInfo[g_NumPeers].ip = NULL;
	TAILQ_INIT(&g_PeerInfo[g_NumPeers].userList);

	/* use first peer for fixed destination peer proxy */
	if (g_FirstPeerIdx == -1) {
		g_FirstPeerIdx = g_NumPeers;
	}

	/* name look up */
	struct hostent* ent = gethostbyname(g_PeerInfo[g_NumPeers].name);
	struct in_addr tmp;
	if (ent == NULL) {
		TRACE("failed in name lookup - %s\n",
				g_PeerInfo[g_NumPeers].name);
		memset(&g_PeerInfo[g_NumPeers].netAddr, 0, sizeof(in_addr_t));
	}
	else {
		memcpy(&g_PeerInfo[g_NumPeers].netAddr, ent->h_addr,
				sizeof(in_addr_t));
		tmp.s_addr = g_PeerInfo[g_NumPeers].netAddr;
		TRACE("%s: %s\n", buf, inet_ntoa(tmp));
		g_PeerInfo[g_NumPeers].ip = strdup(inet_ntoa(tmp));
	}

	/* check localhost */
	if (CheckLocalHost(buf, &g_PeerInfo[g_NumPeers].netAddr) == TRUE) {
		/* save localhost info */
		g_PeerInfo[g_NumPeers].isLocalHost = TRUE;
		g_LocalInfo = &g_PeerInfo[g_NumPeers];
		tmp.s_addr = g_LocalInfo->netAddr;
		TRACE("Local Host: %s / %s / %s\n",
				g_HostName, buf, inet_ntoa(tmp));
	}
	else {
		g_PeerInfo[g_NumPeers].isLocalHost = FALSE;
	}

	/* TODO: automatically add local host as a peer */

	g_PeerInfo[g_NumPeers].isAlive = FALSE;
	if (hashtable_insert(peerNameMap, g_PeerInfo[g_NumPeers].name,
				&g_PeerInfo[g_NumPeers]) == 0) {
		NiceExit(-1, "hashtable_insert failure\n");
	}
	if (hashtable_insert(peerAddrMap, &g_PeerInfo[g_NumPeers].netAddr,
				&g_PeerInfo[g_NumPeers]) == 0) {
		NiceExit(-1, "hashtable_insert failure\n");
	}
	g_NumPeers++;
	assert(g_NumPeers <= MAX_PEER_SIZE);
}
/*-----------------------------------------------------------------*/
void DestroyPeerList()
{
	if (peerNameMap == NULL || peerAddrMap == NULL
			|| g_PeerInfo == NULL) {
		NiceExit(-1, "CreatePeerInfo first\n");
	}

	/* clean up peer list */
	int i;
	for (i = 0; i < g_NumPeers; i++) {
		free(g_PeerInfo[i].name);
	}
	free(g_PeerInfo);

	/* clean up Peer Info conversion tables */
	hashtable_destroy(peerNameMap, 0);
	hashtable_destroy(peerAddrMap, 0);
}
/*-----------------------------------------------------------------*/
PeerInfo* GetPeerInfoByName(char* name)
{
	PeerInfo* pi = hashtable_search(peerNameMap, name);
	return pi;
}
/*-----------------------------------------------------------------*/
PeerInfo* GetPeerInfoByAddr(in_addr_t addr)
{
	PeerInfo* pi = hashtable_search(peerAddrMap, &addr);
	return pi;
}
/*-----------------------------------------------------------------*/
void SetPeerInfoByAssoc(int assocID, int peerID)
{
	assert(assocID >= 0 && assocID < MAX_PEER_SIZE);
	assert(peerID >= 0 && peerID < MAX_PEER_SIZE);
	peerAssocMap[assocID] = &g_PeerInfo[peerID];
}
/*-----------------------------------------------------------------*/
PeerInfo* GetPeerInfoByAssoc(int assocID)
{
	PeerInfo* pi = peerAssocMap[assocID];
	return pi;
}
/*-----------------------------------------------------------------*/
void SendPing(char isPing, char* serverName, PeerInfo* pi,
		struct timeval* timestamp)
{
	if (serverName != NULL) {
		/* find the peer info */
		pi = GetPeerInfoByName(serverName);
		if (pi == NULL) {
			/* ignore */
			TRACE("peer info not found: %s\n", serverName);
			return;
		}
	}

	struct sockaddr_in si_other;
	int s;
	socklen_t slen = sizeof(si_other);
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		NiceExit(-1, "Ping socket creation failed\n");
	}

	memset((char *) &si_other, sizeof(si_other), 0);
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(g_ProxyPingPort);
	si_other.sin_addr.s_addr = pi->netAddr;

	char* buffer = NULL;
	unsigned int buf_len = 0;
	assert(g_LocalInfo != NULL);

	if (isPing) {
		/* ping: record only timestamp for calculating RTT */
		BuildWAPing(TRUE, &g_LocalInfo->netAddr, timestamp,
				0, 0, &buffer, &buf_len);
	}
	else {
		/* pong: give all the information, use the given timestamp */
		/* TODO: send network pending bytes and disk pending chunks */
		BuildWAPing(FALSE, &g_LocalInfo->netAddr, timestamp,
				0, 0, &buffer, &buf_len);
	}

	if (sendto(s, buffer, buf_len, 0, (struct sockaddr*)&si_other, slen)
			== -1) {
		char errMessage[2048];
		sprintf(errMessage, "Error sending ping packet to %s\n",
				pi->name);
		NiceExit(-1, errMessage);
	}

	/* clean up */
	close(s);
	free(buffer);
}
/*-----------------------------------------------------------------*/
void UpdatePeerLiveness(PeerInfo* pi)
{
	/* check liveness */
	if (pi->isAlive == FALSE) {
		pi->isAlive = TRUE;
		g_NumLivePeers++;
		assert(g_NumLivePeers <= g_NumPeers);
	}

	/* update time */
	pi->lastPingTime = timeex(NULL);
}
/*-----------------------------------------------------------------*/
void ProcessPing(char* buffer, unsigned int buf_len)
{
	WAPing* msg = (WAPing*)buffer;

	/* update Peer Info */
	PeerInfo* pi = GetPeerInfoByAddr(msg->netAddr);
	if (pi == NULL) {
		/* ignore */
		struct in_addr tmp;
		tmp.s_addr = msg->netAddr;
		TRACE("peer info not found: %s\n", inet_ntoa(tmp));
		return;
	}

	/* update peer liveness */
	UpdatePeerLiveness(pi);

	if (msg->isPing) {
		/* send a pong packet if we got a ping packet */
		/*TRACE("PING from %s\n", msg->name);*/
		if (pi->idx != g_FirstPeerIdx) {
			SendPing(0, NULL, pi, &msg->timestamp);
		}
	}
	else {
		/* update and calculate RTT */
		/*TRACE("PONG from %s\n", msg->name);*/
		/* update peer status: TODO maybe moving average? */
		pi->networkPendingBytes = ntohl(msg->networkPendingBytes);
		pi->diskPendingChunks = ntohl(msg->diskPendingChunks);
		assert(pi->networkPendingBytes >= 0);
		assert(pi->diskPendingChunks >= 0);
		struct timeval curtime, result;
		UpdateCurrentTime(&curtime);
		timersub(&curtime, &msg->timestamp, &result);
		pi->RTT = (result.tv_sec * 1000) + (result.tv_usec / 1000);
		assert(pi->RTT >= 0);
		TRACE("%s, RTT: %d, Network Load: %d, Disk Load: %d\n",
				pi->name, pi->RTT, pi->networkPendingBytes,
				pi->diskPendingChunks);
	}
}
/*-----------------------------------------------------------------*/
int PickRandomPeer()
{
	/* pick a random peer for test/debug */
	int i;
	int numReady = 0;
	for (i = 0; i < g_NumPeers; i++) {
		if (g_PeerInfo[i].isAlive == TRUE) {
			g_Weights[numReady].index = i;
			numReady++;
		}
	}

	if (numReady == 0) {
		return -1;
	}
	else {
		return g_Weights[rand() % numReady].index;
	}
}
/*-----------------------------------------------------------------*/
int PickDestinationProxy(unsigned int sip, unsigned short sport,
	unsigned int dip, unsigned short dport)
{
	/* TODO: pick the destination proxy considering geo-location */
	/*
	return PickRandomPeer();
	*/
	assert(g_FirstPeerIdx != -1);
	return g_FirstPeerIdx;
}
/*-----------------------------------------------------------------*/
int PickPeerToRedirect(u_char* sha1Name)
{
	return RhrwPickPeer((char*)sha1Name, g_Weights);
}
/*-----------------------------------------------------------------*/
void CheckNodeStatus()
{
	int i;
	for (i = 0; i < g_NumPeers; i++) {
		if (!g_PeerInfo[i].isLocalHost && i != g_FirstPeerIdx) {
			/* send ping to peers other than local and fixed one */
			struct timeval curtime;
			UpdateCurrentTime(&curtime);
			SendPing(1, NULL, &g_PeerInfo[i], &curtime);
		}
		if (g_PeerInfo[i].lastPingTime + PING_TIMEOUT <= timeex(NULL)
				&& g_PeerInfo[i].isAlive == TRUE) {
			g_PeerInfo[i].isAlive = FALSE;
			g_NumLivePeers--;
			assert(g_NumLivePeers >= 0);
		}
	}

	/* make localhost alive always */
	UpdatePeerLiveness(g_LocalInfo);

	/* make fixed destination proxy alive always */
	UpdatePeerLiveness(&g_PeerInfo[g_FirstPeerIdx]);
}
/*-----------------------------------------------------------------*/
void PrintPeerStats()
{
	fprintf(stderr, "-# of Peers: %d\n", g_NumPeers);
	fprintf(stderr, "-# of Live Peers: %d\n", g_NumLivePeers);
}
/*-----------------------------------------------------------------*/
unsigned int WrandHash(unsigned int url_hash, unsigned int svr_hash)
{
	unsigned url_digest, svr_digest;
	unsigned int combined_hval;

	/* only keep 31 bits */
	url_digest = url_hash & 0x7FFFFFFF;
	svr_digest = svr_hash & 0x7FFFFFFF;
	combined_hval = (1103515245 * ((1103515245 * svr_digest + 12345)
				^ url_digest) + 12345) & 0x7FFFFFFF;
	return combined_hval;
}
/*-----------------------------------------------------------------*/
void swap(ServerWeight *server_weight_, int left, int right)
{
	ServerWeight temp;
	temp = server_weight_[left];
	server_weight_[left] = server_weight_[right];
	server_weight_[right] = temp;
}
/*-----------------------------------------------------------------*/
void quickSort(ServerWeight *server_weight_, int left, int right)
{
	ServerWeight curWeight;
	int i, j;
	if (right > left) {
		curWeight = server_weight_[right];
		i = left - 1;
		j = right;
		while (1) {
			while (server_weight_[++i].weight
					> curWeight.weight) {
				;
			}
			while (j > 0) {
				if (server_weight_[--j].weight
						>= curWeight.weight)
					break;
			}
			if (i >= j)
				break;
			swap(server_weight_, i, j);
		}
		swap(server_weight_, i, right);
		quickSort(server_weight_, left, i-1);
		quickSort(server_weight_, i+1, right);
	}
}
/*-----------------------------------------------------------------*/
int RhrwPickPeer(char *chunkName, ServerWeight *weights)
{
	unsigned int chunkHash = 0;
	int i;
	int numReady = 0;
	int leastPos = 0;
	int numMatchingLeast;
	int matches[MAX_PEER_SIZE];

	chunkHash = ChunkHashString(chunkName, 0);

	for (i = 0; i < g_NumPeers; i++) {
		if (i == g_LocalInfo->idx) {
			/* ignore myself */
			continue;
		}

		if (g_PeerInfo[i].isAlive == TRUE) {
			weights[numReady].index = i;
			weights[numReady].weight = WrandHash(chunkHash,
					g_PeerInfo[i].addrHash);
#if 0
			TRACE("chunk hash: %d, server hash: %d\n",
					chunkHash, g_PeerInfo[i].addrHash);
			TRACE("peer weight %s: %d\n", g_PeerInfo[i].name,
					weights[numReady].weight);
#endif
			numReady++;
		}
	}

	if (numReady < 1)
		return(-1);
	if (numReady == 1)
		return(weights[0].index);

	quickSort(weights, 0, numReady -1);

	matches[0] = leastPos;
	numMatchingLeast = 1;

	#if 0
	for (i = 1; i < numReady; i++) {
		/* pick the node with the lowest number of connections.
		   in cases of a tie, pick the one with the lower rtt */
		if (allServers[weights[i].index].si_numConns >
				allServers[weights[leastPos].index].si_numConns)
			continue;
		if (allServers[weights[i].index].si_numConns ==
				allServers[weights[leastPos].index].si_numConns) {
			matches[numMatchingLeast] = i;
			numMatchingLeast++;
		}
		if (allServers[weights[i].index].si_numConns <
				allServers[weights[leastPos].index].si_numConns) {
			leastPos = i;
			matches[0] = leastPos;
			numMatchingLeast = 1;
		}
	}

	/* randomize if we have multiple matches */
	if (numMatchingLeast > 1)
		leastPos = matches[random() % numMatchingLeast];
	#endif

	/* return the first peer for now */
	return (weights[leastPos].index);
}
/*-----------------------------------------------------------------*/
int GetPeerIdxFromFD(int fd)
{
	/* get peer proxy address */
	struct sockaddr peeraddr;
	socklen_t namelen = sizeof(struct sockaddr);
	if (getpeername(fd, &peeraddr, &namelen) != 0) {
		NiceExit(-1, "getpeername failed\n");
	}
	struct sockaddr_in* peeraddr_in = (struct sockaddr_in*)&peeraddr;
	PeerInfo* pi = GetPeerInfoByAddr(peeraddr_in->sin_addr.s_addr);
	if (pi == NULL) {
		/* make a new entry for this peer */
		pi = AddUnknownPeerInfo(peeraddr_in->sin_addr.s_addr);
	}
	assert(pi != NULL);
	return pi->idx;
}
/*-----------------------------------------------------------------*/
int GetNextChunkFlowID(int peerIdx)
{
	int i;
	int flowID = 0;
	for (i = 0; i < g_MaxPeerConns; i++) {
		flowID = (g_PeerInfo[peerIdx].chunkFlowID + 1)
			% g_MaxPeerConns;
		g_PeerInfo[peerIdx].chunkFlowID = flowID;
		if (g_PeerInfo[peerIdx].chunkCI[flowID] != NULL) {
			/* found valid connection. good to go */
			break;
		}
	}
	assert(flowID >= 0 && flowID < g_MaxPeerConns);
	assert(g_PeerInfo[peerIdx].chunkCI[flowID] != NULL);
	return flowID;
}
/*-----------------------------------------------------------------*/
int GetNextControlFlowID(int peerIdx)
{
	#if 0
	int i;
	int flowID = 0;
	for (i = 0; i < g_MaxPeerConns; i++) {
		flowID = (g_PeerInfo[peerIdx].nameFlowID + 1)
			% g_MaxPeerConns;
		g_PeerInfo[peerIdx].nameFlowID = flowID;
		if (g_PeerInfo[peerIdx].nameCI[flowID] != NULL) {
			/* found valid connection. good to go */
			break;
		}
	}
	assert(g_PeerInfo[peerIdx].nameCI[flowID] != NULL);
	#endif
	int flowID = (g_PeerInfo[peerIdx].nameFlowID + 1) % g_MaxPeerConns;
	g_PeerInfo[peerIdx].nameFlowID = flowID;
	assert(flowID >= 0 && flowID < g_MaxPeerConns);
	return flowID;
}
/*-----------------------------------------------------------------*/
