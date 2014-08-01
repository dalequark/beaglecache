#!/usr/bin/python

import os

mc = "MQ_SLICE=princeton_wa ../multicopy"
mq = "MQ_SLICE=princeton_wa ../multiquery"
trafficdump = "/home/sihm/project/codeen/waprox/trafficdump/trafficdump.tar.gz"

def execute(cmd):
	print cmd
	os.system(cmd)

# copy binary
#command = "%s %s @:" % (mc, trafficdump)
#execute(command)

# restart (kill/install/start) trafficdump
command = "%s 'wget -O trafficdump.tar.gz http://codeen.cs.princeton.edu/trafficdump.tar.gz; killall python lpclient trafficdump_watchdog; sleep 1; tar zxf trafficdump.tar.gz; ./trafficdump_start; ./node_livetst'" % (mq)
execute(command)
