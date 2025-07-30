/* -----------------------------------------------------------------------------
 *
 * File Name:
 *	fileIngester.c
 *
 * Description:
 * 	This program initiates processing on weather product files using one of several
 *	possible input methods and performing one of several output actions. It runs as
 *	either a standalone daemon or as a subprocess of LDM to insert files into a
 *	product queue. As a standalone daemon, it can either discard files, or write them
 *	to another directory as a front-end to uplink_send. This latter purpose is now
 *	largely obsolete since the same input methods are now supported natively by
 *	uplink_send.
 *
 *	On input, fileIngester can either poll a single input directory, or read files
 *	to process from a named pipe (from uplink_send through acqserver). Polling method
 *	can be: GOESR, NDE, PDA, or POLL.
 *
 *	The GOESR method relies on each file being renamed to a specific extension when
 *	it's ready to be processed. Full products are broken into smaller tiles to be able
 *	to meet program latency requirements. For each full product, a Product Activity
 *	Report (XML file) is transmitted after all related tiles are sent. The PAR must
 *	be processed by a separate external program. To support PAR processing,
 *	fileIngester can create individual files in a separate directory containing a hash
 *	code of each tile. GOES-R product files do not contain the required WMO
 *	header, which this program will automatically generate from the file name. If
 *	the output is to an LDM queue, the WMO header will be added automatically. If
 *	not, then the '-w' option must be provided for the WMO header to be added to
 *	the output file.
 *
 *	NDE and PDA both use a 'marker' file to indicate a matching product file is
 *	ready for ingest. The NDE and PDA methods are identical: the marker file for each
 *	product will contain a hash code for the product file that fileIngester can process
 *	directly and output non-matching results to the error log. Regardless of status,
 *	each file will be ingested.
 *
 *	The POLL method just assumes files are ready whenever they can be opened.
 *
 *	To output to an LDM product queue, fileIngester must be specifically built as
 *	an LDM extension (see build instructions below).
 *
 * Dependencies:
 *	mlogger, ldmProductQueue, stdclib, goesr_lib
 *
 * Build Instructions:
 *	By default, LDM support is not compiled in.  To use LDM, the source must
 *	be compiled as part of LDM using the Makefile.am automake template.  This
 *	is part of a branch of v6.13.0 of LDM that includes changes to the LDM
 *	build system.  To build it for LDM, follow the normal procedure.  When
 *	ready to build, do the following from the base LDM source directory as
 *	user ldm:
 *
 *		autoreconf -fi
 *		automake	(if version errors occur, follow instructions provided)
 *		./configure --with-ingester --prefix=$HOME
 *		rm -f libtool
 *		ln -s /usr/bin/libtool
 *		make
 *		make install
 *
 *	Configuration
 *	-------------
 *
 *	This program can be used with LDM in several ways, or it can be used without
 *	LDM at all:
 *
 *	1. On a standalone server as upstream LDM feeding a downstream LDM
 *
 *		In addition to the standalone upstream LDM server that this software will
 *		be installed on, the downstream LDM instance must be configured to pull
 *		RaFTR-generated products. If NIS is used, ensure the RaFTR host and the
 *		machine hosting the downstream LDM are defined. If not using NIS, define
 *		both in the local /etc/hosts file on each machine. After LDM has been
 *		successfully built and installed, configure as follows:
 *
 *		RaFTR Server
 *
 *		Ensure portmapper/rpcbind is running.
 *
 *		In /etc/services, add or modify to obtain the following:
 *
 *			ldm	388/tcp	ldm
 *			ldm	388/udp	ldm
 *
 *		In /etc/rpc, add the following if not already present:
 *
 *			ldm	300029	ldmd
 *
 *		In the LDM configuration-file, etc/ldmd.conf, add an entry to
 *		allow the downstream LDM instance to access it. It should be
 *		something like this (with TAB characters between each field:
 *
 *			ALLOW	ANY	^<host regex>.*$
 *
 *		where <host regex> is a regular expression, such as: cpsbn.-tbwo
 *
 *		Remove or comment out any lines that begin with "EXEC" and add one like:
 *
 *			EXEC	"fileIngester -p<polling dir> -L<log dir> -M<log dir> \
 *				-iGOESR -oLDM -q<LDM product queue path/name> -a0 -d2 -n2"
 *
 *		Update the LDM registry, etc/registry.xml, with reasonable
 *		values. Be sure to updated the clock and provide a
 *		fully-qualified name for the local host.
 *
 *		A sample system service file (ldm-raftr) in
 *		/opt/src/ldm/raftr if needed.
 *
 *
 *		Downstream LDM Server
 *
 *		In the LDM configuration-file, etc/ldmd.conf, add an entry to
 *		request data from raftr (white space must be tabs). For
 *		instance, if the upstream LDM instance were raftr-tbwo, this
 *		would be used:
 *
 *			REQUEST ANY ".*" raftr-tbwo
 *
 *		In the pqact(1) configuration-file, etc/pqact.conf, add an entry
 *		for the GOES-R WMO headers using tabs as field separators.  For
 *		instance:
 *
 *			ANY 	^(TI[RS]...) (KNES) (..)(..)(..) (...)
 *			    	FILE	-overwrite -log -close -edex    /data_store/goesr/\1_\2_\3\4\5_\6.nc
 *
 *		Start LDM on both the upstream and downstream servers. Check ldmd.log on
 *		the downstream servers for issues connecting with the upstream servers.
 *
 *	2. On the machine hosting RaFTR feeding a downstream LDM
 *
 *		The only difference between this scenario and the first one is that RaFTR
 *		will not be copying products across the network. Instead, RaFTR will be
 *		writing products locally. Configuration is exactly the same as above, but
 *		RaFTR must be configured to use some file transfer protocol (such as
 *		SFTP, SCP, or FTPS) to copy the files to this machine.
 *
 *	3. On a CPSBN that is also receiving an SBN stream via noaaportIngester
 *
 *	4. Without LDM to generate SBN-ready GOES-R products with WMO headers
 *
 *	5. Without LDM to write PDA and NDE products to directories for ingest with
 *	   uplink_send.
 *
 *	6. To exercise and validate RaFTR without additional downstream processing.
 *
 *	7. To ingest products into a product queue through uplink_send/acqserver.
 *
 *	To compile without LDM support, use the provided Makefile.local file:
 *
 *		make -f Makefile.local fileIngester
 *
 * Functions defined:
 *
 * Author:
 * 	Brian M Rapp		17-Nov-2013
 *
 * Modification History:
 *	Modified by		Date
 *      Description
 *
 *	Brian Rapp <Brian.Rapp@noaa.gov>	Wed Dec 3 15:43:24 2014 -0500
 *	Added NDE/PDA processing to read_raftr
 *
 *	Brian Rapp <Brian.Rapp@noaa.gov>	Thu Feb 5 15:44:17 2015 -0500
 *	Made exit summary message always log
 *
 *	Brian Rapp <Brian.Rapp@noaa.gov>	Thu Feb 5 15:54:15 2015 -0500
 *	Modified summary statistics to use product log instead of error log
 *
 *	Brian M. Rapp <Brian.Rapp@noaa.gov>	Wed Apr 1 08:52:50 2015 -0400
 *	Support for new types/cats/codes
 *
 *	Brian Rapp <Brian.Rapp@noaa.gov>	Thu Apr 23 15:41:34 2015 -0400
 *	Gave read_raftr.c the ability to generate checksum files for each
 *	tile file it processes.
 *
 *	Brian Rapp <brian.rapp@noaa.gov>	Sun May 17 13:54:34 2015 -0400
 *	Refactored read_raftr to fileIngester
 *
 *	Brian Rapp <brian.rapp@noaa.gov>	Sat May 23 15:57:51 2015 -0400
 *	Added NDE/PDA processing to fileIngester.c.
 *
 *	Brian Rapp <brian.rapp@noaa.gov>	Mon May 25 10:59:17 2015 -0400
 *	Changed directory creation permissions in fileIngester.c
 *
 *	commit 51b37cd3ac498413a363812a2c224c282e3be364
 *	Brian Rapp <brian.rapp@noaa.gov>	Fri Jun 5 15:36:06 2015 -0400
 *	Add -h option to fileIngester
 *
 *	commit cb55f0d8dbb3156ef2af69155840f921c2091bc8
 *	Brian Rapp <Brian.Rapp@noaa.gov>	Tue Jun 9 15:15:48 2015 -0400
 *	Fixed bug preventing product files from being deleted for
 *	OutType==LDM in fileIngester.c
 *
 *	commit 25b2f76e3988b50e50b9e99d71d194b6a589438a
 *	Brian Rapp <Brian.Rapp@noaa.gov>	Thu Jun 18 16:18:31 2015 -0400
 *	Fixed fileIngester bug that caused a 2nd WMO header to be
 *	added for PDA products written to an LDM queue.
 *
 *	Brian Rapp		25-Jun-2015
 *	Added support for the POLL input method.
 *
 *	Brian Rapp		26-Jun-2015
 *	Added support for the ACQ_PIPE input method.
 *
 * -------------------------------------------------------------------------- */
#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <glob.h>
#include <locale.h>

#ifdef LDM_SUPPORT
#       include <log.h>
#       include <md5.h>
#       include "ldmProductQueue.h"
#endif

#include "../fileIngest/stdclib.h"
#include "../fileIngest/mlogger.h"
#include "../fileIngest/goesr_lib.h"

#define DIRECTORY_CREATE_PERMS		(S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
#define DIRECTORY_FULL_OPEN_PERMS	(S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH)
#define OUTFILE_FINAL_PERMS		(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
#define OUTFILE_CREATE_PERMS		S_IWUSR
#define DEFAULT_VERBOSITY		V_ERROR
#define DEFAULT_POLLING_INTERVAL	2		/* Default directory polling interval in seconds */
#define SLEEP_TIME_SECS			1		/* Time to sleep between loop iterations */
#define DEFAULT_MAX_QUEUE_SIZE		500
#define DEFAULT_FILE_SPEC		"*"
#define DEFAULT_MAX_SAVE_FILES		500
#define DEFAULT_SAVE_FILE_DIGITS	3
#define	MAX_FILENAME_LEN		128
#define STRINGIZE(x)            #x
#define MAX_PATH_LEN			256
#define SCAN_PATHNAME           "%" STRINGIZE(MAX_PATH_LEN) "s"
#define MAX_ACQ_PATH_LEN		128
#define MAX_HOST_LEN			64
#define MAX_HASH_LEN			128
#define SCAN_HASH_CODE          "%" STRINGIZE(MAX_HASH_LEN) "s"
#define MIN_DISCARD_AGE			60		/* Minimum age for DiscardAge in seconds */
#define DEFAULT_DISCARD_AGE		3600		/* Default discard age in seconds */
#define PROD_LOG			"products.log"
#define PROD_LOG_PATH			"/awips/logs/Products"
#define MESSAGE_LOG			"messages.log"
#define MESSAGE_LOG_PATH		"/awips/logs/Messages"
#define DEF_LDM_QUEUE			"/awips/ldm/data/ldm.pq"
#define DEF_OUTFILE_PREFIX		"goesr"
#define DEF_LOG_SIZE			(4 * 1024 * 1024)
#define TRACE_LOG_SIZE			(100 * 1024 * 1024)
#define LOG_BUFFER_SIZE			1024
#define MIN_PROD_SIZE_READ		25
#define STATUS_FREQUENCY		50
#define MAX_HOST_NAME_LEN		64		/* max length for host name */
#define	DEF_STR_LEN			128

#define SIZE_WMO_HDR			24
#define SIZE_WMO_TERM			3
#define WMO_TERMINATOR			"\r\r\n"
#define SIZE_SBN_HDR			11
#define SIZE_SBN_TLR			4
#define SBN_HEADER_TEMPLATE		"\001\015\015\012%03d\040\015\015\012"
#define SBN_TRAILER			"\015\015\012\003"
#define DEFAULT_FEED_TYPE		OTHER

#define OUT_NONE			0
#define OUT_DISCARD			1
#define OUT_FILE			2
#ifdef LDM_SUPPORT
#	define OUT_LDM			4
#endif

#define IN_NONE				0
#define IN_GOESR			1
#define IN_NDE				2
#define IN_PDA				3
#define IN_POLL				4
#define	IN_ACQ_PIPE			5

#define PROD_TYPE_NWSTG			5
#define PROD_CAT_NWSTG			101
#define SBN_TYPE_ID_GOESR		12
#define PROD_CAT_IMAGE			3
#define PC_WMO_SAT_IMAGE_T		52

#define STAT_ERROR			-1
#define STAT_SUCCESS			0
#define STAT_MORE_FILES			1
#define STAT_ALREADY_QUEUED		3

#define MIN(a, b)	((a < b) ? a : b)

