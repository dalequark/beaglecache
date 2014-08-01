#!/usr/bin/python

import os

mc = "MQ_SLICE=princeton_codeen ../multicopy"
mq = "MQ_SLICE=princeton_codeen ../multiquery"
req_redirect = "/home/sihm/project/codeen/redir_source/api-modules/codeen/req_redirect"
waprox_conf = "/home/sihm/project/codeen/redir_source/api-modules/codeen/waprox_forward.conf"

def execute(cmd):
	print cmd
	os.system(cmd)

# copy new redirector module and conf file
command = "%s %s %s @:CoDeeN/usr/local/prox/api-modules/codeen/" % (mc, waprox_conf, req_redirect)
#command = "%s %s @:" % (mc, req_redirect)
#execute(command)

# restart prox
command = "%s './prox_stop; cp req_redirect CoDeeN/usr/local/prox/api-modules/codeen/; cp waprox_forward.conf var/; ./prox_start'" % (mq)
execute(command)
