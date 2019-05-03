/*
 * NetInterfaceManager.cpp
 *
 *  Created on: Jun 30, 2011
 *      Author: jie
 */

#include "NetInterfaceManager.h"

NetInterfaceManager::NetInterfaceManager() {
	InitIfiInfo(AF_INET, 1);
}

NetInterfaceManager::~NetInterfaceManager() {
	for (PTR_IFI_INFO ptr_ifi = ifi_head; ptr_ifi != NULL; ptr_ifi = ptr_ifi->ifi_next ) {
		if (ptr_ifi->ifi_addr != NULL)
			free(ptr_ifi->ifi_addr);
		if (ptr_ifi->ifi_brdaddr != NULL)
			free(ptr_ifi->ifi_brdaddr);
		if (ptr_ifi->ifi_dstaddr != NULL)
			free(ptr_ifi->ifi_dstaddr);

		free(ptr_ifi);
	}
}


PTR_IFI_INFO NetInterfaceManager::GetIfiHead() {
	return ifi_head;
}

void NetInterfaceManager::InitIfiInfo(int family, int doaliases) {
	PTR_IFI_INFO* ifipnext, ifi;
	int sockfd, flags, myflags;
	char* ptr, *buf, lastname[IFNAMSIZ], *cptr;
	struct ifconf ifc;
	struct ifreq * ifr, ifr_copy;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	buf = GetIfConf(sockfd, &ifc);

	ifi_head = NULL;
	ifipnext = &ifi_head;
	lastname[0] = 0;
	for (ptr = buf; ptr < buf + ifc.ifc_len; ) {
		ifr = (struct ifreq *) ptr;
		ptr += sizeof(ifr->ifr_name) + GetIfreqLength(ifr);

		if (ifr->ifr_addr.sa_family != family)
					continue;

		myflags = 0;
		if ( (cptr = strchr(ifr->ifr_name, ':')) != NULL)
			*cptr = 0;		// replace ':' with null

		if (strncmp(lastname, ifr->ifr_name, IFNAMSIZ) == 0) {
			if (doaliases == 0)
				continue;
			myflags = IFI_ALIAS;
		}
		memcpy(lastname, ifr->ifr_name, IFNAMSIZ);

		ifr_copy = *ifr;
		ioctl(sockfd, SIOCGIFFLAGS, &ifr_copy);
		flags = ifr_copy.ifr_flags;
		if ((flags & IFF_UP) == 0)
			continue;		// ignore if interface not up

		ifi = (PTR_IFI_INFO)calloc(1, sizeof(struct ifi_info));
		memset(ifi, 0, sizeof(ifi));
		*ifipnext = ifi;
		ifipnext = &ifi->ifi_next;

		ifi->ifi_flags = flags;
		ifi->ifi_myflags = myflags;
		FillIfiInfo(sockfd, flags, ifi, ifr, &ifr_copy);
	}

	free(buf);
}


void NetInterfaceManager::FillIfiInfo(int sock, int flags, PTR_IFI_INFO ptr_ifi, IFREQ* ptr_ifr, IFREQ* ptr_ifr_copy) {
	memcpy(ptr_ifi->ifi_name, ptr_ifr->ifr_name, IFI_NAME);
	ptr_ifi->ifi_name[IFI_NAME - 1] = '\0';

	struct sockaddr_in *sin_ptr;
	switch (ptr_ifr->ifr_addr.sa_family) {
	case AF_INET:
		sin_ptr = (struct sockaddr_in *) &(ptr_ifr->ifr_addr);
		if (ptr_ifi->ifi_addr == NULL) {
			ptr_ifi->ifi_addr = (sockaddr *) calloc(1, sizeof(struct sockaddr_in));
			memcpy(ptr_ifi->ifi_addr, sin_ptr, sizeof(struct sockaddr_in));

#ifdef SIOCGIFBRDADDR
			if (flags & IFF_BROADCAST) {
				ioctl(sock, SIOCGIFBRDADDR, ptr_ifr_copy);
				sin_ptr = (struct sockaddr_in *) &(ptr_ifr_copy->ifr_broadaddr);
				ptr_ifi->ifi_brdaddr = (sockaddr *) calloc(1, sizeof(struct sockaddr_in));
				memcpy(ptr_ifi->ifi_brdaddr, sin_ptr, sizeof(struct sockaddr_in));
			}
#endif

#ifdef	SIOCGIFDSTADDR
			if (flags & IFF_POINTOPOINT) {
				ioctl(sock, SIOCGIFDSTADDR, ptr_ifr_copy);
				sin_ptr = (struct sockaddr_in *) &(ptr_ifr_copy->ifr_dstaddr);
				ptr_ifi->ifi_dstaddr = (sockaddr*) calloc(1, sizeof(struct sockaddr_in));
				memcpy(ptr_ifi->ifi_dstaddr, sin_ptr, sizeof(struct sockaddr_in));
			}
#endif
		}
		break;

	default:
		break;
	}
}


char* NetInterfaceManager::GetIfConf(int sock, ifconf * ptr_ifc) {
	char* buf;
	int lastlen = 0;
	int len = 100 * sizeof(struct ifreq);
	for (;;) {
		buf = (char*) malloc(len);
		ptr_ifc->ifc_len = len;
		ptr_ifc->ifc_buf = buf;
		if (ioctl(sock, SIOCGIFCONF, ptr_ifc) < 0) {
			if (errno != EINVAL || lastlen != 0)
				SysError("ioctl error: ");
		} else {
			if (ptr_ifc->ifc_len == lastlen)
				break;

			lastlen = ptr_ifc->ifc_len;
		}

		len += 10 * sizeof(struct ifreq);
		free( buf);
	}
	return buf;
}


int NetInterfaceManager::GetIfreqLength(IFREQ* ifr) {
	int len;

#ifdef HAVE_SOCKADDR_SA_LEN
		len = MAX(sizeof(struct sockaddr), ifr->ifr_addr.sa_len);
#else
		switch (ifr->ifr_addr.sa_family) {
#ifdef	IPV6
		case AF_INET6:
			len = sizeof(struct sockaddr_in6);
			break;
#endif
		case AF_INET:
		default:
			len = sizeof(struct sockaddr);
			break;
		}
#endif		// HAVE_SOCKADDR_SA_LEN

	return len;
}
