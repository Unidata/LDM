/* Retrans Header */

#ifndef __RETRANS_H__
#define __RETRANS_H__

#include <time.h>
#include <string.h>
#include <sys/types.h>

#define SUCCESS		0
#define	ERROR		-1

#define ushort unsigned short
#define ulong  unsigned long

/******************************************************************************/
/* SBN acquisition retransmit related information */
#define MAX_LINKS	14
#define MAX_LINKS_LIMIT 15
#define MAX_LINKS_LO_LIMIT 5
#define	MAX_SHMREGIONS	10

/* Retransmit parameters */
#define RETRANS_TBL_TYP_GOES_EAST 0
#define RETRANS_TBL_TYP_GOES 0
#define RETRANS_TBL_TYP_NMC2 1
#define RETRANS_TBL_TYP_GOES_WEST 1
#define RETRANS_TBL_TYP_NMC 2
#define RETRANS_TBL_TYP_NOAAPORT_OPT 3
#define RETRANS_TBL_TYP_NMC3 4
#define RETRANS_TBL_TYP_NMC1 5
#define RETRANS_TBL_TYP_NWWS 6
#define RETRANS_TBL_TYP_ADD  7
#define RETRANS_TBL_TYP_ENC  8
#define RETRANS_TBL_TYP_EXP  9
#define RETRANS_TBL_TYP_GRW 10
#define RETRANS_TBL_TYP_GRE 11
#define MAX_RETRANS_TBL_TYP 12


#define DEFAULT_RETRANS_ENTRIES 1000
#define DEFAULT_RETRANS_ENTRIES_GOES 2000
#define DEFAULT_RETRANS_ENTRIES_GOES_EAST 2000
#define DEFAULT_RETRANS_ENTRIES_GOES_WEST 2000
#define DEFAULT_RETRANS_ENTRIES_NMC 500000
#define DEFAULT_RETRANS_ENTRIES_NMC2 200000
#define DEFAULT_RETRANS_ENTRIES_NOAAPORT_OPT 100000
#define DEFAULT_RETRANS_ENTRIES_NMC3 1000
#define DEFAULT_RETRANS_ENTRIES_NMC1 5000
#define DEFAULT_RETRANS_ENTRIES_NWWS 5000
#define DEFAULT_RETRANS_ENTRIES_ADD  100000
#define DEFAULT_RETRANS_ENTRIES_ENC  100000
#define DEFAULT_RETRANS_ENTRIES_EXP  100000
#define DEFAULT_RETRANS_ENTRIES_GRW  2000
#define DEFAULT_RETRANS_ENTRIES_GRE  2000

/* Macro to get default channel specific retrans entries */
#define GET_RETRANS_CHANNEL_ENTRIES(s) \
  ((s==SBN_TYP_NMC) ? DEFAULT_RETRANS_ENTRIES_NMC : \
   (s==SBN_TYP_NMC1) ? DEFAULT_RETRANS_ENTRIES_NMC1 : \
   (s==SBN_TYP_NMC2) ? DEFAULT_RETRANS_ENTRIES_NMC2 : \
   (s==SBN_TYP_NMC3) ? DEFAULT_RETRANS_ENTRIES_NMC3 : \
   (s==SBN_TYP_GOES) ? DEFAULT_RETRANS_ENTRIES_GOES : \
   (s==SBN_TYP_NWWS) ? DEFAULT_RETRANS_ENTRIES_NWWS : \
   (s==SBN_TYP_ADD)  ? DEFAULT_RETRANS_ENTRIES_ADD : \
   (s==SBN_TYP_EXP)  ? DEFAULT_RETRANS_ENTRIES_EXP : \
   (s==SBN_TYP_ENC)  ? DEFAULT_RETRANS_ENTRIES_ENC : \
   (s==SBN_TYP_GRW)  ? DEFAULT_RETRANS_ENTRIES_GRW : \
   (s==SBN_TYP_GRE)  ? DEFAULT_RETRANS_ENTRIES_GRE : \
   (s==SBN_TYP_NOAAPORT_OPT) ? DEFAULT_RETRANS_ENTRIES_NOAAPORT_OPT : -1)


