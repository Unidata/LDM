/* noaaport ingest header */

#ifndef __nport__
#define __nport__

typedef struct sbn_struct {
   int version;
   int len;
   int datastream;
   unsigned long seqno;
   int runno;
   int command;
   int checksum; 
   } sbn_struct;

typedef struct pdh_struct {
   int version;
   int len;
   int transtype;
   int pshlen;
   int dbno;
   int dboff;
   int dbsize;
   int records_per_block;
   int blocks_per_record;
   long int seqno;
   } pdh_struct;

typedef struct psh_struct {
   int version;
   int onum,otype,olen;
   int hflag;
   int psdl;
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
	int seqno;
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
int readccb(char *buf,ccb_struct *ccb, psh_struct *psh, int blen);

#endif /* __nport__ */
