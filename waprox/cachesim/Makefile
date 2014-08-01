#
# Makefile for cachesim (cache simulator)
#

CC		= gcc
#CFLAGS		= -g -Wall -O2 -D_BSD_SOURCE -D_GNU_SOURCE -DDEBUG -I../
CFLAGS		= -g -Wall -O5 -D_FILE_OFFSET_BITS=64 -D_BSD_SOURCE -D_GNU_SOURCE -I../
LDFLAGS		= -static
LIBS_CFLAGS	=

LIBS = -lm -lcrypto #-levent -lrt

OBJS = cachesim.o hashtable.o hashtable_itr.o applib.o gettimeofdayex.o util.o

HTTPOBJS = hashtable.o hashtable_itr.o applib.o gettimeofdayex.o util.o cachesimhttp.o

MRCOBJS = hashtable.o hashtable_itr.o applib.o gettimeofdayex.o util.o cachesimmrc.o

.c.o:
	$(CC) -c $(CFLAGS) -I. $(LIBS_CFLAGS) $<

all: cachesim cachesimhttp cachesimmrc

cachesim: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

cachesimmrc: $(MRCOBJS)
	$(CC) -o $@ $(MRCOBJS) $(LDFLAGS) $(LIBS)

cachesimhttp: $(HTTPOBJS)
	$(CC) -o $@ $(HTTPOBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f *.o *~ cachesim cachesimhttp cachesimmrc

new: clean all

# EOF
