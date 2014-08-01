#ifndef __WAPROX_DNSHELPER_H__
#define __WAPROX_DNSHELPER_H__

#include "queue.h"
#include "connection.h"

#define MAX_DNS_NAME_LEN	(255)

typedef struct DnsRequest {
	char name[MAX_DNS_NAME_LEN];
	int length;
} DnsRequest;

typedef struct DnsResponse {
	in_addr_t addr;
} DnsResponse;

struct DnsTransactionInfo {
	DnsRequest req;
	unsigned short port;

	/* connection key */
	unsigned int sip;
	unsigned short sport;

	/* list entry */
	TAILQ_ENTRY(DnsTransactionInfo) reqList;
	TAILQ_ENTRY(DnsTransactionInfo) deadList;
};

typedef TAILQ_HEAD(DnsRequestList, DnsTransactionInfo) DnsRequestList;
extern DnsRequestList g_DnsRequestList;

int CreateDnsHelpers();
void DNSSlaveMain(int fd, void* token);
ConnectionInfo* GetIdleDnsConn();
void SendDnsRequest(UserConnectionInfo* uci, char* name, int length,
		unsigned short port);
void DestroyDnsTransactionInfo(DnsTransactionInfo* dri);
void IssuePendingDnsRequests();
void IssueDnsRequest(ConnectionInfo* ci, DnsTransactionInfo* dri);

#endif /*__WAPROX_DNSHELPER_H__*/
