#include "list.h"

#ifdef PRINT_DEBUG
#include "message.h"
#endif

static int numLists;
static int eleCount;

void initListCounts()
{
  numLists = 0;
  eleCount = 0;
}

list* create()
{
  list* L = (list*)malloc( sizeof( list ) );

  L->size = 0;
  L->head = NULL;
  L->tail = NULL;
  L->iterator = NULL;
  numLists++;
  return L;
}

int init( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }

  L->size = 0;
  L->head = NULL;
  L->tail = NULL;
  L->iterator = NULL;

  return 0;
}

int addToHead( list *L, int value, void *data )
{
#ifdef PRINT_DEBUG
  //printf( "List : Adding to head %s\n", ((diskRequest*)data)->URL );
#endif

  if( L == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }
  
  element *newOne = (element*)malloc( sizeof( element ) );
  eleCount++;

  newOne->data = data;
  newOne->value = value;
  newOne->prev = NULL;
  newOne->next = NULL;

  if( L->size == 0 )
    {
      L->tail = newOne;
      L->head = newOne;
      L->size++;
      return 0;
    }

  newOne->next = L->head;
  L->head->prev = newOne;
  L->head = newOne;

  L->size++;
  
#ifdef PRINT_DEBUG
  //printf( "List : Added to head %s\n", ((diskRequest*)(newOne->data))->URL );
#endif

  return 0;
}

int addToTail( list *L, int value, void *data )
{
  if( L == NULL )
    {
      fprintf( stderr, "List: Check inputs, they might be NULL\n" );
      return -1;
    }

  element *newOne = (element*)malloc( sizeof( element ) );
  eleCount++;

  newOne->data = data;
  newOne->value = value;
  newOne->next = NULL;
  newOne->prev = NULL;

  if( L->size == 0 )
    {
      L->head = newOne;
      L->tail = newOne;
      L->size++;
      return 0;
    }

  newOne->next = NULL;
  newOne->prev = L->tail;
  L->tail->next = newOne;
  L->tail = newOne;

  L->size++;
  
  return 0;
}

int addSorted( list *L, void *data, int value )
{
#ifdef PRINT_DEBUG
  printf( "Adding orderly\n" );
#endif

  if( L == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }

  element *present = L->head;
  element *previous = NULL;
  element *newOne = (element*)malloc( sizeof( element ) );
  eleCount++;

  newOne->data = data;
  newOne->value = value;
  newOne->next = NULL;
  newOne->prev = NULL;

  if( L->head == NULL )
    {
      L->head = newOne;
      L->tail = newOne;
    }
  else
    {
      while( present != NULL && value > present->value )
	{
	  previous = present;
	  present = present->next;
	}

      if( present == NULL )
	{
	  L->tail->next = newOne;
	  newOne->prev = L->tail;
	  L->tail = newOne;
	}
      else
	{
	  if( value <= present->value )
	    {
	      if( previous == NULL )
		{
		  L->head->prev = newOne;
		  newOne->next = L->head;
		  L->head = newOne;
		}
	      else
		{
		  previous->next = newOne;
		  newOne->next = present;
		  present->prev = newOne;
		  newOne->prev = previous;
		}
	    }
	}
    }

  L->size++;

  return 0;
}
	
int delete( list *L, int value )
{
  if( L == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }

  element *present = L->head;
  element *previous = present;
  element *tmp;
  
  while( present != NULL )
    {
      if( present->value == value )
	{
	  tmp = present;
	  previous->next = present->next;
	  free( tmp );
	}
      
      previous = present;
      present = present->next;
    }

  eleCount--;
  L->size--;
  return 0;
}

int deleteHead( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }

  if( L->head == NULL )
    return 0;

  if( L->size == 1 )
    {
      free( L->tail );
      L->tail = NULL;
      L->head = NULL;
      L->size--;
      eleCount--;
      return 0;
    }

  element *tmp = L->head;
  L->head = L->head->next;
  L->head->prev = NULL;

  free( tmp );
  eleCount--;
  L->size--;
  return 0;
}

