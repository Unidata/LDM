/*******************************************************************************
NAME
	name - 
		acq_getshm.c

DESCRIPTION
	Allocate shared memory 

RETURNS

*******************************************************************************/
#include "config.h"
#include "retrans.h"
#include "acq_shm_lib.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#define DEBUG(x) printf("service: x = %d\n", (x))

extern	char	*optarg;

typedef struct	input 	{		/* user input options */
	int		verbose;			/* verbose mode with debug */
	int		memory_region;		/* memory region */

	int		include_flag;		/* include option on */
	int		exclude_flag;		/* exclude option on */

	int		acq_table_flag;			/* acq_table flag */
} INPUT;


	int	DebugShm;				/* Debug shm option flag */

#define NAME_ACQ_TABLE "ACQ_TABLE"

/**** prototypes ****/
void usage(void);
int cmd_line(int argc, char *argv[], INPUT *p_input);


#define NEW_SHMKEY_REGION { \
	(k + (m * 10000))\
}

#define VAR_GET_SHMEM(a,b,c,d)  VAR_LEAN_GET_SHMEM(a,b,c,d,SHM_REUSE_FLAG)

void VAR_LEAN_GET_SHMEM(char *a,ACQ_TABLE ss, int z, int k, int r) { 
    /* allocate and set a shared memory segment if required or requested */ 
	long ii,iii;

	a = (char *)NULL; 
    if (!(r) || !com_shmIsAlloc(k)) { 
        if ((a = (char *)com_shmAlloc((sizeof(ss)*z), k)) == (char *)NULL) { 
            printf(" Key=%d ret(0x%lx) com_shmAlloc " "size(%ld bytes) FAILED\n", k, (unsigned long) a, (sizeof(ss)*z)); 
            exit(99); 
        } 
        ii=(sizeof(ss)*z); 
        iii=z; 
        printf(" Key=%d alloc shm OK.. at(0x%lx) (%ld/%ld bytes) s[%d]\n", k,(unsigned long) a, (sizeof(ss)*z),ii/iii,z); 
        /* fill it and detach it */ 
        (char *)memset((char *)a,0,(size_t)(sizeof(ss)*z)); 
        com_shmDetach(a); 
    } else { 
        if ((a = (char *)get_shm_ptr(k,a,DEBUGGETSHM)) == (char *)NULL) { 
            printf(" KEY %d Get get_shm_ptr FAILED \n", k); 
            exit(0); 
        } else { 
            printf(" KEY %d Get get_shm_ptr OK at (0x%lx)\n", k,(unsigned long) a); 
		}
        memset(a,0,(sizeof(ss)*z)); 
        com_shmDetach(a); 
    } 
}

char	PNAME[40];
int MaxLinks;

