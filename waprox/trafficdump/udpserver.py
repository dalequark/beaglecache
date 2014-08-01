import urllib2
import Queue
import socket
import select
import os
import sys
import time
import datetime
import urlnorm
import string
from datetime import date
from threading import Thread
from threading import RLock

# global variables
num_success = 0
num_failure = 0
num_cancelled = 0
num_robots = 0
logfd = None
curLogFileName = ""
logfilesize = 0
logfilecreationtime = 0
logthreshold = 100 * 1024 * 1024 # 100MB
filethreshold = 2 * 1024 * 1024 # 2MB
#logthreshold = 1024 * 1024 # 1MB
#filethreshold = 500 * 1024 # 500KB
num_workers = 0
lock = RLock()
partialbytes = 0
idealbytes = 0
log_dir = "logs"
staging_dir = "staging"
tmpfile_prefix = "tmpLargeObject"
logfile_prefix = "full_log"
bz2_suffix = ".bz2"
gz_suffix = ".gz"
respHdrLimit = 25*1024
cancelThreshold = 100 * 1024
logfiletimethreshold = 60*30 # 30 min.

##############################################
# Worker Thread class
##############################################
class Worker(Thread):
        def __init__(self, queue, id):
                self.__queue = queue
		self.__id = id
		self.__numjobs = 0
                Thread.__init__(self)

	def getNumJobs(self):
		return self.__numjobs

        def run(self):
		global num_success, num_failure, num_cancelled, num_workers, num_robots
                while True:
			try:
				item = self.__queue.get()
				if item == None:
					# stop thread
					break
				tmp = item.split("\n")[0].split()
				print tmp
				logTime = tmp[0]
				clientIP = tmp[1]
				partial_len = int(tmp[2])
				url = tmp[3]
				header = item.split("\n")[1]
				self.__numjobs += 1
				fetchPage(self.__id, logTime, clientIP, url, partial_len, header)
				print "[Worker %d] success" % (self.__id)
			except:
				print "[Worker %d] thread exception, but keep running" % (self.__id)
			print "STAT: %d success, %d failure, %d cancelled, %d remains, %d robots" \
				% (num_success, num_failure, num_cancelled, self.__queue.qsize(), num_robots)
		# shoudn't be reach here
		print "thread finish", self.__id
		num_workers -= 1

def initThreadPool(max_threads):
	global num_workers
        queue = Queue.Queue(0)
        workers = []
        for i in range(max_threads):
                worker = Worker(queue, i)
                workers.append(worker)
                worker.start()
		num_workers += 1
        return (queue, workers)

def fetchCleanup(s, tmpFile):
	s.close()
	if tmpFile != None:
		tmpFile.close()

def processResponseHeader(response):
	# process response header
	firstLine = True
	tmpLen = 0
	rcode = 0
	headerLength = -1
	for line in response.split("\n"):
		tmpLen += len(line)
		if firstLine:
			# read response code
			firstLine = False
			rcode = int(line.split()[1])
		if len(line) <= 2:
			# found the end of the header
			headerLength = tmpLen
			break
	return (headerLength, rcode)