int deleteTail( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }

  if( L->head == NULL )
    return 0;

  if( L->size == 1 )
    {
      free( L->head );
      L->head = NULL;
      L->tail = NULL;
      L->size--;
      eleCount--;
      return 0;
    }

  element *temp = L->tail;
  L->tail = L->tail->prev;
  L->tail->next = NULL;

  free( temp );
  eleCount--;
  L->size--;

  return 0;
}

void* getHeadData( list *L )
{
   if( L == NULL || L->head == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return (char*) -1;
    }
   
   return L->head->data;
}

void* getTailData( list *L )
{
  if( L == NULL || L->tail == NULL )
    {
      fprintf( stderr, "List: Check inputs, they might be NULL\n" );
      return (char*)-1;
    }

  return L->tail->data;
}

int getHeadValue( list *L )
{
  if( L == NULL || L->head == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }

  return L->head->value;
}

int getTailValue( list *L )
{
  if( L == NULL || L->head == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }
  
  return L->tail->value;
}

int getSize( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }

  return L->size;
}

int sort( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "List : Check inputs, they might be NULL\n" );
      return -1;
    }

  if( L->size == 0 )
    return 0;

  temp *locations = (temp*)malloc( ( L->size ) * sizeof( temp ) );

  element *present = L->head;
  int i = 0;
  
  while( present != NULL )
    {
      locations[ i ].value = present->value;
      locations[ i ].ptr = present;
      present = present->next;
      i++;
    }
    
  //printf( "List : Setup done\n" );
  quicksort( locations, 0, L->size - 1 );
  //printf( "List : Sorting done\n" );

  L->head = (element*)locations[ 0 ].ptr;
  L->tail = (element*)locations[ L->size - 1 ].ptr;
  L->iterator = NULL;

  for( i = 0; i < L->size - 1; i++ )
    {
      locations[ i ].ptr->next = locations[ i + 1 ].ptr;
      locations[ i + 1 ].ptr->prev = locations[ i ].ptr;
    }
  
  locations[ 0 ].ptr->prev = NULL;
  locations[ L->size - 1 ].ptr->next = NULL;
  free( locations );

  return 0;
}

void quicksort( temp* locations, int left, int right )
{
  int pivotValue, pivot, lHold, rHold;
  element *pivotPtr;
  
  lHold = left;
  rHold = right;
  pivotValue = locations[ left ].value;
  pivotPtr = locations[ left ].ptr;

  while( left < right )
  {
    while( ( locations[ right ].value >= pivotValue ) && ( left < right ) )
      {
	right--;
      }

    if( left != right )
    {
      locations[ left ].value = locations[ right ].value;
      locations[ left ].ptr = locations[ right ].ptr;
      left++;
    }

    while( ( locations[ left ].value <= pivotValue ) && ( left < right ) )
      {
	left++;
      }

    if( left != right )
    {
      locations[ right ].value = locations[ left ].value;
      locations[ right ].ptr = locations[ left ].ptr;
      right--;
    }
  }

  locations[ left ].value = pivotValue;
  locations[ left ].ptr = pivotPtr;

  pivot = left;
  left = lHold;
  right = rHold;

  if( left < pivot )
    {
      quicksort( locations, left, pivot - 1 );
    }
  
  if( right > pivot )
    {
      quicksort( locations, pivot + 1, right );
    }
}

list* copy( list *original )
{
#ifdef PRINT_DEBUG
  //printf( "List : Copying\n" );
#endif

  element *present = (original->head);

  list *copy = create();

  while( present != NULL )
    {
#ifdef PRINT_DEBUG
      //printf( "List : Copying element %d\n", i + 1 );
#endif

      addToTail( copy, (present)->value, (present)->data  );
      present = (present)->next;
    }

#ifdef PRINT_DEBUG
  //printf( "List : Copied\n" );
#endif

  return copy;
}

void copyElement( element *copy, element *original )
{
  copy->value = original->value;
  copy->data = original->data;
}

void* getPresentData( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they might be NULL\n" );
      return (char*)-1;
    }

  if( L->iterator == NULL )
    {
      fprintf( stderr, "Iterator is NULL\n" );
      return NULL;
    }

  return L->iterator->data;
}

int setPresentData( list *L, void* dat )
{
  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they might be NULL\n" );
      return -1;
    }

  if( L->iterator == NULL )
    {
      fprintf( stderr, "Iterator is NULL\n" );
      return -1;
    }

  L->iterator->data = dat;
  return 0;
}

