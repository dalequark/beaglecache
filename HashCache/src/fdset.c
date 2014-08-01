#include "fdset.h"

const char FIRST = 128;
const char LAST = 127;
const char ZERO = 0;

void Custom_FD_CLR( int fd, custom_fd_set *fdset )
{
  *(char*)(fdset->__fd_bits + fd) &= LAST;
}

int Custom_FD_ISSET( int fd, custom_fd_set *fdset )
{
  return *(char*)(fdset->__fd_bits + fd) & FIRST != 0 ? 1 : 0;
}

void Custom_FD_SET( int fd, custom_fd_set *fdset )
{
  *(char*)(fdset->__fd_bits + fd) |= FIRST;
}

void Custom_FD_ZERO( custom_fd_set *fdset )
{
  memset( fdset, ZERO, Custom_FDSET_SIZE / 8 );
}



