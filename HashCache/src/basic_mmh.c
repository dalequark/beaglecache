#include <stdio.h>
#include "basic_mmh.h"

unsigned long basic_mmh( unsigned long *rands, unsigned long *URLSHA1, int size )
{
  int i;

  signed long long stmp;
  unsigned long long utmp;
  
  unsigned long long sum = 0LL;

  unsigned long ret;

  for( i = 0; i < size; i++ )
    {
      sum += rands[ i ] * (unsigned long long)URLSHA1[ i ];
    }

  stmp = ( sum & 0xffffffffLL ) - ( ( sum >> 32 ) * 15 );
  utmp = ( stmp & 0xffffffffLL ) - ( ( stmp >> 32 ) * 15 );

  ret = utmp & 0xffffffff;

  if( utmp > 0x10000000fLL )
    ret -= 15;

  return ret;
}

