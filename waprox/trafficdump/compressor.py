import os
import sys
import time
import datetime
from datetime import date

# global variables
log_dir = "logs"
staging_dir = "staging"
logfile_prefix = "full_log"
bz2_suffix = ".bz2"

def moveToStaging():
	# move at least 2 hours old logs to staging directory
	global log_dir, staging_dir, logfile_prefix
	now = time.mktime(time.localtime())
	print "now:", now

	# skip non log files
	files = []
	for filename in os.listdir(log_dir):
		if not filename.startswith(logfile_prefix):
			continue
		else:
			files.append(filename)

	# sort by filename
	files.sort()

	# skip the most recent file: might be in use
	for filename in files[:-1]:
		print filename
		filetimestr = filename.split(".")[1].split("_")[0]
		fileyear = int(filetimestr[0:4])
		filemonth = int(filetimestr[4:6])
		fileday = int(filetimestr[6:8])
		filehour = int(filetimestr[8:10])
		print fileyear, filemonth, fileday, filehour
		filetime = time.mktime((fileyear, filemonth, fileday, filehour,\
				0,0,0,0,0))
		print "filetime:", filetime
		if (now - filetime) > (60 * 60 * 2):
			# 2 hours old. move
			if not filename.endswith(bz2_suffix):
				# compress it if not
				command = "bzip2 %s/%s" % (log_dir, filename)
				print command
				os.system(command)
				filename += bz2_suffix
			command = "mv %s/%s %s/" % (log_dir, filename, staging_dir)
			print command
			os.system(command)
	print "done"

##########################################
# main
##########################################
if __name__ == "__main__":
	while True:
		try:
			# try processing logs
			moveToStaging()

			# sleep 5 min.
			time.sleep(60 * 5)
		except:
			print "exception in compressor"
