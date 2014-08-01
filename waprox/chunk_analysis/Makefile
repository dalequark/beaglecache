#
# Makefile for chunk analysis
#

CC		= gcc
CFLAGS		= -g -Wall -O2 -D_BSD_SOURCE -I../
#LDFLAGS		= -static 
LIBS_CFLAGS	=

LIBS = -lnids -lpcap -lnet -lgthread-2.0 -lnsl -pthread -lglib-2.0 -lm -lcrypto -levent -lrt

OBJS = chunk_analysis.o ../hashtable.o ../hashtable_itr.o ../rabinfinger2.o ../debug.o ../applib.o ../log.o ../util.o ../gettimeofdayex.o ../config.o ../mrc_tree.o ../chunkcache.o ../peer.o ../diskhelper.o ../protocol.o ../disk.o ../connection.o ../waprox.o ../chunkrequest.o ../dnshelper.o ../httphelper.o ../reassemble.o ../ils.o

.c.o:
	$(CC) -c $(CFLAGS) -I. $(LIBS_CFLAGS) $<

all: chunk_analysis

chunk_analysis: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f *.o *~ chunk_analysis

new: clean all

# EOF
