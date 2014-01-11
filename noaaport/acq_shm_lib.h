/*******************************************************************************
NAME
	name - acq_shm_lib.h 

DESCRIPTION
	shared memory macros

RETURNS

*******************************************************************************/

#ifndef acq_shm_lib_h
#define acq_shm_lib_h


#define SHM_REUSE_YES 1		/* reuse shared memory if possible */
#define SHM_REUSE_NO 0		/* do not reuse shared memory if possible */

#define DEBUG_YES 1		/* debug get of shared memory */
#define DEBUG_NO 0		/* debug get of shared memory */

#ifndef DEBUGGETSHM 
#define DEBUGGETSHM DEBUG_NO 	/* default to no debug of get shmem */
#endif 

#ifndef SHM_REUSE_FLAG 
#define SHM_REUSE_FLAG SHM_REUSE_NO 	/* default to no debug of get shmem */
#endif

#define ACQ_TABLE_SHMKEY 2121 	/* shared memory key for UNIX */

#define	ENABLE_FLAG	0
#define	DISABLE_FLAG	1
#define READ_CTL_DISCARD DISABLE_FLAG 

/* #define RETRANS_TABLE_SHMKEY 2138 	 shared memory key for UNIX */

char *get_shm_ptr(int SHMnumber, char *name, int flag);
char *com_shmAlloc(int size, int SHMnumber);
int com_shmIsAlloc(int SHMnumber);
int com_shmFree(int SHMnumber);
int  com_shmDetach(char *address);



#define GET_SHMEM(a,b,c,d)	LEAN_GET_SHMEM(a,b,c,d,SHM_REUSE_FLAG)

#define LEAN_GET_SHMEM(a,ss,z,k,r) { \
	/* allocate and set a shared memory segment if required or requested */ \
    if (!(r) || !com_shmIsAlloc(k)) { \
		if ((a = \
			(char *)com_shmAlloc(sizeof(ss[z]), k)) == (char *)NULL) { \
	 		printf("setup: Key=%d ret(0x%x) com_shmAlloc " \
					"size(%d bytes) FAILED\n", \
					k, a, sizeof(ss[z])); \
			exit(99); \
		} \
		ii=sizeof(ss[z]); \
		iii=z; \
      	printf("setup: Key=%d alloc shm OK at(0x%x) (%d/%d bytes) s[%d]\n", \
		k, \
		a, sizeof(ss[z]),ii/iii,z); \
		/* fill it and detach it */ \
		memset(a,0,sizeof(ss[z])); \
		com_shmDetach(a); \
	} else { \
		if ((a = (char *)get_shm_ptr(k,#a,DEBUGGETSHM)) == (char *)NULL) { \
			printf("setup: KEY %d Get shm FAILED \n", k); \
		} \
		memset(a,0,sizeof(ss[z])); \
	} \
}

#define GET_SHMPTR(a,s,k,f){ \
       if((a = (s *)get_shm_ptr(k,#a,f)) == (s *)NULL) { \
	   if(f == DEBUG_YES) { \
	   	printf("setup: KEY %d Get shm ptr FAILED \n", k); \
	   } \
	} \
}

#define GET_SHMPTR_NOEXIT(a,s,k,f){ \
       if((a = (s *)get_shm_ptr(k,#a,f)) == (s *)NULL) { \
	   printf("setup: KEY %d Get shm FAILED \n", k); \
	} \
}

#define GET_SHMPTR_DEBUG(a,s,k,f){ \
       if((a = (s *)get_shm_ptr(k,#s,f)) == (s *)NULL) { \
	   if(f == DEBUG_YES) { \
	   	printf("setup: KEY %d Get shm ptr FAILED \n", k); \
	   } \
	    exit(0); \
	} \
}

#define FREE_SHMEM(k){ \
	if (com_shmFree(k)) { \
		printf("free: Key %d memory does not exist\n", k); \
	} else { \
		printf("free: Key %d free shared memory OK\n", k); \
	} \
}

/* Compute and return the new key */ 
#define FIND_SHMKEY_REGION(kk,k,m){ \
	kk = k + (m * 10000);\
	printf("find_shmkey: KEY now %d base(%d) region(%d)\n",kk,k,m); \
}

/* Compute the new key */ 
#define SHMKEY_REGION(k,m) (k + (m * 10000))

#endif

