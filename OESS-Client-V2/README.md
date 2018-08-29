provision.py wrkgrpName desc node1 interface1 vlanId1 node2 interface2 vlanId2 

Adds a circuit on the network using the OESS API function `provision_circuit`. The circuit is created immediately and can be removed by `remove_circuit.py`.

Parameters:
wrkgrpName
ID of the workgroup (e.g., “UCAR-LDM”, “Virginia”)
desc
Description of the circuit (e.g., “NEXRAD2 feed”) 
node1
Name of AL2S switch at one endpoint (e.g., “sdn-sw.ashb.net.internet2.edu”)
interface1
Specification of port on `node1` (e.g., “1/7”)
vlanId1
VLAN number for `node`/`interface1` (e.g., “4000”)
node2
Name of AL2S switch at other endpoint (e.g., “sdn-sw.pitt.net.internet2.edu”)
Interface2
Specification of port on `node2` (e.g., “et-3/0/0”)
vlanId2
VLAN number for `node2`/`interface2` (e.g., “4001”)

Output:
On success, the script writes the circuit ID to its standard output stream as a string.

Example:
$ python provision.py Virginia NEXRAD2 \ sdn-sw.ashb.net.internet2.edu et-3/0/0 332 \ sdn-sw.pitt.net.internet2.edu et-8/0/0 332


__________________________________________________________________________

remove.py wrkgrpName circuitId
remove_circuit
Removes a circuit on the network using the OESS API function ‘remove_circuit’. 
If the circuit has been removed successfully or is scheduled for removal from the network.

Parameters:
wrkgrpName
ID of the workgroup (e.g., “UCAR-LDM”, “Virginia”)
circuitId
ID of the circuit (e.g., “123456”)

Example:
$ python remove.py remove_circuit Virginia 123456
__________________________________________________________________________



edit.py wrkgrpName circuitId add|del node1 interface1 vlanId1 

Modify a circuit with specific circuitId on the network using the OESS API function `provision_circuit`.

Parameters:
wrkgrpName
ID of the workgroup (e.g., “UCAR-LDM”, “Virginia”)
circuitId
ID of the circuit(e.g., “123456”)
add|delete
The phase ‘add’ means add the endpoint into the circuit; the phase ‘delete’ means delete the existing endpoint in the circuit.
node1
Name of AL2S switch at one endpoint (e.g., “sdn-sw.ashb.net.internet2.edu”)
interface1
Specification of port on `node1` (e.g., “1/7”)
vlanId1
VLAN number for `node`/`interface1` (e.g., “4000”)

Example:
$ python edit.py Virginia 123456 add sdn-sw.ashb.net.internet2.edu \ et-3/0/0 332
