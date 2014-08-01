#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "getLocation.h"
#include "basic_mmh.h"

static unsigned long rands[5] = {12945,87969,986751,144325,4789857};
static unsigned char URLSHA1num[ 200 ];

int initRands( char *fsPath, int mode )
{
  int i;
  char buffer[ 100 ];
  int fd;
  int status;

  sprintf( buffer, "%s/rands.dat", fsPath );
  fd = open( buffer, O_RDWR | O_CREAT, S_IRWXU ); 
  
  if( fsPath != NULL && mode == REUSE )
    {
      if( fd != -1 )
	{
	  status = read( fd, buffer, 5 * sizeof( unsigned long ) );
	  
	  for( i = 0; i < 5; i ++ )
	    {
	      rands[ i ] = (unsigned long)(*(unsigned long*)( buffer + ( i * 4 ) ) );
	    }
	}

      close( fd );
      
      return 0;
    }

  srand( time( NULL ) );

  for( i = 0; i < 5; i ++ )
    {
      rands[ i ] = (unsigned long)rand();
      *(unsigned long*)( buffer + ( i * 4 ) ) = rands[ i ];
    }
  
  status = write( fd, buffer, 5 * sizeof( unsigned long ) );
  close( fd );

  return 0;
}

unsigned long getLocation( char *URL, int size )
{
  //Have to hash the URL using SHA1 to get a 160 bit output in to URLSHA1char

#ifdef PRINT_DEBUG
  printf( "Calculating SHA1\n" );
#endif

  SHA1( (unsigned char*)URL, size, (unsigned char*)(URLSHA1num) );

#ifdef PRINT_DEBUG
  printf( "SHA1 = %s\n", URLSHA1num );
#endif

  return basic_mmh( rands, (unsigned long*)URLSHA1num, 5 );
}


  
  
