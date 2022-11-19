#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "retrans.h"
#include "log.h"

#define logMsg	printf

CPIO_TABLE cpio_tbl = {
"224.0.1.1", 0x000,0,
"224.0.1.2", 0x010,1,
"224.0.1.3", 0x020,2,
"224.0.1.4", 0x030,3,
"224.0.1.5", 0x040,4,
"224.0.1.6", 0x050,5,
"224.0.1.7", 0x060,6,
"224.0.1.8", 0x070,7,
"224.0.1.9", 0x080,8,
"224.0.1.10",0x090,9
};

ulong              total_prods_retrans_rcvd;     /* prods retrans rcvd by proc */
ulong              total_prods_retrans_rcvd_lost; /* prods retrans rcvd lost */
ulong              total_prods_retrans_rcvd_notlost; /* prods rcvd not lost */
ulong              total_prods_retrans_rqstd;    /* prods retrans requested */
int                retrans_xmit_enable;
char               transfer_type[10];
char               sbn_channel_name[13];
int                sbn_type;
char               mcastAddr[16];
long               global_cpio_addr;
PROD_RETRANS_TABLE *p_prod_retrans_table;
char               log_buff[MAX_LOG_DATA];
BUFF_HDR           *buff_hdr;

static int         global_retransmitpipe_fd;
static char        *global_time_zone;

/* get_cpio_addr -  Routine to get cpio address */
int get_cpio_addr(char *addr){
	int i_row;
	for(i_row=0; i_row < NUM_CPIO_ENTRIES ; i_row++){
		if(!strcmp(cpio_tbl[i_row].mcast_addr,addr)){
			break;
		}
	}	
	if(i_row >= NUM_CPIO_ENTRIES){
		i_row = -1;
		log_error_q("\n Fail to find match for cpio addr=%s\n",addr);
	}
        log_debug("returning i_row = %d",i_row);
	return (i_row);
}


/* init_retrans - Routine to initialize retransmission table */
int init_retrans(PROD_RETRANS_TABLE **pp_prod_retrans_table)
{

	static char FNAME[] = "init_retrans";
	long int	entry_base;		/* base of entry */
	long int	ii,iii;			/* loop counter */
	int	 flags, rtn_val;
	static const char *utc_time = "UTC";

	/* Pointer to prod_retrans table */
	PROD_RETRANS_ENTRY *p_retrans_entry;
	PROD_RETRANS_ENTRY_INFO *p_retrans_entry_info;

	/* pointer to prod_retrans_table */
	PROD_RETRANS_TABLE	*pl_prod_retrans_table;

	pl_prod_retrans_table = *pp_prod_retrans_table;

	log_debug("%s Begin init retrans_table   base=0x%x\n", FNAME, pl_prod_retrans_table);

	global_time_zone = (char *) utc_time;

	if (pl_prod_retrans_table != NULL){
		/* IMPORTANT, special init for local prod_retrans_table */

		/* Assume entries directly follow the entry info for all links */
		/*  and each is variable size */
		/* IMPORTANT, these must be initialized in order of their index value */
		/*   ie, GOES_EAST(0), NMC2(1), NMC(2), NOAAPORT_OPT(3), NMC3(4), NMC1(5) */
		/*    note NMC2 was GOES_WEST */
		/*   as per RETRANS_TBL_TYP_xxxxxxx */
		entry_base = (long)pl_prod_retrans_table + sizeof(PROD_RETRANS_TABLE);

		ii = 0; /* For now set to 0; Later can setup retrans table depending on the channel type */
		pl_prod_retrans_table->entry_info[GET_RETRANS_TABLE_TYP(sbn_type)].
			numb_entries = GET_RETRANS_CHANNEL_ENTRIES(sbn_type);
		log_debug("%s Total retrans numb_entries for channel %s of sbn_type (%d) = %d \n",
                               FNAME, sbn_channel_name,sbn_type,pl_prod_retrans_table->entry_info[ii].numb_entries);

		pl_prod_retrans_table->entry_info[GET_RETRANS_TABLE_TYP(sbn_type)].
			retrans_entry_base_offset = (long)entry_base -
				(long)(&pl_prod_retrans_table->entry_info[GET_RETRANS_TABLE_TYP(sbn_type)].
					retrans_entry_base_offset);

		entry_base += 
			(long) (pl_prod_retrans_table->entry_info[GET_RETRANS_TABLE_TYP(sbn_type)].
				numb_entries * (sizeof(PROD_RETRANS_ENTRY)));

		/*ii = GET_RETRANS_TABLE_TYP(sbn_type);*/
			p_prod_retrans_table->entry_info[ii].entry_bytes = 
				sizeof(PROD_RETRANS_ENTRY);
			pl_prod_retrans_table->entry_info[ii].index_last = 0;
			pl_prod_retrans_table->entry_info[ii].run_id_last = 0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_rcvd = 0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_lost = 0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_lost_seqno = 0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_lost_abort = 0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_lost_other = 0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_retrans_rcvd = 0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_retrans_rcvd_lost = 0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_retrans_rcvd_notlost=0;
			pl_prod_retrans_table->entry_info[ii].tot_prods_retrans_rqstd = 0;
			pl_prod_retrans_table->entry_info[ii].len_wmo_hdr_max = 
				MAX_WMO_ENTRY_LEN;
			pl_prod_retrans_table->entry_info[ii].len_wmo_hdr_abbrev_max = 
				MAX_RETRANS_LEN_WMO_HDR_ABBREV;
			pl_prod_retrans_table->entry_info[ii].last_wmo_hdr[0] = '\0'; 
			pl_prod_retrans_table->entry_info[ii].last_wmo_loghdr_info[0] = '\0'; 

			p_retrans_entry_info =
				&pl_prod_retrans_table->entry_info[ii];

			p_retrans_entry =
				(PROD_RETRANS_ENTRY *)(((long)&p_retrans_entry_info->
					retrans_entry_base_offset) +
					(long)p_retrans_entry_info->retrans_entry_base_offset);
	
			/* Now clear each entry */
			for(iii=0;
				iii < pl_prod_retrans_table->entry_info[ii].numb_entries; iii++) {
				p_retrans_entry[iii].prod_arrive_time = (long) 0;
				p_retrans_entry[iii].prod_seqno = (long) 0;
				p_retrans_entry[iii].prod_run_id = 0;
				p_retrans_entry[iii].prod_type = 0;
				p_retrans_entry[iii].prod_cat = 0;
				p_retrans_entry[iii].prod_code = 0;
				p_retrans_entry[iii].prod_sub_code = 0;
				p_retrans_entry[iii].prod_status = 0;
				p_retrans_entry[iii].prod_err_cause = 0;
				p_retrans_entry[iii].prod_link_id = 0;
				p_retrans_entry[iii].entry_flag = 0;
				p_retrans_entry[iii].WMO_hdr_abbrev[0] = '\0';
			} /* end for each numb entries */
		log_debug("%s  OK init retrans_table for channel [%s] numb_entries = %d\n",FNAME,
				sbn_channel_name,pl_prod_retrans_table->entry_info[ii].numb_entries);

	} /* end if pl_prod_retrans_table != NULL */


	/*Open retransmit pipe */
	if((global_retransmitpipe_fd = open(DEFAULT_RETRANSMIT_PIPENAME, O_RDWR,0)) <= 0){
		log_error_q("Fail to open %s pipe errno=%d \n",DEFAULT_RETRANSMIT_PIPENAME,errno);
		perror("pipe open err");
		return (-1);
	}

	if((flags = fcntl(global_retransmitpipe_fd, F_GETFL)) < 0){
		log_error_q("Fail fcntl(F_GETFL) %s pipe\n",DEFAULT_RETRANSMIT_PIPENAME);
		/* Continue for now */
	}else{

		flags |= DONT_BLOCK;
		if((rtn_val = fcntl(global_retransmitpipe_fd, F_SETFL, flags)) < 0){
			log_error_q("Fail fcntl(F_SETFL) %s pipe \n",DEFAULT_RETRANSMIT_PIPENAME);
		}

		log_notice_q(" OK open pipe[%d] for %s\n", global_retransmitpipe_fd,DEFAULT_RETRANSMIT_PIPENAME);
	}

	log_debug("%s Exiting  init retrans_table   base=0x%lx\n", FNAME,(unsigned long) pl_prod_retrans_table);
	return(0);

} /* end init_retrans */

