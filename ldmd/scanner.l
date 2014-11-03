%e 150
%p 320
%n 60
%k 120
%a 400
%o 1200

%{
/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

/* 
 * Lexical scanner for LDM configuration-file.
 */

#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "ldm.h"
#include "atofeedt.h"
#include "ldm_config_file.h"
#include "error.h"
#include "log.h"
#include "RegularExpressions.h"

#if YYDEBUG
extern int yydebug;
#endif
%}

white		[ \t\f]+

escquote	\\\"

/* a string has no white space */
string		([^" \t\f\n]|{escquote})+

/* pretty much anything can be in a quoted string */
qstring		([^"]|{escquote})*

%%

\#.*		{/*comment */;}


^[Aa][Ll][Ll][Oo][Ww] {
#if YYDEBUG
    if(yydebug)
	udebug("ALLOW");
#endif
    return (ALLOW_K);
}


^[Aa][Cc][Cc][Ee][Pp][Tt] {
#if YYDEBUG
    if(yydebug)
	udebug("ACCEPT");
#endif
    return (ACCEPT_K);
}


^[Mm][Uu][Ll][Tt][Ii][Cc][Aa][Ss][Tt] {
#if YYDEBUG
    if(yydebug)
	udebug("SEND");
#endif
    return (SEND_K);
}


^[Rr][Ee][Cc][Ee][Ii][Vv][Ee] {
#if YYDEBUG
    if(yydebug)
	udebug("RECEIVE");
#endif
    return (RECEIVE_K);
}


^[Rr][Ee][Qq][Uu][Ee][Ss][Tt]|POLL|poll {
#if YYDEBUG
    if(yydebug)
	udebug("REQUEST");
#endif
    return (REQUEST_K);
}


^[Ee][Xx][Ee][Cc] {
#if YYDEBUG
    if(yydebug)
	udebug("EXEC");
#endif
    return (EXEC_K);
}


^[Ii][Nn][Cc][Ll][Uu][Dd][Ee] {
#if YYDEBUG
    if(yydebug)
	udebug("INCLUDE");
#endif
    return (INCLUDE_K);
}


{string} {
    int	status;

    if (yyleng > sizeof(yylval.string)-1) {
        log_start("String too long: \"%s\"", yytext);

	status = -1;
    }
    else {
	(void)strcpy(yylval.string, yytext);

	status = STRING;
    }

    return status;
}


\"{qstring}\" {
    int         status;

    if (yyleng-2 > sizeof(yylval.string)-1) {
        LOG_START1("String too long: \"%s\"", yytext);

	status = -1;
    }
    else {
        /*
         * A backslash escapes the following character, causing it to be copied
         * to the output string verbatim -- even if the following character is
         * a backslash.  The escaping backslash is not copied to the output
         * string.
         */
        const char*             from = yytext + 1;         /* exclude first " */
        const char* const       EOS = yytext + yyleng - 1; /* exclude last " */
        char*                   to = yylval.string;
        int                     backslashSeen = 0;

        status = STRING;

        while (EOS > from) {
            if (backslashSeen) {
                *to++ = *from++;
                backslashSeen = 0;
            }
            else {
                if ('"' == *from) {
                    LOG_START1("Unescaped quote: \"%s\"", yytext);

                    status = -1;

                    break;
                }

                if ('\\' == *from) {
                    from++;
                    backslashSeen = 1;
                }
                else {
                    *to++ = *from++;
                }
            }
        }

        *to = 0;
    }

    return status;
}


{white}		{/* whitespace */;}


\n		{ line++; }

%%

#ifndef PATH_MAX
#    define PATH_MAX	_POSIX_PATH_MAX
#endif

typedef struct FileEntry {
    FILE*		file;
    struct FileEntry*	prev;
    YY_BUFFER_STATE	buf;
    char		path[PATH_MAX];
} FileEntry;

static FileEntry*	currentFileEntry = NULL;

static int
scannerPush(
    const char* const	path)
{
    int		status = -1;		/* failure */
    FileEntry*	entry = malloc(sizeof(FileEntry));

    if (entry == NULL) {
	log_serror("Couldn't allocate new input-file entry");
    }
    else {
	if (path[0] == '/' || currentFileEntry == NULL) {
	    (void)strncpy(entry->path, path, sizeof(entry->path));
	}
	else {
	    char	dupPath[PATH_MAX];

	    /*
	     * Copy the path because dirname() can modify its argument.
	     */
	    (void)strncpy(dupPath, currentFileEntry->path, sizeof(dupPath));
	    dupPath[sizeof(dupPath)-1] = 0;

	    (void)snprintf(entry->path, sizeof(entry->path), "%s/%s",
		dirname(dupPath), path);
	}

	entry->path[sizeof(entry->path)-1] = 0;
	entry->file = fopen(entry->path, "r");

	if (entry->file == NULL) {
	    log_serror("Couldn't open input-file \"%s\"", entry->path);
	}
	else {
	    entry->buf = yy_create_buffer(entry->file, YY_BUF_SIZE);

	    if (entry->buf == NULL) {
		log_serror("Couldn't create new input-file buffer");
	    }
	    else {
		entry->prev = currentFileEntry;
		currentFileEntry = entry;
		yyin = entry->file;
		status = 0;

		yy_switch_to_buffer(entry->buf);
	    }				/* "entry->buf" allocated */

	    if (status)
		(void)fclose(entry->file);
	}				/* "entry->file" open */

	if (status)
	    free(entry);
    }					/* "entry" allocated */

    return status;
}

/*
 * Returns:
 *	 0	More input
 *	!0	No more input
 */
static int
scannerPop(void)
{
    if (currentFileEntry != NULL) {
	FileEntry* const	prev = currentFileEntry->prev;

	yy_delete_buffer(currentFileEntry->buf);
	(void)fclose(currentFileEntry->file);
	free(currentFileEntry);

	if (prev != NULL)
	    yy_switch_to_buffer(prev->buf);

	currentFileEntry = prev;
    }

    return currentFileEntry == NULL;
}