#!/usr/bin/python

import sys

###################################################
# read command line arguments
###################################################
isSWaprox = False
SWaproxAddr = ""

if len(sys.argv) == 2:
	# S-Waprox
	isSWaprox = True
elif len(sys.argv) == 3:
	# R-Waprox
	isSWaprox = False
	SWaproxAddr = sys.argv[2]
else:
	print "Usage: python %s <this host addr> [S-Waprox address]"\
			% (sys.argv[0])
	sys.exit(0)

ip = sys.argv[1]

###################################################
# generate peer.conf
###################################################
f = open("peer.conf", 'w')
if isSWaprox:
	print "configuring S-Waprox..."
	f.write("%s\n" % (ip))
else:
	print "configuring R-Waprox..."
	f.write("%s\n%s\n" % (SWaproxAddr, ip))
f.close()

###################################################
# generate start/stop scripts
###################################################
print "making start/stop scripts..."
f = open("start_edgexl.py", 'w')
cmd = "bash -c 'killall waprox cache; rm dbg_waprox* stat_waprox* exlog_waprox*; mkdir log dat; (./cache -F hashcache.conf &> cache_log &); ./waprox -a %s'" % (ip)
f.write("#!/usr/bin/python\n")
f.write("import os\n")
f.write("cmd = \"%s\"\n" % (cmd))
f.write("os.system(cmd)\n")
f.close()

f = open("stop_edgexl.py", 'w')
cmd = "killall waprox cache"
f.write("#!/usr/bin/python\n")
f.write("import os\n")
f.write("cmd = \"%s\"\n" % (cmd))
f.write("os.system(cmd)\n")
f.close()

print "DONE!"
print "to start EdgeXL: python start_edgxl.py"
print "to stop EdgeXL: python stop_edgxl.py"
