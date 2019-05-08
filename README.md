provision.py credential desc recv_node recv_interface recv_vlanId sender_node sender_interface sender_vlanId

Create a circuit on the network or add recv entry onto a created circuit using the OESS API function `provision_circuit`. The action responds immediately.

Parameters:
credential
credential for AL2S OESS API account
desc
Description of the circuit (e.g., “NEXRAD2 feed”)
recv_node
Name of AL2S switch at receiver side (e.g., “rtsw.ashb.net.internet2.edu”)
recv_interface
Specification of port on `recv_node` (e.g., “et-3/0/0”)
recv_vlanId
VLAN number for `recv_node`/`recv_interface` (e.g., “332”)
sender_node
Name of AL2S switch at sender side (e.g., “rtsw.star.net.internet2.edu”)
sender_interface
Specification of port on `send_node` (e.g., “et-4/0/0”)
sender_vlanId
VLAN number for `send_node`/`send_interface` (e.g., “4001”)

Output:
On success, the script writes the circuit ID to its standard output stream as a string.

Example:
$ python provision.py oess-acount.yaml NEXRAD2 \ rtsw.ashb.net.internet2.edu et-3/0/0 332 \ rtsw.star.net.internet2.edu et-8/0/0 332

__________________________________________________________________________

remove.py credential desc recv_node recv_interface recv_vlanId
remove_circuit

Removes an recv entry when the subscriber leaves.
Note: when the subscriber is the last one, it will remove the circuit from the AL2S network.


Parameters:
credential
credential for AL2S OESS API account

desc
Description of the circuit (e.g., “NEXRAD2 feed”)

recv_node
Name of AL2S switch at receiver side (e.g., “rtsw.ashb.net.internet2.edu”)

recv_interface
Specification of port on `recv_node` (e.g., “et-3/0/0”)

recv_vlanId
VLAN number for `recv_node`/`recv_interface` (e.g., “332”)

Example:
$ python remove.py oess-acount.yaml NEXRAD2 \ rtsw.ashb.net.internet2.edu et-3/0/0 332
__________________________________________________________________________



destroy.py credential desc

destroy a circuit with specific desc on the network using the OESS API function `remove_circuit`. It happens when the sender stops the services.

Parameters:
credential
credential for AL2S OESS API account

desc
Description of the circuit (e.g., “NEXRAD2 feed”)

Example:
$ python edit.py oess-acount.yaml NEXRAD2
