#ifndef _CUSTOM_FDSET_H_
#define _CUSTOM_FDSET_H_

#include <string.h>

#define Custom_FDSET_SIZE 4096

typedef struct custom_fd_set {
  long __fds_bits[ Custom_FDSET_SIZE / 32 ];
} custom_fd_set;

void Custom_FD_CLR   ( int fd, custom_fd_set *fdset );
int  Custom_FD_ISSET ( int fd, custom_fd_set *fdset );
void Custom_FD_SET   ( int fd, custom_fd_set *fdset );
void Custom_FD_ZERO  ( custom_fd_set *fdset ); 

#endif
