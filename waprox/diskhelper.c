#include "diskhelper.h"
#include "connection.h"
#include <assert.h>

char* hint_filename = "hints.txt";

/*-----------------------------------------------------------------*/
int CreateDiskHelper()
{
	int fd = CreateSlave(DiskSlaveMain, NULL, TRUE);
	if (fd <= 0) {
		return FALSE;
	}

	g_DiskHelperCI = CreateNewConnectionInfo(fd, ct_disk);
	if (g_DiskHelperCI == NULL) {
		NiceExit(-1, "disk helper connection creation failed\n");
	}

	TRACE("disk helper connection success, fd = %d\n", fd);
	return TRUE;
}
/*-----------------------------------------------------------------*/
void DiskSlaveMain(int fd, void* token)
{
	/* note: blocking I/O */
	FILE* outFp = fopen(hint_filename, "a");
	if (outFp == NULL) {
		NiceExit(-1, "cannot open hint file\n");
	}
	TRACE("%s ready for flushing chunk name hints\n",
			hint_filename);

	int numChunks = 0;
	while (1) {
		/* read the chunk name from the main process */
		int buflen = SHA1_LEN + sizeof(int);
		char buffer[buflen];
		int nReads = read(fd, buffer, buflen);
		if (nReads != buflen) {
			TRACE("fd: %d, ret: %d, %s\n", fd, nReads,
					strerror(errno));
			NiceExit(-1, "read error\n");
		}

		/* write the chunk name into disk */
		int blocklen = *((int*)(buffer + SHA1_LEN));
		char* sha1str = SHA1Str((u_char*)buffer);
		/*TRACE("%s, %d bytes\n", sha1str, blocklen);*/
		fprintf(outFp, "%s %d\n", sha1str, blocklen);
		numChunks++;

		/* flush into disk periodically */
		if (numChunks >= 20) {
			fflush(outFp);
			numChunks = 0;
		}
	}
}
/*-----------------------------------------------------------------*/
void PreloadHints()
{
	FILE* fp = fopen(hint_filename, "r");
	if (fp == NULL) {
		TRACE("cannot open hint file %s, skip preloading\n",
				hint_filename);
		return;
	}

	int numHints = 0;
	int numIgnore = 0;
	TRACE("preloading chunk name hints from file...\n");
	while (1) {
		char sha1name[SHA1_LEN * 2];
		int length = 0;
		if (fscanf(fp, "%s %d\n", sha1name, &length) == EOF) {
			break;
		}
		/*TRACE("%s %d\n", sha1name, length);*/
		assert(length > 0 && length <= MAX_CHUNK_SIZE);
		u_char* digest = SHA1StrR(sha1name);
		/*TRACE("%s %d\n", SHA1Str(digest), length);*/
		if (GetSendHint(digest) == TRUE) {
			numIgnore++;
			if (numIgnore % 1000 == 0) {
				TRACE("%d duplicate hints found. ignore\n",
						numIgnore);
			}
			continue;
		}

		/* put into memory hint table */
		PutSendHintNoDisk(digest, length);
		assert(GetSendHint(digest) == TRUE);
		numHints++;
		if (numHints % 1000 == 0) {
			TRACE("%d hints preloaded\n", numHints);
		}
	}
	TRACE("EOF. %d hints preloaded\n", numHints);
	fclose(fp);
}
/*-----------------------------------------------------------------*/
