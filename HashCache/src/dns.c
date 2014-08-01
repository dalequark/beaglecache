#include "dns.h"

static DNSListEntry entries[ 2000 ];
static list *dnsList;
static htab *dnsCache;
	
int initDNSCache()
{
  int i;

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating DNS cache\n" );
#endif

  dnsCache = hcreate( 10 );
  
  if( dnsCache == NULL )
    {
      fprintf( stderr, "Error creating a table of connections\n" );
      return -1;
    }

#ifdef PRINT_DEBUG
  fprintf( stderr, "Creating a DNS LRU list\n" );
#endif

  dnsList = create();

  if( dnsList == NULL )
    {
      fprintf( stderr, "Cannot create LRU DNS list\n" );
      return -1;
    }
  
  for( i = 0; i < 2000; i++ )
    {
      entries[ i ].number = i;
      entries[ i ].server[ 0 ] = -1;
      addToHead( dnsList, i, (char*)(&entries[i]) );
    }

  /*  FILE* hostF = fopen( "names.txt", "r" );
  char ip[ 100 ];
  char name[ 100 ];
  char junk[ 10 ];
  char junkjunk[ 10 ];
  DNSRep presentRep;

  while( fscanf( hostF, "%s", name ) != EOF )
    {
      fscanf( hostF, "%s %s %s", junk, junkjunk, ip );
      //      fprintf( stderr, "%s %s\n", ip, name );
      inet_aton( ip, (struct in_addr*)(&(presentRep.presentHost)) );
      updateDNSCache( strcat( name, ".bench.tst" ), &presentRep );
      }*/
      
  return 0;
}

int updateDNSCache( char *server, DNSRep *presentDNSRep )
{
  DNSListEntry *num;
  
  startIter( dnsList );
  num = (DNSListEntry*)getHeadData( dnsList );

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Updating %s as %s\n", server, inet_ntoa( *(struct in_addr*)presentDNSRep->presentHost ) );
#endif

  if( num->server[ 0 ] != -1 )
    {
      if( hfind( dnsCache, num->server, strlen( num->server ) ) == TRUE )
	{
	  hdel( dnsCache );
	}
      else
	{
	  fprintf( stderr, "Cannot find a DNS entry\n" );
	  return -1;
	}
    }

  memcpy( num->server, server, strlen( server ) + 1 );
  hadd( dnsCache, num->server, strlen( server ), (char*)getIter( dnsList ) );
  memcpy( (char*)(&num->dnsEntry), (char*)(presentDNSRep), sizeof( DNSRep ) );

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "%s is %s\n", inet_ntoa( *(struct in_addr*)num->dnsEntry.presentHost ), inet_ntoa( *(struct in_addr*)presentDNSRep->presentHost ) );
#endif

  movePresentToTail( dnsList );

#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "%d %d\n", getSize( dnsList ), (int)hcount( dnsCache ) );
#endif

  return 0;
}

int updateDNSUsage( char *server )
{
  if( hfind( dnsCache, server, strlen( server ) ) == FALSE )
    {
      fprintf( stderr, "Attempt to update non existant entry\n" );
      return -1;
    }

  setIter( dnsList, hstuff( dnsCache ) );

  movePresentToTail( dnsList );

  return 0;
}

int getDNSInfo( char *server, DNSRep *rep )
{
  DNSListEntry *presentEntry;
    
#ifdef PRINT_HCFS_DEBUG
  fprintf( stderr, "Looking up dns entry for %s\n", server );
#endif

  if( hfind( dnsCache, server, strlen( server ) ) == TRUE )
    {
#ifdef PRINT_HCFS_DEBUG
      fprintf( stderr, "DNS entry found for %s\n", server );
#endif

      setIter( dnsList, hstuff( dnsCache ) );
      presentEntry = (DNSListEntry*)getPresentData( dnsList );

#ifdef PRINT_HCFS_DEBUG
      fprintf( stderr, "%s is %s\n", presentEntry->server, inet_ntoa( *(struct in_addr*)presentEntry->dnsEntry.presentHost ) );
#endif

      memcpy( (char*)(rep), (char*)(&presentEntry->dnsEntry), sizeof( DNSRep ) );

      return 0;
    }

  return -1;
}

void printDNSStats()
{
  fprintf( stderr, "DNS Table: %d DNS List: %d\n", (int)hcount( dnsCache ), getSize( dnsList ) );
}
