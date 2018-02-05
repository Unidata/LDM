import urllib2
import urllib
import sys
import time
import string
global values
values = {}
file_name = str('NetInfo_' + sys.argv[1] + '.log')
gh_url = 'https://al2s.net.internet2.edu/oess/services-kerb/data.cgi'
result = 'get_' + sys.argv[1]
if (result == 'get_workgroups'):
	values = {'action' : result}
elif (result == 'get_nodes'): 
	values = {'action' : result}	
elif (result == 'get_node_interfaces'):
        values = {'action' : result, 'node' : sys.argv[2], 'show_trunk' : 1}
elif (result == 'get_all_node_status'):
        values = {'action' : result}
elif (result == 'get_all_link_status'):
        values = {'action' : result}
elif (result == 'get_all_resources_for_workgroup'):
        values = {'action' : result, 'workgroup_id' : sys.argv[2]}
elif (result == 'get_maps'):
        values = {'action' : result, 'workgroup_id' : sys.argv[2], 'link_type' : 'openflow'}
elif (result == 'get_interface'):
	values = {'action' : result, 'interface_id' : sys.argv[2]}
elif (result == 'get_workgroup_interfaces'):
        values = {'action' : result, 'workgroup_id' : sys.argv[2]}    
elif (result == 'get_existing_circuits'):
        values = {'action' : result, 'workgroup_id' : sys.argv[2]}  
elif (result == 'get_circuits_by_interface_id'):
	values = {'action' : result, 'interface_id' : sys.argv[2]}
elif (result == 'get_circuit_details'):
        values = {'action' : result, 'circuit_id' : sys.argv[2]}
elif (result == 'get_circuit_scheduled_events'):         
        values = {'action' : result, 'circuit_id' : sys.argv[2]}
elif (result == 'get_circuit_history'):         
        values = {'action' : result, 'circuit_id' : sys.argv[2]}
elif (result == 'get_workgroup_members'):         
        values = {'action' : result, 'workgroup_id' : sys.argv[2]}
elif (result == 'get_vlan_tag_range'):
        values = {'action' : result, 'workgroup_id' : sys.argv[2], 'node' : sys.argv[3], 'interface' : sys.argv[4]}
elif (sys.argv[1] == 'generate_clr'):
	values = {'action' : 'generate_clr', 'circuit_id' : sys.argv[2]}
elif (sys.argv[1] == 'is_vlan_tag_available'):
	values = {'action' : 'is_vlan_tag_available', 'node' : sys.argv[2], 'interface' : sys.argv[3], 'vlan' : sys.argv[4]}
else:
	print "Input Method Error!"
data = urllib.urlencode(values)
req = urllib2.Request(gh_url, data)

password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
password_manager.add_password(None, gh_url, 'uva4api', 'AL2Ssenyd2011!')
auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)
opener = urllib2.build_opener(auth_manager)

urllib2.install_opener(opener)

handler = urllib2.urlopen(req)
f = open (file_name, 'w')
#for line in handler.read()
print handler.readline()
f.write(handler.read())
print handler.getcode()
print handler.headers.getheader('content-type')