/*
*	SBN type_id for transmission block header
*/
#define	SBN_TYP_GOES          1	/* GOES type 		*/	
#define	SBN_TYP_NMC4          2	/* NMC4 type 		*/	
#define	SBN_TYP_NMC1          3	/* NMC1 type 		*/	
#define	SBN_TYP_NOAAPORT_OPT  4	/* NOAAPORT_OPT type */	
#define	SBN_TYP_NMC           5	/* NMC type 		*/	
#define	SBN_TYP_NMC2          6	/* NMC2 type 		*/	
#define	SBN_TYP_NMC3          7	/* NMC3 type		*/	
#define	SBN_TYP_NWWS          8	/* NWWS type		*/	
#define	SBN_TYP_ADD           9	/* AWIPS Data Delivery type	*/
#define	SBN_TYP_ENC          10	/* Encrypted type	*/
#define	SBN_TYP_EXP          11	/* Experiment type	*/
#define	SBN_TYP_GRW          12	/* GOESR West type	*/
#define	SBN_TYP_GRE          13	/* GOESR East type	*/
#define	MAX_SBN_TYP          13	/* MAX type for now	*/	

/* Future */
#define SBN_TYP_NWS1 SBN_TYP_NMC1       /* Perhaps for software broadcast */

#define OPTION_ENABLE		1
#define OPTION_DISABLE		0
#define	OPTION_NOTSET		-1



/* Product Types */
#define MAX_PROD_TYP 8		/* maximum number of product types or ids */
#define PROD_TYPE_GOES_EAST 1	/* GOES East product type */
#define PROD_TYPE_GOES_WEST 2	/* GOES West product type */
#define PROD_TYPE_NESDIS_NONGOES 3	/* NESDIS nonGOES product type */
#define PROD_TYPE_NOAAPORT_OPT 3 /* NOAAPORT Option product type */
#define PROD_TYPE_NWSTG	 4	/* NWSTG product type (stream) */
#define PROD_TYPE_NEXRAD 5	/* NEXRAD product type */
#define PROD_TYPE_MHS 6	/* MHS message handling system product type */
#define PROD_TYPE_SAT_OTHER 7	/* SAT_OTHER product type */


#define NAME_PROD_TYPE_GOES "GOES"	/* name for GOES product type */
#define NAME_PROD_TYPE_GOES_EAST "GOES_EAST" /* name for GOES East prod type */
#define NAME_PROD_TYPE_SAT_OTHER "SAT_OTHER" /* name SAT_OTHER prod type */
#define NAME_PROD_TYPE_GOES_WEST "GOES_WEST" /* name for GOES West prod type */
#define NAME_PROD_TYPE_NESDIS "NESDIS"	/* name for NESDIS product type */
#define NAME_PROD_TYPE_NESDIS_NONGOES "NESDIS_NONGOES"	/* name for NESDIS nonGOES prod type */
#define NAME_PROD_TYPE_NOAAPORT_OPT "NOAAPORT_OPT" /* name for NOAAPORT_OPT prod type */
#define NAME_PROD_TYPE_SAT_AK_HI_PR "SAT_AK_HI_PR" /* name for AK/HI/PR prod type */
#define NAME_PROD_TYPE_MHS "MHS" /* name for MHS prod type */
#define NAME_PROD_TYPE_NWSTG "NWSTG"	/* name for NWSTG product type */
#define NAME_PROD_TYPE_NEXRAD "NEXRAD"	/* name for NEXRAD product type */
#define NAME_PROD_TYPE_ASOS "ASOS"	/* name for ASOS product type */

/* Obsolete usage */
#define	SBN_TYP_GOES_EAST	1	/* GOES-EAST type 	*/	
#define	SBN_TYP_GOES_WEST	2	/* GOES-EAST type 	*/	
#define	SBN_TYP_AHPR		3	/* AK/HI/PR type 	*/	
#define	SBN_TYP_SAT_AK_HI_PR	3	/* AK/HI/PR type 	*/	
#define	SBN_TYP_RESERVE2  SBN_TYP_NMC3 	/* backward compatibility */
#define SBN_TYP_NPOPT  SBN_TYP_NOAAPORT_OPT /* alternate name  */

