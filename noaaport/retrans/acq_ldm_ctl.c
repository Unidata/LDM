static char Sccsid_acq_ctl_c[]= "@(#)acq_ctl.c 4.1.15 11/21/2003 12:43:20";
/*******************************************************************************
NAME
	name -  acq_ctl.c

DESCRIPTION
	Routines to provide online control of acquisition shared memory parms
	 - General control to print menu & change link
	 - Reader control to stop/suspend active reader
	 - DH process control to stop active process and control image handling
	 - Distrib control to stop active distribution process
	 - Client control to stop/restart active clients, enable debug, 
		and special client_id cleanup
	 - Global control to stop all active acquisition, 
		and special semaphore recovery/cleanup

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
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEBUG(x) printf("service: x = %d\n", (x))

/* include the VxWorks list library */


#define PRINT_MENU 0
#define RESTART_CLIENTS 1
#define MAX_INPUT_VALUE 999999



#define	ENABLE_FLAG	0
#define	DISABLE_FLAG	1
#define READ_CTL_DISCARD DISABLE_FLAG 


/* Begin of stop commands */
#define FIRST_STOP_CMD 2
#define STOP_SPAWN_CLIENTS 2
#define STOP_READER 3
#define STOP_PROCESS 4
#define STOP_DISTRIBUTION 5
#define LAST_STOP_CMD 5
/* End of stop commands */

#define LIST_READER 30
#define SUSPEND_READER 31
#define SUSPEND_READER_EOP 32
#define SET_DEBUG_MODE_READER 33
#define SIGNAL_WAKEUP_READER 34
#define SET_ALARM_WAKEUP_READER 39
#define SET_DISCARD_READER 35
#define SET_LOG_PROD_READER 36
#define ENABLE_SET_TIME_READER 37
#define ENABLE_RETRANS_READER 38
#define SET_CLOSE_SOCKET_READER 133
#define SET_DROP_MEMBERSHIP_READER 134
#define SET_MAX_PRIORITY_QUE_COUNTS 136
#define SET_MASTER_PROD_SEQNO 137
#define ENABLE_SBN_FILE_OPTION 138
#define SET_MAX_PROD_PER_SEC_RDR 139

#define LIST_DH_PROCESS 40
#define SUSPEND_DH_PROCESS_EOP 41
#define ENABLE_FILL_MODIFY_IMAGE 42
#define SET_DEBUG_MODE_DH_PROCESS 43
#define SET_FILEWRITE_MODE_DH_PROCESS 44
#define SET_DISCARD_DH_PROCESS 45
#define SET_LOG_PROD_DH_PROCESS 46
#define SET_RETRANS_XMIT_DLY_RQST_DH_PROCESS 47
#define ENABLE_RETRANS_XMIT_RQST_DH_PROCESS 48
#define ENABLE_RETRANS_RCV_DUP_MATCH_DISCARD_DH 49
#define ENABLE_RETRANS_RCV_NOMATCH_DISCARD_DH 149
#define ENABLE_SBN_FRAME_UNCOMPR 142
#define ENABLE_SBN_FRAME_COMPR_ALWAYS 143
#define ENABLE_SBN_FRAME_COMPR_IF_REQ 144
#define ENABLE_SBN_FRAME_COMPR_IMAGE_ALWAYS 146
#define SET_SBN_UPL_FRAME_MIN_COMPR_LEN 145

#define SPAWN_CLIENT_DISTRIB_DELAY 51
#define SPAWN_CLIENT_DISTRIB_IMMED 52
#define SET_DEBUG_MODE_CLIENT 21
#define SET_WAIT_MODE_CLIENT 22
#define CK_LOG_PROD_CLIENT 26
#define SET_LOG_PROD_CLIENT 27
#define CLEAR_LOG_PROD_CLIENT 28
#define SET_MAX_PROD_PER_SEC_CLIENT 29
#define STOP_CLIENTS_EOP_RST 24
#define STOP_CLIENTS_IMMED_RST 25
#define KILL_CLIENTS_IMMED_RST 125
#define CLEAR_CLIENT_IDS 20

#define RESET_FRAME_AND_PROD_SEQNO 60
#define CLEAR_TMOUT_STATS 65
#define CLEAR_ERR_STATS 66
#define CLEAR_ACQ_RETRANS_STATS 67
#define CLEAR_RETRANS_TABLE 68
#define CHANGE_LINK 77
#define CLEAR_IO_STATS 69
#define CLEAR_HDR_SEM_CNT 88
#define VALIDATE_LINK_LIST 90
#define STOP_ALL 99
#define KILL_ALL 98
#define LAST_COMMAND STOP_ALL+100

#define NAME_PRINT_MENU "print menu"
#define NAME_RESTART_CLIENTS "AUTOSTART spawn client"
#define NAME_STOP_SPAWN_CLIENTS "NO AUTOSTART spawn client"
#define NAME_START_STOP_CLIENTS "yes/no AUTOSTART spawn client"
#define NAME_STOP_READER "term active reader (kill)"
#define NAME_STOP_PROCESS "term active dh process (kill)"
#define NAME_STOP_DISTRIBUTION "term active distrib (kill)"

