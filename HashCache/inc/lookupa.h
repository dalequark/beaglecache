

#ifndef STANDARD
#include "standard.h"
#endif

#ifndef LOOKUPA
#define LOOKUPA

#define CHECKSTATE 8
#define hashsize(n) ((ub4)1<<(n))
#define hashmask(n) (hashsize(n)-1)

ub4  lookup(/*_ ub1 *k, ub4 length, ub4 level _*/);
void checksum(/*_ ub1 *k, ub4 length, ub4 *state _*/);

#endif /* LOOKUPA */
