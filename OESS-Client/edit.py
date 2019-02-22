import urllib2
import urllib
import sys
import time
import string
import json
import account
from subprocess import call
values1 = {}
values2 = {}
node = []
interface = []
tag = []
link = []
link1 = []
(username,passwd)=account.readAccount(sys.argv[2])
wg_id=account.getWkGpID(sys.argv[1],username,passwd)
ct_id=account.getCtID(wg_id, sys.argv[3],username,passwd)
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
if sys.argv[4] == 'add':
	link1=account.getSPath(node[0], sys.argv[5],username,passwd)
	for er in link1:
		if er not in link:
			link.append(er)
	if sys.argv[5] not in node:
		node.append(sys.argv[5])
	if sys.argv[6] not in interface:
		interface.append(sys.argv[6])
	if sys.argv[7] not in tag:
		tag.append(sys.argv[7])
if sys.argv[4] == 'del':
	link1=account.getSPath(node[0], sys.argv[5],username,passwd)
	for er in link1:
		if er in link:
			link.remove(er)
	if sys.argv[5] in node:
		node.remove(sys.argv[5])
	if sys.argv[6] in interface:
		interface.remove(sys.argv[6])
	if sys.argv[7] in tag:
		tag.remove(sys.argv[7])
	index = 1
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
