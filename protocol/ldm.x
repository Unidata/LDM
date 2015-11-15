/*
 * The server- and client-side modules created from this file include support
 * for both "downstream" and "upstream" LDM-s.  For example, the client-side
 * module contains both the functions feedme_6() and hereis_6().  Similarly, the
 * server-side modules contain support for receiving both the FEEDME message and
 * the HEREIS message.
 *
 * This is unavoidable because RPC assumes a pure client/server relationship
 * that doesn't exist between LDM-s, which have a more peer-to-peer
 * relationship.  You'll just have to get your head around this concept.
 */

#ifdef RPC_HDR
%#ifndef __USE_BSD
%#   define __USE_BSD    // to get `u_int`
%#endif
%
%#include <signal.h>     /* sig_atomic_t */
%#include <stdlib.h>     /* at least malloc() */
%#include <sys/time.h>   /* timeval */
%#include <sys/types.h>
%#include <regex.h>
%
%#include "timestamp.h"
#endif /* RPC_HDR */

#ifdef RPC_HDR
%/*
% * these define the range of "transient program numbers"
% */
%#define TRANSIENT_BEGIN 0x40000000 
%#define TRANSIENT_END   0x5fffffff 
#endif /* RPC_HDR */

#ifdef RPC_HDR
%
%/*
% * This is the Internet port number assigned to the ldm by the NIC.
% * We wanted a reserved port so IP layer port moniters could be
% * used for statistics.
% */
%#ifndef LDM_PORT
%#define LDM_PORT 388
%#endif
#endif /* RPC_HDR */

/*
 * The next section provides ENCODE side reference checking using assert().
 * When the code passes this test, it allows us to use xdr_reference()
 * rather than xdr_pointer()
 * and save bytes all over the protocol...
 */
#ifdef RPC_XDR
%
%#include "inetutil.h"
%#include "ldmprint.h"
%#include "xdr.h"
%
%#include <string.h>
%
%#ifndef NDEBUG
%#include <assert.h>
%#include <ulog.h>
%#include <mylog.h>
%#define pIf(a,b) (!(a) || (b))	/* a implies b */
%
%static bool_t
%xdr_stringck(XDR *xdrs, char **cpp, unsigned int maxsize)
%{
%	assert(pIf(xdrs->x_op == XDR_ENCODE, *cpp != NULL && **cpp != 0));
%	return(xdr_string(xdrs, cpp, maxsize));
%}
%
%static bool_t
%xdr_referenceck(XDR *xdrs, char* *pp, unsigned int size, const xdrproc_t proc)
%{
%	assert(pIf(xdrs->x_op == XDR_ENCODE, *pp != NULL));
%	return(xdr_reference(xdrs, pp, size, proc));
%}
%
%/* N.B. Names only scoped to this file */
%#undef xdr_string	/* in case it's a macro */
%#undef xdr_pointer	/* in case it's a macro */
%#define xdr_string xdr_stringck
%#define xdr_pointer xdr_referenceck
%#else
%#undef xdr_pointer	/* in case it's a macro */
%#define xdr_pointer xdr_reference
%#endif /*!NDEBUG*/
#endif /* RPC_XDR */

#ifdef RPC_XDR
%
%/*
% * feedtypet
% * The purpose of this type is to provide a coarse discriminant on
% * the origin and format of data. Think of it as an "address class"
% * to help decided the format of prod_info.ident.
% */
#endif
/*
 * N.B. rpcgen doesnt support having rhs of these declarations spec'ed in
 * terms of each other, eg. "const DDPLUS = (PPS|DDS);" won't work.
 */