typedef  struct	pipe_prod_name_hdr  {				/* prod filename hdr for pipe 	     */
	int		pipe_insert_time;			/* time product added to pipe	     */
	int		pipe_prod_NCF_rcv_time;			/* time product received at NCF	     */
	ushort		pipe_prod_type;				/* product category                  */
								/*     TYPE_GOES, TYPE_NWSTG, etc    */
	ushort		pipe_prod_cat;				/* product category                  */
								/*     CAT_IMAGE, etc		     */
	ushort		pipe_prod_code;				/* product code 1,2,3,etc            */
	ushort		pipe_prod_flag;				/* product flag (error & status)     */
								/*     error_mask & prod_done,etc    */
	ushort		reserve1;				/* reserved	                     */
	ushort		reserve2;				/* reserved	                     */
	uint		pipe_prod_orig_prod_seqno;		/* original prod seqno to retransmit */
	int		pipe_prod_orig_NCF_rcv_time;		/* NCF receive time binary(GMT) */
		/* Retransmit info */
	ushort		pipe_prod_run_id;			/* Unique run identification    */
								/*    for product stream    */
								/*    parm for retransmission   */
	ushort		pipe_prod_orig_run_id;			/* Unique orig run identification    */
	char		pipe_prod_filename[MAX_ACQ_PATH_LEN];	/* UNIX filename for prod */
} PIPE_PROD_NAME_HDR;

typedef struct	file_node {
	char		*fptr;		/* File name (without path) */
	time_t		mtime;		/* File last modification time */
	off_t		fsize;		/* File size in bytes */
}	FILE_NODE;

typedef struct	file_list_hdr {
	FILE_NODE	*fileNodes;	/* Pointer to array of FILE_NODEs */
	int		count;		/* Number of files in list */
}	FILE_LIST;

typedef struct	optdef {
	char		*str;
	unsigned int	val;
}	OPT_SPEC;

OPT_SPEC		inOpts[] = {	{ "GOESR",	IN_GOESR	},
					{ "NDE",	IN_NDE		},
					{ "PDA",	IN_PDA		},
					{ "POLL",	IN_POLL		},
					{ "ACQ_PIPE",	IN_ACQ_PIPE	},
					{ NULL,		IN_NONE		}
			};

OPT_SPEC		outOpts[] = {	{ "DISCARD",	OUT_DISCARD	},
					{ "FILE",	OUT_FILE	},
#ifdef LDM_SUPPORT
					{ "LDM",	OUT_LDM		},
#endif
					{ NULL,         OUT_NONE	}
                        };

#define MD5                             1
#define SHA1                            2
#define SHA224                          3
#define SHA256                          4
#define SHA384                          5
#define SHA512                          6

OPT_SPEC                csOpts[] = {	{ "MD5",        MD5     },
					{ "SHA1",       SHA1    },
					{ "SHA224",     SHA224  },
					{ "SHA256",     SHA256  },
					{ "SHA384",     SHA384  },
					{ "SHA512",     SHA512  },
					{ NULL,         0       }
                        };

char			*hashProgs[] = { "md5sum",
					 "sha1sum",
					 "sha224sum",
					 "sha256sum",
					 "sha384sum",
					 "sha512sum"
			};

#ifdef LDM_SUPPORT
OPT_SPEC		feedOpts[] = {	{ "IMAGE",	IMAGE	},
					{ "TEXT",	TEXT	},
					{ "GRID",	GRID	},
					{ "POINT",	POINT	},
					{ "BUFR",	BUFR	},
					{ "GRAPH",	GRAPH	},
					{ "OTHER",	OTHER	},
					{ "NEXRAD",	NEXRAD	},
					{ "NPORT",	NPORT	}
			};
#endif

extern char             *optarg;

pid_t                   MyPid;
char			*ProgName;
char			LocalHostName[MAX_HOST_NAME_LEN+1];
int			PollInterval;				/* Default time to sleep if no files in polling directory */
int			SleepPollInterval;			/* Time to sleep between poll iterations */
int			Done;					/* Set when need to shut down */
int			SaveFiles;				/* If set, processed files are saved to the 'sent' directory */
int			MaxSentFiles;				/* If using -d, maximum # of files to write before starting over */
int			SentFileDigits;				/* Number of digits to output in sent file names */
int			SaveFails;				/* If set, files that failed valiation are saved to 'fail' directory */
int                     OutAction;                              /* Output options: OUT_DISCARD, OUT_FILE, OUT_LDM, OUT_ACQSERVER */
int                     InType;                                 /* Input options: IN_GOESR, IN_NDE */
int                     Validate;                               /* Validate input files or not.  Validation method depends on InType */
int                     CreateChecksum;                         /* Create checksum of input file contents and write to .hash file */
int			HashOpt;
char			*HashProgram;				/* Program executable name */
int                     MaxQueueSize;                           /* Maximum number of products to read from the polling directory per cycle */
int                     AddLdmWrapper;                          /* If set, an LDM wrapper (header and trailer) is added to each product */
int                     AddWmoHeader;                           /* If set, a WMO header is calculated and added to each product */
char			InputSource[MAX_PATH_LEN+1];		/* Directory to poll for products or pipe name with -iPIPE */
char			Loc[5];					/* Input location string - either "pipe" or "dir" */
char			FailDir[MAX_PATH_LEN+1];		/* Directory to write failed products */
char			SentDir[MAX_PATH_LEN+1];		/* Directory to write sent products */
char			SaveDir[MAX_PATH_LEN+1];		/* Directory to save products to when -o FILE option is provided along with -d */
char			ParDir[MAX_PATH_LEN+1];			/* Directory to save PAR files to (GOES-R) */
char			HostName[MAX_HOST_LEN+1];		/* Local host name */
char			PollFileSpec[MAX_FILENAME_LEN+1];	/* File specification for products that are ready to ingest, can include wildcards */
char			LogPathBase[MAX_PATH_LEN+1];		/* Product log directory */
char			MessagePath[MAX_PATH_LEN+1];		/* Error/Debug log directory */
int			DiscardAge;				/* Files older than this (in seconds) will be discarded */
int			Verbosity;				/* Verbosity level of error log */
LOGGER			*pLog;					/* Product logger */
LOGGER			*eLog;					/* Error logger */
char			*ProdBuf;				/* Buffer used to read products from disk.  Reallocated as necessary */
int			ProdBufSize;				/* Current size of prodBuf */
unsigned long long	TotalProductsProcessed;			/* Total number of products processed since start up */
unsigned long long      TotalBytesProcessed;                    /* Total number of bytes processed since start up */
int                     SbnSeqNo;                               /* Product sequence number for LDM */
int                     SentSeqNo;                              /* Sequence number when the -of option is provided */

#ifdef LDM_SUPPORT

feedtypet		FeedType;				/* LDM Feed Type -- only valid with -oLDM */
char                    pqfName[MAX_PATH_LEN+1];                /* LDM product queue file path */
LdmProductQueue         *prodQueue;                             /* LDM queue structure */
MD5_CTX                 *md5ctxp;                               /* MD5 context for generating hash codes used by LDM */

#endif

/* -----------------------------------------------------------------------------
 * Function Name
 *	usage
 *
 * Format
 *	static void usage (const char *progname)
 *
 * Arguments
 *	char	*progname
 *	Character string containing the name of this program.
 *
 * Description
 *	Print program usage information and exit.
 *
 * Return Values
 *	This function does not return, it exits with value 1 (error).
 *
 * -------------------------------------------------------------------------- */

