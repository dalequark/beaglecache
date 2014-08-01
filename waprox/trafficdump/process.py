import os
import sys
from threading import Thread
import Queue

##############################################
# Worker Thread class
##############################################
class Worker(Thread):
        def __init__(self, queue, id):
                self.__queue = queue
		self.__id = id
                Thread.__init__(self)

        def run(self):
                while True:
			try:
				item = self.__queue.get()
				if item == None:
					# stop thread
					break
				#print item
				#runCommand(item)
				processOneFile(item)
				print "[Worker %d] done, %d remains" \
					% (self.__id, self.__queue.qsize())
			except:
				print "[Worker %d] thread exception, but keep running" \
					% (self.__id)
		# shoudn't be reach here
		print "thread finish", self.__id

def initThreadPool(max_threads):
        queue = Queue.Queue(0)
        workers = []
        for i in range(max_threads):
                worker = Worker(queue, i)
                workers.append(worker)
                worker.start()
        return (queue, workers)

def runCommand(cmd):
	print cmd
	#os.system(cmd)

def processOneFile(filename, verbose = False):
	signature = "!@#FULL_LOG_START!@#"

	# open file
	logname = filename.replace("full_log", "summary")
	print logname
	logfile = open(logname, "w")
	f = None
	isBz2 = False
	if filename.endswith(".bz2"):
		f = os.popen("bzcat %s" % (filename))
		isBz2 = True
	else:
		f = open(filename, "r")

	totlen = 0
	numobjects = 0
	print filename
	state = 0
	partial_len = 0
	fullRespLen = 0
	content_len = 0
	clientIP = "-"
	endTS = 0
	serviceTime = 0
	url = "-"
	type = "-"
	while True:
		try:
			if state == 0:
				# 1. process the first line
				# header, clientIP, endTS, serviceTime, partial_len, datalen, url
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
				clientIP = tmp[1]
				endTS = float(tmp[2])
				serviceTime = int(tmp[3])
				partial_len = int(tmp[4])
				fullRespLen = int(tmp[5])
				content_len = 0
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
					respHdrLen = 0
					for line in tmpHdr.split("\n"):
						if len(line) <= 2:
							# end of response header
							respHdrLen += len(line) + 1
							break
						respHdrLen += len(line) + 1
						if line.startswith("Content-Length:"):
							content_len = int(line.split()[1])
						if line.startswith("Content-Type:"):
							type = line[13:].strip()
						if verbose:
							print "RespHdr:", line

					# drain the rest
					if isBz2:
						f.read(fullRespLen - tmpHdrLen)
					else:
						f.seek(fullRespLen - tmpHdrLen, 1)
					totlen += fullRespLen
					agent_len = fullRespLen - respHdrLen
					logline = "%.03f %d %s %d %d %d \"%s\" \"%s\"" % \
						(endTS, serviceTime, clientIP, partial_len, agent_len, content_len, type, url)
					#print logline
					logfile.write(logline + "\n")

				# init state
				state = 0
				partial_len = 0
				fullRespLen = 0
				content_len = 0
				clientIP = "-"
				endTS = 0
				serviceTime = 0
				url = "-"
				type = "-"

				numobjects += 1
				if verbose:
					print "####### done one object", numobjects, "objects total"
			else:
				assert False
		except:
			print "error processing", filename
			break

	# close file
	if not isBz2:
		f.close()
	logfile.close()

	return (totlen, numobjects)

def getDate(dateStr):
	year = int(dateStr[0:4])
	month = int(dateStr[4:6])
	day = int(dateStr[6:8])
	return (year, month, day)

def compareDate(dateStr1, dateStr2):
	date1 = getDate(dateStr1)
	date2 = getDate(dateStr2)

	# compare year
	if date1[0] > date2[0]:
		return 1
	if date1[0] < date2[0]:
		return -1

	# compare month
	if date1[1] > date2[1]:
		return 1
	if date1[1] < date2[1]:
		return -1

	# compare day
	if date1[2] > date2[2]:
		return 1
	if date1[2] < date2[2]:
		return -1
	
	# the same date!
	return 0

def uncompress(queue, rootdir, outdir, startDate, endDate):
	totbytes = 0
	totobjs = 0
	for monthdir in os.listdir(rootdir):
		cmd = "mkdir data/%s" % (monthdir)
		runCommand(cmd)
		for nodedir in os.listdir(rootdir + "/" + monthdir):
			cmd = "mkdir data/%s/%s" % (monthdir, nodedir)
			runCommand(cmd)
			for file in os.listdir(rootdir + "/" + monthdir + "/" + nodedir):
				tmp = file.split(".")[1]
				if compareDate(tmp, startDate) < 0 or compareDate(tmp, endDate) > 0:
					# out of range
					continue

				filename = rootdir + "/" + monthdir + "/" + nodedir + "/" + file

				# uncompress it
				cmd = "bzcat %s > %s/%s/%s/%s" % (filename, outdir, monthdir, nodedir, file[:-4])
				queue.put(cmd)

def processMain(queue, rootdir, startDate, endDate):
	totbytes = 0
	totobjs = 0
	for monthdir in os.listdir(rootdir):
		for nodedir in os.listdir(rootdir + "/" + monthdir):
			for file in os.listdir(rootdir + "/" + monthdir + "/" + nodedir):
				tmp = file.split(".")[1]
				if compareDate(tmp, startDate) < 0 or compareDate(tmp, endDate) > 0:
					# out of range
					continue

				filename = rootdir + "/" + monthdir + "/" + nodedir + "/" + file
				queue.put(filename)
				#(byte, objs) = processOneFile(filename)
				#totbytes += byte
				#totobjs += objs
				#print "total %d objects, %s bytes" % (totobjs, totbytes)

if __name__ == "__main__":
	num_workers = 4
	(queue, workers) = initThreadPool(num_workers)
	#uncompress(queue, "../princeton_wa", "data", "20100325", "20100325")
	processMain(queue, "data", "20100325", "20100331")
	for i in range(num_workers):
		queue.put(None)
	print "all done. wait for workers"
