#include "unp.h"
#include "loop.h"

int writen( int fd, char *data, int n )
{
  int nleft;
  int nwritten;
  
  nleft = n;
  
  while( nleft > 0 )
    {
      if( ( nwritten = write( fd, data, nleft ) ) <= 0 )
	{
	  if( errno == EINTR )
	    nwritten = 0;           /* and call write() again */
	  else
	    return -1;                     /* error */
	}

      nleft -= nwritten;
      data += nwritten;
    }
     
  return n;
}

int readn( int fd, void *vptr, int n )
{
  int nleft;
  int nread;
  char *ptr;
  
  ptr = vptr;
  nleft = n;
	
  while( nleft > 0 )
    {
      if( ( nread = read( fd, ptr, nleft ) ) < 0 )
	{
	  if( errno == EINTR )
	    {
	      nread = 0;
	    }
	  else
	    {
	      return -1;
	    }
	} 
      else 
	{
	  if( nread == 0 )
	    {
	      return ( n - nleft );
	    }
	  
	  nleft -= nread;
	  ptr   += nread;
	}
    }
  
  return ( n - nleft );
}