static void usage (const char *progname) {

	fprintf (stderr, "usage: %s -p <poll dir or pipe> -i (ACQ_PIPE|GOESR|NDE|PDA|POLL)\n"
#ifdef LDM_SUPPORT
		"\t-o (DISCARD|FILE|LDM) [-q <queue>|-d <dir>] [-F <feedtype>] [-w]\n"
#else
		"\t-o (DISCARD|FILE) [-d <dir>] [-w]\n"
#endif
		"\t[-c <checksum type>] [-h <PAR dir>] [-x <sent digits>]\n"
		"\t[-s <sent dir>] [-f <fail dir>] [-n <polling interval>]\n"
		"\t[-t <template>] [-a <discard age>] [-L <prod log path>]\n"
		"\t[-M <msg log path>] [-Q <max q size] [-D <log level>]\n\n"

	"This program initiates processing on weather product files using one of several\n"
	"possible input methods and performing one of several output actions. It runs as\n"
	"either a standalone daemon or as a subprocess of LDM to insert files into a\n"
	"product queue. As a standalone daemon, it can either discard files, or write them\n"
	"to another directory as a front-end to uplink_send. This latter purpose is now\n"
	"largely obsolete since the same input methods are now supported natively by \n"
	"uplink_send.\n\n"
	"On input, fileIngester can either poll a single input directory, or read files\n"
	"to process from a named pipe (from uplink_send through acqserver). Polling method\n"
	"can be: GOESR, NDE, PDA, or POLL.\n\n"
	"The GOESR method relies on each file being renamed to a specific extension when\n"
	"it's ready to be processed. Full products are broken into smaller tiles to be able\n"
	"to meet program latency requirements. For each full product, a Product Activity\n"
	"Report (XML file) is transmitted after all related tiles are sent. The PAR must\n"
	"be processed by a separate external program. To support PAR processing,\n"
	"fileIngester can create individual files in a separate directory containing a hash\n"
	"code of each tile. GOES-R product files do not contain the required WMO\n"
	"header, which this program will automatically generate from the file name. If\n"
	"the output is to an LDM queue, the WMO header will be added automatically. If\n"
	"not, then the '-w' option must be provided for the WMO header to be added to\n"
	"the output file.\n\n"
	"NDE and PDA both use a 'marker' file to indicate a matching product file is\n"
	"ready for ingest. The NDE and PDA methods are identical: the marker file for each\n"
	"product will contain a hash code for the product file that fileIngester can process\n"
	"directly and output non-matching results to the error log. Regardless of status,\n"
	"each file will be ingested.\n\n"
	"The POLL method just assumes files are ready whenever they can be opened.\n\n"
	"Options:\n"
	"-a <discard age>\n"
	"\tOptional parameter specifying the maximum age (in seconds) of a file for\n"
	"\tprocessing. Old files will be deleted. The minimum is 60; the default is 3600.\n"
	"\tIf specified as 0 or less, then all files are processed regardless of age.\n\n"
	"-c <hash type>\n"
	"\tOptional parameter indicating that a file of same name as input file with '.hash'\n"
	"\textension containing a hashcode for each tile file is to be created. If this\n"
	"\toption is not provided, then checksum files will not be produced. If -i is \"GOESR\"\n"
	"\tand -c is provided, then -h must also be provided. Supported hash\n"
	"\ttypes are:\n"
	"\t\tMD5\n"
	"\t\tSHA1\n"
	"\t\tSHA224\n"
	"\t\tSHA256\n"
	"\t\tSHA384\n"
	"\t\tSHA512\n\n"
	"-D <log level>\n"
	"\tUsed to enable debugging.  Diagnostic messages are written to the error log.\n"
	"\tThe default logging level is 1 (ERROR). Other available levels are:\n"
	"\t2 (INFO), 3 (DEBUG), and 4 (TRACE).\n\n"
	"-d <save dir>\n"
	"\tOptional parameter specifying the directory to save files to when -o FILE\n"
	"\toption is given.\n\n"
#ifdef LDM_SUPPORT
	"-F <feed type>\n"
	"\tOptional parameter to set feed type for products inserted into LDM. Default\n"
	"\tis OTHER. Valid types are: IMAGE,TEXT,GRID,POINT,BUFR,GRAPH,OTHER,NEXRAD,NPORT\n\n"
#endif
	"-f <fail directory>\n"
	"\tOptional parameter specifying where failed and discarded products are written.\n"
	"\tIf not provided, failed files are not saved.\n\n"
	"-h <PAR dir>\n"
	"\tOptional directory for storing GOES-R PAR files. -h must be provided with -iGOESR\n"
	"\tand -c. -h cannot be used alone.\n\n"
	"-i (ACQ_PIPE|GOESR|NDE|PDA|POLL)\n"
	"\tInput method for all ingested files. Can be ACQ_PIPE, GOESR, NDE, PDA, or POLL.\n"
	"\tAll but POLL and ACQ_PIPE require a file template (-t).  With POLL processing,\n"
	"\tany files matching the template ('*' by default) will be ingested as long as\n"
	"\tthey are accessible (readable). With ACQ_PIPE processing, individual files\n"
	"\tto be processed will be written to the acqserver pipe specified with -p. For\n"
	"\tNDE and PDA products, files matching the template specification contain a hash\n"
	"\tcode of the type specified by the -c argument. We are guaranteed that if the\n"
	"\thash file exists, a product with the same name minus the template extension will\n"
	"\texist and will be ready to be ingested. No additional processing is needed for\n"
	"\tNDE products. For GOESR products, the existence of a match to the template\n"
	"\tindicates that the file itself contains a product that is ready for processing.\n"
	"\tGOESR files as received from the provider do not contain WMO headers. Information\n"
	"\tin the file name is used to generate a WMO header, which is inserted at the front\n"
	"\tor the file along with an LDM header and trailer. If -c is provided with -iGOESR,\n"
	"\tthen -h must be provided as well.\n\n"
	"-L <prod log path>\n"
	"\tThis optional parameter provides the path to the product log file directory.\n"
	"\tBy default it is: %s\n\n"
	"-M <msg log path>\n"
	"\tThis optional parameter provides the path to the message log directory.\n"
	"\tBy default it is: %s\n\n"
	"-n <polling interval>\n"
	"\tOptional parameter how long this program should sleep (in seconds) between\n"
	"\tpolls when no files are available for processing. This option is not valid\n"
	"\twith -iACQ_PIPE. The default interval is %d seconds.\n\n"
#ifdef LDM_SUPPORT
	"-o (DISCARD|FILE|LDM)\n"
	"\tRequired parameter indicating output type. Options are 'DISCARD', 'FILE',\n"
	"\tor 'LDM'. If 'FILE', then products will be written to the 'sent' directory.\n"
	"\tIf 'LDM', products will be written to the LDM product queue as specified\n"
	"\twith the '-q' option. If 'DISCARD', products will be discarded after\n"
	"\tvalidation and logging.\n\n"
#else
	"-o (DISCARD|FILE)\n"
	"\tRequired parameter indicating output action.  Options are 'FILE' or 'DISCARD'.\n"
	"\tIf 'FILE', then products will be written to the 'sent' directory. If 'DISCARD'\n"
	"\tproducts will be validated, logged, and discarded.\n\n"
#endif
	"-p <poll dir or pipe>\n"
	"\tRequired parameter specifying the directory that will be polled for products or,\n"
	"\twith -iACQ_PIPE, the pipe that will be read for each product.\n\n"
#ifdef LDM_SUPPORT
	"-q <ldm queue>\n"
	"\tOptional parameter providing the path to the LDM queue.\n"
	"\tDefault is: %s\n\n"
#endif
	"-Q <maximum queue size>\n"
	"\tMaximum number of files to queue from the polling directory per pass.  If\n"
	"\tset to 0 or less, then there is no limit.  Default is 0.\n\n"
	"-s <sent directory>\n"
	"\tOptional parameter specifying the directory for processed products.  If not\n"
	"\tprovided, files are deleted after processing.\n\n"
	"-t <file spec template>\n"
	"\tFile template specification used to find files that are ready for processing.\n"
	"\tThe default spec is: %s\n\n"
	"-w\n"
	"\tOptional parameter to force WMO header to be added to each file.\n\n"
	"-x <max sent files>\n"
	"\tNumber of files to save in the sent directory. Only valid with -d\n\n"
	, progname, PROD_LOG_PATH, MESSAGE_LOG_PATH, DEFAULT_POLLING_INTERVAL
#ifdef LDM_SUPPORT
	, DEF_LDM_QUEUE
#endif
	, DEFAULT_FILE_SPEC
	);
	exit (1);
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	getOptFromString
 *
 * Format
 *	int getOptFromString (OPT_SPEC opts, char *optStr)
 *
 * Arguments
 *	opts
 *	Pointer to array of options
 *
 *	optStr
 *	Option string to match
 *
 * Description
 *	Searches opts for optStr and returns the matching opt value.
 *
 * Return Values
 *	Value corresponding to the option string.
 *
 * -------------------------------------------------------------------------- */

int getOptFromString (OPT_SPEC *opts, char *optStr) {

	OPT_SPEC	*p = opts;
	int		i;

	for (i = 0; p[i].str; i++) {
		if (!strcmp (p[i].str, optStr)) {
			return i;
		}
	}

/*
	while (p->str) {
		if (!strcmp (p->str, optStr)) {
			return p->val;
		}

		p++;
	}
*/

	return -1;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	cmd_line
 *
 * Format
 *	static void cmd_line (int argc, char **argv)
 *
 * Arguments
 *	argc
 *	This will be argc as passed directly from main().
 *
 *	argv
 *	This will be argv as passed directly from main().
 *
 * Description
 *	This function processes command line arguments and assigns them as
 *	required for this program.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void cmd_line (int argc, char **argv) {

	int		optchar;
	int		val;
	char		str[DEF_STR_LEN+1];
	char		optstr[DEF_STR_LEN+1];
#ifdef LDM_SUPPORT
	char		*ldmopt = "q:";
#else
	char		*ldmopt = "";
#endif

	sprintf (optstr, "a:c:D:d:F:f:h:i:L:lM:n:o:p:Q:%ss:t:vwx:?", ldmopt);

	PollInterval            = DEFAULT_POLLING_INTERVAL;
	SleepPollInterval       = SLEEP_TIME_SECS;
	DiscardAge		= DEFAULT_DISCARD_AGE;
	Verbosity		= DEFAULT_VERBOSITY;
	MaxQueueSize		= DEFAULT_MAX_QUEUE_SIZE;
	SaveFiles		= FALSE;
	SaveFails		= FALSE;
	InType			= IN_NONE;
	OutAction		= OUT_NONE;
	AddWmoHeader            = FALSE;
	AddLdmWrapper           = FALSE;
	Validate                = FALSE;
	CreateChecksum          = FALSE;
	MaxSentFiles		= DEFAULT_MAX_SAVE_FILES;
	SentFileDigits		= DEFAULT_SAVE_FILE_DIGITS;
	ParDir[0]		= '\0';

	strcpy (Loc, "dir");
	strcpy (PollFileSpec, DEFAULT_FILE_SPEC);
	strcpy (LogPathBase, PROD_LOG_PATH);
	strcpy (MessagePath, MESSAGE_LOG_PATH);

#ifdef LDM_SUPPORT
	FeedType		= DEFAULT_FEED_TYPE;
	strcpy (pqfName, DEF_LDM_QUEUE);
#endif

	while ((optchar = getopt(argc, argv, optstr)) != EOF) {
		switch (optchar) {
		case 'a':
			if ((val = atoi(optarg)) <= 0) {
				DiscardAge = 0;
			} else if (val < MIN_DISCARD_AGE) {
				DiscardAge = MIN_DISCARD_AGE;
				fprintf(stderr,
					"WARNING: Invalid discard age (%d) - setting to %d\n",
					val, MIN_DISCARD_AGE);
			} else {
				DiscardAge = val;
			}
			break;

		case 'c':
			CreateChecksum = TRUE;
			strncpy(str, optarg, DEF_STR_LEN);
			if ((HashOpt = getOptFromString(csOpts, raiseCase(str))) == -1) {
				fprintf(stderr,
					"Invalid input option: %s\n",
					optarg);
				usage(basename(argv[0]));
			}

			HashProgram = hashProgs[HashOpt];
			break;

		case 'D':
			if ((val = atoi(optarg)) < 0) {
				Verbosity = 0;
				fprintf(stderr,
					"WARNING: Invalid debug level (%d) - setting to %d\n",
					val, 0);
			} else if (val > V_MAX) {
				Verbosity = V_MAX;
				fprintf(stderr,
					"WARNING: Invalid debug level (%d) - setting to %d\n",
					val, V_MAX);
			} else {
				Verbosity = val;
			}
			break;

		case 'd':
			strncpy(SaveDir, optarg, MAX_PATH_LEN);
			stripTrailingChar(SaveDir, '/');
			break;

#ifdef LDM_SUPPORT
		case 'F':
			strncpy(str, optarg, DEF_STR_LEN);
			if ((val = getOptFromString(feedOpts, raiseCase(str))) == -1) {
				fprintf(stderr,
					"Invalid input option: %s\n",
					optarg);
				usage(basename(argv[0]));
			}
			FeedType = feedOpts[val].val;
			break;
#endif

		case 'f':
			strncpy(FailDir, optarg, MAX_PATH_LEN);
			stripTrailingChar(FailDir, '/');
			SaveFails = TRUE;
			break;

		case 'h':
			strncpy(ParDir, optarg, MAX_PATH_LEN);
			break;

		case 'i':
			strncpy(str, optarg, DEF_STR_LEN);
			if ((val = getOptFromString(inOpts, raiseCase(str))) == -1) {
				fprintf(stderr,
					"Invalid input option: %s\n",
					optarg);
				usage(basename(argv[0]));
			}
			InType = inOpts[val].val;
			if (InType == IN_ACQ_PIPE) {
				strcpy (Loc, "pipe");
			}
			break;

		case 'L':
			strncpy(LogPathBase, optarg, MAX_PATH_LEN);
			break;

		case 'l':
			AddLdmWrapper = TRUE;
			break;

		case 'M':
			strncpy(MessagePath, optarg, MAX_PATH_LEN);
			break;

		case 'n':
			if ((val = atoi(optarg)) <= 0) {
				PollInterval = 0;
			} else {
				PollInterval = val;
			}
			break;

		case 'o':
			strncpy(str, optarg, DEF_STR_LEN);
			if ((val = getOptFromString(outOpts, raiseCase(str))) == -1) {
				fprintf(stderr,
					"Invalid output option: %s\n",
					optarg);
				usage(basename(argv[0]));
			}
			OutAction = outOpts[val].val;

			switch (OutAction) {
#ifdef LDM_SUPPORT
			case OUT_LDM:
				AddLdmWrapper = TRUE;

				if (InType == IN_GOESR) {
					AddWmoHeader = TRUE;
				}
				break;
#endif
			case OUT_DISCARD:
				SaveFiles = FALSE;
				break;
			default:
				break;
			}
			break;

		case 'p':
			strncpy(InputSource, optarg, MAX_PATH_LEN);
			stripTrailingChar(InputSource, '/');
			break;

#ifdef LDM_SUPPORT
			case 'q':
			strncpy (pqfName, optarg, MAX_PATH_LEN);
			break;

#endif
		case 'Q':
			if ((val = atoi(optarg)) < 0) {
				val = 0;
				fprintf(stderr,
					"WARNING: Invalid maximum queue age (%d) - setting to %d\n",
					val, 0);
			}

			MaxQueueSize = val;
			break;

		case 's':
			strncpy(SentDir, optarg, MAX_PATH_LEN);
			stripTrailingChar(SentDir, '/');
			SaveFiles = TRUE;
			break;

		case 't':
			strncpy(PollFileSpec, optarg, MAX_FILENAME_LEN);
			break;

		case 'v':
			Validate = TRUE;
			break;

		case 'w':
			AddWmoHeader = TRUE;
			break;

		case 'x':
			SentFileDigits = strlen (optarg);
			if ((val = atoi(optarg)) > 0) {
				MaxSentFiles = val;
			}

			if (MaxSentFiles > 100000) {
				MaxSentFiles		= DEFAULT_MAX_SAVE_FILES;
				SentFileDigits		= DEFAULT_SAVE_FILE_DIGITS;
			}
			break;

		default:
			usage(basename(argv[0]));
			break;
		}
	}

	if (InType == IN_NONE) {
		fprintf (stderr,
			"Input method (-i) is required\n");
		usage (basename (argv[0]));
	}

	if (OutAction == OUT_NONE) {
		fprintf (stderr,
			"Output action (-o) is required\n");
		usage (basename (argv[0]));
	}

	if ((InType == IN_GOESR) && ((CreateChecksum && (ParDir[0] == '\0')) ||
				     (!CreateChecksum && (ParDir[0] != '\0')))) {
		fprintf (stderr,
			"With -iGOESR, both -c and -h must be provided, or neither can be\n");
		usage (basename (argv[0]));
	}

	if ((OutAction == OUT_FILE) && !strlen (SaveDir)) {
		fprintf (stderr,
			"-d <save dir> must be provided with -o FILE\n");
		usage (basename (argv[0]));
	}

	if (!strlen (InputSource)) {
		if (InType == IN_ACQ_PIPE) {
			logMsg (eLog, V_ALWAYS, S_FATAL,
				"(%s) - Pipe not provided - exiting",
				__FUNCTION__);
		} else {
			logMsg (eLog, V_ALWAYS, S_FATAL,
				"(%s) - Polling directory not provided - exiting",
				__FUNCTION__);
		}

		usage (basename (argv[0]));
	}
}


/* -----------------------------------------------------------------------------
 * Function Name
 *	initLogs
 *
 * Format
 *	int initLogs ()
 *
 * Arguments
 *	N/A
 *
 * Description
 *	Initializes the product and error logs.  If LDM support is compiled in,
 *	then the log facility used by LDM is also initialized.
 *
 * Return Values
 *	0	Success
 *	1	Some kind of error, probably fatal
 *
 * -------------------------------------------------------------------------- */

#define COMMON_OPTS	(O_FLUSH_AFTER_EACH | O_ARCHIVE | O_TIMESTAMP | O_KEEP_OPEN | O_ADD_NEWLINE)
#define PROD_LOG_OPTS	(COMMON_OPTS)
#define ERR_LOG_OPTS	(COMMON_OPTS | O_LOG_INIT | O_SHOW_SEVERITY)

static int initLogs () {

	char	logName[MAX_FILENAME_LEN+1] = {0};

	sprintf (logName, "%s.product.log", ProgName);
	pLog = logInitLogger ("Transaction Log", F_FILE, PROD_LOG_OPTS, V_ERROR,
			LogPathBase, logName, DEF_LOG_SIZE, LOG_BUFFER_SIZE);
	if (!pLog) {
		fprintf (stderr,
			"FATAL: %s - could not open transaction log\n",
			__FUNCTION__);
		return 1;
	}

	sprintf (logName, "%s.error.log", ProgName);
	eLog = logInitLogger ("Error Log", F_FILE, ERR_LOG_OPTS, Verbosity,
			MessagePath, logName, DEF_LOG_SIZE, LOG_BUFFER_SIZE);
	if (!eLog) {
		fprintf (stderr,
			"FATAL: %s - could not open transaction log\n",
			__FUNCTION__);
		return 1;
	}

#ifdef LDM_SUPPORT
	snprintf (logName, sizeof(logName)-1, "%s/ldm.log", MessagePath);
	log_set_destination(logName);
	log_set_level(LOG_LEVEL_NOTICE);
#endif

	return 0;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	atExitHandler
 *
 * Format
 *	void adExitHandler ()
 *
 * Arguments
 *	N/A
 *
 * Description
 *	Handler called when the 'exit()' system call is issued to terminate
 *	this program.  It shuts down the logging facilities, and 'free's up
 *	some dynamically allocated memory.
 *
 *	If LDM support is compiled in, the LDM product queue is closed and
 *	the MD5 context is freed up.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void atExitHandler () {

	logMsg (pLog, V_ALWAYS, S_STATUS,
		"Products processed: %'lld  Total Bytes Processed: %'lld",
                TotalProductsProcessed, TotalBytesProcessed);
        logShutdown ();

#ifdef LDM_SUPPORT

        free_MD5_CTX (md5ctxp);
        lpqClose (prodQueue);

#endif

	free (ProgName);
	free (ProdBuf);
	puts ("Done.");
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	sigsetexitflag
 * Format
 *	void sigsetexitflag (int signum)
 *
 * Arguments
 *	int signum
 *	Signal that triggered execution of this trap.
 *
 * Description
 *	Handler function for signals that should terminate the process when
 *	the current product has been processed.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void sigsetexitflag (int signum) {

	/* set global flag for termination	*/
	Done = TRUE;

	logMsg (eLog, V_INFO, S_STATUS,
		"Received signal %d, setting exit flag",
		signum);
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	siglogandcontinue
 *
 * Format
 *	void siglogandcontinue (int signum)
 *
 * Arguments
 *	int signum
 *	Signal that triggered execution of this trap.
 *
 * Description
 *	Handler function for signals that should be ignored.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void siglogandcontinue (int signum) {

	logMsg (eLog, V_INFO, S_STATUS,
		"Received signal %d, ignored",
		signum);
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	sigexitnow
 *
 * Format
 *	void sigexitnow (int signum)
 *
 * Arguments
 *	int signum
 *	Signal that triggered execution of this trap.
 *
 * Description
 *	Handler function for signals that should terminate the process immediately.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void sigexitnow (int signum) {

	logMsg (eLog, V_ALWAYS, S_STATUS,
		"Received signal %d, exit process immediately",
		signum);

	exit (0);
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	setupSignalHandlers
 *
 * Format
 *	void setupSignalHandlers ()
 *
 * Arguments
 *	N/A
 *
 * Description
 *	Set up initial signal handlers for the process.
 *
 * Return Values
 *
 * -------------------------------------------------------------------------- */

static void setupSigHandler () {
	struct sigaction act;
	static const char fname[40+1]="setupSigHandler";

	/* SIGUSR1 will exit after sending the current product	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = sigsetexitflag;
	act.sa_flags=0;
	if (sigaction (SIGUSR1, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=sigsetexitflag, %s\n",
			fname, SIGUSR1, strerror(errno));
	}

	/* SIGTERM will exit after sending the current product	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = sigsetexitflag;
	act.sa_flags=0;
	if (sigaction (SIGTERM, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=sigsetexitflag, %s\n",
			fname, SIGTERM, strerror(errno));
	}

	/* SIGHUP will exit the process	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = sigexitnow;
	act.sa_flags=0;
	if (sigaction (SIGHUP, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=sigexitnow, %s\n",
			fname, SIGHUP, strerror(errno));
	}

	/* SIGINT will be handled	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = sigsetexitflag;
	act.sa_flags=0;
	if (sigaction (SIGINT, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=siglogandcontinue, %s\n",
			fname, SIGCHLD, strerror(errno));
	}

	/* SIGPIPE will be ignored	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = siglogandcontinue;
	act.sa_flags=0;
	if (sigaction (SIGPIPE, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=siglogandcontinue, %s\n",
			fname, SIGCHLD, strerror(errno));
	}

	/* SIGALRM will be ignored	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = siglogandcontinue;
	act.sa_flags=0;
	if (sigaction (SIGALRM, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=siglogandcontinue, %s\n",
			fname, SIGALRM, strerror(errno));
	}

//	/* SIGIO will be handled	*/
//	sigemptyset (&act.sa_mask);
//	act.sa_handler = sighandleIO;
//	act.sa_flags=SA_SIGINFO;
//	if (sigaction (SIGIO, &act, 0) == -1) {
//		fprintf (stderr,
//			"%s: Sigaction FAIL sig=%d, act=sighandleIO, %s\n",
//			fname, SIGIO, strerror(errno));
//	}

	return;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	addFileToList
 *
 * Format
 *	int addFileToList (FILE_LIST *flist, char *fname, time_t ftime, off_t fsize)
 *
 * Arguments
 *	FILE_LIST	*flist
 *	Pointer to FILE_LIST structure that will hold information about matching files.
 *
 *	char		*fname
 *	Name of file to insert
 *
 *	time_t		ftime
 *	File last modification time
 *
 *	off_t		fsize
 *	Size of file in bytes
 *
 * Description
 * 	Adds a new FILE_NODE to the flist FILE_LIST.
 *
 * Return Values
 *	0	Success
 *	1	Failure
 *
 * -------------------------------------------------------------------------- */

int addFileToList (FILE_LIST *flist, char *fname, time_t ftime, off_t fsize) {

	logMsg (eLog, V_DEBUG, S_DEBUG,
		"(%s) Adding %s as item #%d in file list",
		__FUNCTION__, fname, flist->count);

	if (flist->count == 0) {
		if ((flist->fileNodes = malloc (sizeof (FILE_NODE))) == NULL) {
			logMsg (eLog, V_ERROR, S_ERROR,
				"(%s) - could not malloc memory for file node list",
				__FUNCTION__);
			return 1;
		}
	} else {
		if ((flist->fileNodes = realloc ((void *) flist->fileNodes, sizeof (FILE_NODE) * (flist->count + 1))) == NULL) {
			logMsg (eLog, V_ERROR, S_ERROR,
				"(%s) - could not realloc memory for file node list",
				__FUNCTION__);
			return 1;
		}
	}

	if ((flist->fileNodes[flist->count].fptr = strdup (fname)) == NULL) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - could not malloc memory for file name",
				__FUNCTION__);
		return 1;
	}

	flist->fileNodes[flist->count].mtime = ftime;
	flist->fileNodes[flist->count].fsize = fsize;
	flist->count++;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	readAcqPipe
 *
 * Format
 *	int readAcqPipe (int pipefd, FILE_LIST *fileList)
 *
 * Arguments
 *	int		pipefd
 *	File descriptor for already-open pipe
 *
 *	FILE_LIST	*fileList
 *	If successful, will contain a FILE_LIST element for a single file.
 *
 * Description
 *	Read a PIPE_PROD_NAME_HDR structure from the pipe.
 *
 * Return Values
 *	0	success
 *	1	failure
 *
 * -------------------------------------------------------------------------- */

#define RETRIES		3

int readAcqPipe (int pipefd, FILE_LIST *fileList) {
	int				rtn_value;	/* return value from read */
	int				request_size;
	struct pipe_prod_name_hdr	prod_entry;
	char				*p_readbuff;	/* ptr to read buffer */
	struct stat			stat_buf;
	int				err;

	request_size = sizeof(PIPE_PROD_NAME_HDR);
	p_readbuff = (char *) &prod_entry;

	if ((rtn_value = read (pipefd, p_readbuff, request_size)) < 0) {
		err = errno;
		switch (err) {
			case EINTR:
				/* Interrupt received. As long as the process isn't exiting,
				 * this function will be called again immediately.
				 */
				break;

			case EBADF:
				logMsg(eLog, V_ERROR, S_ERROR,
					"%s ERROR bad file descriptor %d",
					__FUNCTION__, pipefd);

				break;

			default:
				break;
		}

		return 1;
	}

	if (rtn_value == 0) {
		/* Assume this means pipe writer (acqserver) has exited.  When this happens,
		 * the read does not block.  Just sleep to keep this process from free running */
		sleep(1);
		return 1;
	}

	if (rtn_value != request_size) {
		logMsg(eLog, V_ERROR, S_ERROR,
			"(%s) %d vs %d bytes fd=%d %s retry_cnt=%d\n",
			__FUNCTION__, rtn_value, request_size, pipefd,
			InputSource);
		return 1;
	}

	if (rtn_value == request_size) { /* Read an entire product */
		logMsg(eLog, V_DEBUG, S_DEBUG,
			"(%s) Read entry for %s",
			__FUNCTION__, prod_entry.pipe_prod_filename);

		if (stat(prod_entry.pipe_prod_filename, &stat_buf) < 0) {
			/* Perhaps we should remove the file ??? */
			logMsg(eLog, V_ERROR, S_WARNING,
				"(%s) - FAIL stat file <%s> errno=%d %s",
				__FUNCTION__, prod_entry.pipe_prod_filename, errno, strerror(errno));
			return 1;
		}

		if (addFileToList (fileList, prod_entry.pipe_prod_filename,
				stat_buf.st_mtime, stat_buf.st_size) != 0) {
			logMsg(eLog, V_ERROR, S_ERROR,
					"(%s) Could not add %s to file list",
					__FUNCTION__,
					prod_entry.pipe_prod_filename);
			return 1;
		}
	}

	return 0;

}

/* -----------------------------------------------------------------------------
 * Function Name
 *	findFilesLike
 *
 * Format
 *	int findFilesLike (char *dir, char *file_spec, FILE_LIST *flist)
 *
 * Arguments
 *	char		*dir
 *	Absolute directory path to scan.
 *
 *	char		*file_spec
 *	File specification to look for in dir.
 *
 *	FILE_LIST	*flist
 *	Pointer to structure containing array of files found in dir that match
 *	file_spec.
 *
 * Description
 *	Uses the glob system call to search for files in dir.  Matching files
 *	are returned in a dynamically allocated array described by flist.  If
 *	file aging is enabled and not set to 0, files older than the provided
 *	age are 'aged-out' and discarded.
 *
 * Return Values
 *	-1	Error
 *	0	Success, all files in the directory were put into flist
 *	1	Success, but there were more files in the directory to process
 *
 * -------------------------------------------------------------------------- */

int findFilesLike (char *dir, char *file_spec, FILE_LIST *flist) {

	glob_t			gbuf;
	int			count		= 0;
	char			*fp;
	char			cdir[MAX_PATH_LEN+1];
	struct stat		stat_buf;
	int			maxProdsToQ;
	int			retval		= 0;
	int			globstat;

	if (getcwd (cdir, MAX_PATH_LEN) == NULL) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - could not get current directory errno=%d errstring=%s",
			__FUNCTION__, errno, strerror (errno));
		return -1;
	}

	if (changeDirectory (dir, TRUE) != 0) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - could not change directory to %s", __FUNCTION__, dir);
		return -1;
	}

	flist->count = 0;

	switch (globstat = glob (file_spec, GLOB_NOSORT, NULL, &gbuf)) {
		case 0:
			logMsg (eLog, V_DEBUG, S_DEBUG,
				"(%s) - found %d files matching \"%s\"",
				__FUNCTION__, gbuf.gl_pathc, file_spec);

			maxProdsToQ = (MaxQueueSize == 0) ? gbuf.gl_pathc : MIN (gbuf.gl_pathc, MaxQueueSize);

			for (count = 0; count < maxProdsToQ; count++) {

				fp = gbuf.gl_pathv[count];

				logMsg (eLog, V_DEBUG, S_DEBUG,
					"(%s) - found file fptr = %s, count = %d",
					__FUNCTION__, fp, count);

				if (stat (fp, &stat_buf) < 0) {
					/* Perhaps we should remove the file ??? */
					logMsg (eLog, V_ERROR, S_WARNING,
						"(%s) - FAIL stat file <%s> errno=%d %s",
						__FUNCTION__, fp, errno, strerror(errno));
					continue;
				}

				if (!(stat_buf.st_mode & (S_IFREG | S_IFLNK))) {
					/** not a regular file or link, skip it */
					logMsg (eLog, V_INFO, S_STATUS,
						"(%s) - skipping matching file %s with mode = %d",
						__FUNCTION__, fp, (int) stat_buf.st_mode);
					continue;
				}

				if (stat_buf.st_size < MIN_PROD_SIZE_READ) {
					/** small size and just created/modified, skip it (this time) */
					continue;
				}

				if (!(stat_buf.st_mode & (S_IRUSR | S_IRGRP | S_IROTH))) {
					/* No one has read permission, skip it */
					/* May want to use access() to check if can read file */
					/* open will fail later anyway */
					continue;
				}

				if (Verbosity >= V_DEBUG) {
					char	*cptr = ctime (&stat_buf.st_mtime);

					cptr[strlen (cptr)-1] = '\0'; /* Get rid of the \n terminator ctime adds */

					logMsg (eLog, V_DEBUG, S_DEBUG,
						"(%s) - #%d %s%s%s%s%s%s%s%s%s    %s    %s",
						__FUNCTION__, flist->count,
						(stat_buf.st_mode & S_IRUSR)?"r":"-",
						(stat_buf.st_mode & S_IWUSR)?"w":"-",
						(stat_buf.st_mode & S_IXUSR)?"x":"-",
						(stat_buf.st_mode & S_IRGRP)?"r":"-",
						(stat_buf.st_mode & S_IWGRP)?"w":"-",
						(stat_buf.st_mode & S_IXGRP)?"x":"-",
						(stat_buf.st_mode & S_IROTH)?"r":"-",
						(stat_buf.st_mode & S_IWOTH)?"w":"-",
						(stat_buf.st_mode & S_IXOTH)?"x":"-",
						cptr, fp);
				}

				if (addFileToList (flist, fp, stat_buf.st_mtime, stat_buf.st_size) != 0) {
					return -1;
				}
			}

			logMsg (eLog, V_DEBUG, S_DEBUG,
				"(%s) - queued %d out of %d products, MaxQueueSize = %d",
				__FUNCTION__, count, gbuf.gl_pathc, MaxQueueSize);

			retval = (count < gbuf.gl_pathc) ? 1 : 0;	/* if count < gbuf.gl_pathc, then all files
									   in the directory were queued */
			break;

		case GLOB_NOMATCH:
			logMsg (eLog, V_DEBUG, S_DEBUG,
				"(%s) - glob returned no matches found for %s in %s",
				__FUNCTION__, file_spec, dir);
			retval = 0;
			break;

		default:
			logMsg (eLog, V_ERROR, S_ERROR,
				"(%s) - glob returned unexpected value %d",
				__FUNCTION__, globstat);
			retval = -1;
			break;
	}

	changeDirectory (cdir, FALSE);

	globfree (&gbuf);
	return (retval);
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logFileList
 *
 * Format
 *	void logFileList (FILE_LIST *fl)
 *
 * Arguments
 *	FILE_LIST	*fl
 *	Pointer to FILE_LIST
 *
 * Description
 *	Debugging function for writing a FILE_LIST to the error log.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

void logFileList (FILE_LIST *fl) {

	int	i;

	if ((fl == NULL) || (fl->fileNodes == NULL)) {
		return;
	}

	if (fl->count == 0) {
		logMsg (eLog, V_DEBUG, S_DEBUG, "No Files Found");
	} else {
		logMsg (eLog, V_DEBUG, S_DEBUG, "Files Found:");
		for (i = 0; i < fl->count; i++) {
			char	*cptr = ctime (&fl->fileNodes[i].mtime);

			cptr[strlen(cptr)-1] = '\0';	/* Get rid of the \n terminator ctime adds */
			logMsg (eLog, V_DEBUG, S_DEBUG,
				"(%s) - f(%s), Mod Time: %s, Size: %d",
				__FUNCTION__, fl->fileNodes[i].fptr, cptr, fl->fileNodes[i].fsize);
		}
	}
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	freeFileList
 *
 * Format
 *	void freeFileList (FILE_LIST *fl)
 *
 * Arguments
 *	FILE_LIST	*fl
 *	Pointer to FILE_LIST
 *
 * Description
 *	This function will free all memory in a FILE_LIST.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

void freeFileList (FILE_LIST *fl) {

	int	i;

	if (fl == NULL) {
		return;
	}

	for (i = 0; i < fl->count; i++) {
		logMsg (eLog, V_DEBUG, S_DEBUG,
			"(%s) - Freeing file node: %s",
			__FUNCTION__, fl->fileNodes[i].fptr);
		free ((void *) fl->fileNodes[i].fptr);
	}

	if (fl->count > 0) {
		free ((void *) fl->fileNodes);
	}

	fl->count = 0;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	compareFileNodeTimes
 *
 * Format
 *	int compareFileNodeTimes (const void *fv1, const void *fv2)
 *
 * Arguments
 *	void	*fv1
 *	Pointer to a FILE_NODE
 *
 *	void	*fv2
 *	Pointer to another FILE_NODE
 *
 * Description
 *	'compar' function used by qsort to sort an array of FILE_NODEs
 *
 * Return Values
 *	-1	fv1 < fv2
 *	0	fv1 == fv2
 *	1	fv1 > fv2
 *
 * -------------------------------------------------------------------------- */

int compareFileNodeTimes (const void *fv1, const void *fv2) {

	if ((((FILE_NODE *) fv1)->mtime) < (((FILE_NODE *) fv2)->mtime)) {
		return -1;
	} else if ((((FILE_NODE *) fv1)->mtime) > (((FILE_NODE *) fv2)->mtime)) {
		return 1;
	} else {
		return 0;
	}
}

/* -----------------------------------------------------------------------------
 * Function Name
 * 	getWmoOffset
 *
 * Format
 * 	int getWmoOffset (char *buf, size_t buflen, size_t *p_wmolen)
 *
 * Arguments
 * 	Type			Name		I/O		Description
 * 	char *			buf		I		buffer to parse for WMO
 * 	size_t			buflen		I		length of data in buffer
 * 	size_t *		p_wmolen	O		length of wmo header
 *
 * Description
 * 	Parse the wmo heading from buffer and load the appropriate prod
 * 	info fields.  The following regular expressions will satisfy this
 * 	parser.  Note this parser is not case sensative.
 * 	The WMO format is supposed to be...
 * 		TTAAii CCCC DDHHMM[ BBB]\r\r\n
 * 		[NNNXXX\r\r\n]
 *
 * 	This parser is generous with the ii portion of the WMO and all spaces
 * 	are optional.  The TTAAII, CCCC, and DDHHMM portions of the WMO are
 * 	required followed by at least 1 <cr> or <lf> with no other unparsed
 * 	intervening characters. The following quasi-grammar describe what
 * 	is matched.
 *
 * 	WMO = "TTAAII CCCC DDHHMM [BBB] CRCRLF [NNNXXX CRCRLF]"
 *
 * 	TTAAII = "[A-Z]{4}[0-9]{0,1,2}" | "[A-Z]{4} [0-9]" | "[A-Z]{3}[0-9]{3} "
 * 	CCCC = "[A-Z]{4}"
 * 	DDHHMM = "[ 0-9][0-9]{3,5}"
 * 	BBB = "[A-Z0-9]{0-3}"
 * 	CRCRLF = "[\r\n]+"
 * 	NNNXXX = "[A-Z0-9]{0,4-6}"
 *
 * 	Most of the WMO's that fail to be parsed seem to be missing the ii
 * 	altogether or missing part or all of the timestamp (DDHHMM)
 *
 * Returns
 * 	offset to WMO from buf[0]
 * 	-1: otherwise
 *
 * -------------------------------------------------------------------------- */

#define WMO_TTAAII_LEN		6
#define WMO_CCCC_LEN		4
#define WMO_DDHHMM_LEN		6
#define WMO_DDHH_LEN		4
#define WMO_BBB_LEN			3

#define WMO_T1	0
#define WMO_T2	1
#define WMO_A1	2
#define WMO_A2	3
#define WMO_I1	4
#define WMO_I2	5

int getWmoOffset(char *buf, size_t buflen, size_t *p_wmolen) {
	char *p_wmo;
	int i_bbb;
	int spaces;
	int	ttaaii_found = 0;
	int	ddhhmm_found = 0;
	int	crcrlf_found = 0;
	int	bbb_found = 0;
	int wmo_offset = -1;

	*p_wmolen = 0;

	for (p_wmo = buf; p_wmo + WMO_I2 + 1 < buf + buflen; p_wmo++) {
		if (isalpha(p_wmo[WMO_T1]) && isalpha(p_wmo[WMO_T2])
				&& isalpha(p_wmo[WMO_A1]) && isalpha(p_wmo[WMO_A2])) {
			/* 'TTAAII ' */
			if (isdigit(p_wmo[WMO_I1]) && isdigit(p_wmo[WMO_I2])
					&& (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
				ttaaii_found = 1;
				wmo_offset = p_wmo - buf;
				p_wmo += WMO_I2 + 1;
				break;
			/* 'TTAAI C' */
			} else if (isdigit(p_wmo[WMO_I1]) && isspace(p_wmo[WMO_I2])
					&& (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
				ttaaii_found = 1;
				wmo_offset = p_wmo - buf;
				p_wmo += WMO_I1 + 1;
				break;
			/* 'TTAA I ' */
			} else if (isspace(p_wmo[WMO_I1]) && isdigit(p_wmo[WMO_I2])
					&& (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
				ttaaii_found = 1;
				wmo_offset = p_wmo - buf;
				p_wmo += WMO_I2 + 1;
				break;
			/* 'TTAAIC' */
			} else if (isdigit(p_wmo[WMO_I1]) && isalpha(p_wmo[WMO_I2])) {
				ttaaii_found = 1;
				wmo_offset = p_wmo - buf;
				p_wmo += WMO_I1 + 1;
				break;
			}
		} else if (isalpha(p_wmo[WMO_T1]) && isalpha(p_wmo[WMO_T2])
				&& isalpha(p_wmo[WMO_A1]) && isdigit(p_wmo[WMO_A2])) {
			/* 'TTA#II ' */
			if (isdigit(p_wmo[WMO_I1]) && isdigit(p_wmo[WMO_I2])
					&& (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
				ttaaii_found = 1;
				wmo_offset = p_wmo - buf;
				p_wmo += WMO_I2 + 1;
				break;
			}
		} else if (!strncmp(p_wmo, "\r\r\n", 3)) {
			/* got to EOH with no TTAAII found, check TTAA case below */
			break;
		}
	}

	if (!ttaaii_found) {
		/* look for TTAA CCCC DDHHMM */
		for (p_wmo = buf; p_wmo + 9 < buf + buflen; p_wmo++) {
			if (isalpha(p_wmo[WMO_T1]) && isalpha(p_wmo[WMO_T2])
					&& isalpha(p_wmo[WMO_A1]) && isalpha(p_wmo[WMO_A2])
					&& isspace(p_wmo[WMO_A2+1]) && isalpha(p_wmo[WMO_A2+2])
					&& isalpha(p_wmo[WMO_A2+3]) && isalpha(p_wmo[WMO_A2+4])
					&& isalpha(p_wmo[WMO_A2+5]) && isspace(p_wmo[WMO_A2+6])) {
				ttaaii_found = 1;
				wmo_offset = p_wmo - buf;
				p_wmo += WMO_A2 + 1;
				break;
			} else if (!strncmp(p_wmo, "\r\r\n", 3)) {
				/* got to EOH with no TTAA found, give up */
				return -1;
			}
		}
	}

	/* skip spaces if present */
	while (isspace(*p_wmo) && p_wmo < buf + buflen) {
		p_wmo++;
	}

	if (p_wmo + WMO_CCCC_LEN > buf + buflen) {
		return -1;
	} else if (isalpha(*p_wmo) && isalnum(*(p_wmo+1))
			&& isalpha(*(p_wmo+2)) && isalnum(*(p_wmo+3))) {
		p_wmo += WMO_CCCC_LEN;
	} else {
		return -1;
	}

	/* skip spaces if present */
	spaces = 0;
	while (isspace(*p_wmo) && p_wmo < buf + buflen) {
		p_wmo++;
		spaces++;
	}

	/* case1: check for 6 digit date-time group */
	if (p_wmo + 6 <= buf + buflen) {
		if (isdigit(*p_wmo) && isdigit(*(p_wmo+1))
				&& isdigit(*(p_wmo+2)) && isdigit(*(p_wmo+3))
				&& isdigit(*(p_wmo+4)) && isdigit(*(p_wmo+5))) {
			ddhhmm_found = 1;
			p_wmo += 6;
		}
	}

	/* case2: check for 4 digit date-time group */
	if (!ddhhmm_found && p_wmo + 5 <= buf + buflen) {
		if (isdigit(*p_wmo) && isdigit(*(p_wmo+1))
				&& isdigit(*(p_wmo+2)) && isdigit(*(p_wmo+3))
				&& isspace(*(p_wmo+4))) {
			ddhhmm_found = 1;
			p_wmo += 4;
		}
	}

	/* case3: check for leading 0 in date-time group being a space */
	if (!ddhhmm_found && p_wmo + 5 <= buf + buflen) {
		if (spaces > 1 && isdigit(*p_wmo) && isdigit(*(p_wmo+1))
				&& isdigit(*(p_wmo+2)) && isdigit(*(p_wmo+3))
				&& isdigit(*(p_wmo+4))) {
			ddhhmm_found = 1;
			p_wmo += 5;
		} else {
			return -1;
		}
	}

	/* skip potential trailing 'Z' on dddhhmm */
	if (*p_wmo == 'Z') {
		p_wmo++;
	}

	/* Everything past this point is gravy, we'll return the current
	   length if we don't get the expected [bbb] crcrlf
 	 */

	/* check if we have a <cr> and/or <lf>, parse bbb if present */
	while (p_wmo < buf + buflen) {
		if ((*p_wmo == '\r') || (*p_wmo == '\n')) {
			crcrlf_found++;
			p_wmo++;
			if (crcrlf_found == 3) {
				/* assume this is our complete cr-cr-lf */
				break;
			}
		} else if (crcrlf_found) {
			/* pre-mature end of crcrlf */
			p_wmo--;
			break;
		} else if (isalpha(*p_wmo)) {
			if (bbb_found) {
				/* already have a bbb, give up here */
				return wmo_offset;
			}
			for (i_bbb = 1;
					p_wmo + i_bbb < buf + buflen && i_bbb < WMO_BBB_LEN;
						i_bbb++) {
				if (!isalpha(p_wmo[i_bbb])) {
					break; /* out of bbb parse loop */
				}
			}
			if (p_wmo + i_bbb < buf + buflen && isspace(p_wmo[i_bbb])) {
				bbb_found = 1;
				p_wmo += i_bbb;
			} else {
				/* bbb is too long or maybe not a bbb at all, give up */
				return wmo_offset;
			}
		} else if (isspace(*p_wmo)) {
			p_wmo++;
		} else {
			/* give up */
			return wmo_offset;
		}
	}

	/* update length to include bbb and crcrlf */
	*p_wmolen = p_wmo - buf - wmo_offset;

	return wmo_offset;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	getWmoFromFile
 *
 * Format
 *	int getWmoFromFile (char *fname, char *wmo)
 *
 * Arguments
 *	char	*fname
 *	String containing file name.
 *
 *	void	*wmo
 *	Pointer to string that will contain wmo header, if found.
 *
 * Description
 *	Find and return the WMO header from a product file.
 *
 * Return Values
 *	0	success
 *	1	failure
 *
 * -------------------------------------------------------------------------- */
#define MAX_PRODID_LEN		32
#define SIZE_WMO		18

int getWmoFromFile (char *fname, char *wmo) {

	int	wmo_offset;
	size_t	wmo_len;
	FILE	*fp;
	size_t	bufsize;
	char	buf[DEF_STR_LEN+1];

	if ((fp = fopen (fname, "r")) == NULL) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Could not open product file %s",
			__FUNCTION__, fname);
		return 1;
	}

	if ((bufsize = fread (buf, 1, DEF_STR_LEN, fp)) > 0) {
		if ((wmo_offset = getWmoOffset (buf, bufsize, &wmo_len)) >= 0) {
			memset (wmo, '\0', SIZE_WMO+1);
			strncpy (wmo, buf+wmo_offset, MIN (wmo_len, SIZE_WMO));
			wmo[MIN (wmo_len, SIZE_WMO)] = '\0';
			fclose (fp);
			return 0;
		}
	}

	fclose (fp);
	return 1;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	makeAgeStr
 *
 * Format
 *	void makeAgeStr (time_t mtime, char *buf)
 *
 * Arguments
 *	time_t	mtime
 *	Absolute time value.
 *
 *	char	*buf
 *	Pointer to string that will contain an age string for logging purposes.
 *
 * Description
 *	Create a message age string for logging
 *
 * Return Values
 * 	nothing
 *
 * -------------------------------------------------------------------------- */

void makeAgeStr (time_t mtime, char *buf) {

	int age = (int) (time (NULL) - mtime);

	if (age > 0) {
		sprintf (buf, " +%ds", age);
	} else {
		buf[0] = '\0';
	}
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	getWmoId
 *
 * Format
 *	int getWmoId (char *fname, char *wmo)
 *
 * Arguments
 *	char	*fname
 *	String containing file name.
 *
 *	char	*wmo
 *	Pointer to string that will contain wmo header, if found.
 *
 * Description
 *	Find and return the WMO header from a product file based on the type of
 *	product (NDE or GOESR) being processed.  For GOESR products, the WMO header
 *	will be constructed based on the file name.
 *
 * Return Values
 *	0	success
 *	1	failure
 *
 * -------------------------------------------------------------------------- */

int getWmoId (FILE_NODE *fnode, char *wmo) {

	int	rstat	= 0;
	char	tmpbuf[40];
	char	fullName[MAX_FILENAME_LEN+1] = {0};

	makeAgeStr (fnode->mtime, tmpbuf);

	switch (InType) {
		case IN_GOESR:
			/* Try to build a WMO header from the file name */
			if (goesrCmiFile2Wmo (fnode->fptr, wmo)) {
				logMsg (pLog, V_ALWAYS, S_STATUS,
					"END/ERROR_DISCARD WMO[] #%d bytes(%d) f(%s) Invalid file name%s",
					-1, fnode->fsize, fnode->fptr, tmpbuf);
				logMsg (eLog, V_ERROR, S_WARNING,
					"(%s) - Invalid file name %s found",
					__FUNCTION__, fnode->fptr);
				rstat = 1;
			}
			break;

		case IN_ACQ_PIPE:
			/* Full file name was read from pipe */
			if (getWmoFromFile (fnode->fptr, wmo)) {
				logMsg (pLog, V_ALWAYS, S_STATUS,
					"END/ERROR_DISCARD WMO[] #%d bytes(%d) f(%s) WMO header not found%s",
					-1, fnode->fsize, fnode->fptr, tmpbuf);
				logMsg (eLog, V_ERROR, S_WARNING,
					"(%s) - Could not find WMO header in %s",
					__FUNCTION__, fnode->fptr);
				rstat = 1;
			}
			break;

		case IN_NDE:
		case IN_PDA:
		case IN_POLL:
			/* Must create full file name */
			snprintf (fullName, sizeof(fullName)-1, "%s/%s", InputSource, fnode->fptr);
			if (getWmoFromFile (fullName, wmo)) {
				logMsg (pLog, V_ALWAYS, S_STATUS,
					"END/ERROR_DISCARD WMO[] #%d bytes(%d) f(%s) WMO header not found%s",
					-1, fnode->fsize, fnode->fptr, tmpbuf);
				logMsg (eLog, V_ERROR, S_WARNING,
					"(%s) - Could not find WMO header in %s",
					__FUNCTION__, fnode->fptr);
				rstat = 1;
			}
			break;

		default:
			logMsg (eLog, V_ERROR, S_ERROR,
				"(%s) - Unknown InType %d - discarding file %s",
				__FUNCTION__, InType, fnode->fptr);
			rstat = 1;
			break;
	}

	return rstat;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	processProducts
 *
 * Format
 *	int processProducts (FILE_LIST *fileList)
 *
 * Arguments
 *	FILE_LIST	*fileList
 *	Pointer to FILE_LIST structure containing list of files to process.
 *
 * Description
 *	Generates a WMO header for each product file from its file name and
 *	outputs it as determined by the -o option specified on the command line.
 *	Products are discarded, saved to disk, or written to an LDM product
 *	queue.
 *
 * Return Values
 *	Number of files processed.
 *
 * -------------------------------------------------------------------------- */

int processProducts (FILE_LIST *fileList) {

        int             i;
	int		rstat;
	FILE_NODE	*flist = fileList->fileNodes;
	char		wmo_id[MAX_PRODID_LEN+1];
	int		processed;
	char		tmpbuf[40];
	int		prodSize;
	char		*p;
	int		fd;
	int		age;
	ssize_t		readSize = 0;
	ssize_t		writeSize;
	size_t		finalSize;
	char		hashFileName[MAX_FILENAME_LEN+1] = {0};
	char		*pFileName;
	off_t		pFileSize;
	time_t		pFileTime;
	char		prodFullName[MAX_FILENAME_LEN+1] = {0};
	char		outFilePath[MAX_FILENAME_LEN+1] = {0};
	struct stat	stat_buf;

#ifdef LDM_SUPPORT
	product		ldmProd;
#endif

	for (i = 0, processed = 0; i < fileList->count; i++) {
		pFileName = flist[i].fptr;
		pFileSize = flist[i].fsize;
		pFileTime = flist[i].mtime;
		age = (int) (time (NULL) - pFileTime);

		logMsg (eLog, V_DEBUG, S_DEBUG,
			"(%s) - processing file %s (%d)",
			__FUNCTION__, pFileName, pFileSize);

		switch (InType) {
			case IN_NDE:
			case IN_PDA:	/* File names will be for checksum files here, not product files */
				snprintf (hashFileName, sizeof(hashFileName)-1, "%s/%s", InputSource, pFileName);
				/* Check age and discard hash file if older than maximum allowable ingest age */
				if (DiscardAge && (age > DiscardAge)) {
					if (unlink (hashFileName)) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - could not delete %s errno = %d errstr = %s",
							__FUNCTION__, pFileName, errno, strerror (errno));
					} else {
						logMsg (pLog, V_ALWAYS, S_STATUS,
							"END/AGE_DISCARD WMO[%s] #%d bytes(%d) f(%s) +%ds",
							wmo_id, -1, pFileSize, pFileName, age);

						logMsg (eLog, V_DEBUG, S_DEBUG,
							"(%s) - file %s deleted (age %d > discard age %d)",
							__FUNCTION__, pFileName, age, DiscardAge);
					}
				}

				strncpy (prodFullName, hashFileName, MAX_FILENAME_LEN);	/* Copy hash file name */
				removeExtension (prodFullName);		/* Now remove the extension to get the product file name */
				removeExtension (pFileName);		/* Remove hash extension from globbed file name */
				if (stat (prodFullName, &stat_buf) < 0) {
					logMsg (eLog, V_ERROR, S_ERROR,
						"Product file %s not found",
						prodFullName);
					if (unlink (hashFileName)) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - could not delete %s errno = %d errstr = %s",
							__FUNCTION__, hashFileName, errno, strerror (errno));
					} else {
						logMsg (eLog, V_DEBUG, S_DEBUG,
							"(%s) - file %s not found, matching hash file deleted",
							__FUNCTION__, pFileName);
					}

					continue;
				} else {
					pFileTime = stat_buf.st_mtime;
					pFileSize = stat_buf.st_size;
					age = (int) (time (NULL) - pFileTime);
				}

				if (DiscardAge && (age > DiscardAge)) {
					if (unlink (prodFullName)) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - could not delete %s errno = %d errstr = %s",
							__FUNCTION__, pFileName, errno, strerror (errno));
					} else {
						logMsg (pLog, V_ALWAYS, S_STATUS,
							"END/AGE_DISCARD WMO[%s] #%d bytes(%d) f(%s) +%ds",
							wmo_id, -1, pFileSize, pFileName, age);

						logMsg (eLog, V_DEBUG, S_DEBUG,
							"(%s) - file %s deleted (age %d > discard age %d)",
							__FUNCTION__, pFileName, age, DiscardAge);
					}

					continue;
				}

				if (CreateChecksum) {
					FILE	*hashFile;
					FILE	*hashPipe;
					char	hashCode[MAX_HASH_LEN+1];
					char	fileHashCode[MAX_HASH_LEN+1];
					char	fName[MAX_PATH_LEN+1];
					char	command[MAX_PATH_LEN+1];

					if ((hashFile = fopen (hashFileName, "r")) == NULL) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - Error %d while opening hash file %s",
							__FUNCTION__, errno, hashFileName);
					} else {
						/* Read the hash code and file name from the file */
						if (fscanf (hashFile, SCAN_HASH_CODE " " SCAN_PATHNAME, hashCode, fName)
						        != 2) {
							logMsg (eLog, V_ERROR, S_ERROR,
								"(%s) - Error %d calling fscanf for hash code in %s",
								__FUNCTION__, errno, hashFileName);
						} else {

							sprintf (command, "%s %s", HashProgram, prodFullName);
							hashPipe = popen (command, "r");
							if (fscanf (hashPipe, SCAN_HASH_CODE " " SCAN_PATHNAME, fileHashCode,
							        fName) != 2) {
								logMsg (eLog, V_ERROR, S_ERROR,
									"(%s) - Error %d calling fscanf while reading pipe",
									__FUNCTION__, errno);
							} else {
								if (strcmp (hashCode, fileHashCode) == 0) {
									logMsg (pLog, V_DEBUG, S_STATUS,
										"INFO Hash code OK %s",
										pFileName);
								} else {
									logMsg (pLog, V_ERROR, S_ERROR,
										"INFO Hash code FAIL %s",
										pFileName);
								}
							}
							pclose (hashPipe);
						}

						fclose (hashFile);
					}
				}

				/* Delete the hash code file */
				if (unlink (hashFileName)) {
					logMsg (eLog, V_ERROR, S_ERROR,
						"(%s) - could not delete %s errno = %d errstr = %s",
						__FUNCTION__, hashFileName, errno, strerror (errno));
				}

				break;

			case IN_ACQ_PIPE:
				strncpy (prodFullName, flist[i].fptr, MAX_FILENAME_LEN);
				break;

			case IN_GOESR:	/* Files will be product files */
			case IN_POLL:
			default:
				snprintf (prodFullName, sizeof(prodFullName), "%s/%s", InputSource, pFileName);

				if (CreateChecksum) {	/* This is only valid for InType==GOESR */
					/* Can't verify hashcode for GOES-R products since the file containing them (PAR file)
					 * won't exist yet.  Create a file containing the hash code that must be processed later
					 * by an external program.
					 */
					char	command[MAX_PATH_LEN+1] = {0};

					snprintf (hashFileName, sizeof(hashFileName)-1, "%s/%s", ParDir, pFileName);
					snprintf (command, sizeof(command)-1, "%s %s > %s.hash", HashProgram,
					        prodFullName, hashFileName);
					system (command);
				}

				/* Check age and Discard if older than maximum allowable ingest age - do this after so
				 * the verification with the PAR can still occur */
				if (DiscardAge && (age > DiscardAge)) {
					if (unlink (prodFullName)) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - could not delete %s errno = %d errstr = %s",
							__FUNCTION__, pFileName, errno, strerror (errno));
					} else {
						logMsg (pLog, V_ALWAYS, S_STATUS,
							"END/AGE_DISCARD WMO[%s] #%d bytes(%d) f(%s) +%ds",
								wmo_id, -1, pFileSize, pFileName, age);

						logMsg (eLog, V_DEBUG, S_DEBUG,
							"(%s) - file %s deleted (age %d > discard age %d)",
							__FUNCTION__, pFileName, age, DiscardAge);
					}

					continue;
				}
				break;
		}

		if (getWmoId (&flist[i], wmo_id)) {
			if (unlink (prodFullName)) {
				logMsg (eLog, V_ERROR, S_ERROR,
					"(%s) - Could not delete %s errno = %d errstr = %s",
					__FUNCTION__, pFileName, errno, strerror (errno));
			} else {
				logMsg (eLog, V_DEBUG, S_DEBUG,
					"(%s) - Failed file %s deleted",
					__FUNCTION__, prodFullName);

				if (SaveFails) {
					/* Create empty file of same name in fail directory */
					snprintf (outFilePath, sizeof(outFilePath)-1, "%s/%s", FailDir, pFileName);
					if ((fd = open (outFilePath, (O_WRONLY | O_CREAT), (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH))) == -1) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - Error (%d) \"%s\" while creating %s",
							__FUNCTION__, errno, strerror (errno), outFilePath);
					} else if (close (fd)) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - Error (%d) \"%s\" while closing %s",
							__FUNCTION__, errno, strerror (errno), outFilePath);
					} else {
						logMsg (eLog, V_DEBUG, S_DEBUG,
							"(%s) - Empty file %s created",
							__FUNCTION__, outFilePath);
					}
				}
			}

			continue;
		}

		prodSize = pFileSize;
#ifdef LDM_SUPPORT
		if ((OutAction == OUT_LDM) || (((OutAction == OUT_FILE) && AddWmoHeader) || SaveFiles)) {
#else
		if (((OutAction == OUT_FILE) && AddWmoHeader) || SaveFiles) {
#endif
			/* No need to open the file if we DISCARDing it, or if we're simply moving it to another directory */
			if (AddWmoHeader) {
				prodSize += strlen (wmo_id) + SIZE_WMO_TERM;
			}

			if (AddLdmWrapper) {
				prodSize += SIZE_SBN_HDR + SIZE_SBN_TLR;
			}

			if (ProdBuf == NULL) {
				if ((ProdBuf = malloc (prodSize)) == NULL) {
					logMsg (eLog, V_ERROR, S_ERROR,
						"(%s) - Could not malloc %d bytes for prodBuf",
						__FUNCTION__, prodSize);
					return processed;
				}

				ProdBufSize = prodSize;
			} else {
				if (prodSize > ProdBufSize) {
					if ((ProdBuf = realloc (ProdBuf, prodSize)) == NULL) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - Could not realloc %d bytes for prodBuf",
							__FUNCTION__, prodSize);
						return processed;
					}

					ProdBufSize = prodSize;
				}
			}

			/* Open the file and read the contents into p */
			if ((fd = open (prodFullName, O_RDONLY)) == -1) {
				logMsg (eLog, V_ERROR, S_ERROR,
					"(%s) - Error (%d) \"%s\" while opening %s",
					__FUNCTION__, errno, strerror (errno), prodFullName);

				continue;	/* Maybe it will be accessible next pass? */
			}

			p = ProdBuf;

			if (AddLdmWrapper) {
				sprintf(p, SBN_HEADER_TEMPLATE, SbnSeqNo);
				p = ProdBuf + strlen (ProdBuf);
			}

			if (AddWmoHeader) {
				sprintf (p, "%s%s", wmo_id, WMO_TERMINATOR);
				p = ProdBuf + strlen (ProdBuf);
			}

			if ((readSize = read (fd, p, pFileSize)) == -1) {
				logMsg (eLog, V_ERROR, S_ERROR,
					"(%s) - Error (%d) \"%s\" while reading %s",
					__FUNCTION__, errno, strerror (errno), prodFullName);

				close (fd);
				continue;
			}

			if (readSize != pFileSize) {
				logMsg (eLog, V_ERROR, S_WARNING,
					"(%s) - %s is %d bytes, but only %d bytes read",
					__FUNCTION__, prodFullName, pFileSize, readSize);
			}

			if (close (fd) == -1) {
				logMsg (eLog, V_ERROR, S_WARNING,
					"(%s) - Error (%d) \"%s\" while closing %s",
					__FUNCTION__, errno, strerror (errno), prodFullName);
			} else {
				logMsg (eLog, V_DEBUG, S_DEBUG,
					"(%s) - Successfully read [%s] %s into memory",
					__FUNCTION__, wmo_id, pFileName);
			}

			if (AddLdmWrapper) {
				memcpy (p+readSize, SBN_TRAILER, SIZE_SBN_TLR);		/* Append the SBN trailer */
			}

			finalSize = prodSize - pFileSize + readSize;
		}

		/* If saving the files, then move to save directory */
		if (SaveFiles) {
			if ((rstat = moveFile (prodFullName, SentDir, YES)) != 0) {
				logMsg (eLog, V_ERROR, S_ERROR,
					"(%s) - could not move %s to %s",
					__FUNCTION__, prodFullName, SentDir);
			} else {
				logMsg (eLog, V_DEBUG, S_DEBUG,
					"(%s) - moved %s to %s",
					__FUNCTION__, prodFullName, SentDir);
			}
		} else if (((OutAction == OUT_FILE) && AddWmoHeader) ||
#ifdef LDM_SUPPORT
			    (OutAction == OUT_LDM) ||
#endif
			    (OutAction == OUT_DISCARD)) {
			/* if action is FILE and a header is being added or action is DISCARD,
			 * this can be deleted here since a new file will be created below */
			if ((rstat = unlink (prodFullName)) != 0) {
				logMsg (eLog, V_ERROR, S_ERROR,
					"(%s) - %s could not be unlinked, errno = %d, strerr = %s",
					__FUNCTION__, prodFullName, errno, strerror (errno));
			} else {
				logMsg (eLog, V_DEBUG, S_DEBUG,
					"(%s) - %s deleted successfully",
					__FUNCTION__, prodFullName);
			}
		}

		makeAgeStr (pFileTime, tmpbuf);

                switch (OutAction) {
#ifdef LDM_SUPPORT
                        case OUT_LDM:
                                MD5Init(md5ctxp);
                                MD5Update (md5ctxp, (unsigned char*)ProdBuf + SIZE_SBN_HDR, finalSize - SIZE_SBN_HDR);
                                MD5Final (ldmProd.info.signature, md5ctxp);

                                ldmProd.info.origin     = LocalHostName;
                                ldmProd.info.feedtype   = FeedType;
				ldmProd.info.seqno	= SbnSeqNo;
				ldmProd.data		= ProdBuf;
				ldmProd.info.sz		= finalSize;
				ldmProd.info.ident	= wmo_id;
				gettimeofday (&ldmProd.info.arrival, NULL);

				if ((TotalProductsProcessed % STATUS_FREQUENCY) == 0) {
					logMsg (pLog, V_ALWAYS, S_STATUS,
						"STATUS [%s] pid(%d) HOST:%s ldmpq(%s) totprods(%lld) totbytes (%lld) %s(%s)",
						ProgName, MyPid, LocalHostName, pqfName, TotalProductsProcessed,
						TotalBytesProcessed, Loc, InputSource);
				}

				processed++;
				TotalBytesProcessed += readSize;
				TotalProductsProcessed++;
				SbnSeqNo++;
				SbnSeqNo %= 1000;

				logMsg (eLog, V_DEBUG, S_DEBUG,
					"(%s) - Before lpqInsert WMO[%s] size(%d)",
					__FUNCTION__, wmo_id, finalSize);

				switch (rstat = lpqInsert(prodQueue, &ldmProd)) {
					case STAT_SUCCESS:
						logMsg (pLog, V_ALWAYS, S_STATUS,
							"END/QUEUED WMO[%s] #%lld bytes(%d) f(%s)%s",
							wmo_id, TotalProductsProcessed, pFileSize, pFileName, tmpbuf);
						logMsg (eLog, V_DEBUG, S_DEBUG,
							"(%s) After lpqInsert - SUCCESS", __FUNCTION__);
						break;

					case STAT_ALREADY_QUEUED:
						logMsg (pLog, V_ALWAYS, S_STATUS,
							"END/ALREADY_QUEUED WMO[%s] #%lld bytes(%d) f(%s)%s",
							wmo_id, TotalProductsProcessed, pFileSize, pFileName, tmpbuf);
						logMsg (eLog, V_DEBUG, S_DEBUG,
							"(%s) After lpqInsert - PRODUCT ALREADY QUEUED", __FUNCTION__);
						break;

					default:
						logMsg (pLog, V_ALWAYS, S_STATUS,
							"END/LDM_ERROR(%d) WMO[%s] #%lld bytes(%d) f(%s)%s",
							rstat, wmo_id, TotalProductsProcessed, pFileSize, pFileName, tmpbuf);
						logMsg (eLog, V_DEBUG, S_DEBUG,
							"(%s) After lpqInsert - ERROR(%d)",
							__FUNCTION__, rstat);
						break;
				}
				break;
#endif

			case OUT_FILE:
				snprintf (outFilePath, sizeof(outFilePath)-1, "%s/%s.%0*d", SaveDir,
				        inOpts[InType-1].str, SentFileDigits, SentSeqNo);
				if (AddWmoHeader) {
					if ((fd = open (outFilePath, O_WRONLY | O_CREAT | O_TRUNC, OUTFILE_CREATE_PERMS)) == -1) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - Error (%d) \"%s\" while opening out file %s",
							__FUNCTION__, errno, strerror (errno), outFilePath);

						continue; /* Maybe it will be accessible next pass? */
					}

					if ((writeSize = write (fd, ProdBuf, finalSize)) == -1) {
						logMsg (eLog, V_ERROR, S_WARNING,
							"(%s) - Error (%d) \"%s\" while writing %s",
							__FUNCTION__, errno, strerror (errno), outFilePath);
					} else if (writeSize != finalSize) {
						logMsg (eLog, V_ERROR, S_WARNING,
							"(%s) - write buffer contains %d bytes, but only %d bytes written",
							__FUNCTION__, finalSize, writeSize);
					}

					if (close (fd) == -1) {
						logMsg (eLog, V_ERROR, S_WARNING,
							"(%s) - Error (%d) \"%s\" while closing %s",
							__FUNCTION__, errno, strerror (errno), outFilePath);
					}

					if (chmod (outFilePath, OUTFILE_FINAL_PERMS) == -1) {
						logMsg (eLog, V_ERROR, S_ERROR,
							"(%s) - Error (%d) \"%s\" while calling chmod on %s",
							__FUNCTION__, errno, strerror (errno), outFilePath);
					}

                		} else {
                			if ((rstat = moveFile (prodFullName, outFilePath, YES)) != 0) {
                				logMsg (eLog, V_ERROR, S_ERROR,
                					"(%s) - could not move %s to %s",
                					__FUNCTION__, prodFullName, outFilePath);
                			} else {
                				logMsg (eLog, V_DEBUG, S_DEBUG,
                					"(%s) - moved %s to %s",
                					__FUNCTION__, prodFullName, outFilePath);
                			}

                		}

				if ((TotalProductsProcessed % STATUS_FREQUENCY) == 0) {
					logMsg (pLog, V_ALWAYS, S_STATUS,
						"STATUS [%s] pid(%d) HOST:%s outdir(%s) totprods(%lld) totbytes (%lld) %s(%s)",
						ProgName, MyPid, LocalHostName, SentDir, TotalProductsProcessed,
						TotalBytesProcessed, Loc, InputSource);
				}

				processed++;
				TotalBytesProcessed += readSize;
				TotalProductsProcessed++;

				if (AddLdmWrapper) {
					SbnSeqNo++;
					SbnSeqNo %= 1000;
				}

				SentSeqNo++;
				SentSeqNo %= MaxSentFiles;

				logMsg (pLog, V_ALWAYS, S_STATUS,
					"END/FILE WMO[%s] #%lld bytes(%d) f(%s)[%s]%s",
					wmo_id, TotalProductsProcessed, pFileSize, pFileName, outFilePath, tmpbuf);
				break;

			case OUT_DISCARD:
				if ((TotalProductsProcessed % STATUS_FREQUENCY) == 0) {
					logMsg (pLog, V_ALWAYS, S_STATUS,
						"STATUS [%s] pid(%d) HOST:%s DISCARD totprods(%lld) totbytes (%lld) %s(%s)",
						ProgName, MyPid, LocalHostName, TotalProductsProcessed,
						TotalBytesProcessed, Loc, InputSource);
				}

				processed++;
				TotalBytesProcessed += readSize;
				TotalProductsProcessed++;

				logMsg (pLog, V_ALWAYS, S_STATUS,
					"END/DISCARD WMO[%s] #%lld bytes(%d) f(%s)%s",
					wmo_id, TotalProductsProcessed, pFileSize, pFileName, tmpbuf);
				break;

			default:
				logMsg (eLog, V_ERROR, S_ERROR,
					"(%s) - Unknown OutType (%d)",
					__FUNCTION__, OutAction);
				break;
		}
	}

	return processed;
}

