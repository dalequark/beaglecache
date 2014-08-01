#!/usr/bin/python

import os

mc = "MQ_SLICE=princeton_wa ../multicopy"
mq = "MQ_SLICE=princeton_wa ../multiquery"

def execute(cmd):
	print cmd
	os.system(cmd)

# restart trafficdump
command = "%s 'killall python lpclient trafficdump_watchdog; sleep 1; tar zxf trafficdump.tar.gz; ./trafficdump_start; ./node_livetst'" % (mq)
execute(command)