#define NAME_LIST_READER "list active readers"
#define NAME_SUSPEND_READER "suspend/resume reader immediately"
#define NAME_SUSPEND_READER_EOP "suspend/resume reader EOP"
#define NAME_SET_DISCARD_READER "set/unset discard reader"
#define NAME_SET_CLOSE_SOCKET_READER "open/close IP socket readr"
#define NAME_SET_DROP_MEMBERSHIP_READER "add/drop mcast member rdr"
#define NAME_SET_DEBUG_MODE_READER "set/unset debug reader"
#define NAME_SIGNAL_WAKEUP_READER "signal wakeup rdr"
#define NAME_SET_ALARM_WAKEUP_READER "set alarm wakeup reader"
#define NAME_ALL_WAKEUP_READER "signal /set_alarm wakeup reader"
#define NAME_SET_LOG_PROD_READER "set/unset log prod readr"
#define NAME_ENABLE_SET_TIME_READER "enable/disable SBN set time readr"
#define NAME_ENABLE_RETRANS_READER "enable/disable retrans"
#define NAME_ENABLE_RETRANS_SBN "enable/disable retrans via SBN"
#define NAME_ENABLE_RETRANS_MHS "enable/disable retrans via MHS"
#define NAME_SET_MASTER_PROD_SEQNO "set NCF uplink prodseqno"
#define NAME_ENABLE_SBN_FILE_OPTION "reuse/copy/nocopy SBN file"
#define NAME_CLEAR_IO_STATS "clear rdr/dh/clnt io stats"
#define NAME_SET_MAX_PROD_PER_SEC_RDR "max prod/sec uplink readr"
#define NAME_SET_MAX_PRIORITY_QUE_COUNTS "set priority que cnts"

#define NAME_LIST_DH_PROCESS "list active dh processes"
#define NAME_SUSPEND_DH_PROCESS_EOP "suspend/resume dh process EOP"
#define NAME_SET_DISCARD_DH_PROCESS "set/unset discard dh process"
#define NAME_SET_DEBUG_MODE_DH_PROCESS "set/unset debug dh process"
#define NAME_SET_FILEWRITE_MODE_DH_PROCESS "set/unset file wr dh process"
#define NAME_ENABLE_FILL_MODIFY_IMAGE "enab/disab fill/modify/group image"
#define NAME_ENABLE_FILL_IMAGE "enable/disable fill image"
#define NAME_ENABLE_MODIFY_IMAGE "enable/disable modify image"
#define NAME_ENABLE_GROUP_ENABLE "enable/disable group enable"
#define NAME_SET_LOG_PROD_DH_PROCESS "set/unset log prod dh proc"
#define NAME_SET_RETRANS_XMIT_DLY_RQST_DH_PROCESS "set delay xmit rqst retrans"
#define NAME_ENABLE_RETRANS_XMIT_RQST_DH_PROCESS "enab/disab rqst xmit retrans"
#define NAME_ENABLE_RETRANS_RCV_DUP_MATCH_DISCARD_DH " retrans dup_mtch_discard"
#define NAME_ENABLE_RETRANS_RCV_NOMATCH_DISCARD_DH " retrans no_mtch_discard"
#define	NAME_ENABLE_RETRANS_RCV_DUP_MAT_NOMATCH_DH "ena/dis match/nomtch discrd"
#define	NAME_ENABLE_SBN_FRAME_UNCOMPR "ena/dis SBN dwn frm uncompress"
#define	NAME_ENABLE_SBN_FRAME_COMPR "ena/dis SBN upl frm cmp"
#define NAME_SET_SBN_UPL_FRAME_MIN_COMPR_LEN "min SBN upl frame compress len"

#define	NAME_ENABLE_SBN_FRAME_UNCOMPR_ALWAYS "SBN frame uncompress_always"
#define	NAME_ENABLE_SBN_FRAME_COMPR_ALWAYS "SBN frame compress_always"
#define	NAME_ENABLE_SBN_FRAME_COMPR_IMAGE_ALWAYS "SBN frame image compress_always"
#define	NAME_ENABLE_SBN_FRAME_COMPR_IF_REQ "SBN frame compress_if_req"

#define NAME_SPAWN_CLIENT_DISTRIB "spawn client hdr>0 /immediate"
#define NAME_SPAWN_CLIENT_DISTRIB_DELAY "spawn client host dist_hdr>0 only"
#define NAME_SPAWN_CLIENT_DISTRIB_IMMED "spawn client host immediately"

#define NAME_SET_DEBUG_MODE_CLIENT "set/unset debug mode client"
#define NAME_SET_WAIT_MODE_CLIENT "set/unset wait servr mode clnt"
#define NAME_CK_LOG_PROD_CLIENT "ck/set/unset log prod clnt"
#define NAME_CLEAR_CLIENT_IDS "clear client ids - Caution!"
#define NAME_STOP_CLIENTS_IMMED_RST "term act clnt(s) IMMED w/RESTART"
#define NAME_KILL_CLIENTS_IMMED_RST "kill act clnt(s) IMMED w/RESTART"
#define NAME_STOP_CLIENTS_EOP_RST "term act clnt(s) EOP w/RESTART"
#define NAME_SET_MAX_PROD_PER_SEC_CLIENT "max prod/sec clnt"