int init_acq_table(ACQ_TABLE	*p_acq_table)
{
	if(p_acq_table == NULL){
		printf("Error in allocatiing memory for acq table\n");
		return ERROR;
	}

	p_acq_table->link_id = -1;
	p_acq_table->pid = 0;
	p_acq_table->proc_base_channel_type_last = SBN_TYP_NMC;
	p_acq_table->proc_retransmit_ctl_flag = 0;
	p_acq_table->proc_retransmit_ctl_flag |= ENABLE_RETRANS_GEN_RQST;
	p_acq_table->proc_retransmit_ctl_flag |= ENABLE_RETRANS_XMIT_RQST;
	p_acq_table->proc_retransmit_ctl_flag |= ENABLE_RETRANS_DUP_DISCARD;
	p_acq_table->proc_retransmit_delay_send = DEFAULT_RETRANSMIT_DELAY_SEND; 
	
	p_acq_table->proc_base_prod_seqno_last = 0;
	p_acq_table->proc_orig_prod_seqno_last = 0;
	p_acq_table->proc_prod_run_id = 0;
	p_acq_table->proc_orig_prod_run_id= 0;
	p_acq_table->proc_base_prod_cat_last = 0;
	p_acq_table->proc_base_prod_code_last = 0;
	p_acq_table->proc_base_prod_type_last = 0;

	p_acq_table->proc_tot_prods_handled = 0;
	p_acq_table->read_tot_buff_read = 0;
	p_acq_table->read_frame_tot_lost_errs = 0;
	p_acq_table->proc_tot_prods_lost_errs = 0;
	p_acq_table->proc_tot_prods_retrans_rcvd = 0;	
	p_acq_table->proc_tot_prods_retrans_rcvd_lost = 0;	
	p_acq_table->proc_tot_prods_retrans_rcvd_notlost = 0;
	p_acq_table->proc_tot_prods_retrans_rqstd = 0;
	
	return SUCCESS;
	
}


int init_buff_hdr(BUFF_HDR	*p_buff_hdr)
{
	if(p_buff_hdr== NULL){
		printf("Error in allocatiing memory for buffer header\n");
		return ERROR;
	}

	p_buff_hdr->buff_blks_per_record = 0;
	p_buff_hdr->buff_bytes_record = 0;
	p_buff_hdr->buff_datahdr_length = 0;
	p_buff_hdr->buff_tot_length = 0;
	p_buff_hdr->proc_prod_flag = 0;
	p_buff_hdr->read_channel_type = 0;
	return SUCCESS;


	
}