/* ---------------------------------------------------------------------------- */
/* Main starts here */

int main (int argc, char **argv) {

	time_t			now_time;
	time_t			lastPollTime;
	FILE_LIST		fileList;
	int			sleepTime;
	int			moreProds;
	int			pollAgain;
	int			qProcessCount;
	int			readPipe;

#ifdef LDM_SUPPORT
	int			status;
#else
#	ifdef REOPEN_STD_FILES

	char			bufr[MAX_FILENAME_LEN+1];

#	endif
#endif

	setlocale(LC_ALL, ""); /* use user selected locale */

	MyPid = getpid ();
	ProgName = strdup (basename (argv[0]));
	if (gethostname(LocalHostName, MAX_HOST_NAME_LEN)) {
		LocalHostName[0] = '\0';
	}

	if (strlen (LocalHostName) == 0) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - call to gethostname failed",
			__FUNCTION__);
	}

	cmd_line (argc, argv);

	if (!fileExists (LogPathBase)) {
		if (makeDirectory (LogPathBase, YES, DIRECTORY_CREATE_PERMS)) {
			fprintf (stderr,
				"ERROR: %s - Could not create directory %s\n",
				__FUNCTION__, LogPathBase);
		}
	}

	if (strcmp (LogPathBase, MessagePath) && !fileExists (LogPathBase)) {
		if (makeDirectory (MessagePath, YES, DIRECTORY_CREATE_PERMS)) {
			fprintf (stderr,
				"ERROR: %s - Could not create directory %s\n",
				__FUNCTION__, MessagePath);
		}
	}

