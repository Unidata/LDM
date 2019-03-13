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
import account
from subprocess import call

def edit_endpoint(wg_id,node_id,interface_id,vlan_id,ct_id,function,username,passwd):
	values1 = {}
	values2 = {}
	node = []
	interface = []
	tag = []
	link = []
	link1 = []
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
	searchResults2 = jsonData1['results']['links']
	for er in searchResults1:
	    if er['node'] != None:
		node.append(er['node'])
	for er in searchResults1:
	    if er['interface'] != None:
		interface.append(er['interface'])
	for er in searchResults1:
	    if er['tag'] != None:
		tag.append(er['tag'])
	for er in searchResults2:
	    if er['name'] != None:
		link.append(er['name'])
	if function == 'add':
		link1=account.getSPath(node[0],node_id,username,passwd)
		for er in link1:
			if er not in link:
				link.append(er)
		if node_id not in node:
			node.append(node_id)
		if interface_id not in interface:
			interface.append(interface_id)
		if vlan_id not in tag:
			tag.append(vlan_id)
	if function == 'del':
		link1=account.getSPath(node[0], node_id,username,passwd)
		for er in link1:
			if er in link:
				link.remove(er)
		if node_id in node:
			node.remove(node_id)
		if interface_id in interface:
			interface.remove(interface_id)
		if vlan_id in tag:
			tag.remove(vlan_id)
		index = 1
		if len(node) == 1:
			link = []
		else:
			while index < len(node):
				link1=account.getSPath(node[0], node[index],username,passwd)
				for er in link1:
					if er not in link:
						link.append(er['link'])
				index += 1
	values2 = {'method' : 'provision_circuit', 'workgroup_id' : wg_id, 'circuit_id' : ct_id, 'provision_time' : -1, 'remove_time' : -1, 'description' : sys.argv[3], 'link' : link, 'node' : node, 'interface' : interface, 'tag' : tag}
	data = urllib.urlencode(values2, doseq=True)
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
	return jsonData