int do_prod_lost( BUFF_HDR *buff_hdr, ACQ_TABLE *acq_tbl)
{
	long prod_errors;

	if(acq_tbl->proc_base_prod_seqno_last == 0){
		prod_errors = 0;
	}else{
		prod_errors = (buff_hdr->proc_prod_seqno - (acq_tbl->proc_base_prod_seqno_last + 1));
	}


	if(prod_errors <= 0){
		prod_errors = 0;
	}else{
		/* Now generate retransmission request */
		generate_retrans_rqst(acq_tbl,(acq_tbl->proc_base_prod_seqno_last + 1),
		(buff_hdr->proc_prod_seqno-1),
		RETRANS_RQST_CAUSE_RCV_ERR);
	}

	acq_tbl->proc_tot_prods_lost_errs += prod_errors;
	/* Need to log prod_errors */
	
	if(acq_tbl->proc_base_prod_seqno_last == 0){
		log_info_q("LOST=%ld total(%ld) %s prod(%ld) prod_seqno was RESET to 0 \n",
		prod_errors,acq_tbl->proc_tot_prods_lost_errs,
		GET_PROD_TYPE_NAME(buff_hdr->proc_prod_type),buff_hdr->proc_prod_seqno);
	}else{
		log_info_q("LOST=%ld total(%ld) %s prod(%ld) expect prod(%ld)\n",
			prod_errors, acq_tbl->proc_tot_prods_lost_errs,
			GET_PROD_TYPE_NAME(buff_hdr->proc_prod_type),buff_hdr->proc_prod_seqno,
			acq_tbl->proc_base_prod_seqno_last + 1);
	}
	return (0);
}


int generate_retrans_rqst(ACQ_TABLE *p_acqtable, 
		long first_prod_seqno,
		long last_prod_seqno,
		int rqst_cause)
{
	static char FNAME[] = "generate_retrans_rqst";

	static ulong request_numb;
	int prod_lost;
	PIPE_RETRANSMIT_HDR pipe_request_hdr;
	PIPE_RETRANSMIT_HDR *p_pipe_requesthdr;
        size_t bytes_written;
        time_t tmp_time;

	request_numb++;

	prod_lost = (last_prod_seqno - first_prod_seqno) + 1;
	if(prod_lost < 0)
		prod_lost = 0;

	p_acqtable->proc_tot_prods_retrans_rqstd += prod_lost;
	time(&p_acqtable->proc_last_retrans_rqst);

	if(global_retransmitpipe_fd <= 0){
		/* Unable to open/write to pipe */
		log_error_q("Unable to open or write to pipe %s \n",DEFAULT_RETRANSMIT_PIPENAME);
		return (0);
	}
	
	p_pipe_requesthdr = &pipe_request_hdr;

	p_pipe_requesthdr->pipe_ctl_flag = 0;

	if(p_acqtable->proc_retransmit_ctl_flag & ENABLE_RETRANS_XMIT_RQST){
		p_pipe_requesthdr->pipe_ctl_flag |= ENABLE_RETRANS_XMIT_RQST;
	}

	p_pipe_requesthdr->pipe_request_numb = request_numb;
	p_pipe_requesthdr->pipe_link_id = p_acqtable->link_id;
	p_pipe_requesthdr->pipe_channel_type = p_acqtable->proc_base_channel_type_last;
	p_pipe_requesthdr->pipe_first_prod_seqno = (int)first_prod_seqno;
	p_pipe_requesthdr->pipe_last_prod_seqno = (int)last_prod_seqno;
	p_pipe_requesthdr->pipe_run_numb = (int) p_acqtable->proc_prod_run_id;
	p_pipe_requesthdr->pipe_cpio_addr = (int) global_cpio_addr;
	time(&tmp_time);
        p_pipe_requesthdr->pipe_request_time = (int) tmp_time;
	p_pipe_requesthdr->pipe_delay_send = p_acqtable->proc_retransmit_delay_send;
	
	p_pipe_requesthdr->pipe_request_cause = RETRANS_RQST_CAUSE_RCV_ERR;


	log_debug("pipe_request_numb = %ld | ctl_flag = %d | link_id = %d | channel_type = %d | rqst cause = %d \n",
			p_pipe_requesthdr->pipe_request_numb,p_pipe_requesthdr->pipe_ctl_flag,p_pipe_requesthdr->pipe_link_id,
			p_pipe_requesthdr->pipe_channel_type,p_pipe_requesthdr->pipe_request_cause);

	log_debug("cpio addr = %ld | rqst time = %ld | first prod seqno = [%d-%ld] | last prod seqno = %ld | run number = %d | delay_send = %d \n",
			p_pipe_requesthdr->pipe_cpio_addr, p_pipe_requesthdr->pipe_request_time,p_pipe_requesthdr->pipe_first_prod_seqno,first_prod_seqno,
			p_pipe_requesthdr->pipe_last_prod_seqno,p_pipe_requesthdr->pipe_run_numb,p_pipe_requesthdr->pipe_delay_send);

	/* Now write to pipe */
	if((bytes_written = write(global_retransmitpipe_fd,
		p_pipe_requesthdr, sizeof(PIPE_RETRANSMIT_HDR))) !=
			sizeof(PIPE_RETRANSMIT_HDR)) {
		log_error_q("FAIL write#%ld pipe[%d] tot(%ld) %s link[%d] prod(%ld-%ld)\n",
			p_pipe_requesthdr->pipe_request_numb,
			global_retransmitpipe_fd,
			p_acqtable->proc_tot_prods_retrans_rqstd, 
			GET_SBN_TYP_NAME(p_pipe_requesthdr->pipe_channel_type),
			p_pipe_requesthdr->pipe_link_id,
			first_prod_seqno, last_prod_seqno);
	} else {
		if(first_prod_seqno != last_prod_seqno) {
			log_info_q("OK rqst#%ld tot(%ld) %s link[%d] prod(%ld-%ld)\n",
				request_numb,
				p_acqtable->proc_tot_prods_retrans_rqstd, 
				GET_SBN_TYP_NAME(p_pipe_requesthdr->pipe_channel_type),
				p_pipe_requesthdr->pipe_link_id,
				first_prod_seqno, last_prod_seqno);
		} else {
			log_info_q("OK rqst#%ld tot(%ld) %s link[%d] prod(%ld)\n",
				request_numb,
				p_acqtable->proc_tot_prods_retrans_rqstd, 
				GET_SBN_TYP_NAME(p_pipe_requesthdr->pipe_channel_type),
				p_pipe_requesthdr->pipe_link_id,
				last_prod_seqno);
		}
	}

	return(0);

	
}


