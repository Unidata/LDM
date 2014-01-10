/*******************************************************************************
NAME
	name - 
		acq_freehsm.c

DESCRIPTION
	Free shared memory 

RETURNS

*******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>

#include <errno.h>
#include <ctype.h>
#include "retrans.h"
#include "acq_shm_lib.h"

#define DEBUG(x) printf("service: x = %d\n", (x))

extern	char	*optarg;
extern char *com_shmAttach(key_t new_key);

typedef struct	input 	{		/* user input options */
	int		verbose;			/* verbose mode with debug */
	int		memory_region;		/* memory region */


} INPUT;

#define NAME_ACQ_TABLE "ACQ_TABLE"

/**** prototypes ****/
void usage(void);
int cmd_line(int argc, char *argv[], INPUT *p_input);

char	PNAME[40];

main(argc, argv)	
int argc;
char **argv;
{
	INPUT   input;          /* user input options */
	int SHMnumber;
	char *address; 
	long	tot_shmem;	/* total amount of shared memory */
	long	new_shmem;	/* new amount of shared memory */
	int	shm_region;	/* shm region */
	int	new_key;	/* shm key */
	


	strcpy(PNAME, "acq_freeshm");
	/* get operator command line inputs */
	if (cmd_line(argc, argv, &input) == -1) {
		exit(0);
	}

	shm_region = input.memory_region;

	/* free it up... */ 
	printf("acq_freeshm Begin free shared memory \n");

/* 	ACQ_TABLE         acquisition table */

		ACQ_TABLE *acq_table;
		int link;
		printf("acq_freeshm get shmem for acq_table\n");
		FIND_SHMKEY_REGION(new_key,ACQ_TABLE_SHMKEY,shm_region);
		if (acq_table = (ACQ_TABLE *) com_shmAttach(new_key)) {
			for (link = 0; link < acq_table[0].max_links; link++) {
				acq_table[link].link_id = 0xff; 
			}
		}
		FREE_SHMEM(new_key);


	printf("acq_freeshm Done OK\n" );

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
		" [-v (verbose)]\n",
		PNAME);
	exit(1);
} /* end usage routine */