#define NAME_SBN_TYP_GOES "GOES" 	/* name GOES type */
#define NAME_SBN_TYP_NOAAPORT_OPT "NOAAPORT_OPT" /* name NOAAPORT_OPT type */
#define NAME_SBN_TYP_NMC "NMC" 		/* name NMC type */
#define NAME_SBN_TYP_NMC1 "NMC1" 	/* name NMC1 type */
#define NAME_SBN_TYP_NMC2 "NMC2" 	/* name NMC2 type */
#define NAME_SBN_TYP_NMC3 "NMC3" 	/* name NMC3 type */
#define NAME_SBN_TYP_NMC4 "NMC4" 	/* name NMC4 type */
#define NAME_SBN_TYP_NWWS "NWWS" 	/* name NWWS type */
#define NAME_SBN_TYP_ADD  "ADD" 	/* name ADD type */
#define NAME_SBN_TYP_ENC  "ENC" 	/* name ENC type */
#define NAME_SBN_TYP_EXP  "EXP" 	/* name EXP type */
#define NAME_SBN_TYP_GRW  "GRW" 	/* name GRW type */
#define NAME_SBN_TYP_GRE  "GRE" 	/* name GRE type */
/* Future */
#define NAME_SBN_TYP_NWS1 "NWS1" 	/* name NWS1 type */

/* Obsolete usage */
#define NAME_SBN_TYP_GOES_EAST "GOES_EAST"      /* name GOES_EAST type */
#define NAME_SBN_TYP_GOES_WEST "GOES_WEST"      /* name GOES_WEST type */
#define NAME_SBN_TYP_SAT_AK_HI_PR "SAT_AK_HI_PR" /* name SAT_AK_HI_PR type */
#define NAME_SBN_TYP_RESERVE2 NAME_SBN_TYP_NMC3         /* name RESERVE2 type */
/**************************************************/
/* Following are obsolete and should not be used */
#define PROD_TYPE_GOES 1        /* GOES product type */
#define PROD_TYPE_NESDIS 3      /* NESDIS product type */
#define PROD_TYPE_RESERVE 6     /* Reserved (Alaska/HI/PR??) product type */
#define PROD_TYPE_SAT_AK_HI_PR 6 /* Future (Alaska/HI/PR??) product type */
#define PROD_TYPE_ASOS   7      /* ASOS product type */


#define	MAX_RETRANS_LEN_WMO_HDR_ABBREV	6
#define MAX_WMO_STR_LEN 32	/* Max length of WMO prefix */
#define MAX_WMO_ENTRY_LEN MAX_WMO_STR_LEN	/* Max length of WMO prefix */
#define MAX_WMO_LOGHDR_INFO_LEN MAX_WMO_ENTRY_LEN+32	/* Max len WMO loghdr info */


/* Defines for product type for acquition products */
#define MAX_PROD_CAT 10		/* maximum number of product categories */
#define PROD_CAT_TEXT 1		/* text product cat */
#define PROD_CAT_GRAPHIC 2	/* graphic product cat */
#define PROD_CAT_IMAGE 3	/* image product cat */
#define PROD_CAT_GRID 4		/* grid data product cat */
#define PROD_CAT_POINT 5	/* point data product cat */
#define PROD_CAT_BINARY 6	/* binary product cat */
#define PROD_CAT_OTHER 7	/* other product cat */
#define NAME_PROD_CAT_TEXT "TEXT"	/* name for text product cat */
#define NAME_PROD_CAT_GRAPHIC "GRAPHIC"	/* name for graphic product cat */
#define NAME_PROD_CAT_IMAGE "IMAGE"	/* name for image product cat */
#define NAME_PROD_CAT_GRID "GRID"	/* name for grid data product cat */
#define NAME_PROD_CAT_POINT "POINT"	/* name for point data product cat */
#define NAME_PROD_CAT_BINARY "BINARY"	/* name for binary product cat */
#define NAME_PROD_CAT_OTHER "OTHER"	/* name for other product cat */

/* Macros for use of PROD_TYPE */

#define PROD_TYPE_NESDIS_HDR_TRUE(ptype)  \
	((ptype==PROD_TYPE_GOES_EAST) ||\
	(ptype==PROD_TYPE_GOES_WEST) ||\
	(ptype==PROD_TYPE_NESDIS) ||\
	(ptype==PROD_TYPE_NESDIS_NONGOES) ||\
	(ptype==PROD_TYPE_NOAAPORT_OPT) ||\
	(ptype==PROD_TYPE_SAT_AK_HI_PR))