#define NAME_CHANGE_LINK "change link"
#define NAME_RESET_FRAME_AND_PROD_SEQNO "reset frame/prod_seqno to 0"
#define NAME_CLEAR_ERR_STATS "clear error stats"
#define NAME_CLEAR_TMOUT_STATS "clear tmout stats"
#define NAME_CLEAR_ACQ_RETRANS_STATS "clear acq retrans stats"
#define NAME_CLEAR_ERR_AND_ACQ_RETRANS_STATS "clear acq err/retrans stats"
#define NAME_CLEAR_TMOUT_AND_IO_STATS "clear tmout/rdr|dh|clnt iostats"
#define NAME_CLEAR_RETRANS_TABLE "clear retrans table"
#define NAME_CLEAR_ALL_ERR_RETRANS "clear err/retrans_stat/tbl"
#define NAME_CLEAR_HDR_SEM_CNT "clear prod|dist sem_cnt-Caution!"
#define NAME_VALIDATE_LINK_LIST "validate linked lists"
#define NAME_STOP_ALL "term all acq immed for link(s)"
#define NAME_KILL_ALL "kill all acq all link(s)"

#define SET_DEBUG_PROD_FLAG 1
#define SET_DEBUG_BUFF_FLAG 2
#define CLEAR_DEBUG_PROD_FLAG 3
#define CLEAR_DEBUG_BUFF_FLAG 4

#define INPUT_YES 1			/* Operator input yes */
#define INPUT_NO 2			/* Operator input no */
#define INPUT_SUSPEND 3		/* Operator input suspend(wait) */
#define INPUT_RESUME 4		/* Operator input resume(continue) */
#define INPUT_NONE 5		/* Operator input none */
#define INPUT_DISCARD 6		/* Operator input discard */
#define INPUT_SET 7			/* Operator input set */
#define INPUT_CLEAR 8		/* Operator input clear */
#define INPUT_ENABLE 9		/* Operator input clear */
#define INPUT_DISABLE 10	/* Operator input clear */
#define ASK_YES_NO 12		/* Operator input question yes/no */
#define ASK_SET_CLEAR 13	/* Operator input question set/clear */
#define ASK_SUSPEND_RESUME 14	/* Operator question suspend/resume */
#define ASK_DISCARD_RESUME 15	/* Operator question dicard/resume */
#define ASK_YES_NO_DFLT_YES 16	/* Operator question yes/no default yes */
#define ASK_ENABLE_DISABLE 17	/* Operator question enable/disable dflt yes*/
#define ASK_OPEN_CLOSE 18	/* Operator question open/close dflt yes*/
#define ASK_ADD_DROP 19		/* Operator question add/drop dflt yes*/
#define INPUT_OPEN 20		/* Operator input open */
#define INPUT_CLOSE 21		/* Operator input close */
#define INPUT_DROP 22		/* Operator input drop */
#define INPUT_ADD 23		/* Operator input add */

#define FLAG_OFF 0
#define FLAG_ON 1

#define SLEEP_SEMTAKE_WAIT 2 /* wait for semaphore */

#define MAX_INPUT_STRING	256
#define LEN_INPUT_OPTION	128

/* setup shared memory parameters in this process */
	ACQ_TABLE       *acq_table;	/* acquisition table ptr */


#define MAX_INPUT_CMDS 64	/* max input commands */
typedef struct		input {		/* user input options */
	int		input_command;	/* command input */
	int		input_command_list[MAX_INPUT_CMDS];	/* command input list */
	int		input_command_count;	/* command input count */
	int		input_link;	/* input link */
	int     shmem_region;   /* memory region */
	int		verbose;	/* verbose mode with debug */
	int		query_mode;	/* query mode */
	int		input_host;	/* input host */
	int		input_group; /* input host */
	int		force_mode;	/* force mode no operator input */
	int		input_flag;	/* operator input flag */
					/*   0=none, 1=yes */
	int		shutdown_flag;	/* operator shutdown acq flag */
	int		NCF_only_flag;	/* sbn downlink flag */
	int		kill_flag;	/* operator kill acq flag */
	char	input_option[LEN_INPUT_OPTION+1];	/* operator input option string */
}    INPUT;

/**** prototypes ****/
void usage(void);
int cmd_line(int argc, char *argv[], INPUT *p_input);

int 	acqctl_do_action(int flag, INPUT *p_input, ACQ_TABLE *p_acqtable);



static 	int	acqctl_do_caution(char *in_command, INPUT *p_input);
static 	int	acqctl_do_confirm(char *in_command, INPUT *p_input);
static 	int	acqctl_get_response(INPUT *p_input, char *in_command, int in_question, 
			int *rtn_code);
static  int     acqctl_get_pid(int host_id, pid_t client_pid);

int do_new_menu();

	char	PNAME[40];
	extern char *optarg;

/* Global variables */
	int	global_i_cpiofd;	/* cpiofd */
	ulong	*global_logpipe_flag;	/* global logpipe flag */
	ulong	*global_logconsole_flag; /* global logpipe flag */
	int	global_logpipe_fd;	/* global logpipe fd */
	int	global_origin_code;	/* global origin code */
	int	global_NCF_flag = 0; /* NCF only commands */

	int	MaxLinks=0;		/* max links (variable) */
	int	MaxGroups=0;	/* max groups (variable) */
	int	MaxHosts=0;		/* max hosts (variable) */

#define MAXHOSTNAMELEN 64       /* max length for host name */
    char LocalHostName[MAXHOSTNAMELEN+1];

	INPUT	input;			/* operator inputs */

