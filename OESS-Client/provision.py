import sys
import urllib2
import urllib
import json
import account

global values1
values1 = {}
link = []
(username,passwd)=account.readAccount(sys.argv[2])
link=account.getSPath(sys.argv[4], sys.argv[7],username,passwd)
wg_id=account.getWkGpID(sys.argv[1],username,passwd)
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

if (searchResults == None):
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Provision.py:"+jsonData['error_text']])
else:
	circuit_id = jsonData['results']['circuit_id']
	file_name = str('circuit_id.log')
	f = open (file_name, 'w')
	f.write(circuit_id)
	f.close()
	call(["ulogger", "-i","-l","$LDM/var/logs/ldmd.log","Provision.py: circuit_id is "+circuit_id])