def fetchPage(workerid, logTime, clientIP, url, partial_len, header):
	global num_success, num_failure, num_cancelled, respHdrLimit
	global log_dir, staging_dir, logfile_prefix, tmpfile_prefix

	# build a request
	startTS = time.time()
	url = urlnorm.normalize(url)
	#print "### processing:", url
	host = url.split("/")[2]
	port = 80
	if len(host.split(":")) > 1:
		port = int(host.split(":")[1])
	path = ""
	for item in url.split("/")[3:]:
		path += "/" + item
	#agent = "Mozilla/5.0"
	#print url
	request = "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n" % (path, host)
	orghdr = ""
	for hdrline in header.split("\0\0"):
		orghdr += hdrline + "\r\n"
		#if hdrline.startswith("x-"):
		#	# skip codeen specific optional header
		#	continue
		if hdrline.startswith("Connection:"):
			# we've already set this properly
			continue
		if hdrline.startswith("Host:"):
			# we've already set this properly
			continue
		request += hdrline + "\r\n"
	if not request.endswith("\r\n\r\n"):
		request += "\r\n"
	if not orghdr.endswith("\r\n\r\n"):
		orghdr += "\r\n"
	#print request

	# make a connection
	s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	errno = s.connect_ex((host, port))
	if errno < 0:
		print "connect failed", os.strerror(errno)
		num_failure += 1
		s.close()
		return

	# send the request
	nSend = 0
	try:
		nSend = s.send(request)
	except socket.error, msg:
		print "send error", msg
		num_failure += 1
		s.close()
		return
	assert len(request) == nSend

	# receive the response
	response = ""
	response_len = 0
	fileWriting = False
	tmpFile = None
	tmpFileName = "%s/%s.%02d" % (log_dir, tmpfile_prefix, workerid)
	rcode = 0
	headerLength = -1

	while True:
		try:
			data = s.recv(128 * 1024)
			if data:
				# keep reading
				response += data
				response_len += len(data)

				# process response header
				if headerLength == -1:
					(headerLength, rcode) = processResponseHeader(data)
					if headerLength == -1 and len(data) >= respHdrLimit:
						print "can't find response header in %d bytes data. give up" % (respHdrLimit)
						num_failure += 1
						fetchCleanup(s, tmpFile)
						return

				# if it's too large, write to a file
				if fileWriting is False and response_len > filethreshold:
					fileWriting = True
					tmpFile = open(tmpFileName, "w")

				# if it's too large, write it to a file
				if fileWriting:
					tmpFile.write(response)
					response = ""

				# check body length
				if headerLength != -1:
					if abs(response_len - partial_len) > cancelThreshold and response_len >= (partial_len * 1.5):
						# we stop here
						print "STOP! cur_len: %d partial_len: %d" % (response_len, partial_len)
						num_cancelled += 1
						break
			else:
				# connection closed
				break
		except socket.error, msg:
			print "recv error", msg
			num_failure += 1
			fetchCleanup(s, tmpFile)
			return

	endTS = time.time()
	fetchCleanup(s, tmpFile)
	if rcode >= 200 and rcode < 400:
		num_success += 1
	else:
		num_failure += 1

	# write them to file
	if fileWriting:
		writeLogFile(logTime, request, clientIP, endTS, int((endTS - startTS)*1000), url, partial_len, response_len, None, tmpFileName)
	else:
		writeLogFile(logTime, request, clientIP, endTS, int((endTS - startTS)*1000), url, partial_len, response_len, response, tmpFileName)

def generateLogFileName():
	global log_dir, staging_dir, logfile_prefix, tmpfile_prefix
	now = datetime.datetime.fromtimestamp(time.time())
	index = 0
	while True:
		filename = "%s/%s.%04d%02d%02d%02d_%03d" % (log_dir, logfile_prefix, now.year, now.month, now.day, now.hour, index)
		print filename
		if os.path.exists(filename):
			# already exists
			index += 1
			continue
		elif os.path.exists(filename + bz2_suffix):
			# already exists
			index += 1
			continue
		elif os.path.exists(filename + gz_suffix):
			# already exists
			index += 1
			continue
		else:
			# we can use this file
			return filename

def writeLogFile(logTime, orghdr, clientIP, endTS, serviceTime, url, partial_len, datalen, data, tmpFileName):
	global logfd, logfilesize, logfilecreationtime, logthreshold, lock, partialbytes, idealbytes, curLogFileName
	global log_dir, staging_dir, logfile_prefix, tmpfile_prefix
	lock.acquire()
	if logfd == None:
		# open one
		curLogFileName = generateLogFileName()
		logfd = open(curLogFileName, "w")
		logfilecreationtime = time.time()

	print endTS, serviceTime, "ms", datalen, "bytes", url
	logfd.write("!@#FULL_LOG_START!@# %s %s %.3f %d %d %d %s\r\n" % (logTime, clientIP, endTS, serviceTime, partial_len, datalen, url))
	logfd.write(orghdr)
	if data == None:
		# file
		tmpfd= open(tmpFileName, "r")
		while True:
			data = tmpfd.read(128*1024)
			if data == "":	
				# we're done
				break
			else:
				logfd.write(data)
		print "### tmp file writing done", datalen, "bytes"
		tmpfd.close()
		print "### removing", tmpFileName
		os.remove(tmpFileName)
	else:
		logfd.write(data)
	logfilesize += datalen
	idealbytes += datalen
	partialbytes += partial_len
	logfd.flush()
	if logfilesize >= logthreshold or (time.time() - logfilecreationtime) > logfiletimethreshold:
		# close and make a new one
		logfd.close()
		logfilesize = 0
		oldLogFileName = curLogFileName
		curLogFileName = generateLogFileName()
		logfd = open(curLogFileName, "w")
		logfilecreationtime = time.time()
	lock.release()