#ifdef REOPEN_STD_FILES
	sprintf (bufr, "%s/%s.stdout", MessagePath, ProgName);
	reopenStdFile (1, bufr);

	sprintf (bufr, "%s/%s.stderr", MessagePath, ProgName);
	reopenStdFile (2, bufr);

	if (Verbosity < S_WARNING) {
		reopenStdFile (1, "/dev/null");
		reopenStdFile (2, "/dev/null");
	}
#endif

	if (initLogs ()) {
		fprintf (stderr,
			"InitLogs failed -- exiting\n");
		return 1;
	}

	if (atexit (&atExitHandler)) {
		logMsg (eLog, V_ERROR, S_FATAL,
			"atExitHandler could not be registered with atexit()");

		return 1;
	}

	logMsg (eLog, V_INFO, S_STATUS,
		"Start up parameters:");
	logMsg (eLog, V_INFO, S_STATUS,
		"Debug Level: %d",
		Verbosity);
	if (InType == IN_ACQ_PIPE) {
		logMsg (eLog, V_INFO, S_STATUS,
			"ACQ Pipe: %s",
			InputSource);
	} else {
		logMsg (eLog, V_INFO, S_STATUS,
			"Polling Directory: %s",
			InputSource);
	}
	if (SaveFiles) {
		logMsg (eLog, V_INFO, S_STATUS,
			"Sent Directory: %s",
			SentDir);
	}
	if (SaveFails) {
		logMsg (eLog, V_INFO, S_STATUS,
			"Fail Directory: %s",
			FailDir);
	}
	if ((OutAction == OUT_FILE) && strlen (SaveDir)) {
		logMsg (eLog, V_INFO, S_STATUS,
			"Save Directory: %s",
			SaveDir);
        }

	if (CreateChecksum) {
		logMsg (eLog, V_INFO, S_STATUS,
			"Checksum Type: %s",
			csOpts[HashOpt].str);
	} else {
		logMsg (eLog, V_INFO, S_STATUS,
			"Create Checksum: NO");
	}

	logMsg (eLog, V_INFO, S_STATUS,
		"Input Type: %s",
		(InType == IN_ACQ_PIPE) ? "ACQ_PIPE" :
		(InType == IN_POLL) ? "POLL" :
		(InType == IN_NDE) ? "NDE" :
		(InType == IN_PDA) ? "PDA" :
                (InType == IN_GOESR) ? "GOESR" : "Unknown");
	logMsg (eLog, V_INFO, S_STATUS,
		"Discard Age: %d",
		DiscardAge);
	logMsg (eLog, V_INFO, S_STATUS,
		"Polling Interval: %d",
		PollInterval);
	logMsg (eLog, V_INFO, S_STATUS,
		"Log Path: %s",
		LogPathBase);
	logMsg (eLog, V_INFO, S_STATUS,
		"Message Path: %s",
		MessagePath);
	logMsg (eLog, V_INFO, S_STATUS,
		"File Template: %s",
		PollFileSpec);
	logMsg (eLog, V_INFO, S_STATUS,
		"Output Type: %s",
		(OutAction == OUT_FILE) ? "FILE" :
		(OutAction == OUT_DISCARD) ? "DISCARD" :
#ifdef LDM_SUPPORT
		(OutAction == OUT_LDM) ? "LDM" :
#endif
		"Unknown");
