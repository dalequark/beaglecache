import sys
import os

verbose = False
signature = "!@#FULL_LOG_START!@#"

for filename in sys.argv[1:]:
	# open file
	f = None
	if filename.endswith(".bz2"):
		f = os.popen("bzcat %s" % (filename))
	else:
		f = open(filename, "r")

	totlen = 0
	numobjects = 0
	print filename
	state = 0
	fullRespLen = 0
	while True:
		try:
			if state == 0:
				# 1. process the first line
				# clientIP, endTS, serviceTime, partial_len, contentLength, datalen, url
				line = f.readline()
				totlen += len(line)
				if line == "":
					print "Done. total %d bytes, %d objects"\
							% (totlen, numobjects)
					break
				tmp = line.split()
				if tmp[0] != signature:
					print "can't find the log record signature", signature
					break
				partial_len = int(tmp[4])
				fullRespLen = int(tmp[5])
				url = tmp[6]
				if verbose:
					print "FirtLine:", line.strip()
				state += 1
			elif state == 1:
				# 2. process the request header
				line = f.readline()
				totlen += len(line)
				if len(line) <= 2:
					# end of request header
					assert line.endswith("\n")
					state += 1
				else:
					if verbose:
						print "ReqHdr:", line.strip()
			elif state == 2:
				# 3. process the entire response
				if fullRespLen > 0:
					# read the response header first
					tmpHdrLen = min(fullRespLen, 2048)
					tmpHdr = f.read(tmpHdrLen)
					for line in tmpHdr.split("\n"):
						if len(line) <= 2:
							# end of response header
							break
						if verbose:
							print "RespHdr:", line

					# drain the rest
					f.read(fullRespLen - tmpHdrLen)
					totlen += fullRespLen

				# init state
				state = 0
				fullRespLen = 0
				numobjects += 1
				if verbose:
					print "####### done one object", numobjects, "objects total"
			else:
				assert False
		except:
			print "error processing", filename
			break

	# close file
	if not filename.endswith(".bz2"):
		f.close()
