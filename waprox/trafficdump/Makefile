CC = gcc
CFLAGS = -g -Wall -O

.c.o:
	${CC} -c ${CFLAGS} $< -o $@

OBJS = trafficdump_watchdog.o watchdog.o applib.o gettimeofdayex.o

all: trafficdump_watchdog

trafficdump_watchdog: ${OBJS}
	${CC} ${CFLAGS} -o trafficdump_watchdog ${OBJS} ${SLIBS} ${LDFLAGS} ${LIBS}

clean:
	rm -rf *~ *.o 

image: trafficdump_watchdog
	tar zcf trafficdump.tar.gz udpserver.py urlnorm.py lpclient lpclient.conf compressor.py trafficdump_watchdog trafficdump_start node_livetst
