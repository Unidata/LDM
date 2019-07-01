#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Copyright (C) 2019 University of Virginia. All rights reserved.

file      edit.py
author    Yuanlong Tan <yt4xb@virginia.edu>
version   1.0
date      Feb. 14, 2019
brief     add/delete entry
"""
import urllib2
import urllib
import sys
import time
import string
import json
from subprocess import call

def edit_endpoint(wg_id,node_id,interface_id,vlan_id,ct_id,function,username,passwd):
	values1 = {}
	values2 = {}
	node = []
	interface = []
	tag = []
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
	for er in searchResults1:
	    if er['interface'] != None:
		interface.append(er['interface'])
	for er in searchResults1:
	    if er['tag'] != None:
		tag.append(er['tag'])
	if function == 'add':
		node.append(node_id)
		interface.append(interface_id)
		tag.append(vlan_id)
	elif function == 'del':
		for i in range(len(node)):
			if node_id == node[i]:
				if interface_id == interface[i] and vlan_id == tag[i]:
					del node[i]
					del interface[i]
					del tag[i]
					break
			
	values2 = {'method' : 'provision_circuit', 'workgroup_id' : wg_id, 'circuit_id' : ct_id, 'provision_time' : -1, 'remove_time' : -1, 'description' : sys.argv[2], 'node' : node, 'interface' : interface, 'tag' : tag}
	data = urllib.urlencode(values2, doseq=True)
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
	return jsonData
