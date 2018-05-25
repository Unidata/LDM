import urllib2
import urllib
import sys
import time
import string
import array
import numpy
import json
from subprocess import call
global values1, values2
values1 = {}
values2 = {}
link = []
gh_url = 'https://al2s.net.internet2.edu/oess/services-kerb/data.cgi'
values1 = {'method' : 'get_workgroups'}
data1 = urllib.urlencode(values1, doseq=True)
req1 = urllib2.Request(gh_url, data1)
password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url, 'username', 'passwd')
auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
opener = urllib2.build_opener(auth_manager)
urllib2.install_opener(opener)
handler1 = urllib2.urlopen(req1)
result1 = handler1.read()
jsonData1 = json.loads(result1)
searchResults1 =  jsonData1['results']
for er in searchResults1:
	if er['name'] == sys.argv[1]:
		wg_id = er['workgroup_id']
values2 = values = {'action' : 'remove_circuit', 'workgroup_id' : wg_id, 'circuit_id' : sys.argv[2], 'remove_time' : -1}
data = urllib.urlencode(values2, doseq=True)
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
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log",jsonData['error_text']])
else:
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Remove.py: Success"])