#define GET_PROD_TYPE_NAME(ptype)  \
	((ptype==PROD_TYPE_GOES_EAST)?NAME_PROD_TYPE_GOES_EAST:\
	(ptype==PROD_TYPE_GOES_WEST)?NAME_PROD_TYPE_GOES_WEST:\
	(ptype==PROD_TYPE_NOAAPORT_OPT)?NAME_PROD_TYPE_NOAAPORT_OPT:\
	(ptype==PROD_TYPE_NESDIS)?NAME_PROD_TYPE_NESDIS:\
	(ptype==PROD_TYPE_NESDIS_NONGOES)?NAME_PROD_TYPE_NESDIS_NONGOES:\
	(ptype==PROD_TYPE_NWSTG)?NAME_PROD_TYPE_NWSTG:\
	(ptype==PROD_TYPE_MHS)?NAME_PROD_TYPE_MHS:\
	(ptype==PROD_TYPE_SAT_AK_HI_PR)?NAME_PROD_TYPE_SAT_AK_HI_PR:\
	(ptype==PROD_TYPE_ASOS)?NAME_PROD_TYPE_ASOS:\
	(ptype==PROD_TYPE_NEXRAD)?NAME_PROD_TYPE_NEXRAD:\
	(ptype==PROD_TYPE_RESERVE)?"RESERVE":"UNKNOWN")

/* Macros for use of SBN_TYP */
#define GET_SBN_TYP_NAME(ptype)  \
	((ptype==SBN_TYP_GOES)?NAME_SBN_TYP_GOES:\
	(ptype==SBN_TYP_NOAAPORT_OPT)?NAME_SBN_TYP_NOAAPORT_OPT:\
	(ptype==SBN_TYP_NMC)?NAME_SBN_TYP_NMC:\
	(ptype==SBN_TYP_NMC1)?NAME_SBN_TYP_NMC1:\
	(ptype==SBN_TYP_NMC2)?NAME_SBN_TYP_NMC2:\
	(ptype==SBN_TYP_NMC3)?NAME_SBN_TYP_NMC3:\
	(ptype==SBN_TYP_NMC4)?NAME_SBN_TYP_NMC4:\
	(ptype==SBN_TYP_NWS1)?NAME_SBN_TYP_NWS1:\
	(ptype==SBN_TYP_NWWS)?NAME_SBN_TYP_NWWS:\
	(ptype==SBN_TYP_ADD)?NAME_SBN_TYP_ADD:\
	(ptype==SBN_TYP_ENC)?NAME_SBN_TYP_ENC:\
	(ptype==SBN_TYP_EXP)?NAME_SBN_TYP_EXP:\
	(ptype==SBN_TYP_GRW)?NAME_SBN_TYP_GRW:\
	(ptype==SBN_TYP_GRE)?NAME_SBN_TYP_GRE:\
	(ptype==SBN_TYP_GOES_EAST)?NAME_SBN_TYP_GOES_EAST:\
	(ptype==SBN_TYP_GOES_WEST)?NAME_SBN_TYP_GOES_WEST:\
	(ptype==SBN_TYP_SAT_AK_HI_PR)?NAME_SBN_TYP_SAT_AK_HI_PR:\
	"TYPE_UNKNOWN")

/* Macros for use of RETRANS_TABLE */
#define GET_RETRANS_TABLE_TYP(s)  0
/***
#define GET_RETRANS_TABLE_TYP(s)  \
	((s==SBN_TYP_NMC)?RETRANS_TBL_TYP_NMC:\
		(s==SBN_TYP_NMC1)?RETRANS_TBL_TYP_NMC1:\
		(s==SBN_TYP_NMC2)?RETRANS_TBL_TYP_NMC2:\
		(s==SBN_TYP_NMC3)?RETRANS_TBL_TYP_NMC3:\
		(s==SBN_TYP_NWWS)?RETRANS_TBL_TYP_NWWS:\
		(s==SBN_TYP_ADD)?RETRANS_TBL_TYP_ADD:\
		(s==SBN_TYP_ENC)?RETRANS_TBL_TYP_ENC:\
		(s==SBN_TYP_EXP)?RETRANS_TBL_TYP_EXP:\
		(s==SBN_TYP_GRE)?RETRANS_TBL_TYP_GRE:\
		(s==SBN_TYP_GRW)?RETRANS_TBL_TYP_GRW:\
		(s==SBN_TYP_GOES_EAST)?RETRANS_TBL_TYP_GOES_EAST:\
		(s==SBN_TYP_GOES)?RETRANS_TBL_TYP_GOES:\
		(s==SBN_TYP_GOES_WEST)?RETRANS_TBL_TYP_GOES_WEST:\
		(s==SBN_TYP_NOAAPORT_OPT)?RETRANS_TBL_TYP_NOAAPORT_OPT:-1)
***/
/************************** entry flag for retransmit table ***************/
#define RETRANS_ENTRY_FLAG_AVAIL 0x0 /* entry unused */

