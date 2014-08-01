#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <errno.h>
#include <error.h>

int writen( int fd, char *data, int n );
int readn( int fd, void *data, int n );

#endif
