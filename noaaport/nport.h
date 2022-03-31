/* noaaport ingest header */

#ifndef __nport__
#define __nport__

#include <sys/types.h>

#define	SBN_CMD_INIT			1	/* Initialize receiver process		*/
#define	SBN_CMD_DATA			3 	/* Product format data transfer		*/
#define	SBN_CMD_U_DATA			4 	/* Unformatted data transfer		*/
#define	SBN_CMD_TIME			5	/* Synchronize timing			*/
#define	SBN_CMD_TBD 			6	/* TBD					*/
#define	SBN_CMD_ABORT 			7	/* Abort product data transfer		*/
#define	SBN_CMD_RESET			8	/* Reset of satellite link		*/
#define	SBN_CMD_SHUTDWN			9	/* Shutdown receive link (test purposes)*/
#define	SBN_CMD_TEST			10	/* Test message				*/
#define	SBN_CMD_TEST_RQST_REPLY		11	/* Test message with request reply	*/
#define	SBN_CMD_NULL			12	/* Null message to ignore		*/
#define	MAX_SBN_CMD			15	/* Max value of SBN_CMD 		*/

#define	SBN_CHAN_GOES			1	/* GOES channel			*/
#define	SBN_CHAN_NMC4			2	/* NMC4 channel	(obsolete)	*/
#define	SBN_CHAN_NMC1			3	/* NMC1 channel	(obsolete)	*/
#define	SBN_CHAN_NOAAPORT_OPT		4	/* NOAAPORT_OPT channel		*/
#define	SBN_CHAN_NMC			5	/* NMC channel			*/
#define	SBN_CHAN_NMC2			6	/* NMC2 channel			*/
#define	SBN_CHAN_NMC3			7	/* NMC3 channel			*/
#define SBN_CHAN_NWWS			8	/* NWWS channel			*/
#define SBN_CHAN_ADD			9	/* AWIPS Data Delivery channel	*/
#define SBN_CHAN_ENC			10	/* Encrypted channel		*/
#define SBN_CHAN_EXP			11	/* Experimental channel		*/
#define SBN_CHAN_GRW			12	/* GOESR West channel		*/
#define SBN_CHAN_GRE			13	/* GOESR East channel		*/

#define PROD_TYPE_GOES_EAST		1	/* GOES East product type			*/
#define PROD_TYPE_GOES_WEST		2	/* GOES West product type			*/
#define PROD_TYPE_NESDIS_NONGOES	3	/* NESDIS nonGOES product type			*/
#define PROD_TYPE_NOAAPORT_OPT		3	/* NOAAPORT Option product type			*/
#define PROD_TYPE_NWSTG			4	/* NWSTG product type (stream)			*/
#define PROD_TYPE_NEXRAD		5	/* NEXRAD product type				*/
#define PROD_TYPE_MHS			6	/* MHS message handling system product type	*/
#define PROD_TYPE_SAT_OTHER		7	/* SAT_OTHER product type			*/
#define PROD_TYPE_DATA_DELIVERY		8	/* Product retrieved via AWIPS Data Delivery	*/
#define PROD_TYPE_GOESR_EAST		9	/* GOES-R East product type			*/
#define PROD_TYPE_GOESR_WEST		10	/* GOES-R West product type			*/
#define PROD_TYPE_POLAR_SAT		11	/* Polar Satellite product type (NPP/JPSS)	*/

#define IS_PROD_TYPE_IMAGE(ptype)  \
	((ptype==PROD_TYPE_GOES_EAST) ||\
	(ptype==PROD_TYPE_GOES_WEST) ||\
	(ptype==PROD_TYPE_NOAAPORT_OPT))

#define NAME_PROD_TYPE_GOES		"GOES"			/* name for GOES product type 		*/
#define NAME_PROD_TYPE_GOES_EAST	"GOES_EAST"		/* name for GOES East prod type		*/
#define NAME_PROD_TYPE_SAT_OTHER	"SAT_OTHER"		/* name SAT_OTHER prod type		*/
#define NAME_PROD_TYPE_GOES_WEST	"GOES_WEST"		/* name for GOES West prod type		*/
#define NAME_PROD_TYPE_NESDIS		"NESDIS"		/* name for NESDIS product type		*/
#define NAME_PROD_TYPE_NESDIS_NONGOES	"NESDIS_NONGOES"	/* name for NESDIS nonGOES prod type	*/
#define NAME_PROD_TYPE_NOAAPORT_OPT	"NOAAPORT_OPT"		/* name for NOAAPORT_OPT prod type	*/
#define NAME_PROD_TYPE_SAT_AK_HI_PR	"SAT_AK_HI_PR"		/* name for AK/HI/PR prod type		*/
#define NAME_PROD_TYPE_MHS		"MHS"			/* name for MHS prod type		*/
#define NAME_PROD_TYPE_NWSTG		"NWSTG"			/* name for NWSTG product type		*/
#define NAME_PROD_TYPE_NEXRAD		"NEXRAD"		/* name for NEXRAD product type		*/
#define NAME_PROD_TYPE_ASOS		"ASOS"			/* name for ASOS product type		*/
#define NAME_PROD_TYPE_DATA_DELIVERY	"DATA_DELIVERY"		/* Name for Data Delivery product type	*/
#define NAME_PROD_TYPE_GOESR_EAST	"GOES_R_EAST"		/* name for GOES-R East product type	*/
#define NAME_PROD_TYPE_GOESR_WEST	"GOES_R_WEST"		/* name for GOES-R West product type	*/
#define NAME_PROD_TYPE_POLAR_SAT	"POLAR_SAT"		/* name for (NPP/JPSS) product type	*/

