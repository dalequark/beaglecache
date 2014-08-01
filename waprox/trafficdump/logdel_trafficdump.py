#!/usr/bin/python

import os

mc = "MQ_SLICE=princeton_wa ../multicopy"
mq = "MQ_SLICE=princeton_wa ../multiquery"

def execute(cmd):
	print cmd
	os.system(cmd)

# kill trafficdump
command = "%s 'rm -rf logs staging'" % (mq)
execute(command)