int prod_retrans_ck(ACQ_TABLE *p_acqtable, BUFF_HDR *p_buffhdr, time_t *orig_arrive_time)
{

	char FNAME[] = "prod_retrans_ck";

	int index_value;
	int match_value;
	int retrans_table_type;
	PROD_RETRANS_ENTRY *p_retrans_entry;
	PROD_RETRANS_ENTRY_INFO *p_retrans_entry_info;
	long orig_prod_seqno;
	long now_prod_seqno;
	long	delta_prod_seqno;	/* delta for prod seqno */
	int		valid_retrans_flag;	/* valid retrans flag for table */


	match_value = PROD_NODUPLICATE;
	*orig_arrive_time = 0;
	
	if(prod_retrans_get_addr(p_acqtable->proc_base_channel_type_last,
		p_prod_retrans_table, &p_retrans_entry_info, &p_retrans_entry, 
		&retrans_table_type) < 0) {
		log_notice_q("%s ignore retrans_ck\n",	FNAME);
		return(match_value);
	}

	if(p_acqtable->proc_orig_prod_seqno_last != 0) {

		log_debug("%s ok retrans channel_typ=%d tbl[%d] so ck more\n",
			FNAME, p_acqtable->proc_base_channel_type_last, retrans_table_type);

		/* Assume have retransmitted product do following */
		p_acqtable->proc_tot_prods_retrans_rcvd++,
		p_retrans_entry_info->tot_prods_retrans_rcvd++;

		/* If is retransmission, need to see if original product was OK */
		/*  and ignore the product, else process the product */
		index_value = p_acqtable->proc_orig_prod_seqno_last % 
			p_retrans_entry_info->numb_entries;
		if(index_value < 0) {
			index_value = -index_value;
		}
		if(index_value >= p_retrans_entry_info->numb_entries) {
			index_value = 0;
		}

		/* Check if already have product in table */
		if((p_retrans_entry[index_value].prod_seqno ==
			p_acqtable->proc_orig_prod_seqno_last) &&
			(p_retrans_entry[index_value].prod_run_id ==
				p_acqtable->proc_orig_prod_run_id) &&
			(p_retrans_entry[index_value].entry_flag != 
				RETRANS_ENTRY_FLAG_AVAIL)) {
			/* Assume product matched */	
			match_value = PROD_DUPLICATE_MATCH;
			p_acqtable->proc_tot_prods_retrans_rcvd_notlost++;
			p_retrans_entry_info->tot_prods_retrans_rcvd_notlost++;
			*orig_arrive_time = p_retrans_entry[index_value].prod_arrive_time;
		} else {
			match_value = PROD_DUPLICATE_NOMATCH;
			p_acqtable->proc_tot_prods_retrans_rcvd_lost++;
			p_retrans_entry_info->tot_prods_retrans_rcvd_lost++;
			
		}

		log_debug("%s %s duplicate run(%d) prod|orig(%ld|%ld) tbl[%d]=%ld\n",
			FNAME,
			(match_value==PROD_DUPLICATE_MATCH)?"OK MATCH":"NO MATCH",
			p_acqtable->proc_orig_prod_run_id,
			p_buffhdr->proc_prod_seqno,
			p_acqtable->proc_orig_prod_seqno_last,
			index_value,
			p_retrans_entry[index_value].prod_seqno);
	
		/* Check if retransmit prod is within range of table entries */
		orig_prod_seqno = (long)p_acqtable->proc_orig_prod_seqno_last;
		now_prod_seqno = (long)p_buffhdr->proc_prod_seqno;

		valid_retrans_flag = 0;
		delta_prod_seqno = now_prod_seqno - orig_prod_seqno;

		if((match_value == PROD_DUPLICATE_MATCH) ||
			(match_value == PROD_DUPLICATE_NOMATCH)) {
			if((delta_prod_seqno > 0) && 
				(delta_prod_seqno < p_retrans_entry_info->numb_entries)) {
				valid_retrans_flag = 1;
			}
		}
		if((match_value == PROD_DUPLICATE_MATCH) ||
			(match_value == PROD_DUPLICATE_NOMATCH)) {
			if((delta_prod_seqno < 0) && 
				(now_prod_seqno < p_retrans_entry_info->numb_entries)) {
					valid_retrans_flag = 1;
			}
		}


		/* Now if the original is within the number of active entries */
		/*  go ahead and update the match table entry */
		/* Note, that p_acqtable->proc_base_prod_seqno is not yet set */
		if((match_value == PROD_DUPLICATE_NOMATCH) &&
			(valid_retrans_flag == 1)){
			/* Now update the prod_retrans_entry */
			prod_retrans_update_entry(p_acqtable, p_buffhdr,
				p_retrans_entry_info,
				&p_retrans_entry[index_value], 
				index_value,
				p_acqtable->proc_orig_prod_seqno_last, 
				p_acqtable->proc_orig_prod_run_id, 
				RETRANS_ENTRY_FLAG_RETRANS_VALID, 0);
			p_retrans_entry_info->index_last = index_value;
			p_retrans_entry_info->run_id_last = 
				p_acqtable->proc_prod_run_id; 
		} else {
			/* Check if retransmit prod is within range of table entries */
			if((match_value == PROD_DUPLICATE_MATCH) &&
				(valid_retrans_flag == 1)){
				/* Now update the prod_retrans_entry */
				/*  to tell other product is ok but duplicate */
				prod_retrans_update_entry(p_acqtable, p_buffhdr,
					p_retrans_entry_info,
					&p_retrans_entry[index_value], 
					index_value,
					p_acqtable->proc_orig_prod_seqno_last, 
					p_acqtable->proc_orig_prod_run_id, 
					RETRANS_ENTRY_FLAG_RETRANS_DUP, 0);

			} else {
				if(p_retrans_entry[index_value].prod_run_id ==
					p_acqtable->proc_orig_prod_run_id) {
					/* Possibly orig prod_seqno greater than current prod_seqno */
					/*  so discard */
				}
			}

			/* Note, prod is possibly too old to fit in table */
			/* Always, set product for discard */
			match_value = PROD_DUPLICATE_DISCARD;

		}

	} else {
		/* If this a new (not retransmitted) product do following */
		match_value = PROD_NODUPLICATE;

	} /* end if-else retransmitted product */


	/* In either case need to update entry for new product */
	index_value = p_buffhdr->proc_prod_seqno % 
			p_retrans_entry_info->numb_entries;
	if(index_value < 0) {
		index_value = -index_value;
	}
	if(index_value >= p_retrans_entry_info->numb_entries) {
		index_value = 0;
	}

	/* IMPORTANT, this value will be removed later if product in error */
	/* Now update the prod_retrans_entry */
	/*  and also indicate if was a retransmission product */
	prod_retrans_update_entry(p_acqtable, p_buffhdr,
		p_retrans_entry_info,
		&p_retrans_entry[index_value], 
		index_value,
		p_buffhdr->proc_prod_seqno,
		p_acqtable->proc_prod_run_id, 
		RETRANS_ENTRY_FLAG_NEW_VALID|
		((p_acqtable->proc_orig_prod_seqno_last != 0)?
			RETRANS_ENTRY_FLAG_NEW_W_DUP:0), 0);

	p_retrans_entry_info->index_last = index_value;
	p_retrans_entry_info->run_id_last = p_acqtable->proc_prod_run_id;

/* Debug only */

	if(match_value & PROD_NODUPLICATE) {
		log_debug(" %s %s entry(%d) prod(%ld) code=%d %s[%d]\n",
			FNAME,
			GET_PROD_TYPE_NAME(p_buffhdr->proc_prod_type),
			index_value,
			p_buffhdr->proc_prod_seqno,
			p_buffhdr->proc_prod_code,
			(match_value & PROD_NODUPLICATE)?"NO_DUPL":
			(match_value & PROD_DUPLICATE_MATCH)?"DUPL_MATCH":
			(match_value & PROD_DUPLICATE_NOMATCH)?"DUPL_NOMATCH":
				"UNKNOWN",
			match_value);
	} else {
		log_debug("%s %s entry(%d) prod|orig(%ld|%ld) code=%d %s[%d]\n",
			FNAME,
			GET_PROD_TYPE_NAME(p_buffhdr->proc_prod_type),
			index_value,
			p_buffhdr->proc_prod_seqno,
			p_acqtable->proc_orig_prod_seqno_last,
			p_buffhdr->proc_prod_code,
			(match_value & PROD_NODUPLICATE)?"NO_DUPL":
			(match_value & PROD_DUPLICATE_MATCH)?"DUPL_MATCH":
			(match_value & PROD_DUPLICATE_NOMATCH)?"DUPL_NOMATCH":
				"UNKNOWN",
			match_value);
	}
/* Debug only */

	return(match_value);

}


