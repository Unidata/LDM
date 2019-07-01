#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Copyright (C) 2019 University of Virginia. All rights reserved.

file      remove.py
author    Yuanlong Tan <yt4xb@virginia.edu>
version   1.0
date      Mar. 1, 2019
brief     remove entry from the circuits
"""
import urllib2
import urllib
import sys
import string
import json
import account
import edit
from subprocess import call
global values1, values2
node = []
values1 = {}
values2 = {}
(workgroup,username,passwd)=account.readAccount(sys.argv[1])
wg_id=account.getWkGpID(workgroup,username,passwd)
ct_id=account.getCtID(wg_id, sys.argv[2],username,passwd)
gh_url = 'https://al2s.net.internet2.edu/oess/services-kerb/data.cgi'
values1 = {'method' : 'get_circuit_details', 'circuit_id' : ct_id}
data1 = urllib.urlencode(values1, doseq=True)
req1 = urllib2.Request(gh_url, data1)
password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url, username, passwd)
auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
opener = urllib2.build_opener(auth_manager)
urllib2.install_opener(opener)
handler1 = urllib2.urlopen(req1)
result1 = handler1.read()
jsonData1 = json.loads(result1)
searchResults1 = jsonData1['results']['endpoints']
for er in searchResults1:
	    if er['node'] != None:
		node.append(er['node'])
if len(node) > 2:
	jsonData = edit.edit_endpoint(wg_id,sys.argv[3],sys.argv[4],sys.argv[5],ct_id,"del",username,passwd)
	searchResults = jsonData['results']
else:
	values2 = {'method' : 'remove_circuit', 'workgroup_id' : wg_id, 'circuit_id' : ct_id, 'remove_time' : -1}
	data2 = urllib.urlencode(values2, doseq=True)
	gh_url2 = 'https://al2s.net.internet2.edu/oess/services-kerb/provisioning.cgi'
	req = urllib2.Request(gh_url2, data2)
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
        sys.stderr.write("remove.py: " + jsonData['error_text'] + '\n')
else:
        sys.stderr.write("remove.py: Success\n")
