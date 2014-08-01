#include "dnshelper.h"
#include <arpa/inet.h>
#include <assert.h>
#include "util.h"
#include "config.h"
#include "waprox.h"

/* pending dns request list */
DnsRequestList g_DnsRequestList;

/*-----------------------------------------------------------------*/
int CreateDnsHelpers()
{
	TAILQ_INIT(&g_DnsRequestList);

	/* create slaves for DNS resolving */
	int i;
	assert(g_MaxDnsConns < MAX_DNS_CONNS);
	for (i = 0; i < g_MaxDnsConns; i++) {
		int fd = CreateSlave(DNSSlaveMain, NULL, TRUE);
		if (fd <= 0) {
			return FALSE;
		}
		g_DnsHelperConnections[i] = CreateNewConnectionInfo(fd,
				ct_dns);
		if (g_DnsHelperConnections[i] == NULL) {
			NiceExit(-1, "new connection info failed\n");
		}
		CreateDnsConnectionInfo(g_DnsHelperConnections[i]);
		TRACE("dns connection success[%d], total: %d\n",
				fd, g_NumDnsConns);
	}

	return TRUE;
}
/*-----------------------------------------------------------------*/
void DNSSlaveMain(int fd, void* token)
{
	/* note: blocking I/O */
	while (1) {
		DnsRequest dr;
		if (read(fd, &dr, sizeof(DnsRequest)) <= 0) {
			NiceExit(-1, "read error\n");
		}

		/* perform DNS lookup here */
		struct hostent* hp;
		hp = gethostbyname(dr.name);
		if (hp == NULL) {
			/* lookup failed */
			TRACE("DNS lookup failure: %s\n", dr.name);
			continue;
		}

		/* success, convert it to in_addr type */
		DnsResponse resp;
		memcpy(&resp.addr, hp->h_addr, sizeof(in_addr_t));
		struct in_addr tmp;
		tmp.s_addr = resp.addr;
		TRACE("DNS lookup success: %s -> %s\n",
				dr.name, inet_ntoa(tmp));

		/* return the result to the main process */
		if (write(fd, &resp, sizeof(resp)) != sizeof(resp)) {
			NiceExit(-1, "write failed\n");
		}
		TRACE("sent DNS response to parent\n");
	}
}
/*-----------------------------------------------------------------*/
ConnectionInfo* GetIdleDnsConn()
{
	int i;
	for (i = 0; i < g_MaxDnsConns; i++) {
		if (g_DnsHelperConnections[i] == NULL) {
			continue;
		}

		DnsConnectionInfo* dci = g_DnsHelperConnections[i]->stub;
		if (dci->pCurTrans == NULL) {
			/* found available conn */
			return g_DnsHelperConnections[i];
		}
	}

	/* every conn is busy */
	return NULL;
}
/*-----------------------------------------------------------------*/
void SendDnsRequest(UserConnectionInfo* uci, char* name, int length,
		unsigned short port)
{
	/* create and fill dns request info */
	DnsTransactionInfo* dti = malloc(sizeof(DnsTransactionInfo));
	if (dti == NULL) {
		NiceExit(-1, "malloc failed\n");
	}
	assert(length > 0 && length < MAX_DNS_NAME_LEN);
	memcpy(dti->req.name, name, length);
	dti->req.name[length] = '\0';
	dti->req.length = length;
	dti->port = port;
	dti->sip = uci->sip;
	dti->sport = uci->sport;

	/* find idle DNS helper */
	ConnectionInfo* ci = GetIdleDnsConn();
	if (ci == NULL) {
		/* add to the queue */
		TAILQ_INSERT_TAIL(&g_DnsRequestList, dti, reqList);
	}
	else {
		IssueDnsRequest(ci, dti);
	}
}
/*-----------------------------------------------------------------*/
void DestroyDnsTransactionInfo(DnsTransactionInfo* dti)
{
	assert(dti != NULL);
	free(dti);
}
/*-----------------------------------------------------------------*/
void IssuePendingDnsRequests()
{
	DnsTransactionInfo* walk;
	DnsRequestList deadList;
	TAILQ_INIT(&deadList);
	TAILQ_FOREACH(walk, &g_DnsRequestList, reqList) {
		ConnectionInfo* ci = GetIdleDnsConn();
		if (ci != NULL) {
			IssueDnsRequest(ci, walk);
			TAILQ_INSERT_TAIL(&deadList, walk, deadList);
		}
		else {
			break;
		}
	}

	while (!TAILQ_EMPTY(&deadList)) {
		walk = TAILQ_FIRST(&deadList);
		TAILQ_REMOVE(&deadList, walk, deadList);
		TAILQ_REMOVE(&g_DnsRequestList, walk, reqList);
	}
}
/*-----------------------------------------------------------------*/
void IssueDnsRequest(ConnectionInfo* ci, DnsTransactionInfo* dti)
{
	assert(ci->type == ct_dns);
	DnsConnectionInfo* dci = ci->stub;
	assert(dci->pCurTrans == NULL);
	assert(dti != NULL);

	/* allocate a transaction to this connnection */
	dci->pCurTrans = dti;

	/* send request */
	char* request = NULL;
	int req_len = 0;
	request = malloc(sizeof(DnsRequest));
	if (request == NULL) {
		NiceExit(-1, "malloc failed\n");
	}
	memcpy(request, &dti->req, sizeof(DnsRequest));
	req_len = sizeof(DnsRequest);

	SendData(ci, request, req_len);
}
/*-----------------------------------------------------------------*/