int prod_retrans_update_entry(
	ACQ_TABLE 	*p_acqtable,	/* ptr to acquisition table */
	BUFF_HDR	*p_buffhdr,		/* ptr to buff hdr */
	PROD_RETRANS_ENTRY_INFO *p_retrans_entry_info, /* ptr to retrans info */
	PROD_RETRANS_ENTRY *p_retrans_entry, /* ptr to retrans entry */
	int		in_index,			/* entry index */
	long	prod_seqno,			/* prod_seqno */
	ushort	in_run_id,			/* run_id */
	int		entry_flag,			/* retrans entry flag */
	int		err_cause)			/* err cause entry flag */
{
	static char FNAME[]="retrans_update_entry";
	

/* Debug only */
		if(p_buffhdr != (BUFF_HDR *)NULL) {
			log_debug("%s %s prod(%ld) code=%d %s[0x%x] update\n",
				FNAME,
				GET_PROD_TYPE_NAME(p_buffhdr->proc_prod_type),
				prod_seqno,
				p_buffhdr->proc_prod_code,
				(entry_flag & RETRANS_ENTRY_FLAG_AVAIL)?"AVAIL":
				(entry_flag & RETRANS_ENTRY_FLAG_NEW_VALID)?"NEW_VALID":
				(entry_flag & RETRANS_ENTRY_FLAG_RETRANS_VALID)?"RETRANS_VALID":
				(entry_flag & RETRANS_ENTRY_FLAG_RETRANS_DUP)?"RETRANS_DUP":
				(entry_flag & RETRANS_ENTRY_FLAG_NEW_W_DUP)?"NEW_W_DUP":
				"UNKNOWN",
				entry_flag);
		} else {
			log_debug("%s prod(%ld)  %s[0x%x] update\n",
				FNAME,
				prod_seqno,
				(entry_flag & RETRANS_ENTRY_FLAG_AVAIL)?"AVAIL":
				(entry_flag & RETRANS_ENTRY_FLAG_NEW_VALID)?"NEW_VALID":
				(entry_flag & RETRANS_ENTRY_FLAG_RETRANS_VALID)?"RETRANS_VALID":
				(entry_flag & RETRANS_ENTRY_FLAG_RETRANS_DUP)?"RETRANS_DUP":
				(entry_flag & RETRANS_ENTRY_FLAG_NEW_W_DUP)?"NEW_W_DUP":
				"UNKNOWN",
				entry_flag);
		}
/* Debug only */

	/* Check if entry is to be made available again */
	if(entry_flag == RETRANS_ENTRY_FLAG_AVAIL){
		/* Assume entry will no longer be used */
		p_retrans_entry->entry_flag = RETRANS_ENTRY_FLAG_AVAIL; 
		p_retrans_entry->prod_err_cause = err_cause; 
		return(0);
	}

	/* OK just update status since entry already valid */
	if(entry_flag & RETRANS_ENTRY_FLAG_RETRANS_DUP){
		p_retrans_entry->entry_flag |= RETRANS_ENTRY_FLAG_RETRANS_DUP; 
		/* Also, update the arrival time */
		p_retrans_entry->prod_arrive_time = 
			p_acqtable->proc_prod_start_time;	
		/* Note this does not change any other contents of the entry */
		return(0);
	}

	/* IMPORTANT, prod_seqno may either be new or original */
	p_retrans_entry->prod_seqno = prod_seqno;

	/* Use last acqtable values for these */
	p_retrans_entry->prod_run_id = in_run_id;
	p_retrans_entry->prod_arrive_time = 
		p_acqtable->proc_prod_start_time;	

	/* Use current buffhdr values for these */
	if(p_buffhdr != (BUFF_HDR *)NULL) {
		p_retrans_entry->prod_type = p_buffhdr->proc_prod_type;
		p_retrans_entry->prod_cat = p_buffhdr->proc_prod_cat;
		p_retrans_entry->prod_code = p_buffhdr->proc_prod_code;
		p_retrans_entry->prod_sub_code = p_buffhdr->proc_sub_code;
		p_retrans_entry->prod_link_id = p_acqtable->link_id;	
	}

	if(entry_flag & RETRANS_ENTRY_FLAG_NEW_VALID){
		p_retrans_entry->entry_flag = RETRANS_ENTRY_FLAG_NEW_VALID; 
		p_retrans_entry->prod_err_cause = 0; 

		/* Check if need to update status since entry already valid */
		/* Note, leave the prod_err_cause as already set */
		if(entry_flag & RETRANS_ENTRY_FLAG_NEW_W_DUP){
			p_retrans_entry->entry_flag |= RETRANS_ENTRY_FLAG_NEW_W_DUP; 
		} else {
			p_retrans_entry->entry_flag &= ~RETRANS_ENTRY_FLAG_NEW_W_DUP; 
		}
	} /* end if NEW_VALID entry */

	/* Check if updating entry for enclosed retransmitted product */
	/*   so basically have a new entry for a previous product */
	/*   that is enclosed within a new seqno product that was entered */
	/*   (hopefully) as a separate update entry call */
	if(entry_flag == RETRANS_ENTRY_FLAG_RETRANS_VALID){
		p_retrans_entry->entry_flag = RETRANS_ENTRY_FLAG_RETRANS_VALID; 
		p_retrans_entry->prod_err_cause = 0; 
	}

	return(0);

} 

