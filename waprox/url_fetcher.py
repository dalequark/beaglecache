import urllib2
import Queue
import sys
import random
import time
from threading import Thread

def fetchPage(url):
	request = urllib2.Request(url)
	request.add_header("User-Agent", "Mozilla/5.0")
	try:
		response = urllib2.urlopen(request)
	except urllib2.HTTPError, e:
		#print "HTTPError:", e.code
		return (None, "")
	except urllib2.URLError, e:
		#print "URLError:", e.reason
		return (None, "")
	else:
		the_page = response.read()
		#print the_page
		#print response.info().gettype()
		#print response.geturl()
		return (response, the_page)

class Worker(Thread):
        def __init__(self, queue, id):
                self.__queue = queue
                Thread.__init__(self)
		self.id = id

        def run(self):
		totSuccess = 0
		totFailure = 0
                while True:
			# some random pause time here
			time.sleep(random.randint(0,10))
		        obj = self.__queue.get()
                        if obj is None:
                                # done!
                                break
			try:
				r, content = fetchPage(obj[1])
				print "success %d, %s" % (obj[0], obj[1])
				totSuccess += 1
				print "Thread [%d]: %d / %d" % (self.id, \
						totSuccess, totFailure)
			except:
				print "failure %d, %s" % (obj[0], obj[1])
				totFailure += 1
				True

def initThreadPool(maxClients):
	queue = Queue.Queue(0)
	workers = []
	for i in range(maxClients):
		worker = Worker(queue, i)
		workers.append(worker)
		worker.start()
	return (queue, workers)

def joinThreadPool(queue, workers, maxClients):
	for i in range(maxClients):
		# end of task queue marker
		queue.put(None)
	for worker in workers:
		worker.join()

def threadMain():
	if len(sys.argv) < 3:
		print "Usage: python %s '# clients' 'url list files'"\
				% (sys.argv[0])
		sys.exit(0)

	numClients = int(sys.argv[1])
	proxy_support = urllib2.ProxyHandler(\
		{"http" : "http://spring.cs.princeton.edu:55555"})
	opener = urllib2.build_opener(proxy_support)
	urllib2.install_opener(opener)

	(queue, workers) = initThreadPool(numClients)
	number = 0

	for urlfile in sys.argv[2:]:
		try:
			f = open(urlfile, 'r')
			while True:
				line = f.readline()
				if line == '':
					# EOF
					break
				url = line.split()[0].split('"')[1]
				queue.put((number, url))
				number += 1
		except:
			print "error open file:", urlfile
			True
	joinThreadPool(queue, workers, numClients)

threadMain()
