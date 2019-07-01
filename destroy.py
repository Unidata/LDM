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
(workgroup,username,passwd)=account.readAccount(sys.argv[1])
wg_id=account.getWkGpID(workgroup,username,passwd)
ct_id=account.getCtID(wg_id, sys.argv[2],username,passwd)
values1 = {'method' : 'remove_circuit', 'workgroup_id' : wg_id, 'circuit_id' : ct_id, 'remove_time' : -1}
data = urllib.urlencode(values1, doseq=True)
gh_url2 = 'https://al2s.net.internet2.edu/oess/services-kerb/provisioning.cgi'
req = urllib2.Request(gh_url2, data)
password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url2, username, passwd)
auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
opener = urllib2.build_opener(auth_manager)
urllib2.install_opener(opener)
try:	
	handler = urllib2.urlopen(req)
	result = handler.read()
	jsonData = json.loads(result)
	searchResults = jsonData['results']
except urllib2.URLError:
	jsonData= {'error_text': 'URLError', 'results':None}
except urllib2.HTTPError:
	jsonData = {'error_text': 'HTTPError', 'results':None}
searchResults = jsonData['results']

if (searchResults == None):
        sys.stderr.write("destroy.py: " + jsonData['error_text'] + '\n')
        sys.stderr.flush()
else:
        sys.stderr.write()
        sys.stderr.flush("destroy.py: Success\n")
