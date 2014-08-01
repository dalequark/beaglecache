#
# Makefile for cochunk
#

CC		= gcc
#CFLAGS		= -g -Wall -O2 -D_BSD_SOURCE -D_GNU_SOURCE -DDEBUG -I../
CFLAGS		= -g -Wall -O5 -D_BSD_SOURCE -D_GNU_SOURCE -I../
LDFLAGS		= -static 
LIBS_CFLAGS	=

LIBS = -lm -lcrypto -lGeoIP  #-levent -lrt

OBJS = cochunk.o hashtable.o hashtable_itr.o rabinfinger2.o debug.o applib.o gettimeofdayex.o mrc.o util.o

.c.o:
	$(CC) -c $(CFLAGS) -I. $(LIBS_CFLAGS) $<

all: cochunk

cochunk: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f *.o *~ cochunk

new: clean all

# EOF