int prod_retrans_get_addr(
	int	channel_type,						/* SBN channel type */
	PROD_RETRANS_TABLE *prod_retrans_table, /* retrans table */
	PROD_RETRANS_ENTRY_INFO **in_p_retrans_entry_info,  /* retrans tab info  */
	PROD_RETRANS_ENTRY **in_p_retrans_entry, 	/* retrans table entry */
	int *in_retrans_table_typ)					/* retrans table type */
{
	static char FNAME[]="prod_retrans_get_addr";

	int		retrans_table_typ;	/* retrans table typ */
	PROD_RETRANS_ENTRY *p_retrans_entry;
	PROD_RETRANS_ENTRY_INFO *p_retrans_entry_info;


	if(prod_retrans_table == (PROD_RETRANS_TABLE *)NULL) {
		log_error_q("%s null prod_retrans_table ptr so give up\n",	FNAME);
		return(ERROR);
	}

	retrans_table_typ = 0;
	p_retrans_entry_info = &prod_retrans_table->entry_info[retrans_table_typ];
	p_retrans_entry = (PROD_RETRANS_ENTRY *)
			(((long) &p_retrans_entry_info->retrans_entry_base_offset) +
				  p_retrans_entry_info->retrans_entry_base_offset);

	*in_p_retrans_entry_info = p_retrans_entry_info;
	*in_p_retrans_entry = p_retrans_entry;
	*in_retrans_table_typ = retrans_table_typ;

	if(p_retrans_entry_info->numb_entries == 0) {
		/* assume have no entries */
		log_error_q("%s OK prod_retrans_table entry_info=0x%x numb_entry=%d\n",
			FNAME,
			(unsigned long)*in_p_retrans_entry_info,
			p_retrans_entry_info->numb_entries);
		return(ERROR);
	}

	return(0);

} /* end prod_retrans_get_addr routine */ 

