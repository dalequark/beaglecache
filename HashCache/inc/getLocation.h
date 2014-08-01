#ifndef _GET_LOCATION_H_
#define _GET_LOCATION_H_

#define REUSE 1
#define NO_REUSE 2

int initRands( char *fsPath, int mode );
unsigned long getLocation( char *URL, int size );

#endif
