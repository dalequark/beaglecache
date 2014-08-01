#
# Makefile for replay
#

CC		= gcc
CFLAGS		= -g -Wall -O2 -D_BSD_SOURCE -I../
LDFLAGS		= -static 
LIBS_CFLAGS	=

LIBS = -lnids -lpcap -lnet -lgthread-2.0 -lnsl -pthread -lglib-2.0 -lm -lcrypto

OBJS = replay.o ../hashtable.o ../hashtable_itr.o ../debug.o ../applib.o ../util.o ../gettimeofdayex.o

EVOBJS = replayev.o ../hashtable.o ../hashtable_itr.o ../debug.o ../applib.o ../util.o ../gettimeofdayex.o

PINGOBJS = myping.o ../applib.o ../gettimeofdayex.o ../util.o

.c.o:
	$(CC) -c $(CFLAGS) -I. $(LIBS_CFLAGS) $<

all: replay replayev myping

myping: $(PINGOBJS)
	$(CC) -o $@ $(PINGOBJS) $(LIBS)

replay: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

replayev: $(EVOBJS)
	$(CC) -o $@ $(EVOBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f *.o *~ replay replayev myping

new: clean all

# EOF