#ifdef LDM_SUPPORT
	if (OutAction == OUT_LDM) {
		logMsg (eLog, V_INFO, S_STATUS,
			"LDM Product Queue: %s",
			pqfName);
	}
#endif

	if ((OutAction == OUT_FILE) && strlen (SaveDir) && !fileExists (SaveDir)) {
		if (makeDirectory (SaveDir, YES, DIRECTORY_CREATE_PERMS)) {
			logMsg (eLog, V_ERROR, S_FATAL,
				"(%s) - could not create save directory %s",
				__FUNCTION__, SaveDir);
			return 1;
		}
	}

	if (SaveFiles && !fileExists (SentDir)) {
		if (makeDirectory (SentDir, YES, DIRECTORY_CREATE_PERMS)) {
			logMsg (eLog, V_ERROR, S_FATAL,
				"(%s) - could not create sent directory %s",
				__FUNCTION__, SentDir);
			return 1;
		}
	}

	if (SaveFails && !fileExists (FailDir)) {
		if (makeDirectory (FailDir, YES, DIRECTORY_CREATE_PERMS)) {
			logMsg (eLog, V_ERROR, S_FATAL,
				"(%s) - could not create fail directory %s",
				__FUNCTION__, FailDir);
			return 1;
		}
	}

	if (ParDir[0] && !fileExists (ParDir)) {
		if (makeDirectory (ParDir, YES, DIRECTORY_CREATE_PERMS)) {
			logMsg (eLog, V_ERROR, S_FATAL,
				"(%s) - could not create PAR file directory %s",
				__FUNCTION__, FailDir);
			return 1;
		}
	}

        setupSigHandler ();