#ifdef RPC_HDR
%
%/*
% * Note: there is a dependency between these #defines and atofeed.c fassoc -aw
% */
%
%/*
% * the empty set
% */
const NONE = 0;
const FT0 = 1;
%/*
% * Public Products Service
% */
const PPS = 1;
const FT1 = 2;
%/*
% * Domestic Data Service
% */
const DDS = 2;
%/*
% * Zephyr Domestic Data PLUS = PPS union DDS
% */
const DDPLUS = 3;
const FT2 = 4;
%/*
% * High Res. Data Service. Replaces NPS
% */
const HDS = 4;
%/*
% * Another name for High Res. Data Service.
% */
const HRS = 4;
const FT3 = 8;
%/*
% * International products
% */
const IDS = 8;
%/*
% * Old name for International products
% */
const INTNL = 8;
const FT4 = 16;
%/*
% * spare, formerly Numerical Products Service
% */
const SPARE = 16;
%/*
% * Any of the above... WMO format products except SPARE
% */
const WMO = 15;
const FT5 = 32;
%/*
% * Unidata/Wisconsin Broadcast
% */
const UNIWISC = 32;
const MCIDAS = 32;
%/*
% * All of the above
% */
const UNIDATA = 47;
const FT6 = 64;
%/*
% * Forecast Systems Lab PC DARE workstation feed
% */
const PCWS = 64;
const ACARS = 64;
const FT7 = 128;
%/*
% * FSL profiler data
% */
const FSL2 = 128;
const PROFILER = 128;
const FT8 = 256;
const FSL3 = 256;
const FT9 = 512;
const FSL4 = 512;
const FT10 = 1024;
const FSL5 = 1024;
%/*
% * Any of 64,128,256,512,or 1024
% */
const FSL = 1984;
const FT11 = 2048;
const AFOS = 2048;
%/*
% * GPS gathering feed
% */
const GPSSRC  = 2048;
const FT12 = 4096;
%/*
% * CONDUIT data
% */
const CONDUIT = 4096;
const NMC2 = 4096;
const NCEPH = 4096;
const FT13 = 8192;
const NMC3 = 8192;
const FNEXRAD = 8192;
%/*
% * Any of 2048, 4096, 8192
% */
const NMC = 14336;
const FT14 = 16384;
%/*
% * National Lighting Data Network
% */
const NLDN = 16384;
const FT15 = 32768;
%/*
% * NIDS products
% */
const WSI = 32768;
const FT16 = 65536;
%/*
% * DIFAX products
% */
const DIFAX = 65536;
const FT17 = 131072;
%/*
% * FAA604 products
% */
const FAA604 = 131072;
const FT18 = 262144;
%/*
% * GPS data - UNAVACO
% */
const GPS = 262144;
const FT19 = 524288;
%/*
% * Seismic data - IRIS
% */
const SEISMIC = 524288;
const NOGAPS = 524288;
const FNMOC = 524288;
const FT20 = 1048576;
%/*
% * Canadian Model Data
% */
const CMC = 1048576;
const GEM = 1048576;
const FT21 = 2097152;
%/*
% * NOAAport imagery
% */
const NIMAGE = 2097152;
const IMAGE = 2097152;
const FT22 = 4194304;
%/*
% * NOAAport text
% */
const NTEXT = 4194304;
const TEXT = 4194304;
const FT23 = 8388608;
%/*
% * NOAAport grided products
% */
const NGRID = 8388608;
const GRID = 8388608;
const FT24 = 16777216;
%/*
% * NOAAport point
% */
const NPOINT = 16777216;
const POINT = 16777216;
%/*
% * NOAAport BUFR
% */
const NBUFR = 16777216;
const BUFR = 16777216;
const FT25 = 33554432;
%/*
% * NOAAport graphics
% */
const NGRAPH = 33554432;
const GRAPH = 33554432;
const FT26 = 67108864;
%/*
% * NOAAport other data
% */
const NOTHER = 67108864;
const OTHER = 67108864;
%/*
% * NPORT consists of NTEXT, NGRID, NPOINT, NGRAPH, and NOTHER
% */
const NPORT = 130023424;
const FT27 = 134217728;
%/*
% * NEXRAD Level-III
% */
const NEXRAD3 = 134217728;
const NNEXRAD = 134217728;
const NEXRAD = 134217728;
const FT28 = 268435456;
%/*
% * NEXRAD Level-II
% */
const CRAFT = 268435456;
const NEXRD2 = 268435456;
const FT29 = 536870912;
%/*
% * NEXRAD gathering for archiving
% */
const NXRDSRC = 536870912;
const FT30 = 1073741824;
%/*
% * For testing & experiments
% */
const EXP = 0x40000000;
%/*
% * wildcard
% */
const	ANY    = 0xffffffff;

