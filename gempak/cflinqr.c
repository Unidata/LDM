#define _XOPEN_SOURCE 500

#include "geminc.h"
#include "gemprm.h"

void cfl_inqr ( char *filnam, char *defdir, long *flen, char *newfil,
		int *iret )
/************************************************************************
 * cfl_inqr								*
 *									*
 * This function determines whether a file exists and the file size.	*
 *									*
 * The file is located by searching in the following order (environment	*
 * variables may be used as part of the paths):				*
 *									*
 *	1. filnam (as given)						*
 *	2. defdir/filnam						*
 *									*
 * cfl_inqr ( filnam, defdir, flen, newfil, iret )			*
 *									*
 * Input parameters:							*
 *	*filnam		char		File name			*
 *	*defdir		char		Default directory		*
 *									*
 * Output parameters:							*
 *	*flen		long		File size			*
 *	*newfil		char		Expanded file name		*
 *	*iret		int		Return code			*
 *					  0 = Normal, file exists	*
 *					 -1 = File does not exist	*
 **									*
 * G. Krueger/EAI	 3/96						*
 * G. Krueger/EAI        8/96	Match with FL library			*
 * T. Lee/SAIC		12/02	Initialize flen
 ***********************************************************************/
{
	int		ier, ier1;
	char		newname[LLPATH];
	struct stat	stbuf;
/*---------------------------------------------------------------------*/
	*iret = 0;
	*flen = 0;

	css_envr ( filnam, newname, &ier );
	strcpy ( newfil, newname );
	if ( (ier1 = stat ( newfil, &stbuf )) != 0 ) {
/*
 *	    Try the DEFDIR directory.
 */
	    if ( defdir != NULL ) {
		css_envr ( defdir, newfil, &ier );
		strcat ( newfil, "/" );
		strcat ( newfil, newname );
		ier1 = stat ( newfil, &stbuf );
	    }
	}

	if ( ier1 == 0 ) {
	    *flen = (long)stbuf.st_size;
	} else {
	    cfl_iret ( errno, iret, &ier );
	}
}