def cleanExit(queue, workers):
	queue.empty()
	for i in range(len(workers)):
		queue.put(None)
	for worker in workers:
		worker.join()
	print "all thread joined: clean exit"
	sys.exit(0)

def processUDP(s, queue, workers):
	global partialbytes, idealbytes, num_workers, max_workers, num_robots
	try:
		# udp server socket
		data, addr = s.recvfrom(1500)
		#for line in data.split("\0\0"):
		#	print line
		#for i in range(len(data)):
		#	print data[i], ord(data[i])
		#print data
		tmp = data.split("\n")[0]
		#print "RECV:", tmp
		if int(tmp.split()[2]) <= 0:
			return
		if tmp.split("\"")[1] != "Human":
			# robot
			num_robots += 1
			return
		queue.put(data)
		worker_stats = []
		for worker in workers:
			worker_stats.append(worker.getNumJobs())
		duration = time.time() - starttime
		mbps1 = float(partialbytes) * 8 / duration / 1000000
		mbps2 = float(idealbytes) * 8 / duration / 1000000
		print "### queue length:", queue.qsize()
		print "### num workers:", num_workers, worker_stats
		print "### traffic: partial %.3f Mbps (ideal %.3f Mbps)" % (mbps1, mbps2)
		if num_workers != max_workers:
			print "some worker thread died"
			cleanExit(queue, workers)
	except socket.error, msg:
		print "recv error", msg
		cleanExit(queue, workers)

def processTCP(s):
	try:
		client, address = s.accept()
		print "got TCP connection %d from %s" % (client.fileno(), address)
		data = client.recv(1024)
		print data
		client.send("I'm alive!\r\n")
		client.close()
	except:
		print "TCP error?"

##########################################
# main
##########################################
if __name__ == "__main__":
	if len(sys.argv) < 2:
		print "usage %s num_workers" % (sys.argv[0])
		sys.exit(0)

	# create log/staging directories
	os.system("mkdir %s %s" % (log_dir, staging_dir))

	# delete tmpLargeObject* files
	os.system("rm %s/%s*" % (log_dir, tmpfile_prefix))

	# setting up server
	max_workers = int(sys.argv[1])
	print "create %d worker threads" % (max_workers)
	(queue, workers) = initThreadPool(max_workers)
	udpport = 9989
	tcpport = 9990
	UDPSock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	UDPSock.bind(("0.0.0.0", udpport))
	print "UDP listens on", udpport
	TCPSock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	TCPSock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	TCPSock.bind(("0.0.0.0", tcpport))
	TCPSock.listen(5)
	print "TCP listens on", tcpport
	input = [TCPSock, UDPSock]
	output = []
	print "old socket timeout:", socket.getdefaulttimeout()
	socket.setdefaulttimeout(30)
	print "new socket timeout:", socket.getdefaulttimeout()
	print "server starts..."
	starttime = time.time()
	while True:
		try:
			inputready, outputready, exceptready = select.select(input, output, [], 5)
			for s in inputready:
				if s == UDPSock:
					processUDP(s, queue, workers)
				elif s == TCPSock:
					processTCP(s)
				else:
					assert False
				sys.stdout.flush()
				sys.stderr.flush()
		except:
			cleanExit(queue, workers)
