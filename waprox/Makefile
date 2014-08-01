#
# this Makefile is for linux
#

# options
#
# _WAPROX_NODELAY: disable Nagle's algorithm
# _WAPROX_DEBUG_OUTPUT: generate debug messages
# _WAPROX_NO_PEEK: don't use chunk peek req/resp. use body req/resp directly
# _WAPROX_NAME_SCHEDULE: multiplex control connection with RR for fairness
# _WAPROX_FAST_RECONSTRUCT: try reconstruction without delay
# _WAPROX_RESPONSE_FIFO: send chunk response in order we receive the requests
# _WAPROX_ILS: enalbe ILS
# _WAPROX_ILS_NOHISTORY: ignore pending bytes/chunks when estimating latency
# _WAPROX_LAZY_PUT: perform MRC and store chunks into cache only when idle
# _WAPROX_INTEGRITY_CHECK: perform heavy integrity check (verify sha1-hash)
# _WAPROX_FULL_MRC_TREE: the basic protocol. always send the whole tree
# _WAPROX_REASSEMBLE_OPTIMIZE: use separate lists for resolving and reassembling
# _WAPROX_USE_SCTP: use sctp for transport among proxies
# _WAPROX_USE_KERNEL_SCTP: use kernel sctp for transport among proxies
# NDEBUG: disable assert()

CC = gcc
CFLAGS = -g -Wall -O -I/usr/local/include -I./pptp \
	-D_WAPROX_NODELAY \
	-D_WAPROX_NO_PEEK \
	-D_WAPROX_REASSEMBLE_OPTIMIZE \
	-D_WAPROX_ILS_NOHISTORY \
	-D_WAPROX_NAME_SCHEDULE \
	-D_WAPROX_LAZY_PUT \
	-D_WAPROX_ILS \
	-DDEBUG \
	#-D_WAPROX_DEBUG_OUTPUT \
	#-DNDEBUG
	#-D_WAPROX_RESPONSE_FIFO \
	#-D_WAPROX_FAST_RECONSTRUCT \
	#-D_WAPROX_INTEGRITY_CHECK
LDFLAGS = #-static

OBJS = waprox.o util.o rabinfinger2.o hashtable.o hashtable_itr.o \
	chunkcache.o config.o protocol.o connection.o peer.o debug.o \
	applib.o gettimeofdayex.o log.o main.o mrc_tree.o disk.o \
	reassemble.o chunkrequest.o ils.o dnshelper.o httphelper.o \
	diskhelper.o #sctpglue.o
LIBS = -lrt -lm #-lsctp #-lpthread -lglib-2.0 #-lsctplib -levent -lcrypto
SLIBS = libevent.a libcrypto.a #sctp/libsctplib.a

.c.o:
	${CC} -c ${CFLAGS} $< -o $@

all: waprox

# do not use shared object in order to deploy easily on PlanetLab
waprox: ${OBJS}
	${CC} ${CFLAGS} -o waprox ${OBJS} ${SLIBS} ${LDFLAGS} ${LIBS}

clean:
	rm -rf *~ *.o *.bz2 dbg_waprox* stat_waprox* exlog_waprox*

image: waprox waprox.conf peer.conf
	tar jcf waprox_image.tar.bz2 waprox *.conf cache config_edgexl.py

new: clean all image
