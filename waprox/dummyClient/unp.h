#ifndef	_UNP_H_
#define	_UNP_H_

#include	<sys/socket.h>	/* basic socket definitions */
#include	<netinet/in.h>	/* sockaddr_in{} and other Internet defns */
#include	<arpa/inet.h>	/* inet(3) functions */

#ifndef	AF_LOCAL
#define AF_LOCAL	AF_UNIX
#endif
#ifndef	PF_LOCAL
#define PF_LOCAL	PF_UNIX
#endif


/* Following could be derived from SOMAXCONN in <sys/socket.h>, but many
   kernels still #define it as 5, while actually supporting many more */
#define	LISTENQ		1024	/* 2nd argument to listen() */

/* Miscellaneous constants */
#define	MAXLINE		8192	/* max text line length */
#define	MAXSOCKADDR  128	/* max socket address structure size */
#define	BUFFSIZE	8192	/* buffer size for reads and writes */

/* Define some port number that can be used for client-servers */
#define	SERV_PORT  8000			/* TCP and UDP client-servers */
#define	SERV_PORT_STR	"9877"			/* TCP and UDP client-servers */
#define SERV_ADDR "128.112.92.157"

#ifndef min
#define	min(a,b)	((a) < (b) ? (a) : (b))
#define	max(a,b)	((a) > (b) ? (a) : (b))
#endif

int writen( int fd, char* data, int n );
int readn( int fd, void *ptr, int n );
#endif

