#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Copyright (C) 2019 University of Virginia. All rights reserved.

file      account.py
author    Yuanlong Tan <yt4xb@virginia.edu>
version   1.0
date      Mar. 1, 2019
brief     Read OESS API account
"""

import yaml
import urllib2
import urllib
import string
import json
global values1
global gh_url
gh_url = 'https://al2s.net.internet2.edu/oess/services-kerb/data.cgi'
def readAccount(filename):
	f = open(filename, 'r')
	yamlData = yaml.load(f)
	username = yamlData['username']
	passwd = yamlData['passwd']
	return (username,passwd)

def getWkGpID(workgroup_name,username,passwd):
	values1 = {'method' : 'get_workgroups'}
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
	searchResults1 =  jsonData1['results']
	for er in searchResults1:
		if er['name'] == workgroup_name:
			wg_id = er['workgroup_id']
	return wg_id

def getCtID(wg_id,feedtype,username,passwd):
	circuit_id = 0
	values1 = {'method' : 'get_existing_circuits', 'workgroup_id' : wg_id}
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
	searchResults1 = jsonData1['results']
	for er in searchResults1:
		if er['description'] == feedtype:
			circuit_id = er['circuit_id']
	return circuit_id

