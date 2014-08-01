#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "waprox.h"
#include "applib.h"
#include "config.h"

/* function decl. */
void getOptions(int argc, char** argv);
void Usage(char* prog);

/* options */
int debug = TRUE;
char* proxconf = NULL;
char* peerconf = NULL;

/*-----------------------------------------------------------------*/
void getOptions(int argc, char** argv)
{
	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "l:r:w:f:p:a:m:dhn")) != -1) {
		switch (c)
		{
			case 'h': /* show usage */
				Usage(argv[0]);
				exit(1);
			case 'd': /* dump chunk blocks */
				debug = TRUE;
				break;
			case 'f': /* prox conf file */
				proxconf = optarg;
				break;
			case 'p': /* peer conf file */
				peerconf = optarg;
				break;
			case 'l': /* min chunk size */
				o_minChunkSize = atoi(optarg);
				break;
			case 'r': /* max chunk size */
				o_maxChunkSize = atoi(optarg);
				break;
			case 'w': /* # ways for multi-resolution chunking */
				o_numWays = atoi(optarg);
				break;
			case 'n': /* disable chunking */
				o_disableChunking = TRUE;
				break;
			case 'a': /* local host name */
				o_localHost = optarg;
				break;
			case 'm': /* max per peer connections */
				o_maxPeerConns = atoi(optarg);
				break;
			case '?':
				if (optopt == 'f'|| optopt == 'p'
						|| optopt == 'l'
						|| optopt == 'r'
						|| optopt == 'w'
						|| optopt == 'm'
						|| optopt == 'a') {
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				}
				else if (isprint(optopt)) {
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				}
				else {
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				}
				exit(1);
			default:
				break;
		}
	}
}
/*-----------------------------------------------------------------*/
void Usage(char* prog)
{
	fprintf(stderr, "Usage: %s [-options]\n", prog);
	fprintf(stderr,"\t-d: debug mode, runs in foreground\n");
	fprintf(stderr,"\t-l: min (leaf) chunk size in bytes\n");
	fprintf(stderr,"\t-r: max (root) chunk size in bytes\n");
	fprintf(stderr,"\t-w: # ways for multi-resolution chunking\n");
	fprintf(stderr,"\t-n: disable chunking. forward only\n");
	fprintf(stderr,"\t-f: prox conf file\n");
	fprintf(stderr,"\t-p: peer conf file\n");
	fprintf(stderr,"\t-a: local host name (for Emulab)\n");
	fprintf(stderr,"\t-h: show options\n");
}
/*-----------------------------------------------------------------*/
int main(int argc, char **argv)
{
	/* process the arguments */
	getOptions(argc, argv);

	if (debug == FALSE) {
		/* daemon code from:
		   http://michael.toren.net/mirrors/sock-faq/unix-socket-faq-4.html */
		/* become a daemon: */
		switch (fork())
		{
			case -1:
				/* can't fork */
				perror ("fork()");
				NiceExit(-1, "fork() failure");
			case 0:                    
				/* child, process becomes a daemon: */
				close(STDIN_FILENO);
				close(STDOUT_FILENO);
				close(STDERR_FILENO);
				if (setsid() == -1) {
					/* request a new session (job control) */
					NiceExit(-1, "setsid() failure");
				}
				break;
			default:                   
				/* parent returns to calling process: */
				return 0;
		}
	}

	/* main program loop */
	WaproxMain(debug, proxconf, peerconf);

	return (0);
}
/*-----------------------------------------------------------------*/
