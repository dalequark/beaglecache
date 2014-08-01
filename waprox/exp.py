#!/usr/bin/python
import sys
import os
import time

client = "sihm@128.112.139.209" # sea.cs.princeton.edu
waprox1 = "sihm@spring.cs.princeton.edu"
waprox2 = "sihm@corona.cs.princeton.edu"
waprox1_ip = "128.112.139.204"
waprox2_ip = "128.112.139.220"
server = "sihm@corona.cs.princeton.edu"

def runCommand(command):
	print command
	os.system(command)
	time.sleep(2)

def getLogFileName(prefix, bandwidth, delay, chunking, ways, min, max):
	return "%s_b%05d_d%04d_%s_w%02d_min%06d_max%06d" % (prefix, bandwidth, delay, chunking, ways, min, max)

def runReplay(replayClient, replayServer, bandwidth, delay, tracefile, replay_log):
	print "========================================================"
	print "\trun replay, bandwidth: %d kbps, delay: %d ms" % (bandwidth, delay)
	print "========================================================"
	# set delay and bandwidth
	runCommand("ssh %s %s" % (waprox1, "\"./remove_delay.sh\""))
	runCommand("ssh %s %s" % (waprox2, "\"./remove_delay.sh\""))
	if bandwidth > 0:
		runCommand("ssh %s %s" % (waprox1, "\"./add_delay.sh %s %s %s\"" % (bandwidth, delay/2, waprox2_ip)))
		runCommand("ssh %s %s" % (waprox2, "\"./add_delay.sh %s %s %s\"" % (bandwidth, delay/2, waprox1_ip)))

	cmd_run_replay_server = "\"killall replay; ./replay -d %s &> %s &\"" % (tracefile, replay_log)
	cmd_run_replay_client = "\"killall replay; ./replay -a corona %s &> %s\"" % (tracefile, replay_log)
	#cmd_run_replay_client = "\"killall replay; ./replay -a corona %s\"" % (tracefile)
	runCommand("ssh %s %s" % (replayServer, cmd_run_replay_server))
	runCommand("ssh %s %s" % (replayClient, cmd_run_replay_client))
	#test_client = "\"wget http://nclab.kaist.ac.kr/~shihm/BMQ.tar\""
	#runCommand("ssh %s %s" % (client, test_client))

	print "========================================================"
	print "\tkill replay"
	print "========================================================"
	cmd_kill_replay = "\"killall replay\"";
	runCommand("ssh %s %s" % (replayClient, cmd_kill_replay))
	runCommand("ssh %s %s" % (replayServer, cmd_kill_replay))

def oneExp(bandwidth, delay, chunking, min, max, ways, tracefile):
	print "========================================================"
	print "\trun waprox"
	print "========================================================"
	if chunking == "on":
		forwardOption = ""
	else:
		forwardOption = "-n"
	cmd_run_waprox = "\"killall waprox cache; rm dbg_waprox* stat_waprox* exlog_waprox*; ./waprox -l %d -r %d -w %d %s ;./cache -F hashcache.conf &> cache_log &\"" % (min, max, ways, forwardOption)
	runCommand("ssh %s %s" % (waprox1, cmd_run_waprox))
	runCommand("ssh %s %s" % (waprox2, cmd_run_waprox))

	replay_log = "%s.txt" % (getLogFileName("replay_log", bandwidth, delay, chunking, ways, min, max))
	runReplay(client, server, bandwidth, delay, tracefile, replay_log)

	print "========================================================"
	print "\tkill waprox"
	print "========================================================"
	cmd_kill_waprox = "\"killall waprox cache\""
	runCommand("ssh %s %s" % (waprox1, cmd_kill_waprox))
	runCommand("ssh %s %s" % (waprox2, cmd_kill_waprox))

	# reset delay and bandwidth
	runCommand("ssh %s %s" % (waprox1, "\"./remove_delay.sh\""))
	runCommand("ssh %s %s" % (waprox2, "\"./remove_delay.sh\""))

	print "========================================================"
	print "\tgenerate waprox logs"
	print "========================================================"
	waprox_log = "%s.tar.bz2" % (getLogFileName("waprox_log", bandwidth, delay, chunking, ways, min, max))
	cmd_copy_logs = "\"tar cjf %s dbg_waprox* stat_waprox* exlog_waprox*\"" % (waprox_log)
	runCommand("ssh %s %s" % (waprox1, cmd_copy_logs))
	runCommand("ssh %s %s" % (waprox2, cmd_copy_logs))

def oneThroughputExp(bandwidth, delay, minChunkSize, maxChunkSize, numWays, trace):
	print "########################################################"
	print "\tdirect replay"
	print "########################################################"
	runReplay(waprox1, waprox2, bandwidth, delay, trace, "replay_log_b%05d_d%04d_direct.txt" % (bandwidth, delay))

	print "########################################################"
	print "\treplay w/ chunking"
	print "########################################################"
	oneExp(bandwidth, delay, "on", minChunkSize, maxChunkSize, numWays, trace)

	print "########################################################"
	print "\treplay w/o chunking"
	print "########################################################"
	oneExp(bandwidth, delay, "off", minChunkSize, maxChunkSize, numWays, trace)

def SingleResolutionChunkTest(minChunkSize, maxChunkSize, trace):
	# single size chunk scheme
	chunkSize = maxChunkSize
	while True:
		if chunkSize < minChunkSize:
			break
		oneExp(0, 0, "on", chunkSize, chunkSize, 1, trace)
		chunkSize /= 2

def MultiResolutionChunkTest(minChunkSize, maxChunkSize, trace):
	# multi-resolution chunk scheme
	numWays = [2, 4, 8, 16, 32, 64]
	for i in numWays:
		chunkSize = maxChunkSize / i
		while True:
			if chunkSize < minChunkSize:
				break
			oneExp(0, 0, "on", chunkSize, maxChunkSize, i, trace)
			chunkSize /= i

def ThroughputTest(minChunkSize, maxChunkSize, numWays, trace):
	# 10kbps, 100ms
	oneThroughputExp(0, 0, minChunkSize, maxChunkSize, numWays, trace)
	# 56kbps, 100ms
	#oneThroughputExp(56, 100, minChunkSize, maxChunkSize, numWays, trace)
	# 384kbps, 30ms
	#oneThroughputExp(384, 30, minChunkSize, maxChunkSize, numWays, trace)
	# 10Mbps, 100ms
	#oneThroughputExp(10000, 100, minChunkSize, maxChunkSize, numWays, trace)

trace_small = "1M.news"
#SingleResolutionChunkTest(32, 65536, trace_small)
#MultiResolutionChunkTest(32, 65536, trace_small)
ThroughputTest(1024, 65536, 8, trace_small)
