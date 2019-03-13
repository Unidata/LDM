#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Copyright (C) 2019 University of Virginia. All rights reserved.

file      provision.py
author    Yuanlong Tan <yt4xb@virginia.edu>
version   1.0
date      Mar. 1, 2019
brief     create circuit and add entry onto circuit
"""

import sys
import urllib2
import urllib
import json
import account
import edit
from subprocess import call
global values1
values1 = {}
link = []
(username,passwd)=account.readAccount(sys.argv[2])
link=account.getSPath(sys.argv[4], sys.argv[7],username,passwd)
wg_id=account.getWkGpID(sys.argv[1],username,passwd)
ct_id=account.getCtID(wg_id, sys.argv[3],username,passwd)
if ct_id == 0:
	values1 = {'method' : 'provision_circuit', 'workgroup_id' : wg_id, 'provision_time' : -1, 'remove_time' : -1, 'description' : sys.argv[3], 'link' : link, 'node' : [sys.argv[4], sys.argv[7]], 'interface' : [sys.argv[5], sys.argv[8]], 'tag' : [sys.argv[6], sys.argv[9]]}
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
else:
	searchResults = edit.edit_endpoint(wg_id,sys.argv[4],sys.argv[5],sys.argv[6],ct_id,"add",username,passwd)

if (searchResults == None):
        sys.stderr.write("provision.py: " + jsonData['error_text'] + '\n')
else:
	circuit_id = searchResults['circuit_id']
	file_name = str('circuit_id.log')
	f = open (file_name, 'w')
	f.write(circuit_id + '\n')
	f.close()
        sys.stdout.write(circuit_id + '\n')