/* New entries are "valid" as either a new product or a new duplicate */
#define RETRANS_ENTRY_FLAG_NEW_VALID 0x1 /* rcvd product as new */
#define RETRANS_ENTRY_FLAG_RETRANS_VALID 0x2 /* rcvd prod retrans ok (nomatch)*/

/* This flag is used to further update a previous entry as a "duplicate" */
/*   as a duplicate (NEW_VALID + RETRANS_DUP) or */
/*                  (RETRANS_VALID + RETRANS_DUP)  */
#define RETRANS_ENTRY_FLAG_RETRANS_DUP 0x4 /* rcvd prod match retrans */

/* This flag further defines the new entry as a "duplicate" */
/*   or a new valid entry with an enclosed duplicate (NEW_VALID + NEW_W_DUP) */
#define RETRANS_ENTRY_FLAG_NEW_W_DUP 0x8 /* rcvd prod with retrans enclosed */
/************************** entry flag for retransmit table ***************/


#define	DEFAULT_RETRANSMIT_PIPENAME	"/dev/p_LOST"
#define	DONT_BLOCK		O_NONBLOCK
#define	DEFAULT_RETRANSMIT_DELAY_SEND	30 /*delay for sending request */

#define PROD_NODUPLICATE 0x1
#define PROD_DUPLICATE_NOMATCH 0x2
#define PROD_DUPLICATE_MATCH 0x4
#define PROD_DUPLICATE_DISCARD 0x8



/* File retransmission table */
typedef struct prod_retrans_entry {
	time_t  prod_arrive_time;	/* 4 bytes product arrival time GMT */
	long    prod_seqno;			/* 4 bytes NCF generated prod_seqno */
	ushort  prod_run_id;		/* 2 bytes NCF run_id */
	ushort  prod_orig_run_id;	/* 2 bytes NCF run_id */
	char    prod_type;			/* 1 byte (GOES_EAST, GOES_WEST, NWSTG, */
								/*          NOAAPORT_OPT, etc) */
	char    prod_cat;			/* 1 byte (TEXT, GRAPHIC, IMAGE, GRID, */
								/*          POINT, IMAGE) */
	ushort  prod_code;			/* 2 bytes (code 0,1,2,...,n) */
	ushort  prod_sub_code;		/* 2 bytes (subcode 0,1,2,...,n) */
	ushort  prod_status;		/* 2 bytes */
	char    prod_err_cause;		/* 1 byte */
	char    prod_link_id;		/* 1 byte (link 0,1,2,...,) */
	char    entry_flag;			/* 1 byte */
	char    reserve1;			/* 1 byte */
	char	WMO_hdr_abbrev[MAX_RETRANS_LEN_WMO_HDR_ABBREV+1];	/* 6 bytes T1T2A1A3I1I2 */
									/* 1 byte for end */
	char	reserve2;			/* 1 byte */
								/* total = 32 bytes */
} PROD_RETRANS_ENTRY;


/* File retransmission entry info */
typedef struct entry_info {
	long     retrans_entry_base_offset;  /* base entry table ptr */
	int     entry_bytes;                /* bytes per entry */
	int     numb_entries;               /* numb entries for this type */
	int     index_last;                 /* last index entry for this type */
	ushort  run_id_last;                /* last run_id for this type */
	ushort  run_id_orig_last;           /* last run_id for this type */
	ulong   tot_prods_rcvd;             /* all prods rcvd by proc */
	ulong   tot_prods_lost;             /* tot prods lost any reason */
	ulong   tot_prods_lost_seqno;       /* tot prods lost seqno proc only */
	ulong   tot_prods_lost_abort;       /* prods lost abort (proc or client) */
	ulong   tot_prods_lost_other;       /* prods lost other (proc or client) */
	ulong   tot_prods_retrans_rcvd;     /* prods retrans rcvd by proc */
	ulong   tot_prods_retrans_rcvd_lost; /* prods retrans rcvd lost */
	ulong   tot_prods_retrans_rcvd_notlost; /* prods rcvd not lost */
	ulong   tot_prods_retrans_rqstd;    /* prods retrans requested */
	int		len_wmo_hdr_max;			/* max WMO hdr length (w/o null term) */
	int		len_wmo_hdr_abbrev_max;		/* max WMO hdr abbrev len w/o null */
	char	last_wmo_hdr[MAX_WMO_ENTRY_LEN+1]; /* WMO header copy */
	char	last_wmo_loghdr_info[MAX_WMO_LOGHDR_INFO_LEN+1]; /* log info include WMO hdr */
} PROD_RETRANS_ENTRY_INFO;

