#ifndef _LIST_H_
#define _LIST_H_

//#define PRINT_DEBUG 100
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "standard.h"

#define MAX_DAT_SIZ 150
#define MAX_LST_SIZ 100
#define LINKED 'l'
#define ARRAY 'a'
#define FREE 'f'
#define USED 'u'

typedef struct element
{
  int value;
  char *data;
  struct element *next;
  struct element *prev;
} element;

typedef struct list
{
  element* head;
  element *tail;
  int size;
  element *iterator;
} list;

typedef struct temp
{
  int value;
  element *ptr;
} temp;

list* create();
int init( list *L );
int addToHead( list *L, int value, void *data );
int addToTail( list *L, int value, void *data );
int addSorted( list *L, void *data, int value );
int delete( list *L, int value );
int deleteHead( list *L );
int deleteTail( list *L );
void *getHeadData( list *L );
void *getTailData( list *L );
int getHeadValue( list *L );
int getTailValue( list *L );
int getSize( list *L );
int sort( list *L );
void quicksort( temp* locations, int left, int right );
list* copy( list *original );
void copyElement( element *copy, element *original );
void* getPresentData( list *L );
int setPresentData( list *L, void* dat );
int getPresentValue( list *L );
char notEnd( list *L );
int deletePresent( list *L );
int startIter( list *L );
int endIter( list *L );
int move( list *L );
int ldestroy( list *L );
int movePresentToTail( list *L );
char *getIter( list *L );
int setIter( list *L, char *data );
void initListCounts();
void printListStats();

#endif 