#endif /* RPC_HDR */

typedef unsigned int feedtypet;


#ifdef RPC_HDR
%
%/*
% * max length of a network hostname, aka MAXHOSTNAMELEN
% */
const HOSTNAMESIZE = 64;

%
%/*
% * The maximum length of a host name is 255 bytes
% * according to the Single UNIX� Specification, Version 2
% * <http://www.opengroup.org/onlinepubs/7908799/xns/gethostname.html>.  On a
% * SunOS 5.8 system, MAXHOSTNAMELEN is defined in /usr/include/netdb.h; hence,
% * the definition here is conditioned.
% */
%#include <netdb.h>
%#ifndef MAXHOSTNAMELEN
const MAXHOSTNAMELEN = 255;
%#endif

#endif /* RPC_HDR */

#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Data to build an RPC connection using the portmapper
% */
#endif
struct ldm_addr_rpc {
	string hostname<HOSTNAMESIZE> ;
	unsigned long prog ;
	unsigned long vers ;
};

#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Data to build an IP connection directly
% */
#endif
struct ldm_addr_ip {
	int protocol; /* rfc 790: IPPROTO_TCP (or IPPROTO_UDP) */
	unsigned short port; /* (struct sockaddr_in).sin_port */
	unsigned long addr; /*(struct sockaddr_in).(struct in_addr)sin_addr.s_addr*/
};

#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * What type of a rendezvous
% */
#endif
enum ldm_addrt {
	LDM_ADDR_NONE = 0,	
	LDM_ADDR_RPC = 1,	
	LDM_ADDR_IP = 2	
};

#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * A REDIRECT reply is a rendezvous,
% * specifies where to really send data.
% */
#endif
union rendezvoust switch (ldm_addrt type) {
	case LDM_ADDR_NONE:	
		void;
	case LDM_ADDR_RPC:	
		ldm_addr_rpc rpc;
	case LDM_ADDR_IP:	
		ldm_addr_ip ip;
};


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/* md5 digest */
#endif
typedef opaque signaturet[16];


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * pkey: product identification string (Not used as a key anymore).
% * max length of pkey, _POSIX_PATH_MAX.
% */
#ifdef RPC_HDR
const KEYSIZE = 255;
#endif /* RPC_HDR */
typedef string keyt<KEYSIZE>;
#endif


#ifdef RPC_HDR
%
%/*
% * max length of a regular expression
% */
%#define MAXPATTERN 255
%
%/*
% * prod_spec is a feedtype, pattern pair.
% */
%struct prod_spec {
%	feedtypet feedtype;
%	char *pattern;
%	regex_t rgx;	/* volatile, not sent over the wire */
%};
%typedef struct prod_spec prod_spec;
%bool_t xdr_prod_spec(XDR *, prod_spec*);

#endif /* RPC_HDR */

#ifdef RPC_XDR
%
%bool_t
%xdr_prod_spec(XDR *xdrs, prod_spec *objp)
%{
%	 if (!xdr_feedtypet(xdrs, &objp->feedtype)) {
%		 return (FALSE);
%	 }
%	 if (!xdr_string(xdrs, &objp->pattern, MAXPATTERN)) {
%		 return (FALSE);
%	 }
%	 if (xdrs->x_op == XDR_DECODE) {
%		memset(&objp->rgx, 0, sizeof(regex_t));
%	 }
%	 if (xdrs->x_op == XDR_FREE
%			&& objp->pattern != NULL) {
%		regfree(&objp->rgx);
%	 }
%	return (TRUE);
%}
#endif /* RPC_XDR */