/* File retransmission table */
typedef struct prod_retrans_table {
	/*PROD_RETRANS_ENTRY_INFO entry_info[MAX_RETRANS_TBL_TYP+1];*/
	PROD_RETRANS_ENTRY_INFO entry_info[1];
} PROD_RETRANS_TABLE;

/*  Process pipe retransmit product headers */  
/*	Generated by read_noaaport process, for use by retransmit routine */
typedef  struct pipe_retransmit_hdr  {   
	unsigned int	pipe_request_numb;		/* seq numb of request */
									/* (nonblock pipe) so needed*/
	char	pipe_ctl_flag;			/* control flag from requestor */
									/* 1=ENABLE_RETRANS_GEN_RQST */
									/* 2=ENABLE_RETRANS_XMIT_RQST */
									/* 4=ENABLE_RETRANS_LOG_RQST */
	char	pipe_link_id;			/* link id of requestor */
	char	pipe_channel_type;		/* channel_type, eg SBN type */
	char	pipe_request_cause;		/* request cause */
									
	int	pipe_cpio_addr;			/* cpio address */
	int	pipe_request_time;		/* time of request */
	int	pipe_first_prod_seqno;	/* first missing prod_seqno */
	int	pipe_last_prod_seqno;	/* last missing prod_seqno */
	int	pipe_run_numb;			/* unique run numb */
	ushort	pipe_delay_send;		/* time to delay for each send to NCF */
									/*  pass in pipe since retransmit daemon */
									/*  does not attach to shmem */
	ushort	reserved;				/* reserved */

} PIPE_RETRANSMIT_HDR;

#define ENABLE_RETRANS_GEN_RQST 1
#define ENABLE_RETRANS_XMIT_RQST 2
#define ENABLE_RETRANS_LOG_RQST 4
#define ENABLE_RETRANS_DUP_MATCH_DISCARD 8
#define ENABLE_RETRANS_PROD_ENABLE 16
#define ENABLE_RETRANS_PROD_SBN 32
#define ENABLE_RETRANS_PROD_MHS 64
#define ENABLE_RETRANS_DUP_NOMATCH_DISCARD 128
#define ENABLE_RETRANS_DUP_DISCARD ENABLE_RETRANS_DUP_MATCH_DISCARD

#define RETRANS_RQST_CAUSE_NONE 0
#define RETRANS_RQST_CAUSE_RCV_ERR 1
#define RETRANS_RQST_CAUSE_CHANGE_LINK 2
#define RETRANS_RQST_CAUSE_CHANGE_RUN_NO 3
#define RETRANS_RQST_CAUSE_FAIL_SVR 4
#define RETRANS_RQST_CAUSE_OTHER 5
#define RETRANS_RQST_CAUSE_DH_BUFF_1ST 6
#define RETRANS_RQST_CAUSE_DH_BUFF_NEXT 7
#define RETRANS_RQST_CAUSE_LOW_BUFF 8

#define	XFR_PROD_RETRANSMIT	0x10	/* Prod is retransmit of original	*/
/**** Done for retransmit ******/

#define ulong	unsigned long
#define uchar	unsigned char

#define	MAX_LOG_DATA	250

