/*
 * NetInterfaceManager.h
 *
 *  Created on: Jun 30, 2011
 *      Author: jie
 */

#ifndef NETINTERFACEMANAGER_H_
#define NETINTERFACEMANAGER_H_

#include "vcmtp.h"

#define IFI_NAME 	16		// same as IFNAMSIZ in <net/if.h>
#define	IFI_HADDR	8		// allow for 64-bit EUI-64 in future
#define IFI_ALIAS	1		// ifi_addr is an alias

typedef struct ifi_info {
	char		ifi_name[IFI_NAME];		// interface name, null terminated
	u_char		ifi_haddr[IFI_HADDR];	// hardware address
	u_short		ifi_hlen;				// #bytes in hardware address: 0, 6, 8
	short 		ifi_flags;				// IFF_xxx constants from <net/if.h>
	short 		ifi_myflags;			// our own IFI_XXX flags
	struct sockaddr*	ifi_addr;		// primary address
	struct sockaddr*	ifi_brdaddr;	// broadcast address
	struct sockaddr*	ifi_dstaddr;	// destination address
	struct ifi_info*	ifi_next;		// next of ifi_info
} IFI_INFO, *PTR_IFI_INFO;


class NetInterfaceManager {
public:
	NetInterfaceManager();
	~NetInterfaceManager();

	PTR_IFI_INFO GetIfiHead();

private:
	PTR_IFI_INFO ifi_head;

	void 	InitIfiInfo(int family, int doaliases);
	char* 	GetIfConf(int sock, ifconf * ptr_ifc);
	int 	GetIfreqLength(IFREQ* ifr);
	void 	FillIfiInfo(int sock, int flags, PTR_IFI_INFO ptr_ifi, IFREQ* ptr_ifr, IFREQ* ptr_ifr_copy);
};

#endif /* NETINTERFACEMANAGER_H_ */
