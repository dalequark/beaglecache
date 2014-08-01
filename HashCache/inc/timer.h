#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "list.h"


struct typedef event
{
  stamp original;
  stamp arrival;
  stamp expiry;
  char type;
} event;

int timer();
int addEvent( stamp original, stamp expiry, charchar *data );
int notifyEvent( event *present );