typedef struct acq_table {
  	unsigned char link_id;
	long link_addr;
	unsigned char	max_links;		/* max links for this entire shm region */
	int	 proc_base_channel_type_last;	/* channel type last, eg SBN_type  	*/  //(populate from sbn->datastream)


	pid_t	pid;
	time_t	proc_last_retrans_rqst;	/* proc last retransmit request   (populate when gen. retrans request) */
	int 	proc_retransmit_ctl_flag;	/* proc retransmit rqst ctl flag */
                                                /* (enable all by default..if input option is diable, then disable retrans_rqst) */
					/* 1=ENABLE_RETRANS_GEN_RQST */
					/* 2=ENABLE_RETRANS_XMIT_RQST */
					/* 4=ENABLE_RETRANS_LOG_RQST */
					/* 8=ENABLE_RETRANS_DUP_DISCARD */
	int		read_distrib_enable;	/* Enable/disable reader */
	int		proc_retransmit_delay_send; /* delay for sending rqst */
	/* Updated at start of each product */
	time_t	proc_last_buff_time;	/* time rcvd last buff time  	*/
	time_t	proc_prod_start_time;	/* time prod start processing  */
	time_t	proc_prod_NCF_rcv_time; /* time prod start rcv at NCF 	*/
	time_t	proc_prod_NCF_xmit_time; /* time prod start XMIT at NCF  */
	ushort	proc_blkno;					/* block number */
	ulong	proc_prod_seqno;			/* Current seqno */
	ulong	proc_base_prod_seqno_last;	/* prod seqno last processed */ //(updated from curr_prod_seqno)
	ulong	proc_orig_prod_seqno_last;	/* last orig prod_seqno retrans */ //(Updated from psh->seqno)
	ulong	proc_prod_bytes_read;
	ushort	proc_prod_run_id;    /* Unique run identification    */// (psh->runid)
							   /*    for product stream    */
							   /*    parm for retransmission   */
	ushort	proc_orig_prod_run_id;    /* Unique run identification    */ //(Update from psh->orig_runid)

	int	proc_base_prod_type_last;	/* last prod_type processed */// (Updated from psh->type,cat,code)
	int	proc_base_prod_cat_last;	/* last prod cat processed */
	int	proc_base_prod_code_last;	/* last prod_code processed */

	/* Runtime statistics */
	ulong	proc_tot_prods_lost_errs; /* total prod lost errors */
	ulong	read_tot_buff_read;		/* total bufs read */
	ulong	read_frame_tot_lost_errs; /* read frame total lost errors */
	ulong	proc_tot_prods_handled;	/* total products for process */
	ulong	proc_tot_prods_retrans_rcvd; /* prods retrans rcvd by proc */
	ulong	proc_tot_prods_retrans_rcvd_lost; /* prods retrans rcvd lost */
	ulong	proc_tot_prods_retrans_rcvd_notlost; /* prods rcvd not lost */
	ulong	proc_tot_prods_retrans_rqstd; /* prods retrans request by proc */
	ulong	proc_acqtab_prodseq_errs;	/* prodseq errs */

}ACQ_TABLE;



/*	Buffer headers */
typedef  struct	buff_hdr  {		/* buffer headers for each prod hdr */
	long	proc_prod_seqno;	/* process prod_seqno unique for each*/
	ushort	proc_blkno; 		/* process assgn put blkno */
	char	proc_prod_type;		/* prod type */
	char	proc_prod_cat;		/* prod category */
	ushort	proc_prod_code;		/* prod code */
	ushort	proc_sub_code;		/* prod subcode */
	ushort	proc_prod_scale;	/* prod scale, */
					/*  i.e.,base,natl,regl,wfo,etc. */
	char	proc_prod_flag;		/* prod flag  data handler (process)*/
					/*   PROD_DONE, etc */
	char	read_channel_type;	/* channel type set by reader, eg SBN_type */
	char	read_prod_flag;		/* prod flag */
					/*   PROD_DONE, etc */
	char	read_io_flag;		/* io flag for use by read interface */
					/*   IO_FLAG_LOW_BUFF_HIT, etc */
	ushort	buff_tot_length;	/* tot length of buff ref by buff_ptr */
	ushort	buff_data_length;	/* length of prod data in buff */

	ushort	buff_data_offset;	/* begin offset of prod data in buff */
					/*   within buff data_length */
	ushort	buff_bytes_record;	/* bytes per record in buffer */
					/*    should be same for all buffs */
					/*    of individual product */
	uchar   buff_records_per_blk;  /* Numb of record per blk     */
					/* i.e. multiple records in blk*/
	uchar   buff_blks_per_record;  /* Numb of blks per record    */
					/* i.e., record spans numb blks */
		/* Optional header lengths, if nonzero */
	ushort	buff_commhdr_length;	/* length of comm hdr in buff */
					/*   e.g. SBN protocol header */
	ushort	buff_xfrhdr_length;	/* length of xfr hdr in buff */
					/*   e.g. AWIPS prod xfr header in SBN*/
	ushort	buff_datahdr_length;	/* length of data hdr in buff */
					/*   e.g. NESDIS, WMO, etc */
		/* Flag used for client send */
	char	buff_send_flag;		/* send flag for use by acq client */
					/*   e.g., ACQ_COMMHDR_FLAG, etc. */
} BUFF_HDR;