int
main(argc, argv)	/* argument #1 is a test SHMnumber */
        int argc;
        char **argv;
{

	const char FUNCNAME[] = "acq_ctl";
	int	flag;		/* control for control of client */

	int	debug_shm_flag;	/* debug shm flag */

	char 	*get_shm_ptr();
	int		ii;
	char	in_line[MAX_INPUT_STRING];	/* operator input */
	int     shm_region;             /* shared mem region */
	int		extend_flag=0;	/* extended acq_table flag */
	ACQ_TABLE	*p_acqtable_tmp; 	/* pointer to acq_table */

	INPUT	*p_input;	/* ptr to operator inputs */


	global_i_cpiofd = 0;

	signal(SIGUSR1, SIG_IGN);

	strcpy(PNAME, "acq_ctl:");

	/* get operator command line inputs */
	p_input = &input;

	printf("Cmd Line: %s\n", *argv);
	if (cmd_line(argc, argv, p_input) < 0){
		printf("Error in parse routine cmd_line");
		return(0);
	}

	global_i_cpiofd = p_input->input_link;
	if(global_i_cpiofd > 0) {
		printf("Input link = %d", global_i_cpiofd);
	}

	shm_region = p_input->shmem_region;        /* shmem region */
	if(shm_region > 0) {
		printf("Shmem region = %d", shm_region);
	}



	/* See if host name starts with cp or nrs to remove NCF options */
	if(gethostname((char *)LocalHostName, MAXHOSTNAMELEN)) {
		printf("Fail get hostname\n");
		LocalHostName[0] = '\0';
	} else {
		if(!strncmp(LocalHostName, "cp", 2)) {
			global_NCF_flag = 0;
		}
		if(!strncmp(LocalHostName, "nrs", 3)) {
			global_NCF_flag = 0;
		}
		if(p_input->verbose > 0) {
			if(global_NCF_flag > 0) {
		  		printf("%s Assume NCF options\n", PNAME);
			} else {
		  		printf("%s Assume local CP options only\n", PNAME);
			}
		}
	}

	if(p_input->verbose > 0) {
		debug_shm_flag =  DEBUG_YES;
		global_NCF_flag = 1;
	} else {
		debug_shm_flag =  DEBUG_NO;
	}

	/* need to attach to shared memory for global parameters */
	/* get all the shared memory pointers */
	GET_SHMPTR(acq_table,ACQ_TABLE,
		SHMKEY_REGION(ACQ_TABLE_SHMKEY,shm_region),debug_shm_flag);

	/* check if the attach succeeded */
	if (!acq_table) {
		fprintf (stderr,
			"Acquisition shared memory region %d is not allocated\n", shm_region);
		exit (1);
	}

	
	/* Get the variable max links */
	ACQ_GET_ACQ_TABLE_LINK_INFO(acq_table, 0,
		MaxLinks, p_acqtable_tmp, extend_flag);

	flag = 0;


	if(p_input->input_flag >= 1){
		flag = p_input->input_command;
	}

	if(p_input->shutdown_flag > 0) {
		/* Assume operator has specified shutdown of acq */
		flag = STOP_ALL;
		printf("Operator has specified shutdown of acq");
	}

	if((flag > LAST_COMMAND) || (flag <= 0) || (p_input->query_mode > 0)){
		printf("Another command is required\n");
		do_new_menu();
		for(;;) {
		  /* Select menu options */
		  if(global_i_cpiofd < 0){
		  	printf("\n%s Select link(ALL[0-%d]) option[0-%d]?", PNAME, MaxLinks, LAST_COMMAND);
		  } else {
		  	printf("\n%s Select link(%d) option[0-%d]?", PNAME,global_i_cpiofd, LAST_COMMAND);
		  }
		  fgets(in_line, MAX_INPUT_STRING, stdin);

		  flag = 0;
		  if((in_line[0]!='\0')) {
		  	printf("Input command: %s", in_line);
			flag = atoi(in_line);
			if((flag > LAST_COMMAND) || (flag < 0)){
				printf("\n%s Invalid option[%d]?",
					PNAME, flag);
				printf("Invalid option[%d]",
					flag);
				do_new_menu();
				continue;
			}
			if(flag == PRINT_MENU) {
				printf("Flag is equal to PRINT_MENU");
				do_new_menu();
				continue;
			}
			if(flag == STOP_ALL) {
				break;
			} else {
				printf("Perform action: %d\n", flag);
				acqctl_do_action(flag, p_input, acq_table);
			}
		  } else {
			break;
		  }
		} /* end for(;;) loop */

	} else {
		if(p_input->input_command_count > 1) {
			for(ii=0; ii<p_input->input_command_count; ii++) {
				p_input->input_command = p_input->input_command_list[ii];
				flag = p_input->input_command;
				acqctl_do_action(flag, p_input, acq_table);
			}
		} else {
			acqctl_do_action(flag, p_input, acq_table);
		}
	}

	/* call start routine to do all the work */

	printf("acq_ctl: Done execute\n");

	return(0);

}

/******************************************************************************
	Name
		acqctl_do_confirm

	Description
		Confirm operator input

	
******************************************************************************/
static int acqctl_do_confirm(char *in_command, INPUT *p_input)
{
	const char FUNCNAME[] = "acqctl_do_confirm";
	char	in_line[MAX_INPUT_STRING];

	in_line[0] = '\0';

	if(p_input->force_mode > 0) {
		return(0);
	}

	printf("%s Are you sure want to %s [Y/N]?",
		PNAME, in_command);
	fgets(in_line, MAX_INPUT_STRING, stdin);
	printf("Returned confirmation is: %s",in_line);

	if((in_line[0]!='Y') && in_line[0]!='y') {
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		return(ERROR);
	}
	printf("%s OK perform %s\n",
		PNAME, in_command);
	return(0);
} /* end routine acqctl_do_confirm() */

