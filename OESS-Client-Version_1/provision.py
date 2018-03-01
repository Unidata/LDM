import urllib2
import urllib
import sys
import time
import string
import array
import numpy
global values
file_name = str(sys.argv[2] + '.log')
gh_url = 'https://al2s.net.internet2.edu/oess/services-kerb/provisioning.cgi'
values = {}
#workgroup_id=611, link=I2-ASHB-PITT-100GE-07737,
#node=sdn-sw.ashb.net.internet2.edu, interface=et-3/0/0, tag=332
#node=sdn-sw.pitt.net.internet2.edu, interface=et-8/0/0, tag=332
#provision_time=-1 means immediately, remove_time = -1 means never.
values = {'method' : 'provision_circuit', 'workgroup_id' : sys.argv[1], 'provision_time' : -1, 'remove_time' : -1, 'description' : sys.argv[2], 'link' : sys.argv[3], 'node' : [sys.argv[4], sys.argv[7]], 'interface' : [sys.argv[5], sys.argv[8]], 'tag' : [sys.argv[6], sys.argv[9]]}
data = urllib.urlencode(values, doseq=True)
req = urllib2.Request(gh_url, data)
#print data
password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url, 'uva4api', 'AL2Ssenyd2011!')

auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
opener = urllib2.build_opener(auth_manager)

urllib2.install_opener(opener)

handler = urllib2.urlopen(req)
f = open (file_name, 'w')
print handler.readline()
f.write(handler.read())
print handler.getcode()
print handler.headers.getheader('content-type')
