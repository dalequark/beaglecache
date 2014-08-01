#ifndef _HTTP_HEADERS_H_
#define _HTTP_HEADERS_H_

#include "http.h"

extern int start;
extern int state;
extern int processed;
extern int end;
extern htab *ccTab;
extern size_t toklen;
extern char* token;
extern int tokenType;
extern char *buf;
extern char *ccNames;
extern int numbers[];
extern char *values[];
extern char *ctrlNames;

int months( char *name );

#endif