/******************************************************************************
	Name
		acqctl_do_caution

	Description
		Confirm operator input

	
******************************************************************************/
static int acqctl_do_caution(char *in_command, INPUT *p_input)
{
	const char FUNCNAME[] = "acqctl_do_caution";
	char	in_line[MAX_INPUT_STRING];

	in_line[0] = '\0';

	if(p_input->force_mode > 0) {
		return(0);
	}

	printf("%s Note, this action could cause unpredictable results!\n",
		PNAME);
	printf("%s Are you sure want to %s [Y/N]?",
		PNAME, in_command);
	fgets(in_line, MAX_INPUT_STRING, stdin);
	printf("Returned confirmation is: %s",in_line);

	if((in_line[0]!='Y') && in_line[0]!='y') {
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		return(ERROR);
	}
	printf("%s OK perform %s\n",
		PNAME, in_command);
	return(0);
} /* end routine acqctl_do_caution() */

/******************************************************************************
	Name
		acqctl_get_response

	Description
		Get operator response

	
******************************************************************************/
static int acqctl_get_response(
		INPUT *p_input,
		char *in_command, 
		int in_question,
		int *rtn_code)
{
	const char FUNCNAME[] = "acqctl_get_response";
	char	in_line[MAX_INPUT_STRING];

	in_line[0] = '\0';

	switch(in_question) {
	    case ASK_YES_NO: {
		printf("%s Input %s[Y(y),N(n),cancel(cr)][No]?",
			PNAME, in_command);
		if(p_input->input_option[0] != '\0') {
			strcpy(in_line, p_input->input_option);
			printf("%s\n", in_line);
		} else {
			fgets(in_line, MAX_INPUT_STRING, stdin);
		}
		printf("Response to ASK_YES_NO is: %s",in_line);
		if((in_line[0]=='Y') || in_line[0]=='y') {
			printf("%s Input Yes command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_YES;
			return(0);
		}
		if((in_line[0]=='N') || in_line[0]=='n') {
			printf("%s Input No command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_NO;
			return(0);
		}
		/* Set default for no */
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		*rtn_code = INPUT_NONE;
	    break;
	    }
	    case ASK_YES_NO_DFLT_YES: {
		printf("%s Input %s[Y(y),N(n)][Yes(cr)]?",
			PNAME, in_command);
		if(p_input->input_option[0] != '\0') {
			strcpy(in_line, p_input->input_option);
			printf("%s\n", in_line);
		} else {
			fgets(in_line, MAX_INPUT_STRING, stdin);
		}
		printf("Response to ASK_YES_NO_DFLT_YES is: %s",in_line);
		if((in_line[0]=='Y') || in_line[0]=='y') {
			printf("%s Input Yes command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_YES;
			return(0);
		}
		if((in_line[0]=='N') || in_line[0]=='n') {
			printf("%s Input No command so CANCEL %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_NO;
			return(0);
		}
		/* Set default for yes */
		*rtn_code = INPUT_NONE;
	    break;
	    }
	    case ASK_SET_CLEAR: {
		printf("Question type is ASK_SET_CLEAR");
		printf("%s Input %s[Set(s),Clear(c),cancel(cr)]?",
			PNAME, in_command);
		if(p_input->input_option[0] != '\0') {
			strcpy(in_line, p_input->input_option);
			printf("%s\n", in_line);
		} else {
			fgets(in_line, MAX_INPUT_STRING, stdin);
		}
		printf("Response to question is: %s",in_line);
		if((in_line[0]=='S') || in_line[0]=='s') {
			printf("%s Input Set command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_SET;
			return(0);
		}
		if((in_line[0]=='C') || in_line[0]=='c') {
			printf("%s Input Clear command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_CLEAR;
			return(0);
		}
		/* Set default for no */
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		*rtn_code = INPUT_NONE;
	    break;
	    }
	    case ASK_SUSPEND_RESUME: {
		printf("Question type is ASK_SUSPEND_RESUME");
		printf("%s Input %s[Suspend(s),Resume(r),cancel(cr)]?",
			PNAME, in_command);
		if(p_input->input_option[0] != '\0') {
			strcpy(in_line, p_input->input_option);
			printf("%s\n", in_line);
		} else {
			fgets(in_line, MAX_INPUT_STRING, stdin);
		}
		printf("Response to question is: %s",in_line);
		if((in_line[0]=='S') || in_line[0]=='s') {
			printf("%s Input Suspend command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_SUSPEND;
			return(0);
		}
		if((in_line[0]=='R') || in_line[0]=='r') {
			printf("%s Input Resume command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_RESUME;
			return(0);
		}
		/* Set default for no */
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		*rtn_code = INPUT_NONE;
	    break;
	    }
	    case ASK_DISCARD_RESUME: {
		printf("%s Input %s[Discard(d),Resume(r),cancel(cr)]?",
			PNAME, in_command);
		if(p_input->input_option[0] != '\0') {
			strcpy(in_line, p_input->input_option);
			printf("%s\n", in_line);
		} else {
			fgets(in_line, MAX_INPUT_STRING, stdin);
		}
		printf("Response to ASK_DISCARD_RESUME is: %s",in_line);
		if((in_line[0]=='D') || in_line[0]=='d') {
			printf("%s Input Discard command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_DISCARD;
			return(0);
		}
		if((in_line[0]=='R') || in_line[0]=='r') {
			printf("%s Input Resume command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_RESUME;
			return(0);
		}
		/* Set default for no */
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		*rtn_code = INPUT_NONE;
	    break;
	    }
	    case ASK_ENABLE_DISABLE: {
		printf("%s Input %s[Enable(e),Disable(d),cancel(cr)]?",
			PNAME, in_command);
		if(p_input->input_option[0] != '\0') {
			strcpy(in_line, p_input->input_option);
			printf("%s\n", in_line);
		} else {
			fgets(in_line, MAX_INPUT_STRING, stdin);
		}
		printf("Response to ASK_ENABLE_DISABLE is: %s",in_line);
		if((in_line[0]=='E') || in_line[0]=='e') {
			printf("%s Input Enable command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_ENABLE;
			return(0);
		}
		if((in_line[0]=='D') || in_line[0]=='d') {
			printf("%s Input Disable command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_DISABLE;
			return(0);
		}
		/* Set default for no */
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		*rtn_code = INPUT_NONE;
	    break;
	    }
	    case ASK_OPEN_CLOSE: {
		printf("%s Input %s[Close(c),Open(o),cancel(cr)]?",
			PNAME, in_command);
		if(p_input->input_option[0] != '\0') {
			strcpy(in_line, p_input->input_option);
			printf("%s\n", in_line);
		} else {
			fgets(in_line, MAX_INPUT_STRING, stdin);
		}
		printf("Response to ASK_OPEN_CLOSE is: %s",in_line);
		if((in_line[0]=='C') || in_line[0]=='c') {
			printf("%s Input Close command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_CLOSE;
			return(0);
		}
		if((in_line[0]=='O') || in_line[0]=='o') {
			printf("%s Input Open command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_OPEN;
			return(0);
		}
		/* Set default for no */
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		*rtn_code = INPUT_NONE;
	    break;
	    }
	    case ASK_ADD_DROP: {
		printf("%s Input %s[Drop(d),Add(a),cancel(cr)]?",
			PNAME, in_command);
		if(p_input->input_option[0] != '\0') {
			strcpy(in_line, p_input->input_option);
			printf("%s\n", in_line);
		} else {
			fgets(in_line, MAX_INPUT_STRING, stdin);
		}
		printf("Response to ASK_ADD_DROP is: %s",in_line);
		if((in_line[0]=='D') || in_line[0]=='d') {
			printf("%s Input Drop command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_DROP;
			return(0);
		}
		if((in_line[0]=='A') || in_line[0]=='a') {
			printf("%s Input Open command %s\n",
				PNAME, in_command);
			*rtn_code = INPUT_ADD;
			return(0);
		}
		/* Set default for no */
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		*rtn_code = INPUT_NONE;
	    break;
	    }
	    default:
		printf("Invalid question for command=%s",in_command);
		printf("%s ERROR invalid question for command=%s\n",	
			PNAME, in_command);
		/* Set default for no */
		printf("%s CANCEL command %s\n",
			PNAME, in_command);
		*rtn_code = INPUT_NONE;
		break;
	} /* end switch */

	return(0);

} /* end routine acqctl_get_response() */

/******************************************************************************
	Name
		do_new_menu

	
******************************************************************************/
int
do_new_menu()
{
	const char FUNCNAME[] = "do_new_menu";
	/* Use this to skip first menu item */
	/* printf(" %-5.5s%-33.33s", "",""); */

	printf(" Usage %s  help     HIT return to exit\n", PNAME);
	printf("---------- GENERAL CONTROL ---------------- \n");
	printf(" %2.1d - %-33.33s", 
		PRINT_MENU, NAME_PRINT_MENU);

	printf(" %2.1d - %-33.33s", 
		SET_DISCARD_READER, NAME_SET_DISCARD_READER);
	return(0);
} /* end routine do_new_menu */


/******************************************************************************
	Name
		acqctl_do_action

	
******************************************************************************/
int
acqctl_do_action(
	int		flag,
	INPUT		*p_input,	/* input parameters */
	ACQ_TABLE	*p_acqtable) 	/* pointer to acq_table */
{
	const char FUNCNAME[] = "acqctl_do_action";
	int	ii,iii;
	int	exit_flag;
	int	debug_flag=0;

	ACQ_TABLE	*p_acqtable_tmp; 	/* pointer to acq_table */

	int		log_product_flag;		/* log product flag */	

	char	in_line[MAX_INPUT_STRING];		/* operator input */
	int	temp_i_cpiofd;		/* temp cpiofd */
	int	temp_i_wait_connect_interval;	/* temp wait connect interval */
	int	temp_i_wait_delay_xmit_interval;	/* temp wait xmit rqst interval */
	int	temp_i_set_alarm_reader_interval;	/* temp wait xmit rqst interval */
	int	temp_i_proc_frame_min_compr_len;	/* min len for frame compress in bytes */
	int	temp_i_max_prod_per_sec;	/* temp max_prod_per_sec  */
	char	LINK_NAME[16];		/* link range */
	int	temp_save_value;	/* temp save value */
	int	ii_save=0;		/* save ii value */
	int	iii_save=0;		/* save iii value */

	int	done_flag;		/* done flag */
	int	skip_flag;		/* skip flag */

	int	ilink2;			/* link counter */
	int	ilink_save=0;		/* link counter */

	int	ilink;			/* link counter */
	int	ilink_1st;		/* first link */
	int	ilink_last;		/* last link */

	int	ihost_1st;		/* first host */
	int	ihost_last;		/* last host */
	char	HOST_NAME[16];	/* host range char */
	pid_t	client_pid;		/* client pid */

	int	igroup_1st;		/* first group */
	int	igroup_last;		/* last group */
	char	GROUP_NAME[16];	/* group range char */

	int	print_flag;		/* print flag */

	int	rtn_code;		/* return code from operator input */

	int	list_cnt;		/* list count */
	int	dist_hdr_cnt;	/* dist hdr count */

	if(global_i_cpiofd < 0) {
		ilink_1st = 0;
		ilink_last = MaxLinks;
		sprintf(LINK_NAME,"links[%d-%d]",
			ilink_1st, (ilink_last-1));
	} else {
		if(global_i_cpiofd >= MaxLinks) {
			global_i_cpiofd = MaxLinks;
		}
		/* Changed back to global_i_cpiofd LEK 9Jul01 */
		ilink_1st = global_i_cpiofd;
		ilink_last = global_i_cpiofd + 1;
		sprintf(LINK_NAME,"link[%d]",
			ilink_1st);
	}

	ilink = ilink_1st;

	switch(flag) {

	    case SET_DISCARD_READER: {
		printf("Command: SET_DISCARD_READER");
		for(ilink=ilink_1st; ilink<ilink_last; ilink++) {
		    if(acq_table[ilink].pid <= 0){
		   	    printf("%s NOTE - readr[%d] not active\n", 
				PNAME, ilink);
		    }
		    printf("%s Current mode link[%d] discard_reader=%s[0x%x]\n",
			PNAME, ilink,
			(acq_table[ilink].read_distrib_enable & 
				READ_CTL_DISCARD)?
			"DISCARD":"NORMAL",
			acq_table[ilink].read_distrib_enable);
		} /* end for() */
		acqctl_get_response(p_input, NAME_SET_DISCARD_READER, ASK_DISCARD_RESUME, 
			&rtn_code);
		if(rtn_code == INPUT_DISCARD) {
			/* Set suspend bit in shared memory control */
			for(ilink=ilink_1st; ilink<ilink_last; ilink++) {
				acq_table[ilink].read_distrib_enable |=
					READ_CTL_DISCARD;
			}
		}
		if(rtn_code == INPUT_RESUME) {
			/* Clear suspend bit in shared memory control */
			for(ilink=ilink_1st; ilink<ilink_last; ilink++) {
				acq_table[ilink].read_distrib_enable &=
					~(READ_CTL_DISCARD);
			}
		}
		if(rtn_code == INPUT_NONE) {
			/* Do nothing */
		}
		for(ilink=ilink_1st; ilink<ilink_last; ilink++) {
		    printf("%s Current mode link[%d] discard_reader=%s[0x%x]\n",
			PNAME, ilink,
			(acq_table[ilink].read_distrib_enable & 
				READ_CTL_DISCARD)?
			"DISCARD":"NORMAL",
			acq_table[ilink].read_distrib_enable);
		} /* end for() */
		return(0);
		/* break */
		}
	    default:
		printf("%s NOT IMPLEMENTED command=%d [link=%d]\n",	
			PNAME, flag, global_i_cpiofd);
		break;
	} /* end switch(flag) */

	return(0);
} /* end acqctl_do_action */




/*****************************************************************************
	Name
		acqctl_get_pid

	Function
		Input command routine 
*****************************************************************************/
/* command line input routine for pipes, etc */
static int
acqctl_get_pid(
	int		client_id,		/* client id */
	pid_t client_pid)		/* client pid */
{
    static  char FNAME[]="get_pid";
	char	*PGM_NAME = "acq_client";

#ifdef HPRT
#define PSCOMMAND "ps -ax"
#else
#define PSCOMMAND "ps -e"
#endif

	FILE *fp;
/*	char buf[BUFSIZ]; */
	char buf[4096];
	pid_t this_pid = 0;
	pid_t last_pid = 0;
	int	pid_flag = -1;

	if (!(fp = popen(PSCOMMAND, "r"))) {
		printf("%s %s FAIL %s popen %d, %s\n",
				PNAME, PNAME, PSCOMMAND, errno, (char *)strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp)) {

		if (strstr(buf, PGM_NAME)) {
			this_pid = atol(buf);
			if(client_pid == this_pid) {
				/* Found it so can return */
				last_pid = this_pid;
				pid_flag = this_pid;
				printf("%s %s OK found host_id=%d pid=%d name=%s\n", 
					PNAME, FNAME, client_id, this_pid, PGM_NAME); 
				break;
			}
		}
	} /* end while */

	if(pid_flag <= 0) {
		printf("%s %s not found pid=%d name=%s\n", 
			PNAME, FNAME, client_pid, PGM_NAME); 
		pclose(fp);
		return -1;
	}

	pclose(fp);
	return (int) last_pid;

} /* end routine acqctl_get_pid */

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
	const char FUNCNAME[] = "cmd_line";
	int	c;
	int ii;	/* loop counter */
	char	input_string[80];
	char	*p_in_args;		/* char ptr */
	char	*p_next_args;	/* char ptr */

	/* input initialization */
	p_input->input_link = 0;	/* input link */
	p_input->verbose = 0;		/* verbose mode */
	p_input->shmem_region = 0;	/* shared mem region (0,1,2,etc) */
	p_input->query_mode = 0;	/* query mode */
	p_input->force_mode = 0;	/* force mode */
	p_input->input_flag=0;		/* input from operator flag */
	p_input->shutdown_flag=0;	/* input shutdown operator flag */
	p_input->input_command = 0;	/* input command */
	p_input->input_option[0] = '\0'; /* input option */
	p_input->input_host = -1;	/* input host */
	p_input->input_group = -1;	/* input group */
	p_input->input_command_count = 0;	/* command input count */

	while((c=getopt(argc,argv,"i:k:m:g:h:c:FfASKvq")) != EOF) {
	    p_input->input_flag=1;
  	    printf("getopt returned %c\n", (char)c);
	    switch(c) {
		case 'i': {
			strncpy(p_input->input_option,optarg, LEN_INPUT_OPTION);
			break;
		}
		case 'k': {
			p_input->input_link = atoi(optarg);
			if ((p_input->input_link < 0) || 
				(p_input->input_link >= MAX_LINKS)) {
	    		    printf("%s link range(%d to %d)\n",
				PNAME, 0, MAX_LINKS-1);
				usage();
			}
			break;
		}
		case 'm': {
			p_input->shmem_region = atoi(optarg);
			if ((p_input->shmem_region < 0) ||
				(p_input->shmem_region >= MAX_SHMREGIONS)) {
				printf("%s Memory region must be between 0 and %d\n",
					PNAME, MAX_SHMREGIONS-1);
					usage();
			}
			printf("%s Shm region set to %d\n",
				PNAME, p_input->shmem_region);
			break;
		}
		case 'A': {
			/* Assume want all links */
			p_input->input_link = -1;
			printf("%s Operator input: -A[all links(0-%d)]\n",
				PNAME, MAX_LINKS-1);
			break;
		}
		case 'S': {
			/* Assume to do shutdown of acq */
			p_input->shutdown_flag = 1;
			printf("%s Operator input: -S[shutdown]\n",
				PNAME);
			break;
		}
		case 'K': {
			/* Assume to do shutdown of acq */
			p_input->kill_flag = 1;
			printf("%s Operator input: -K[kill]\n",
				PNAME);
			break;
		}
		case 'c': {
			strncpy(input_string, optarg, 79);
			printf(" input is %s\n", input_string);
			/* strcpy(input_string, p_input->input_command); */
			p_in_args = &input_string[0];
			/* check for max input commands */
			for(ii=0;ii<MAX_INPUT_CMDS;ii++) {
				p_next_args = (char *)strchr(p_in_args, ',');
				if(p_next_args != (char *)NULL) {
					p_next_args[0] = '\0';
					if(p_next_args && (p_in_args[0] != '\n') && (p_in_args[0] != '\r')) {
						p_input->input_command_list[ii] = atoi(p_in_args);
						if(p_input->input_command_count == 0) {
							/* get first command */
							p_input->input_command = p_input->input_command_list[ii];
						}
						p_input->input_command_count++;
					} else {
						/* Invalid option */
						printf("%s invalid arg list error inputs(%d) exit\n",
							PNAME, ii);
						return(-1);
					}
					
				}
				if(!p_next_args) {
					if(p_input->input_command_count > 0) {
						/* Assume have last command */
						p_input->input_command_list[ii] = atoi(p_in_args);
						p_input->input_command_count++;
					} else {
						p_input->input_command_count++;
						p_input->input_command = atoi(optarg);
						printf("%s Operator input: -c%s[%d]\n",
							PNAME, optarg, p_input->input_command);
					}
					break;
				}
				p_in_args = p_next_args+1;
			} /* end for */
	
			/*
			p_input->input_command = atoi(optarg);
			printf("%s Operator input: -c%s[%d]\n",
				PNAME, optarg, p_input->input_command);
			*/
			break;
		}
		case 'v': {
			p_input->verbose = 1;
			printf("%s Set mode verbose\n", PNAME);
		    	break;
		}
		case 'F': 
		case 'f': {
			p_input->force_mode = 1;
			if(p_input->verbose > 0) {
				printf("%s Set force (no input) mode\n", PNAME);
			}
		    break;
		}
		case 'q': {
			p_input->query_mode = 1;
			printf("%s Set mode query\n", PNAME);
		    	break;
		}
		case '?': {
			usage();
			break;
		}
		default:
		        break; 
	    }  /* end switch */

	}  /* end while */

	return(0);
}   /* end of cmd_line routine */

void usage(void) {
	const char FUNCNAME[] = "usage";

	printf("Usage: %s", PNAME);
	printf(" [-k <link>] [-g <group>] [-h <host>]\n"
		"                [-c <command_list,...> [-i <command input>]\n"
		"                [-A (all links)] [-S (shutdown)]\n"
		"                [-v (verbose)] [-q (query_mode)]\n"
		"                [-F (force no input)]\n"
		"                [-m <0,1,2,etc shmem_region>]\n");
	printf("Exiting usage\n");
	exit(0);
} /* end usage routine */
/*****************************************************************************/


