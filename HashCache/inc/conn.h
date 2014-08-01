#ifndef _CONN_H_
#define _CONN_H_

#include "hashtab.h"
#include "list.h"
#include "http.h"
#include "dns.h"
#include <stdio.h>

#define EXPIRY_TIME 100.00

typedef struct connection
{
  char *server;
  int port;
  int fd;
  struct timeval stamp;
  struct timeval used;
  DNSRep dnsInfo;
  char lDel;
  char added;
  char inUse;
  httpResponse *presentRes;
} connection;

typedef struct baseEntry
{
  char server[ 50 ];
  list *conns;
} baseEntry;

int initConnectionBase();
connection *getConnectionFromBase( char *server, int port );
int addConnectionToBase( connection *newConn );
int delConnectionFromBase( connection *oldConn );
int delConnectionPerm( connection* oldConn );
void closeOldConnections();

#endif
