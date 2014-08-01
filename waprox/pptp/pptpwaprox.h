#ifndef __WAPROX_PPTP_H__
#define __WAPROX_PPTP_H__

#define PPTP_NAT_PORT		(9980)
#define WAPROX_USER_PORT	(9981)
#define PPTP_LOCAL_IP		"10.1.0.1"

typedef struct NATRequest {
	unsigned int ip;
	unsigned short port;
} NATRequest;

typedef struct NATResponse {
	unsigned int ip;
	unsigned short port;
	unsigned int org_ip;
	unsigned short org_port;
} NATResponse;

extern void MangleTcpIpHeader(char* buffer, int reverseDirection);
extern int CreatePrivateAcceptSocket(int portNum, int nonBlocking);
extern int ProcessNATQuery(int waprox_fd);

#endif /*__WAPROX_PPTP_H__*/
