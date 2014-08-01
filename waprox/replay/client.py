#!/usr/bin/python

import select
import socket
import time
import sys

if len(sys.argv) < 5:
	print "usage: %s host port clients objects [mode]" % (sys.argv[0])
	print "\tmode a: make clients all at once"
	print "\tmode b: gradually increase clients [default]"
	sys.exit(0)

host = sys.argv[1]
port = int(sys.argv[2])
size = (128 * 1024)
maxClients = int(sys.argv[3])
maxObjects = int(sys.argv[4])
mode = "b"
if len(sys.argv) == 6:
	mode = sys.argv[5]
errors = 0
objs = range(0, maxObjects)
clients = {}
input = []
output = []

# socket, start time, object ID, bytes read
def makeClient():
	client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	client.setblocking(0)
	client.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	errno = client.connect_ex((host, port))
	if errno < 0 and errno != EINPROGRESS:
		print "connect failed", os.strerror(errno)
		errors += 1
		return
	assert not clients.has_key(client)
	clients[client] = [objs.pop(0), 0, time.time()]
	output.append(client)

def finishClient(s):
	assert clients.has_key(s)
	objID = clients[s][0]
	bytes = clients[s][1]
	timegap = time.time() - clients[s][2]
	print "ID %d %d bytes %f sec done %d clients active %d objs remains" % (objID, bytes, timegap, len(clients), len(objs))
	del clients[s]
	input.remove(s)
	s.close()
	throughput = totalBytes * 8 / (time.time() - starttime) / 1000
	print "%d objects %d clients %d errors aggregate throughput: %.2f kbps" % (maxObjects, maxClients, errors, throughput)

	# check end sequence
	if len(objs) == 0 and len(clients) == 0:
		sys.exit(0)

	# make a new client
	numtry = 0
	if mode == "a":
		# maintain the current number
		numtry = 1
	else:
		# doubles the client connections
		numtry = 2

	while len(clients) < maxClients and numtry >= 1 and len(objs) > 0:
		makeClient()
				

# start with N clients
starttime = time.time()
last_activity = starttime
for i in range(maxClients):
	makeClient()
	if mode == "b":
		# start with just one
		break

running = 1
totalBytes = 0
while running:
	inputready,outputready,exceptready = select.select(input,output,[], 5)
	for s in inputready:
		last_activity = time.time()
		try:
			data = s.recv(size)
		except socket.error, msg:
			# some error happend..
			timegap = time.time() - clients[s][2]
			print msg, timegap, "seconds"
			del clients[s]
			input.remove(s)
			s.close()
			errors += 1
			continue

		if data:
			assert clients.has_key(s)
			clients[s][1] += len(data)
			totalBytes += len(data)
			#print len(data), "bytes received"
		else:
			finishClient(s)

	for s in outputready:
		last_activity = time.time()
		assert clients.has_key(s)
		objID = clients[s][0]
		request = "%d\r\n" % (objID)
		try:
			nSend = s.send(request)
		except socket.error, msg:
			# some error happend..
			timegap = time.time() - clients[s][2]
			print msg, timegap, "seconds"
			del clients[s]
			output.remove(s)
			s.close()
			errors += 1

			# try again
			#objs.insert(0, objID)
			#makeClient()
			continue

		# success
		assert len(request) == nSend
		output.remove(s)
		input.append(s)

	if (time.time() - last_activity) > 60:
		print "too much idle time (60 sec), exit"
		sys.exit(0)