#define ACQ_GET_ACQ_TABLE_LINK_INFO(in_p, in_lnk, out_L, out_p, out_flg) { \
	if(((int)((in_p)->max_links) >= MAX_LINKS_LO_LIMIT) && \
		((int)((in_p)->max_links) <= MAX_LINKS_LIMIT)) { \
		out_L = (int)((in_p)->max_links); \
		out_flg = 1; \
	} else { \
		out_L = MAX_LINKS; \
	} \
	if(in_lnk == 0) { \
		/* Just want to set max values and extend flag */ \
		out_p = in_p; \
	} \
}


typedef struct cpio_table_entry{
	char	*mcast_addr;
	long	cpio_addr;
	int 	cpio_fd;
}CPIO_TABLE_ENTRY;

typedef	CPIO_TABLE_ENTRY CPIO_TABLE[];

#define NUM_CPIO_ENTRIES	10

extern ulong   total_prods_retrans_rcvd;     /* prods retrans rcvd by proc */
extern ulong   total_prods_retrans_rcvd_lost; /* prods retrans rcvd lost */
extern ulong   total_prods_retrans_rcvd_notlost; /* prods rcvd not lost */
extern ulong   total_prods_retrans_rqstd;    /* prods retrans requested */
extern ulong   total_prods_handled;    /* prods retrans requested */
extern ulong   total_prods_lost_err;
extern ulong   total_frame_cnt;
extern ulong   total_frame_err;
extern int     retrans_xmit_enable;
extern char    transfer_type[10];
extern char    sbn_channel_name[13];
extern int     sbn_type;
extern char    mcastAddr[16];


extern PROD_RETRANS_TABLE *p_prod_retrans_table;
extern char               log_buff[MAX_LOG_DATA];
extern BUFF_HDR           *buff_hdr;


int init_retrans(PROD_RETRANS_TABLE **p_prod_retrans_table);
int init_acq_table(ACQ_TABLE	*p_acq_table);
int generate_retrans_rqst(ACQ_TABLE *p_acqtable, 
		long first_prod_seqno,
		long last_prod_seqno,
		int rqst_cause);

int prod_retrans_ck(ACQ_TABLE *acq_tbl, BUFF_HDR *buff_hdr, time_t *orig_arrive_time);
int do_prod_lost( BUFF_HDR *buff_hdr, ACQ_TABLE *acq_tbl);
int prod_retrans_update_entry(ACQ_TABLE *p_acqtable, BUFF_HDR *p_buffhdr,
		PROD_RETRANS_ENTRY_INFO *p_retrans_entry_info,
		PROD_RETRANS_ENTRY *p_retrans_entry, 
		int	index_value, long prod_seqno, 
		ushort in_run_id,
		int entry_flag, int err_cause);

int prod_retrans_get_addr(
	int	channel_type,						
	PROD_RETRANS_TABLE *prod_retrans_table, 
	PROD_RETRANS_ENTRY_INFO **in_p_retrans_entry_info,  
	PROD_RETRANS_ENTRY **in_p_retrans_entry, 	
	int *in_retrans_table_typ);

int log_prod_end(char *end_msg, 
		long	in_orig_prod_seqno,
		long 	in_prod_seqno,
		int 	in_prod_blkno,
		int 	in_prod_code,
		int 	in_prod_bytes,
		time_t	in_prod_start_time);

int do_prod_mismatch(ACQ_TABLE *acq_tbl, BUFF_HDR *buff_hdr);

int prod_retrans_abort_entry(
	ACQ_TABLE 	*p_acqtable,	
	long		prod_seqno,	
	int		err_cause);	

int log_prod_lost(long in_prod_errors, long in_tot_prods_errs, long in_prod_seqno);
char *get_date_time(const struct tm *p_tm, char *tz);
int get_cpio_addr(char *addr);
void freeRetransMem();
int init_buff_hdr(BUFF_HDR *p_buff_hdr);

#endif /* __RETRANS_H__ */