char notEnd( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they might be NULL\n" );
      return -1;
    }

  if( L->iterator == NULL || L->size == 0 )
    {
      return FALSE;
    }

  return TRUE;
}

int deletePresent( list *L )
{
  element *temp;

  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they might be NULL\n" );
      return -1;
    }

  if( L->iterator == NULL )
    {
      return 0;
    }

  if( L->size == 0 )
    {
      return 0;
    }

  if( L->size == 1 && L->iterator != L->head )
    {
      fprintf( stderr, "Possible error state\n" );
      return -1;
    }

  if( L->size == 1 && L->iterator == L->head )
    {
      free( L->head );
      L->tail = NULL;
      L->head = NULL;
      L->iterator = NULL;
      L->size--;
      eleCount--;
      return 0;
    }
  else
    {
      if( L->iterator == L->head )
	{
	  L->head = L->head->next;
	  L->head->prev = NULL;
	  free( L->iterator );
	  L->size--;
	  eleCount--;
	  L->iterator = L->head;
	  return 0;
	}
      else
	{
	  if( L->iterator == L->tail )
	    {
	      L->tail = L->tail->prev;
	      L->tail->next = NULL;
	      free( L->iterator );
	      L->size--;
	      eleCount--;
	      L->iterator = NULL;
	      return 0;
	    }
	  else
	    {
	      temp = L->iterator;
	      L->iterator->prev->next = L->iterator->next;
	      L->iterator->next->prev = L->iterator->prev;
	      L->iterator = L->iterator->next;
	      free( temp );
	      eleCount--;
	      L->size--;
	    }
	}
    }

  return 0;
}


int startIter( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they might be NULL\n" );
      fprintf( stderr, "Trying to start an iterator on a NULL list\n" );
      return -1;
    }

  L->iterator = L->head;
  
  return 0;
}

int endIter( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they might be NULL\n" );
      fprintf( stderr, "Trying to start an iterator on a NULL list\n" );
      return -1;
    }

  L->iterator = L->tail;
  
  return 0;
}

int move( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they might be NULL\n" );
      return -1;
    }

  if( L->iterator == NULL )
    {
      return 0;
    }

  L->iterator = L->iterator->next;

  return 0;
}

int ldestroy( list *L )
{
  if( L == NULL )
    {
      return 0;
    }

  if( L->size == 0 )
    {
      free( L );
      numLists--;
      return 0;
    }

  element *present = L->head;
  element *temp;

  while( present != NULL )
    {
      temp = present;
      present = present->next;
      eleCount--;
      free( temp );
    }

  free( L );
  numLists--;

  return 0;
}

int getPresentValue( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "List is empty\n" );
      return -1;
    }

  if( L->iterator == NULL )
    {
      fprintf( stderr, "Position first\n" );
      return -1;
    }

  return L->iterator->value;
}

int movePresentToTail( list *L )
{
  element *prev, *pres, *next;

  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they may be NULL\n" );
      return -1;
    }

  if( L->size == 1 )
    {
      return 0;
    }

  if( L->iterator == L->head )
    {
      pres = L->iterator;
      prev = NULL;
      next = L->head->next;
      L->head = next;
      next->prev = NULL;
      L->tail->next = pres;
      pres->prev = L->tail;
      L->tail = pres;
      return 0;
    }

  if( L->tail == L->iterator )
    {
      return 0;
    }

  pres = L->iterator;
  prev = pres->prev;
  next = pres->next;
  L->tail->next = pres;
  prev->next = next;
  next->prev = prev;
  pres->prev = L->tail;
  L->tail = pres;

  return 0;
}

char *getIter( list *L )
{
  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they may be NULL\n" );
      return (char*)-1;
    }

  return (char*)L->iterator;
}

int setIter( list *L, char *data )
{
  if( L == NULL )
    {
      fprintf( stderr, "Check inputs, they may be NULL\n" );
      return -1;
    }
  
  L->iterator = (element*)data;

  return 0;
}

void printListStats()
{
  fprintf( stderr, "Lists: %d Elements: %d\n", numLists, eleCount );
}
 