/**
 * Prints to the end of a buffer. Does not print past the end of the buffer.
 * Ensures a terminating NUL.
 *
 * @param[in] buf   The buffer to be printed/appended to.
 * @param[in] size  The size of the buffer including the terminating NUL.
 * @param[in] fmt   The format specification for the arguments.
 * @param[in] ...   The arguments.
 */
static void catPrint(
        char* const  restrict      buf,
        const size_t               size,
        const char* const restrict fmt,
        ...)
{
    size_t        used = strlen(buf);
    const ssize_t remaining = size - used;

    if (remaining > 1) {
        va_list ap;

        va_start(ap, fmt);
        (void)vsnprintf(buf+used, remaining, fmt, ap);
        va_end(ap);
    }
}

int log_prod_end(char *end_msg, 
		long	in_orig_prod_seqno,
		long 	in_prod_seqno,
		int	 	in_prod_blkno,
		int 	in_prod_code,
		int	 	in_prod_bytes,
		time_t	in_prod_start_time)

{
	static const char FNAME[40+1]="log_prod_end";
#define MAX_WMO_HDR_NAME_LEN 64 /* max wmo hdr name len */

	char	prod_log_buff[256];	/* prod log buffer */
	time_t	now_time;			/* current time */
	struct tm *tmtime;          /* time */
	int		ii;					/* loop counter */
	long	log_ratio;			/* log ratio of compr to total */
static long log_eop_count;  /* counter to track number of log EOP entries */


#define LOG_DH_PROC_STATUS_CNT_INTERVAL 50  /* log status every 50 entries */

	log_eop_count++;

	prod_log_buff[0] = '\0';
	time(&now_time);

	tmtime = (struct tm *)gmtime(&now_time);   /* time */
	catPrint(prod_log_buff, sizeof(prod_log_buff), "%s %s", "END",
			get_date_time(tmtime, global_time_zone));

	if(in_orig_prod_seqno != 0) {
		/* Assume have an original SBN prod seqno */
		catPrint(prod_log_buff, sizeof(prod_log_buff), " #%ld/%d orig(#%ld)",
				in_prod_seqno,
				in_prod_blkno,
				in_orig_prod_seqno);
	} else {
		catPrint(prod_log_buff, sizeof(prod_log_buff), " #%ld/%d",
				in_prod_seqno,
				in_prod_blkno);
	}

	catPrint(prod_log_buff, sizeof(prod_log_buff), " bytes(%d)", in_prod_bytes);
	
	catPrint(prod_log_buff, sizeof(prod_log_buff), " c(%d)", in_prod_code);

	if((now_time - in_prod_start_time) > 0){
		catPrint(prod_log_buff, sizeof(prod_log_buff), " +%lds ",
			(long)(now_time - in_prod_start_time));
		
	}

	if(end_msg[0] != '\0') {
		catPrint(prod_log_buff, sizeof(prod_log_buff), " %s", end_msg);
	}

	/* Finally do the brief product logging */
	log_notice_q("%s \n", prod_log_buff);

    	return(0);
} /* end acqpro_log_prod_end */

char *get_date_time(const  struct tm *p_tm, char *tz)
{
	static char buf[100];
	char *ptr;

	if (p_tm) {
		if (tz) {
			/*
			strftime (buf, sizeof (buf), "%c", p_tm);
			*/
			strftime (buf, sizeof (buf), "%m/%d/%Y %T", p_tm);
			if (!strcmp(tz, "GMT") || !strcmp(tz, "UTC")) {
				/* Skip the GMT or UTC label for time */
			} else {
				ptr = (char *)(buf + strlen(buf));
				snprintf(ptr, buf+sizeof(buf)-ptr, " %s", tz);
			}
		} else {
			strftime (buf, sizeof (buf), "%m/%d/%Y %T %Z", p_tm);
			if (!strstr(buf, "GMT") && !strstr(buf, "UTC")) {
				/* Skip the GMT or UTC label for time */
				strftime (buf, sizeof (buf), "%m/%d/%Y %T", p_tm);
			}
		}
	} else {
		strcpy (buf, "UNKNOWN DATE/TIME");
	}

	return buf;

} /* end get_date_time */


