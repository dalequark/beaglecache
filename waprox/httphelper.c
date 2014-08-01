#include "httphelper.h"
#include "util.h"

/*-----------------------------------------------------------------*/
char* GetHttpHostName(char* buffer, int length, unsigned short* dport)
{
	/* quick and dirty implementation */
	char* line = strncasestr(buffer, length, "X-WaproxTarget:");
	if (line == NULL) {
		/* not found, look at URI line */
		return NULL;
	}
	else {
		/* this will be common case */
		char* host = GetWord((unsigned char*)line, 1);
		char* port = strchr(host, ':');
		if (port != NULL) {
			/* port info specified */
			*dport = atoi(port + 1);

			/* make host name NULL string */
			port[0] = '\0';
		}
		else {
			/* no port specified. should be 80 */
			*dport = 80;
		}

		return host;
	}
}
/*-----------------------------------------------------------------*/
