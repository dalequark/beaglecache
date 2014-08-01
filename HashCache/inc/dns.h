#ifndef _DNS_H_
#define _DNS_H_

#include "list.h"
#include "hashtab.h"
#include "helper.h"

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

typedef struct DNSListEntry
{
  int number;
  DNSRep dnsEntry;
  char server[ 100 ];
} DNSListEntry;

int initDNSCache();
int updateDNSCache( char *server, DNSRep *rep );
int updateDNSUsage( char *server );
int getDNSInfo( char *server, DNSRep *rep );
void printDNSStats();

#endif
