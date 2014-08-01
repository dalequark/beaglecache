#include "queue.h"

#define TIME_LIMIT 100

static list *nowList;
static list *lateList;
static list *tmpList;
static struct timeval currentStart;
static struct timeval now;
static int setTime();
static int checkTime();

int initSched()
{
  nowList = create();
  lateList = create();
  setTime();

  return 0;
}
  
int enQueueObject( int priority, char *object )
{
  int status;

  status = checkTime();

  if( status == -1 )
    {
      addSorted( lateList, object, priority );
    }
  else
    {
      addSorted( nowList, object, priority );
    }

  return 0;
}

char *moveQueue()
{
  char *temp;

  if( getSize( nowList ) == 0 )
    {
      tmpList = lateList;
      lateList = nowList;
      nowList = tmpList;
      setTime();
    }
  
  temp = getHeadData( nowList );

  deleteHead( nowList );

  return temp;
}

int checkTime()
{
  gettimeofday( &now, NULL );

  int gap = ( now.tv_sec - currentStart.tv_sec ) * 1000 + ( now.tv_usec - currentStart.tv_usec ) / 1000;

  if( gap > TIME_LIMIT )
    {
      return -1;
    }
  else
    {
      return 0;
    }

  return 0;
}

int setTime()
{
  gettimeofday( &currentStart, NULL );
  return 0;
}