int do_prod_mismatch(ACQ_TABLE *p_acqtable, BUFF_HDR *p_buffhdr)
{
	static char FNAME[] = "do_prod_mismatch";
	
	long	first_err_prod_seqno = 0;	/* first invalid prod seqno */
	long	last_err_prod_seqno;	/* last invalid prod seqno */
	long	prod_errors;			/* product errors */
	long	proc_prod_seqno;		/* prod seqno */
	int		proc_blkno;			/* blkno */
	int		proc_prod_code;		/* prod code */
	int		proc_prod_type;		/* prod type */
	char	log_buff[MAX_LOG_DATA];

	proc_prod_type = p_buffhdr->proc_prod_type;
	proc_prod_seqno = p_buffhdr->proc_prod_seqno;
	proc_blkno = p_buffhdr->proc_blkno;
	proc_prod_code = p_buffhdr->proc_prod_code;
	log_buff[0] = '\0';
	
	if(p_buffhdr->proc_blkno == 0){
		/* Begining of new prod*/
		last_err_prod_seqno = p_buffhdr->proc_prod_seqno - 1;
		}else{
			/*Not begin of new prod,assume new prod in error */
			last_err_prod_seqno = p_buffhdr->proc_prod_seqno;
		}
		
		if(p_acqtable->proc_base_prod_seqno_last == 0){
			prod_errors = -1;
		}else{
			first_err_prod_seqno = p_acqtable->proc_base_prod_seqno_last;
			prod_errors = (last_err_prod_seqno - first_err_prod_seqno) + 1;
		}
		
		if(prod_errors < 0){
			prod_errors = 0;
			}else{
				if(prod_errors > 0){
					/* Clean up retrans table entry and need to abort the prod */
					prod_retrans_abort_entry(p_acqtable, first_err_prod_seqno, RETRANS_RQST_CAUSE_RCV_ERR);
					/* Generate retrans request to send to NCF */
					generate_retrans_rqst(p_acqtable, first_err_prod_seqno, last_err_prod_seqno, RETRANS_RQST_CAUSE_RCV_ERR);
					/* Clear retrans table for the original product as it may appear */
					/* its a duplicate retrans and hence may get discarded */
					/*  Note: This needs to be done only when retrans prod is in error again and again*/
					if(p_acqtable->proc_orig_prod_seqno_last != 0){
						log_debug(" Aborting orig seqno [%ld] in retrans table. Cuurent prod seqno [%ld] \n",
							p_acqtable->proc_orig_prod_seqno_last,proc_prod_seqno);
							prod_retrans_abort_entry(p_acqtable, p_acqtable->proc_orig_prod_seqno_last,
							RETRANS_RQST_CAUSE_RCV_ERR);
					}
					
				}
			}

			p_acqtable->proc_tot_prods_lost_errs += prod_errors;
			if(prod_errors > 0){
				strcpy(log_buff,"ERROR");
				if(p_acqtable->proc_orig_prod_seqno_last != 0){
					if(log_buff[0] == '\0'){
						strcpy(log_buff,"RETRANS");
					}else{
						strcat(log_buff,"/RETRANS");
					}
				}
			}

			log_prod_end(log_buff,p_acqtable->proc_orig_prod_seqno_last,proc_prod_seqno,
				p_buffhdr->proc_blkno,p_acqtable->proc_base_prod_code_last,p_acqtable->proc_prod_bytes_read,p_acqtable->proc_prod_start_time);

			if(p_buffhdr->proc_blkno == 0){
				p_acqtable->proc_base_prod_seqno_last = p_buffhdr->proc_prod_seqno - 1;
			}else{
				p_acqtable->proc_base_prod_seqno_last = proc_prod_seqno;
			}

			return(0);
			
}


int log_prod_lost(long in_prod_errors, long in_tot_prods_lost_errs, long in_prod_seqno)
{
	static  char FNAME[]="log_prod_lost";

	char	prod_log_buff[256];	/* prod log buffer */
	time_t	now_time;		/* now time */
	struct tm *tmtime;          /* time */

	/* "STATUS LOST %ld product(s) total(%ld) before prod(%ld)", */
	snprintf(prod_log_buff, sizeof(prod_log_buff), "STATUS LOST %ld product(s) before prod(%ld) total(%ld)",
		in_prod_errors,
		in_prod_seqno,
		in_tot_prods_lost_errs);

	time(&now_time);

	tmtime = (struct tm *)gmtime(&now_time);   /* time */
	catPrint(prod_log_buff, sizeof(prod_log_buff), " %s",
			get_date_time(tmtime, global_time_zone));

	log_info_q("%s %s \n",get_date_time(tmtime, global_time_zone),prod_log_buff);

	return(0);

}

int prod_retrans_abort_entry (ACQ_TABLE *p_acqtable, long prod_seqno, int err_cause)
{
	static char FNAME[]="retrans_abort_entry";

	int		index_value;		/* index for prod_seqno */
	int		retrans_table_typ;	/* retrans table type */
	PROD_RETRANS_ENTRY *p_retrans_entry;
	PROD_RETRANS_ENTRY_INFO *p_retrans_entry_info;

	if(prod_retrans_get_addr(p_acqtable->proc_base_channel_type_last, p_prod_retrans_table,
		&p_retrans_entry_info, &p_retrans_entry, &retrans_table_typ) < 0){
			log_error_q("%s ignore abort \n",FNAME);
			return(ERROR);
		}

	/* Now get the index value */
	index_value = prod_seqno % 
		p_retrans_entry_info->numb_entries;
	if(index_value < 0) {
		index_value = -index_value;
	}
	if(index_value >= p_retrans_entry_info->numb_entries) {
		index_value = 0;
	}

	log_info_q("%s ok abort %s tbl[%d]=%ld\n",FNAME,
		GET_SBN_TYP_NAME(p_acqtable->proc_base_channel_type_last),
		index_value,
		p_retrans_entry[index_value].prod_seqno);

	prod_retrans_update_entry(p_acqtable, (BUFF_HDR *)NULL,
		p_retrans_entry_info,
		&p_retrans_entry[index_value], 
		index_value,
		prod_seqno, 
		p_acqtable->proc_prod_run_id,
		RETRANS_ENTRY_FLAG_AVAIL, err_cause);

	return(0);
	

}

void freeRetransMem()
{
   /** Release all acquired resources for retransmission purpose **/
   if(retrans_xmit_enable == OPTION_ENABLE){
	if(buff_hdr)
	  free(buff_hdr);
	if(p_prod_retrans_table)
	  free(p_prod_retrans_table);
   }
}