#ifdef RPC_HDR
%
%/*
% * max number of specs in a class. There are at most 32 feedtypes
% */
const PSA_MAX = 32;
#endif /* RPC_HDR */
#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * prod_class_t is a set of products
% */
#endif
struct prod_class {
	timestampt from;
	timestampt to;
	prod_spec psa<PSA_MAX>;
};
typedef struct prod_class prod_class_t;

#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * The maximum size of a HEREIS data product.  Products larger than this will
% * be sent using COMINGSOON/BLKDATA messages.
% */
#endif
typedef unsigned max_hereis_t;

#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * The parameters of a feed:
% */
#endif
struct feedpar {
    prod_class_t* prod_class;  /* class of products */
    max_hereis_t  max_hereis;  /* HEREIS/COMINGSOON threshold in bytes */
};
typedef struct feedpar feedpar_t;


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * prod_info describes a specific data product.
% * (not a class of products).
% *
% */
#endif
struct prod_info {
	/* time when data entered the system */
	timestampt arrival; 
	/* (sortof) unique identifier */
	signaturet signature;
	/* fully qualified name of host where data entered the system */
	string origin<HOSTNAMESIZE>;
	feedtypet feedtype;
	unsigned seqno; 
	keyt ident;
	/* total size of the (opaque) data */
	unsigned sz;
};


#ifdef RPC_HDR
%/*
% * HEREIS/COMINGSOON threshold in bytes.
% * IF THIS VALUE IS INCREASED, THEN DISTRIBUTION AND INSTALLATION OF THE NEXT 
% * VERSION OF THE LDM FOR THE IDD WILL HAVE TO BE MANAGED.
% */
const DBUFMAX = 16384;
/*
 * We don't rpcgen the following so that xdr_dbuf() can use the "xdr_data" 
 * module to improve performance.
 */
%
%struct dbuf {
%	unsigned int dbuf_len;
%	char *dbuf_val;
%};
%typedef struct dbuf dbuf;
#endif /* RPC_HDR */


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Transfer of a product begins with one of these.
% */
#endif
struct comingsoon_args {
	prod_info *infop;	/* description of the product to be sent */
	unsigned int pktsz;	/* max datapkt.data.dbuf_len */
};

#ifdef RPC_HDR
%
%/*
% * Number of bytes needed in a dbuf_len == 0 BLKDATA call,
% * (auth AUTH_NONE)
% * Determined empirically to be 68.
% * Round it up to 72 (something divisible by 8 == sizeof(double).
% */
%#define DATAPKT_RPC_OVERHEAD ((unsigned int)72)
%/*
% * The size of the RPC receiving buffer.  Such a buffer is like a stdio
% * buffer: it doesn't limit the size of an entity, only the efficiency 
% * with which it's transmitted.
% */
%#define MAX_RPC_BUF_NEEDED (DATAPKT_RPC_OVERHEAD + 262144)
#endif /* RPC_HDR */
#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Transfer of a product begins with the prod_info.
% * Then, Send a sequence of these datapkts to
% * transfer the actual data.
% */
#endif
struct datapkt {
	signaturet *signaturep;	/* which product */
	unsigned int pktnum;	/* datapkt sequence number within product */
	dbuf data;		/* the data */
};


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Used to request a missed datapkt.
% * (UDP only)
% */
#endif
struct datapktd {
	signaturet *signaturep;	/* which product */
	unsigned int pktnum;	/* datapkt sequence number within product */
};


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Descriminant for ldm_replyt
% */
#endif
enum ldm_errt {
	OK = 0,			/* Everything is fine */
	SHUTTING_DOWN = 1,	/* Server going away */
	BADPATTERN = 2,		/* HIYA, FEEDME, NOTIFYME:  invalid reg exp */
	DONT_SEND = 3,		/* COMINGSOON response: I already have that */
	RESEND = 4,		/* BLKDATA response: resend a datapkt */
	RESTART = 5,		/* BLKDATA response: Please start over */
	REDIRECT = 6,		/* Send to someone else */
	RECLASS = 7		/* Send (or will send) a subset */
};