int
main(argc, argv)	/* argument #1 is a test SHMnumber */
int argc;
char **argv;
{
	INPUT   input;				/* user input options */
	int 	SHMnumber;
	char 	*address=NULL; 
	char 	*address1; 
	int		ii,iii,iiii;		/* loop counter */
	long	tot_shmem;			/* total amount of shared memory */
	long	new_shmem;			/* new amount of shared memory */
	long	add_array_shmem;	/* add array amount of shared memory */
	long	add_bytes_shmem;	/* add bytes amount of shared memory */
	int		shm_region;			/* shm region */
	int		new_key;			/* shm key */
	ACQ_TABLE *acq_table;				/* acq table */
	ACQ_TABLE	*p_acqtable;			/* acq table ptr */
	ACQ_TABLE	*p_acqtable_link;		/* acq table link ptr */
	int		extend_host_flag;	/* extended structure flag */
	int		extend_group_flag;	/* extended structure flag */
	int		size_of_acqtable;	/* size of acqtable per link */
	int		size_of_group_info;	/* size of group_info per link */
	ACQ_TABLE	acq_tbl;

	strcpy(PNAME, "acq_getshm");

	/* get operator command line inputs */
	if (cmd_line(argc, argv, &input) == -1) {
		exit(0);
	}

	if(input.verbose) {
		DebugShm = DEBUG_YES;
	} else {
		DebugShm = DEBUG_NO;
	}

	
	shm_region = input.memory_region;
	
	tot_shmem = 0;
/* 	ACQ_TABLE         acquisition table */

	 	MaxLinks = MAX_LINKS;

		FIND_SHMKEY_REGION(new_key,ACQ_TABLE_SHMKEY,shm_region);
		

			VAR_LEAN_GET_SHMEM(address,acq_tbl,MAX_LINKS, new_key, SHM_REUSE_FLAG);
			new_shmem = (sizeof(ACQ_TABLE) * MaxLinks);
			tot_shmem += new_shmem;
			printf("  acq_table tot=%ld(%d links x %ld per acq_tbl) bytes address=%lx\n",
				new_shmem, MaxLinks, sizeof(ACQ_TABLE),(unsigned long)address);



		/* Now update the internal acq_table value */
		GET_SHMPTR_DEBUG(acq_table, ACQ_TABLE,
			SHMKEY_REGION(ACQ_TABLE_SHMKEY ,shm_region), DebugShm);
		/* tcq_table = address; */
		printf("xxxxxxxxxxxxxxxx  = 0x%lx\n",(unsigned long) acq_table);
		printf("yyyyyyyyyyyyyyyy  = 0x%lx\n",(unsigned long) &acq_table[0]);
		p_acqtable = acq_table;
		for(iiii=0; iiii< MaxLinks; iiii++) {
				p_acqtable_link = &acq_table[iiii];
				if(input.verbose) {
					printf("  p_acqtable_link[%d]=0x%lx\n", 
						iiii,(unsigned long) p_acqtable_link);
				} else {
					if((iiii <= 1) || (iiii == (MaxLinks -1))) {
						printf("  p_acqtable_link[%d]=0x%lx\n", 
							iiii,(unsigned long) p_acqtable_link);
					}
				}
			p_acqtable_link->max_links = (unsigned char)MaxLinks; 
			p_acqtable_link->link_id = (unsigned char)iiii; 
			p_acqtable_link->pid = 0; 

			p_acqtable_link->proc_base_prod_cat_last = 0;
			p_acqtable_link->proc_base_prod_code_last = 0;
			p_acqtable_link->proc_base_prod_type_last = 0;
			p_acqtable_link->proc_base_prod_seqno_last = 0;
			p_acqtable_link->proc_orig_prod_seqno_last = 0;
			p_acqtable_link->read_distrib_enable = 0;

			p_acqtable_link->proc_base_channel_type_last = SBN_TYP_NMC;
			p_acqtable_link->proc_retransmit_ctl_flag = 0;
			p_acqtable_link->proc_retransmit_ctl_flag |= ENABLE_RETRANS_GEN_RQST;
			p_acqtable_link->proc_retransmit_ctl_flag |= ENABLE_RETRANS_XMIT_RQST;
			p_acqtable_link->proc_retransmit_ctl_flag |= ENABLE_RETRANS_DUP_DISCARD;
			p_acqtable_link->proc_retransmit_delay_send = DEFAULT_RETRANSMIT_DELAY_SEND; 
	
			p_acqtable_link->proc_prod_run_id = 0;
			p_acqtable_link->proc_orig_prod_run_id= 0;

			p_acqtable_link->proc_tot_prods_retrans_rcvd = 0;	
			p_acqtable_link->proc_tot_prods_retrans_rcvd_lost = 0;	
			p_acqtable_link->proc_tot_prods_retrans_rcvd_notlost = 0;
			p_acqtable_link->proc_tot_prods_retrans_rqstd = 0;
			
		}
	
		printf("  subtotal ACQ_TABLE key(0x%x)= %ld bytes\n", 
			new_key, new_shmem);

	
	printf("acq_getshm Done OK get shmem total = %ld bytes\n", tot_shmem);

	exit(0);
} /* end main */


/******************************************************************************
	Name
		cmd_line
	Function
		Get operator inputs
******************************************************************************/
/* command line input routine for pipes, etc */
int
cmd_line(
	int argc,
	char *argv[],
	INPUT *p_input)
{
	int     c;
	int		flag;

	/* input initialization */
	p_input->verbose = 0;
	p_input->memory_region = 0;
	p_input->include_flag = 0;			/* include flag */
	p_input->exclude_flag = 0;			/* exclude flag */

	p_input->acq_table_flag = 0;		/* acq_table flag */

	while((c=getopt(argc,argv,"m:v:")) != EOF) {
		switch(c) {
			case 'v': {
				p_input->verbose = 1;
				printf("%s Set mode verbose\n", PNAME);
				break;
			}
			case 'm': {
				p_input->memory_region = atoi(optarg);
				if ((p_input->memory_region < 0) || 
					(p_input->memory_region >= MAX_LINKS)) {
					printf("%s Memory region must be between 0 and %d\n",
					    PNAME, MAX_LINKS);
					  usage();
				}
				printf("%s Shm region set to %d\n", 
						PNAME, p_input->memory_region);
				break;
			}
			case '?': {
				usage();
			}
			default:
			break;
		}  /* end switch */
	}  /* end while */


	return 0;
}   /* end of cmd_line routine */

void usage(void) {
	printf("Usage: %s"
		" [-m memory_region]"
		" [-v (verbose)]\n"
		"          ",
		PNAME);
	exit(1);
} /* end usage routine */

