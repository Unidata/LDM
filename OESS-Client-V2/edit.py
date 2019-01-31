import urllib2
import urllib
import sys
import time
import string
import json
from subprocess import call
global values1, values2, values3
values1 = {}
values2 = {}
values3 = {}
values4 = {}
node = []
interface = []
tag = []
link = []
f = open("/home/yt4xb/etc/OESS-password", 'r+')
line = f.read().split()
username = line[0]
passwd = line[1]
gh_url = 'https://al2s.net.internet2.edu/oess/services-kerb/data.cgi'
values1 = {'method' : 'get_workgroups'}
values2 = {'method' : 'get_circuit_details', 'circuit_id' : sys.argv[2]}
data1 = urllib.urlencode(values1, doseq=True)
data2 = urllib.urlencode(values2, doseq=True)
req1 = urllib2.Request(gh_url, data1)
req2 = urllib2.Request(gh_url, data2)
password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url, username, passwd)
auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
opener = urllib2.build_opener(auth_manager)
urllib2.install_opener(opener)
handler1 = urllib2.urlopen(req1)
handler2 = urllib2.urlopen(req2)
result1 = handler1.read()
result2 = handler2.read()
jsonData1 = json.loads(result1)
jsonData2 = json.loads(result2)
searchResults1 =  jsonData1['results']
for er in searchResults1:
	if er['name'] == sys.argv[1]:
		wg_id = er['workgroup_id']
description = jsonData2['results']['description']
searchResults2 = jsonData2['results']['endpoints']
searchResults3 = jsonData2['results']['links']
for er in searchResults2:
    if er['node'] != None:
        node.append(er['node'])
for er in searchResults2:
    if er['interface'] != None:
        interface.append(er['interface'])
for er in searchResults2:
    if er['tag'] != None:
        tag.append(er['tag'])
for er in searchResults3:
    if er['name'] != None:
        link.append(er['name'])
if sys.argv[3] == 'add':
	values3 = {'method' : 'get_shortest_path', 'node' : [node[0], sys.argv[4]], 'type' : 'mpls'}
	data3 = urllib.urlencode(values3, doseq=True)
	req3 = urllib2.Request(gh_url, data3)
	password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
	password_manager.add_password(None, gh_url, 'username', 'passwd')
	auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
	opener = urllib2.build_opener(auth_manager)
	urllib2.install_opener(opener)
	handler3 = urllib2.urlopen(req3)
	result3 = handler3.read()
	jsonData3 = json.loads(result3)
	searchResults4 =  jsonData3['results']
	for er in searchResults4:
		if er['link'] != None:
			if er['link'] not in link:
				link.append(er['link'])
	if sys.argv[4] not in node:
		node.append(sys.argv[4])
	if sys.argv[5] not in interface:
		interface.append(sys.argv[5])
	if sys.argv[6] not in tag:
		tag.append(sys.argv[6])
if sys.argv[3] == 'del':
	values3 = {'method' : 'get_shortest_path', 'node' : [node[0], sys.argv[4]], 'type' : 'mpls'}
	data3 = urllib.urlencode(values3, doseq=True)
	req3 = urllib2.Request(gh_url, data3)
	password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
	password_manager.add_password(None, gh_url, 'username', 'passwd')
	auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
	opener = urllib2.build_opener(auth_manager)
	urllib2.install_opener(opener)
	handler3 = urllib2.urlopen(req3)
	result3 = handler3.read()
	jsonData3 = json.loads(result3)
	searchResults4 =  jsonData3['results']
	for er in searchResults4:
		if er['link'] != None:
			if er['link'] in link:
				link.remove(er['link'])
	if sys.argv[4] in node:
		node.remove(sys.argv[4])
	if sys.argv[5] in interface:
		interface.remove(sys.argv[5])
	if sys.argv[6] in tag:
		tag.remove(sys.argv[6])
	index = 1
	while index < len(node):
		values3 = {'method' : 'get_shortest_path', 'node' : [node[0], node[index]], 'type' : 'mpls'}
		data3 = urllib.urlencode(values3, doseq=True)
		req3 = urllib2.Request(gh_url, data3)
		password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
		password_manager.add_password(None, gh_url, 'username', 'passwd')
		auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
		opener = urllib2.build_opener(auth_manager)
		urllib2.install_opener(opener)
		handler3 = urllib2.urlopen(req3)
		result3 = handler3.read()
		jsonData3 = json.loads(result3)
		searchResults4 =  jsonData3['results']
		for er in searchResults4:
			if er['link'] != None:
				if er['link'] not in link:
					link.append(er['link'])
		index += 1
values4 = {'method' : 'provision_circuit', 'workgroup_id' : wg_id, 'circuit_id' : sys.argv[2], 'provision_time' : -1, 'remove_time' : -1, 'description' : description, 'link' : link, 'node' : node, 'interface' : interface, 'tag' : tag}
data = urllib.urlencode(values4, doseq=True)
gh_url2 = 'https://al2s.net.internet2.edu/oess/services-kerb/provisioning.cgi'
req = urllib2.Request(gh_url2, data)
password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url2, 'username', 'passwd')
auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
opener = urllib2.build_opener(auth_manager)
urllib2.install_opener(opener)
handler = urllib2.urlopen(req)
result = handler.read()
jsonData = json.loads(result)
searchResults = jsonData['results']
if (searchResults == None):
	print jsonData['error_text']
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Edit.py:"+jsonData['error_text']])
else:
	circuit_id = jsonData['results']['circuit_id']
	print circuit_id
	file_name = str('circuit_id.log')
	f = open (file_name, 'w')
	f.write(circuit_id)
	f.close()
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Edit.py: circuit_id is "+circuit_id])