#ifdef RPC_HDR
%
%#define LDM6_OK            OK
%#define LDM6_SHUTTING_DOWN SHUTTING_DOWN
%#define LDM6_BADPATTERN    BADPATTERN
%#define LDM6_DONT_SEND     DONT_SEND
%#define LDM6_RECLASS       RECLASS
#endif


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Remote procedure return values.
% */
#endif
union ldm_replyt switch (ldm_errt code) {
	case OK:
		void;
	case SHUTTING_DOWN:
		void;
	case BADPATTERN:
		void;
	case DONT_SEND:
		void;
	case RESEND:
		datapktd *dpktdp;
	case RESTART:
		signaturet *signaturep;
	case REDIRECT:
		rendezvoust *alternatep;
	case RECLASS:
		prod_class_t *newclssp;
};

union hiya_reply_t switch (ldm_errt code) {
    case OK:
	max_hereis_t max_hereis;
    case DONT_SEND:
	void;
    case RECLASS:
	feedpar_t feedPar;
};

union fornme_reply_t switch (ldm_errt code) {
    case OK:
	unsigned int       id;
    case BADPATTERN:
	void;
    case RECLASS:
	prod_class_t *prod_class;
};

typedef ldm_errt comingsoon_reply_t;  /* OK or DONT_SEND */


program LDMPROG {
#   if defined(RPC_HDR) || defined(RPC_XDR)
	version FIVE {
		/* NULLPROC = 0 */
		ldm_replyt HEREIS(product) = 1;
		ldm_replyt FEEDME(prod_class_t) = 4;
		ldm_replyt HIYA(prod_class_t) = 5;
		ldm_replyt NOTIFICATION(prod_info) = 8;
		ldm_replyt NOTIFYME(prod_class_t) = 9;
		ldm_replyt COMINGSOON(comingsoon_args) = 12;
		ldm_replyt BLKDATA(datapkt) = 13;
	} = 5;
#   endif
	version SIX {
		/* void NULLPROC(void) = 0; */
		/*
		 * Downstream to upstream messages:
		 */
		fornme_reply_t     FEEDME(feedpar_t) = 4;
		fornme_reply_t     NOTIFYME(prod_class_t) = 9;
		bool               IS_ALIVE(unsigned int) = 14;
		/*
		 * Upstream to downstream messages:
		 */
		hiya_reply_t       HIYA(prod_class_t) = 5;
		void               NOTIFICATION(prod_info) = 8;
		void               HEREIS(product) = 1;
		comingsoon_reply_t COMINGSOON(comingsoon_args) = 12;
		void               BLKDATA(datapkt) = 13;
	} = 6;
#if WANT_MULTICAST
        version SEVEN {
            /*
             * Downstream to upstream RPC messages:
             */
            SubscriptionReply SUBSCRIBE(feedtypet feedtype) = 1;
            void              REQUEST_PRODUCT(McastProdIndex) = 2;
            void              REQUEST_BACKLOG(BacklogSpec) = 3;
            void              TEST_CONNECTION() = 4;
            /*
             * Upstream to downstream RPC messages:
             */
            void              DELIVER_MISSED_PRODUCT(MissedProduct) = 5;
            void              NO_SUCH_PRODUCT(McastProdIndex) = 6;
            void              DELIVER_BACKLOG_PRODUCT(product) = 7;
            void              END_BACKLOG() = 8;
        } = 7;
#endif
} = LDM_PROG; /* LDM = 300029, use 0x2ffffffe for experiments */


