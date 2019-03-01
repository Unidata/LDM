#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Copyright (C) 2019 University of Virginia. All rights reserved.

file      desroy.py
author    Yuanlong Tan <yt4xb@virginia.edu>
version   1.0
date      Mar. 1, 2019
brief     destroy the circuits
"""
import urllib2
import urllib
import sys
import string
import json
import account
import edit
from subprocess import call
global values1
(username,passwd)=account.readAccount(sys.argv[2])
wg_id=account.getWkGpID(sys.argv[1],username,passwd)
ct_id=account.getCtID(wg_id, sys.argv[3],username,passwd)
values1 = {'method' : 'remove_circuit', 'workgroup_id' : wg_id, 'circuit_id' : ct_id, 'remove_time' : -1}
data = urllib.urlencode(values1, doseq=True)
gh_url2 = 'https://al2s.net.internet2.edu/oess/services-kerb/provisioning.cgi'
req = urllib2.Request(gh_url2, data)
password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url2, username, passwd)
auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
opener = urllib2.build_opener(auth_manager)
urllib2.install_opener(opener)
handler = urllib2.urlopen(req)
result = handler.read()
jsonData = json.loads(result)
searchResults = jsonData['results']

if (searchResults == None):
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log",jsonData['error_text']])
else:
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Remove.py: Success"])
