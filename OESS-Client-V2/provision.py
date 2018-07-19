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
link = []
gh_url = 'https://al2s.net.internet2.edu/oess/services-kerb/data.cgi'
values1 = {'method' : 'get_workgroups'}
values2 = {'method' : 'get_shortest_path', 'node' : [sys.argv[3], sys.argv[6]], 'type' : 'mpls'}
data1 = urllib.urlencode(values1, doseq=True)
data2 = urllib.urlencode(values2, doseq=True)
req1 = urllib2.Request(gh_url, data1)
req2 = urllib2.Request(gh_url, data2)
password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url, 'username', 'passwd')
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
searchResults2 =  jsonData2['results']
for er in searchResults2:
	if er['link'] != None:
		link.append(er['link'])

values3 = {'method' : 'provision_circuit', 'workgroup_id' : wg_id, 'provision_time' : -1, 'remove_time' : -1, 'description' : sys.argv[2], 'link' : link, 'node' : [sys.argv[3], sys.argv[6]], 'interface' : [sys.argv[4], sys.argv[7]], 'tag' : [sys.argv[5], sys.argv[8]]}
data = urllib.urlencode(values3, doseq=True)
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
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Provision.py:"+jsonData['error_text']])
else:
	circuit_id = jsonData['results']['circuit_id']
	file_name = str('circuit_id.log')
	f = open (file_name, 'w')
	f.write(circuit_id)
	f.close()
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Provision.py: circuit_id is "+circuit_id])