#ifdef RPC_HDR
%
%#define MIN_LDM_VERSION 5
#if !WANT_MULTICAST
%#define MAX_LDM_VERSION 6
#else
%#define MAX_LDM_VERSION 7
#endif
%
%void  ldmprog_5(struct svc_req *rqstp, register SVCXPRT *transp);
%void  ldmprog_6(struct svc_req *rqstp, register SVCXPRT *transp);
#if WANT_MULTICAST
%void  ldmprog_7(struct svc_req *rqstp, register SVCXPRT *transp);
%const char* ldm7_errmsg(int status);
#endif
%int   one_svc_run(const int xp_sock, const unsigned inactive_timeo);
%void* nullproc_6(void *argp, CLIENT *clnt);
%enum  clnt_stat clnt_stat(CLIENT *clnt);
#if WANT_MULTICAST
%int   clntStatusToLdm7Status(const CLIENT* clnt);
#endif
#endif

/*
 * We don't rpcgen the following so that we can use the "xdr_data" module to
 * improve performance.
 */
#if defined(RPC_HDR)
%struct product {
%	prod_info info;
%	void *data;
%};
%typedef struct product product;
%
%bool_t xdr_product(XDR *, product*);
%bool_t xdr_dbuf(XDR* xdrs, dbuf* objp);
#endif

#if defined(RPC_XDR)
%
%
%#include <stddef.h>
%
%#include "ulog.h"
%#include "mylog.h"
%#include "xdr_data.h"
%
%
%bool_t
%xdr_product(XDR *xdrs, product *objp)
%{
%	if (!xdr_prod_info(xdrs, &objp->info)) {
%		return (FALSE);
%	}
%	
%	switch (xdrs->x_op) {
%
%		case XDR_DECODE:
%			if (objp->info.sz == 0) {
%				return (TRUE);
%			}
%			if (objp->data == NULL) {
%				objp->data = xd_getBuffer(objp->info.sz);
%				if(objp->data == NULL) {
%					return (FALSE);
%				}
%			}
%			/*FALLTHRU*/
%
%		case XDR_ENCODE:
%			return (xdr_opaque(xdrs, objp->data, objp->info.sz));
%
%		case XDR_FREE:
%			objp->data = NULL;
%			return (TRUE);
%		
%	}
%	return (FALSE); /* never reached */
%}
%
%
%bool_t
%xdr_dbuf(XDR* xdrs, dbuf* objp)
%{
%    /*
%     * First, deal with the length since dbuf-s are counted.
%     */
%    if (!xdr_u_int(xdrs, &objp->dbuf_len))
%	return FALSE;
%
%    /*
%     * Now, deal with the actual bytes.
%     */
%    switch (xdrs->x_op) {
%
%	case XDR_DECODE:
%	    if (objp->dbuf_len == 0)
%		return TRUE;
%
%	    if (NULL == (objp->dbuf_val =
%		    (char*)xd_getNextSegment(objp->dbuf_len))) {
%		serror("xdr_dbuf()");
%		return FALSE;
%	    }
%
%	    /*FALLTHROUGH*/
%
%	case XDR_ENCODE:
%	    return (xdr_opaque(xdrs, objp->dbuf_val, objp->dbuf_len));
%
%	case XDR_FREE:
%	    objp->dbuf_val = NULL;
%
%	    return TRUE;
%    }
%
%    return FALSE;
%}

#endif /* RPC_XDR */

#if defined(RPC_HDR) || defined(RPC_XDR)
%
/**
 * Address of an Internet TCP service:
 */
struct ServiceAddr {
    /* Internet address of the service. May be hostname or IP address. */
    string         inetId<>;
    /* Port number of the service. */
    unsigned short port;
};
#endif

#if WANT_MULTICAST

#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * LDM-7 status values:
% */
#endif
enum Ldm7Status {
    LDM7_OK = 0,   /* Success */
    LDM7_INTR,     /* Interrupted by signal */
    LDM7_TIMEDOUT, /* Timed out */
    LDM7_RPC,      /* Other RPC error */
    LDM7_INVAL,    /* Invalid argument */
    LDM7_UNAUTH,   /* Unauthorized */
    LDM7_IPV6,     /* IPv6 not supported */
    LDM7_REFUSED,  /* Remote LDM-7 refused connection */
    LDM7_SYSTEM,   /* System error */
    LDM7_MCAST,    /* multicast error */
    LDM7_SHUTDOWN, /* LDM-7 was shut down */
    LDM7_NOENT,    /* no such entry */
    LDM7_DUP,      /* duplicate entry */
    LDM7_EXISTS,   /* something exists */
    LDM7_PQ        /* Error in product-queue */
};

