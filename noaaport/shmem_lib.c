/*******************************************************************************
NAME
	name -  shmem_lib.c

DESCRIPTION

RETURNS

*******************************************************************************/
#include <config.h>

#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#define DEBUG(x) printf("service: x = %d\n", (x))

#define logError(x) fprintf(stderr, "%s\n", x)
char *com_shmAttach(int SHMnumber);
int com_shmIsAlloc(int SHMnumber);


char *
get_shm_ptr(SHMnumber,name,flag) 
	int SHMnumber;		/* shared memory number to validate */
	char *name;		/* name for shared memory */
	int	flag;		/* print flag 0=no, >0 yes */
	
{
	char *address;

	/* does the memory segment really exist */
	if (!com_shmIsAlloc(SHMnumber)) {
		if(flag) {
			fprintf(stderr,
				"getshmptr: SHMnumber Key=%d shmem not allocated for %s\n", 
				SHMnumber, name);
		}
		return((char *)NULL);
		/* exit(1); */
	}

	if ((address = (char *)com_shmAttach(SHMnumber)) == (char *)NULL) {
		fprintf(stderr, "getshmptr: SHMnumber Key=%d cannot attach for %s\n", 
			SHMnumber, name);
		return((char *)NULL);
		/* exit(2); */
	}

	/* print */
	if(flag) {
		fprintf(stderr, "getshmptr: SHMnumber Key=%d addr(0x%lx) for %s\n",
			SHMnumber,(unsigned long)address,name);
	}

	return(address);
}
#ifdef LEK
int
taskNameToId(name)
	char *name;
	{
		return(getpid());
	}

int
	taskDelay(ticks)
	int     ticks;
	{
		sleep(ticks/60);
		return(0);
	}
#endif 


/*******************************************************************************
NAME
	com_shmAttach - Attach to an Allocated Shared Memory Segment

DESCRIPTION
	A unique well-known key is used to attach to an existing segment.
	Every process that wishes to access shared memory allocated elsewhere
	must perform this call.

RETURNS
	Returns the adress of the segment or NULL.

*******************************************************************************/


char *
com_shmAttach(SHMnumber)	/* return address of segment or NULL */
int SHMnumber;		/* shared memory segment key */
{
	register char *address;
	register int shmid;
	char	errbuf[80];


	/* obtain the identifier of an existing segment */
	if ((shmid = shmget(SHMnumber, 0, 0666)) == -1) {
		sprintf(errbuf, "com_shmAttach() shmget - %s", strerror(errno));
		logError(errbuf);
		return((char *)0);
	}
	
	/* attach a shared memory area */
	/* convert the identifier to an address */
	if ((address = shmat(shmid, (char *)0, 0)) == (char *)-1) {
		sprintf(errbuf, "com_shmAttach() shmat - %s", strerror(errno));
		logError(errbuf);
		return((char *)0);
	}
	else
		return(address);
}

/*******************************************************************************
NAME
	com_shmIsAlloc - Determine if a Key is Allocated.

DESCRIPTION
	Each shared memory segment is allocated with a unique key, the
	SHMnumber that is unique to the host and well-known to allocator
	and user.  Who frees the segment is a matter of higher-level protocol.
	Stray segments are freed by com_shmAlloc().  ShmIsAlloc() makes it
	possible for an application to determine if a key is in use before
	com_shmAlloc() is called.

RETURNS
	Returns one, if the key is currently allocated;  returns zero if not.

*******************************************************************************/
int
com_shmIsAlloc(SHMnumber)	/* returns 1 if key is allocated; 0 if not */
int SHMnumber;		/* unique well-known key */
{
	/* attempt to obtain allocated segment identifier */
	if (shmget(SHMnumber, 0, 0666) == -1)
		return(0);
	else
		return(1);
}


/*******************************************************************************
NAME
	com_shmAlloc - Allocate a Shared Memory Segment

DESCRIPTION
	Each shared memory segment is allocated with a unique key, the
	SHMnumber that is unique to the host and well-known to allocator
	and user.  Who frees the segment is a matter of higher-level protocol.
	Stray segments must be freed before allocation.  The protocol promises
	that the segment is unique.

RETURNS
	ShmAlloc() returns the address of the shared memory segment or NULL.


*******************************************************************************/



char *
com_shmAlloc(size, SHMnumber) /* return pointer to allocated segment or NULL */
int size;		  /* size of the shared memory segment in bytes */
int SHMnumber;		  /* unique well-known key */
{
	register int shmid;
	char *address; 

	char errbuf[80];

	/* if it already exists, free it!! */
	if ((shmid = shmget(SHMnumber, 0, 0666)) != -1)
 		if (shmctl(shmid, IPC_RMID, 0) == -1) { 
			sprintf(errbuf, "com_shmAlloc() shmctl - %s", 
					strerror(errno));
			logError(errbuf);
		}
printf("\ntttttttttt");
	/* allocate an exclusive shared memory area */
	if ((int)(shmid=shmget(SHMnumber,size,0666|IPC_CREAT|IPC_EXCL)) == -1) {
		sprintf(errbuf, "com_shmAlloc() shmget - %s", strerror(errno));
		logError(errbuf);
		return((char *)NULL);
	}
printf("\nuuuuuuuuuu");
	
 	if ((char *)(address = shmat(shmid, 0, 0)) == (char *)(-1)) { 
		sprintf(errbuf, "com_shmAlloc() shmat - %s", strerror(errno));
		logError(errbuf);
		return((char *)NULL);
	}
	else
		return(address);
}

/*******************************************************************************
NAME
	com_shmDetach - Detach from a Shared Memory Segment

DESCRIPTION
	The segment, specified by its address, is detached.  This does not free
	the segment, but merely relinquishes sharing.

RETURNS
	Returns zero if sucessfully detached; otherwise returns -1.

*******************************************************************************/

int
com_shmDetach(address)	/* return 0 if detached;  otherwise, -1 */
char *address;		/* data segment address to detach */
{
	if ((long)shmdt(address))
		return(-1);
	else
		return(0);	/* how detached can you get?! */
}

/*******************************************************************************
NAME
	com_shmFree - Free an Allocated Shared Memory Segment

DESCRIPTION
	The well-known key is used to free a shared memory resource.

RETURNS
	Returns zero if sucessfully freed;  otherwise, returns -1.

*******************************************************************************/

int 
com_shmFree(SHMnumber)	/* return 0 if freed;  otherwise, -1 */
int SHMnumber;		/* free the segment associated with this key */
{
	register int shmid;

	/* attach a shared memory area */
	if ((shmid = shmget(SHMnumber, 0, 0666)) == -1)
		return(-1);

	/* release me... */
	if (shmctl(shmid, IPC_RMID, 0))
		return(-1);
	else
		return(0);
}