/*
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
	(ptype==PROD_TYPE_DATA_DELIVERY)?NAME_PROD_TYPE_DATA_DELIVERY:\
	(ptype==PROD_TYPE_GOESR_EAST)?NAME_PROD_TYPE_GOESR_EAST:\
	(ptype==PROD_TYPE_GOESR_WEST)?NAME_PROD_TYPE_GOESR_WEST:\
	(ptype==PROD_TYPE_POLAR_SAT)?NAME_PROD_TYPE_POLAR_SAT:\
	(ptype==PROD_TYPE_RESERVE)?"RESERVE":"UNKNOWN")
*/

#define PROD_CAT_TEXT			1	/* text product cat		*/
#define PROD_CAT_GRAPHIC		2	/* graphic product cat		*/
#define PROD_CAT_IMAGE			3	/* image product cat		*/
#define PROD_CAT_GRID			4	/* grid data product cat	*/
#define PROD_CAT_POINT			5	/* point data product cat	*/
#define PROD_CAT_BINARY			6	/* binary product cat		*/
#define PROD_CAT_OTHER			7	/* other product cat		*/
#define PROD_CAT_NETCDF_IMAGE		8	/* Image encoded in NetCDF	*/
#define PROD_CAT_NIDS			99
#define PROD_CAT_HDS_TEXT		(PROD_CAT_TEXT+100)
#define PROD_CAT_HDS_OTHER		(PROD_CAT_OTHER+100)

#define GET_PROD_CAT_NAME(pcat)  \
	((pcat==PROD_CAT_TEXT)?NAME_PROD_CAT_TEXT:\
	(pcat==PROD_CAT_GRAPHIC)?NAME_PROD_CAT_GRAPHIC:\
	(pcat==PROD_CAT_IMAGE)?NAME_PROD_CAT_IMAGE:\
	(pcat==PROD_CAT_GRID)?NAME_PROD_CAT_GRID:\
	(pcat==PROD_CAT_POINT)?NAME_PROD_CAT_POINT:\
	(pcat==PROD_CAT_BINARY)?NAME_PROD_CAT_BINARY:\
	(pcat==PROD_CAT_NETCDF_IMAGE)?NAME_PROD_CAT_NETCDF_IMAGE:\
	(pcat==PROD_CAT_OTHER)?NAME_PROD_CAT_OTHER:"UNKNOWN")

typedef struct sbn_struct {
   int version;
   int len; // Length of frame header in bytes
   int datastream;
   unsigned long seqno; // ProductMaker's arithmetic on this depends on this type
   int runno;
   int command;
   int checksum; 
   } sbn_struct;

typedef struct pdh_struct {
   int version; // Version of product definition header
   int len; // Length of product-definition in bytes
   /**
    * Indicates the status of a product transfer:
    *    1 = Start of a new product
    *    2 = Product transfer still in progress
    *    4 = End (last packet) of this product
    *    8 = Product error
    *    16 = Product compressed (from `ProductMaker`)
    *    32 = Product Abort
    *    64 = Option headers follow; e. g., product-specific header
    */
   int transtype;
   int pshlen; // Length of product-specific header in bytes
   /**
    * Used during fragmentation and reassembly to identify the sequence
    * of the fragmented blocks. Blocks are number o to n.
    */
   int dbno;
   /**
    * Offset in bytes where the data for this block can be found relative
    * to beginning of data block area.
    */
   int dboff;
   int dbsize; // Number of data bytes in the data block
   int records_per_block;
   int blocks_per_record;
   long int seqno;
   } pdh_struct;

typedef struct psh_struct {
   int version;
   int onum,otype,olen;
   int hflag;
   int psdl; // Length of AWIPS product-specific header (in bytes)
   int bytes_per_record;
   int ptype;
   int pcat;
   int pcode;
   int frags;
   int nhoff;
   int source;
   long int seqno,rectime,transtime;
   int runid,origrunid;
   char pname[1024];
   int hasccb,ccbmode,ccbsubmode;
   char ccbdtype[20];
   char metadata[512];
   int metaoff;
   } psh_struct;

typedef struct ccb_struct {
   int b1;
   int len;
   int user1, user2;
   } ccb_struct;

typedef struct pdb_struct {
   int len;
   int source;
   int year,month,day,hour,minute,second,sechunds;
   int platform,sector,channel;
   long nrec,recsize;
   int proj;
   int nx,ny;
   int la1,lo1,lov,latin;
   int bit1f,flags;
   long dx,dy;
   int res, compress;
   } pdb_struct;

typedef struct datastore {
   int seqno;
   int fragnum;
   /*char *rec;*/
   size_t offset;
   int recsiz;
   struct datastore *next;
   } datastore;

typedef struct prodstore {
	int seqno;              ///< Product sequence number from PDH
	int nfrag;
	struct datastore *head;
	struct datastore *tail;
	} prodstore;

/*
 * function prototypes
 */

int readsbn(char *buf, sbn_struct *sbn);
int readpdh(char *buf, pdh_struct *pdh);
int readpdb(char *buf, psh_struct *psh, pdb_struct *pdb, int zflag, int bufsz );
int readpsh(char *buf, psh_struct *psh);
int readccb(
        char* const                  buf,
        ccb_struct* const            ccb,
        psh_struct* const            psh,
        const int                    blen);

#endif /* __nport__ */