#if RPC_CLNT
%#include <errno.h>
%
%/**
% * Returns the message associated with an LDM-7 status.
% *
% * @param[in] status  The LDM-7 status.
% * @return            The associated message.
% */
%const char*
%ldm7_errmsg(
%    const int status)
%{
%    switch (status) {
%    case LDM7_OK:
%        return "Success";
%    case LDM7_INTR:
%        return "Interrupted by signal";
%    case LDM7_TIMEDOUT:
%        return "Transaction timed-out";
%    case LDM7_RPC:
%        return "RPC error";
%    case LDM7_INVAL:
%        return "Invalid argument";
%    case LDM7_UNAUTH:
%        return "Not authorized to receive multicast group";
%    case LDM7_IPV6:
%        return "IPv6 not supported";
%    case LDM7_REFUSED:
%        return "Connection refused by remote LDM-7";
%    case LDM7_SYSTEM:
%        return "System error";
%    case LDM7_MCAST:
%        return "Multicast error";
%    case LDM7_SHUTDOWN:
%        return "LDM-7 was shut down";
%    default:
%        return "Unknown status";
%    }
%}
%
%int
%clntStatusToLdm7Status(
%    const CLIENT* clnt)
%{
%    int    status = clnt_stat(clnt);
%    if (status != 0) {
%        if (status == RPC_TIMEDOUT) {
%            status = LDM7_TIMEDOUT;
%        }
%        else if (status == RPC_SYSTEMERROR) {
%            struct rpc_err rpcErr;
%            clnt_geterr(clnt, &rpcErr);
%            if (rpcErr.re_errno == ECONNREFUSED) {
%                status = LDM7_REFUSED;
%            }
%            else {
%                status = LDM7_SYSTEM;
%            }
%        }
%        else if (status == RPC_AUTHERROR) {
%            status = LDM7_UNAUTH;
%        }
%        else {
%            status = LDM7_RPC;
%        }
%     }
%     return status;
%}
#endif // RPC_CLNT

typedef u_int McastProdIndex;

#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Missed data-product:
% */
#endif
struct MissedProduct {
    /*
     * The multicast file identifier of the missed data-product:
     */
    McastProdIndex iProd;
    /*
     * The missed LDM data-product:
     */
    product        prod;
};


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Data-product backlog specification:
% */
#endif
struct BacklogSpec {
    /*
     * The signature of the last data-product received by the multicast
     * downstream LDM during the previous session.
     */
    signaturet   after;
    /*
     * Whether `after` is set.
     */
    int          afterIsSet;
    /*
     * The signature of the first data-product received by the multicast
     * downstream LDM during the current session.
     */
    signaturet   before;
    /*
     * The time-offset, in seconds, from the current time to go back if the
     * "after" data-product isn't found.
     */
    unsigned int timeOffset;
};

#if defined(RPC_HDR) || defined(RPC_XDR)
%
/**
 * Information on a multicast group:
 */
struct McastInfo {
    /*
     * Multicast group feedtype:
     */
    feedtypet     feed;
    /*
     * Address of associated multicast group.
     */
    ServiceAddr   group;
    /*
     * Address of associated TCP server for data-blocks missed by a multicast
     * receiver.
     */
    ServiceAddr   server;
};
#endif


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Multicast subscription return values:
% */
#endif
union SubscriptionReply switch (Ldm7Status status) {
    case LDM7_OK:
        McastInfo mgi;
    case LDM7_INVAL:
        void;
    case LDM7_UNAUTH:
        void;
};

#endif /* WANT_MULTICAST */

#ifdef RPC_CLNT
%#include <string.h>
#endif