#ifdef LDM_SUPPORT
        md5ctxp = new_MD5_CTX();

        if (md5ctxp == NULL) {
		logMsg (eLog, V_ERROR, S_FATAL,
			"(%s) - could not allocate MD5 object",
			__FUNCTION__);
                exit (1);
        }

        if (OutAction == OUT_LDM) {
                if ((status = lpqGet(pqfName, &prodQueue)) != 0) {
                        logMsg (eLog, V_ERROR, S_ERROR,
                        	"(%s) - Error (%d) could not open LDM product queue %s",
				__FUNCTION__, status, pqfName);
			exit (1);
		}
	}
#endif

        /*
         * If InType is ACQ_PIPE, then InputSource will be a pipe name, otherwise it will be a directory
         */
	if (InType == IN_ACQ_PIPE) {
		if (!fileExists (InputSource)) {
			if (mknod (InputSource, S_IFIFO | OUTFILE_FINAL_PERMS, 0)) {
				logMsg (eLog, V_ERROR, S_FATAL,
					"(%s) - Error (%d) \"%s\" creating pipe %s",
					__FUNCTION__, errno, strerror (errno), InputSource);
				return 1;
			}
		} else if (getFileType (InputSource) != S_IFIFO) {
			logMsg (eLog, V_ERROR, S_FATAL,
				"(%s) - %s must be a pipe",
				__FUNCTION__, InputSource);
			return 1;
		}

		/*
		 * IMPORTANT NOTE -- It's possible for this open to block
		 */
		if ((readPipe = open (InputSource, O_RDONLY, 0)) == -1) {
			logMsg (eLog, V_ERROR, S_FATAL,
				"(%s) - Error (%d) \"%s\" opening pipe %s",
				__FUNCTION__, errno, strerror (errno), InputSource);
			return 1;
		}
	} else if (!fileExists (InputSource)) {
		if (makeDirectory (InputSource, YES, DIRECTORY_FULL_OPEN_PERMS)) {
			logMsg (eLog, V_ERROR, S_FATAL,
				"(%s) - could not create polling directory %s",
				__FUNCTION__, InputSource);
			return 1;
		}
	}

	TotalProductsProcessed = 0;
	TotalBytesProcessed = 0;
	lastPollTime = 0;
	moreProds = TRUE;
	pollAgain = FALSE;
	Done = FALSE;
	SbnSeqNo = 1;
	SentSeqNo = 0;
	fileList.count = 0;

	while (!Done) {
		now_time = time (NULL);
		/* lastDeltaTime = now_time - lastPollTime; */

		if (PollInterval <= 0) {
			Done = TRUE;
		}

		if (InType == IN_ACQ_PIPE) {
			if (!readAcqPipe (readPipe, &fileList)) {
				logFileList (&fileList);
				if ((qProcessCount = processProducts (&fileList)) != fileList.count) {
					logMsg (eLog, V_ERROR, S_WARNING,
						"(%s) - only %d of %d queued products processed",
						__FUNCTION__, qProcessCount, fileList.count);
				}

				freeFileList (&fileList);
			}
		} else {	/* Poll a directory for files */
			if (moreProds || pollAgain || ((now_time - lastPollTime) >= PollInterval)) {

				if ((moreProds = findFilesLike (InputSource, PollFileSpec, &fileList)) < 0) {
					logMsg (eLog, V_ERROR, S_ERROR,
						"(%s) - Unrecoverable error calling find_files_like",
						__FUNCTION__);
					break;
				}

				if (fileList.count > 0) {
					if (Verbosity >= V_DEBUG) {
						logFileList (&fileList);
					}

					if (fileList.count > 1) {
						/* Sort by last modification time so oldest files are sent first */
						qsort (fileList.fileNodes, fileList.count, sizeof (FILE_NODE), compareFileNodeTimes);

						if (Verbosity >= V_DEBUG) {
							logFileList (&fileList);
						}

					}

					if ((qProcessCount = processProducts (&fileList)) != fileList.count) {
						logMsg (eLog, V_ERROR, S_WARNING,
							"(%s) - only %d of %d queued products processed",
							__FUNCTION__, qProcessCount, fileList.count);

						/* Do anything else??? */
						pollAgain = FALSE;
						moreProds = FALSE;
					} else {
						pollAgain = TRUE;
					}

					freeFileList (&fileList);
				} else {
					pollAgain = FALSE;
				}

				lastPollTime = time (NULL);
			} else {
				sleepTime = PollInterval;

				if (sleepTime > SleepPollInterval) {
					sleepTime = SleepPollInterval;
				}

				if (sleepTime > 0) {
					logMsg (eLog, V_DEBUG, S_DEBUG,
						"(%s) - sleeping for %d seconds",
						__FUNCTION__, sleepTime);
					sleep (sleepTime);
				}
			}
		}
	}

	return 0;
}
