#include "applib.h"
#include "watchdog.h"
//#include "ports.h"

#define SERVICE_RESTART_TIME 300
#define SERVICE_CHECK_INTERVAL 30
#define FAILS_TO_RESTART 6


#define HOME "/home/princeton_wa"
#define START_FILE "/home/princeton_wa/trafficdump_start"


static WatchdogPortInfo ports[] = {
  {9990, 0, "GET http://localhost:9990/? HTTP/1.0\r\nHost:127.0.0.1\r\n\r\n"},
};

static int numPorts = sizeof(ports)/sizeof(WatchdogPortInfo);

HANDLE hdebugLog;
/*-----------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
  StartWatchdog(HOME, START_FILE, ports, numPorts, 
		SERVICE_RESTART_TIME, SERVICE_CHECK_INTERVAL, 
		FAILS_TO_RESTART);

  return(0);			/* never gets here */
}
/*-----------------------------------------------------------------*/
