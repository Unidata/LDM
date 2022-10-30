/*
 *   Copyright 2014 University Corporation for Atmospheric Research
 *
 *   See file "COPYRIGHT" in the top-level source-directory for copying and
 *   redistribution conditions.
 */

#include <config.h>
#include <errno.h>
#include <stdio.h>
#include <log.h>
#include <string.h>
#include <ctype.h>
#include <limits.h> /* PATH_MAX */
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#ifndef PATH_MAX
#define PATH_MAX 255                    /* _POSIX_PATH_MAX */
#endif /* !PATH_MAX */
#include <sys/types.h>
#include <sys/sem.h>                                          
#include <sys/shm.h>                                          
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h> /* O_RDONLY et al */
#include <unistd.h> /* access, lseek */
#include <signal.h>

#if !defined(_DARWIN_C_SOURCE) && !defined(__BSD_VISIBLE)
union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux specific) */
};
#endif

#if defined(_AIX) && defined(HAVE_WAITPID)
/*
 * Use POSIX wait macros, not _BSD
 */
#define _H_M_WAIT
#endif
#include <sys/wait.h>

#include "child_map.h"
#include "error.h"
#include "filel.h"
#include "action.h"
#include "ldm.h"
#include "ldmalloc.h"
#include "ldmfork.h"
#include "ldmprint.h"
#include "log.h"
#include "mkdirs_open.h"
#include "registry.h"
#include "log.h"
#include "pbuf.h"
#include "pq.h"

extern pqueue*     pq;
extern ChildMap*   execMap;
/*
 * Defined in pqcat.c
 */
extern int         pipe_timeo;

static unsigned    maxEntries = 0;
static int         shared_id = -1;
static int         sem_id = -1;
static unsigned    shared_size;
static unsigned    queue_counter = 0;
static unsigned    largest_queue_element = 0;
static union semun semarg;

#ifndef NO_DB

/*
 * Define DB_XPROD non zero if you want the whole "product" data
 * structure put into the database, otherwise just the data goes in.
 * Be sure this is consistant with ../dbcat and whatever else used
 * to read the files.
 */
# ifndef DB_XPROD
# define DB_XPROD 1
# endif

/*
 * Backward compatibility.
 * If you want to use gdbm interfaces, define USE_GDBM
 */
# ifdef USE_GDBM
# include "gdbm.h"
# else
# include <ndbm.h>
# endif

#endif /* !NO_DB */

/*
 * The types of entries in the list. Keep consonant with TYPE_NAME.
 */
typedef enum {
    FT_NONE = 0,
    UNIXIO,
    STDIO,
    PIPE,
    FT_DB
} ft_t;

/*
 * Converts "ft_t" into ASCII. Keep consonant with "ft_t".
 */
static const char* const TYPE_NAME[] = {
        "NOOP",
        "FILE",
        "STDIOFILE",
        "PIPE",
        "DBFILE" };

/*
 * Entry deletion information.
 */
typedef struct {
	char*       adjective; ///< Describes reason for deletion
	log_level_t logLevel;  ///< Logging level
} DeleteReason;
static const DeleteReason DR_TERMINATED = {"terminated",            LOG_LEVEL_DEBUG};
static const DeleteReason DR_SIGNALED =   {"abnormally-terminated", LOG_LEVEL_WARNING};
static const DeleteReason DR_CLOSED =     {"closed",                LOG_LEVEL_DEBUG};
static const DeleteReason DR_LRU =        {"least-recently-used",   LOG_LEVEL_DEBUG};
static const DeleteReason DR_FAILED =     {"failed",                LOG_LEVEL_DEBUG};
static const DeleteReason DR_INACTIVE =   {"inactive",              LOG_LEVEL_DEBUG};

union f_handle {
    int       fd;
    FILE*     stream;
    pbuf*     pbuf;
#ifndef NO_DB
# ifdef USE_GDBM
    GDBM_FILE db;
# else
    DBM*      db;
# endif
#endif /*!NO_DB*/
};
typedef union f_handle f_handle;

/**
 * An entry in a list of entries, each of which has a open output.
 */
struct fl_entry {
    struct fl_entry* next;
    struct fl_entry* prev;
    struct fl_ops*   ops;
    f_handle         handle;
    unsigned long    private;           // pid, hstat*, R/W flg
    time_t           lastUse;        // Time of last access
    int              flags;
    ft_t             type;
    char             path[PATH_MAX];    // PATH_MAX includes NUL
};
typedef struct fl_entry fl_entry;

/**
 * The operations that can performed on an entry.
 */
struct fl_ops {
    int (*cmp)(
            fl_entry* entry,
            int       argc,
            char**    argv);
    /**
     * Opens the file of an entry. Does not add the entry to the open-file list.
     *
     * @param[in] entry  The entry.
     * @param[in] argc   Number of arguments in the command.
     * @param[in] argv   Command arguments.
     * @retval    -1     Failure.
     * @return           File-descriptor of the open file.
     */
    int (*open)(
            fl_entry* entry,
            int       argc,
            char**    argv);
    /**
     * Closes the output of an entry.
     *
     * @param[in] entry  The entry.
     */
    void (*close)(
            fl_entry* entry);
    /**
     * Flushes any outstanding I/O of an entry to the associated output.
     *
     * @param[in] entry  The open-file entry.
     * @param[in] block  Whether or not the I/O should block.
     * @retval    0      Success.
     * @return           `errno` error-code.
     */
    int (*sync)(
            fl_entry* entry,
            int       block);
};

/**
 * The one global list of entries.
 */
static struct fl {
    int size;
    fl_entry *head;
    fl_entry *tail;
} thefl[] = {{ 0, NULL, NULL }};

/// Maximum amount of time for an unused entry, in seconds
static const unsigned long maxTime = 6 * 3600;

/**
 * Frees an open-file entry -- releasing all resources including flushing and
 * closing the associated output.
 *
 * @param[in] entry  The entry to be freed or `NULL`.
 */
static void
entry_free(
        fl_entry *entry)
{
    if (entry != NULL) {
        if (entry->ops != NULL)
            entry->ops->close(entry);

        free(entry);
    }
}

/*
 * Forward reference
 */
static fl_entry* entry_new(
        ft_t         type,
        int          argc,
        char** const argv);

static inline void
entry_setFlag(
        fl_entry* const entry,
        const int       flag)
{
    entry->flags |= flag;
}

static inline void
entry_unsetFlag(
        fl_entry* const entry,
        const int       flag)
{
    entry->flags &= ~flag;
}

/**
 * Indicate if a particular flag is set.
 *
 * Because profiling revealed that the utility pqact(1) spent about 15% of its
 * time in this "function", it is now a macro. SRE 2016-06-21
 */
#define entry_isFlagSet(entry, flag) (entry->flags & flag)

/**
 * Removes an entry from the open-file list.
 *
 * @param[in] entry  The entry to be removed.
 * @pre              {The entry is in the list.}
 */
static void
fl_remove(
        fl_entry* const entry)
{
    if (entry->prev != NULL )
        entry->prev->next = entry->next;
    if (entry->next != NULL )
        entry->next->prev = entry->prev;

    if (thefl->head == entry )
        thefl->head = entry->next;
    if (thefl->tail == entry)
        thefl->tail = entry->prev;

    entry->prev = NULL;
    entry->next = NULL;

    thefl->size--;
}

/**
 * Adds an entry to the head of the open-file list.
 *
 * @pre              {The entry is not in the list.}
 * @param[in] entry  The entry to be added.
 */
static void
fl_addToHead(
        fl_entry* const entry)
{
    if (thefl->head != NULL )
        thefl->head->prev = entry;

    entry->next = thefl->head;
    entry->prev = NULL;
    thefl->head = entry;

    if (thefl->tail == NULL)
        thefl->tail = entry;

    thefl->size++;
}

/**
 * Moves an entry in the open-file list to the head of the list.
 *
 * @param[in] entry  The entry to be moved.
 * @pre              {The entry is in the list.}
 */
static inline void
fl_makeHead(fl_entry *entry)
{
    entry->lastUse = time(NULL);
    if (thefl->head != entry) {
        fl_remove(entry);
        fl_addToHead(entry);
    }
}

#ifdef FL_DEBUG
static void
dump_fl(void)
{
    fl_entry *entry;
    int fd;

    log_debug("thefl->size %d", thefl->size);
    for(entry = thefl->head; entry != NULL;
            entry = entry->next )
    {
        switch (entry->type) {
        case UNIXIO :
            fd = entry->handle.fd;
            break;
        case STDIO :
            fd = entry->handle.stream == NULL
            ? -1 : fileno(entry->handle.stream);
            break;
        case PIPE :
            fd = entry->handle.pbuf == NULL
            ? -1 : entry->handle.pbuf->pfd;
            break;
        case FT_DB :
#ifndef NO_DB
            fd = entry->handle.db == NULL
            ? -1 : -2;
            break;
#endif /* !NO_DB */
        default :
            fd = -2;
        }
        log_debug("%d %s", fd, entry->path);
    }
}
#endif

/**
 * Finds the entry in the list corresponding to a given type of entry and
 * command arguments.
 *
 * @param[in] type  Type of entry.
 * @param[in] argc  Number of command arguments.
 * @param[in] argv  Command arguments.
 * @retval    NULL  No such entry.
 * @return          Corresponding entry.
 */
static fl_entry*
fl_find(
        const ft_t   type,
        const int    argc,
        char** const argv)
{
    fl_entry *entry = NULL;

    for (entry = thefl->head; entry != NULL ; entry = entry->next) {
        if (entry->type == type && entry->ops->cmp(entry, argc, argv) == 0)
            break;
    }

    return entry;
}

/**
 * Removes an entry in the open-file list and frees the entry's resources. Logs
 * a single message at an appropriate logging level.
 *
 * NB: Dereferencing the entry after this call results in undefined behavior.
 *
 * @pre             {If `entry != NULL`, then the entry is in the list.}
 * @param[in] entry Pointer to the entry to be deleted or NULL.
 * @param[in] dr    The reason for the deletion.
 */
static void
fl_removeAndFree(
        fl_entry* const           entry,
        const DeleteReason* const dr)
{
    if (entry != NULL) {
    	const int         logLevel = dr->logLevel;
        const char*       fmt = (PIPE == entry->type)
                ? "Deleting %s %s entry: cmd=\"%s\", pid=%lu"
                : "Deleting %s %s entry: cmd=\"%s\"";

        log_log(logLevel, fmt, dr->adjective,
                TYPE_NAME[entry->type], entry->path, entry->private);

        fl_remove(entry);
        entry_free(entry);
    }
}

/**
 * Flushes outstanding I/O of entries in the list starting with the tail of the
 * list and moving to the head.
 *
 * @param[in] block     Whether or not the I/O should block.
 */
void
fl_sync(const int block)
{
    fl_entry*                  entry;
    fl_entry*                  prev;
    const time_t               now = time(NULL);

    for (entry = thefl->tail; entry != NULL; entry = prev) {
        prev = entry->prev;
        if (entry_isFlagSet(entry, FL_NEEDS_SYNC)) {
            if (entry->ops->sync(entry, block)) {
                fl_removeAndFree(entry, &DR_FAILED); // public function so remove
                entry = NULL;
            }
        }
        if (entry && (now - entry->lastUse > maxTime))
			fl_removeAndFree(entry, &DR_INACTIVE);
    }
}

/**
 * Closes, removes, and frees the "least recently used" entry that doesn't have
 * certain flags set. Starts with the tail of the list.
 *
 * @param[in] skipflags  Flags that will cause the entry to be skipped.
 */
void
fl_closeLru(
        const int skipflags)
{
    if (thefl->size <= 0)
        return;

    fl_entry *entry, *prev;
    for (entry = thefl->tail; entry != NULL ; entry = prev) {
        prev = entry->prev;
        /* twisted logic */
        if (entry_isFlagSet(entry, skipflags))
            continue;
        /* else */
        fl_removeAndFree(entry, &DR_LRU);
        return;
    }
}

/**
 * Closes, removes, and frees all entries from the list.
 */
void
fl_closeAll(
        void)
{
    while (thefl->size > 0)
        fl_closeLru(0);
}

/**
 * Returns the entry in the list corresponding to a given type and command.
 * Creates the entry if it doesn't exist. INVARIANT: An entry in the list has
 * its output open.
 *
 * @param[in]  type     Type of entry.
 * @param[in]  argc     Number of command arguments.
 * @param[in]  argv     Command arguments.
 * @param[out] isNew    Whether or not a new entry was created or `NULL`.
 * @retval     NULL     Couldn't create new entry.
 * @return              Corresponding entry. `*isNew` is set if possible.
 */
static fl_entry*
fl_getEntry(
        const ft_t            type,
        const int             argc,
        char** const restrict argv,
        bool* const restrict  isNew)
{
    fl_entry* entry = fl_find(type, argc, argv);

    if (NULL != entry) {
        fl_makeHead(entry);
        #ifdef FL_DEBUG
            dump_fl();
        #endif
        if (isNew)
            *isNew = false;
    }
    else {
        log_assert(maxEntries > 0);

        while (thefl->size >= maxEntries)
            fl_closeLru(0); // 0 => unconditional removal

        entry = entry_new(type, argc, argv);
        if (NULL != entry) {
            fl_addToHead(entry);
            #ifdef FL_DEBUG
                dump_fl();
            #endif
            if (isNew)
                *isNew = true;
        }
    }

    return entry;
}

/**
 * Returns the PIPE entry in the list corresponding to a PID (only PIPE entries
 * have PID-s).
 *
 * @param[in] pid   PID of the PIPE entry to return.
 * @retval    NULL  No such entry.
 * @return          Corresponding entry.
 */
static fl_entry*
fl_findByPid(
        const pid_t pid)
{
    fl_entry* entry;

    for (entry = thefl->tail; entry != NULL ; entry = entry->prev) {
        if (pid == entry->private)
            break;
    }

    return entry;
}


/**
 * Ensures that a given file descriptor will be closed upon execution of an
 * exec(2) family function.
 *
 * @param[in] fd      The file descriptor to be set to close-on-exec.
 * @retval    0       Success.
 * @retval    -1      Failure. log_add() called.
 */
static int
ensureCloseOnExec(
        const int fd)
{
    int status = 0; // Success
    int flags = fcntl(fd, F_GETFD);
    if (-1 == flags) {
        log_add_syserr("Couldn't get flags for file descriptor %d", fd);
        status = -1;
    }
    else if (!(flags & FD_CLOEXEC)
            && (-1 == fcntl(fd, F_SETFD, flags | FD_CLOEXEC))) {
        log_add_syserr("Couldn't set file descriptor %d to close-on-exec()",
                fd);
        status = -1;
    }
    return status;
}

/**
 * Flushes the I/O buffers of an entry if the FL_FLUSH flag is set.
 *
 * @param[in] entry  Entry.
 * @retval    0      Success.
 * @return           `errno` error-code. `log_error()` called.
 */
static inline int flushIfAppropriate(
        fl_entry* const restrict entry)
{
    return entry_isFlagSet(entry, FL_FLUSH)
            ? entry->ops->sync(entry, 1)
            : 0;
}

/**
 * Returns a copy of a character array with all control characters removed
 * except newlines. Remember to free the result.
 *
 * @param[in] in       Input character array.
 * @param[in] len      Input size in bytes.
 * @param[in] outlenp  Output size in bytes.
 * @retval    NULL     System failure. `log_syserr()` called.
 * @return             Pointer to new character array. Caller should free when
 *                     it's no longer needed.
 */
static void *
dupstrip(
        const void*            in,
        const size_t           len,
        size_t* const restrict outlenp)
{
    void*                out;
    size_t               blen;
    const unsigned char* ip;
    unsigned char*       op;

    if (in == NULL || len == 0)
        return NULL ;

    out = malloc(len);
    if (out == NULL ) {
        log_syserr("dupstrip: malloc %ld failed", (long) len);
        return NULL ;
    }

    for (blen = len, ip = in, op = out, *outlenp = 0; blen != 0; blen--, ip++) {
        if (iscntrl(*ip) && *ip != '\n')
            continue;
        /* else */
        *op++ = *ip;
        (*outlenp)++;
    }

    return out;
}

typedef struct {
    const char*  name;
    void       (*action)(fl_entry*, int);
    const int    flag;
} Option;
static Option OPT_STRIPWMO    = {"removewmo", entry_setFlag,   FL_STRIPWMO};
static Option OPT_CLOSE       = {"close",     entry_setFlag,   FL_CLOSE};
static Option OPT_EDEX        = {"edex",      entry_setFlag,   FL_EDEX};
static Option OPT_FLUSH       = {"flush",     entry_setFlag,   FL_FLUSH};
static Option OPT_LOG         = {"log",       entry_setFlag,   FL_LOG};
static Option OPT_METADATA    = {"metadata",  entry_setFlag,   FL_METADATA};
static Option OPT_NODATA      = {"nodata",    entry_setFlag,   FL_NODATA};
static Option OPT_OVERWRITE   = {"overwrite", entry_setFlag,   FL_OVERWRITE};
static Option OPT_STRIP       = {"strip",     entry_setFlag,   FL_STRIP};
static Option OPT_TRANSIENT   = {"transient", entry_unsetFlag, FL_NOTRANSIENT};

/**
 * Decodes action options.
 *
 * @param[in] entry  Associated action entry.
 * @param[in] argc   Number of arguments.
 * @param[in] argv   NULL-terminated list of arguments.
 * @param[in] ...    NULL-terminated sequence of possible options (e.g.,
 *                   `&OPT_CLOSE`).
 * @return           Number of options decoded.
 */
static unsigned
decodeOptions(
        fl_entry* const restrict entry,
        int                      argc,
        char** restrict          argv,
        ...)
{
    const int ac = argc;

    for (; argc > 1 && **argv == '-'; argc--, argv++) {
        va_list opts;

        va_start(opts, argv);

        for (const Option* opt = va_arg(opts, const Option*); opt;
                opt = va_arg(opts, const Option*))
            if (strncmp(*argv+1, opt->name, 2) == 0)
                opt->action(entry, opt->flag);

        va_end(opts);
    }

    return ac - argc;
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
 * 	parser.  Note this parser is not case sensitive.
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
#define WMO_BBB_LEN		3

#define WMO_T1	0
#define WMO_T2	1
#define WMO_A1	2
#define WMO_A2	3
#define WMO_I1	4
#define WMO_I2	5

int getWmoOffset (char *buf, size_t buflen, size_t *p_wmolen) {
	char *p_wmo;
	int i_bbb;
	int spaces;
	int	crcrlf_found = 0;
	int	bbb_found = 0;
	int wmo_offset = -1;

	*p_wmolen = 0;

	for (p_wmo = buf; p_wmo + WMO_I2 + 1 < buf + buflen; p_wmo++) {
		if (isalpha(p_wmo[WMO_T1]) && isalpha(p_wmo[WMO_T2]) &&
		    isalpha(p_wmo[WMO_A1]) && isalpha(p_wmo[WMO_A2])) {
			/* 'TTAAII ' */
			if (isdigit(p_wmo[WMO_I1]) && isdigit(p_wmo[WMO_I2]) &&
			   (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
				wmo_offset = p_wmo - buf;
				p_wmo += WMO_I2 + 1;
				break;
			}
		} else if (!strncmp(p_wmo, "\r\r\n", 3)) {
			/* got to EOH with no TTAAII found, check TTAA case below */
			break;
		}
	}

	/* skip spaces if present */
	while (isspace(*p_wmo) && p_wmo < buf + buflen) {
		p_wmo++;
	}

	if (p_wmo + WMO_CCCC_LEN > buf + buflen) {
		return -1;
	} else if (isalpha(*p_wmo) && isalnum(*(p_wmo+1)) &&
		   isalpha(*(p_wmo+2)) && isalnum(*(p_wmo+3))) {
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
		if (isdigit(*p_wmo) && isdigit(*(p_wmo+1)) &&
		    isdigit(*(p_wmo+2)) && isdigit(*(p_wmo+3)) &&
		    isdigit(*(p_wmo+4)) && isdigit(*(p_wmo+5))) {
			p_wmo += 6;
		}
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
			for (i_bbb = 1;	p_wmo + i_bbb < buf + buflen && i_bbb < WMO_BBB_LEN; i_bbb++) {
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

	/* Advance past NNNXXX, if found */
	if (p_wmo + 9 <= buf + buflen) {
		if (isalnum(p_wmo[0]) && isalnum(p_wmo[1]) && isalnum(p_wmo[2]) &&
		    isalnum(p_wmo[3]) && isalnum(p_wmo[4]) && isalnum(p_wmo[5]) &&
		    (p_wmo[6] == '\r') && (p_wmo[7] == '\r') && (p_wmo[8] == '\n')) {
			p_wmo += 9;
		}
	}

	/* update length to include bbb and crcrlf */
	*p_wmolen = p_wmo - buf - wmo_offset;

	return wmo_offset;
}


/* -----------------------------------------------------------------------------
 * Function Name
 * 	stripHeaders
 *
 * Format
 * 	static void *stripHeaders (const void *data, size_t *sz)
 *
 * Arguments
 * 	Type			Name		I/O		Description
 * 	const void *		data		I		pointer to product in memory
 * 	size_t			sz		I/O		product size in bytes - will
 * 								be updated if headers are found
 *
 * Description
 * 	Finds LDM and WMO headers in a product if either exists within the first 100 bytes.
 * 	Adjusts the product size 'sz' to account for any headers found.  Returns a pointer
 * 	to the beginning of the product data.  This function calls getWmoOffset() to find a
 * 	WMO header, its offset, and length.
 *
 * Returns
 * 	Pointer to start of actual product data past any headers found.
 *
 * -------------------------------------------------------------------------- */

#define SIZE_SBN_HDR		11	/* noaaportIngester adds a header and trailer to products - header is 11 bytes */
#define SIZE_SBN_TLR		4	/* trailer is 4 bytes - if there is a header, there will also be a trailer */
#define CHECK_DEPTH		100	/* This is how far into each product to look for a WMO header */
#define MIN_PRODUCT_SIZE	21	/* Ignore any files smaller than this since they are too small to contain a WMO header */

static void *stripHeaders (const void *data, size_t *sz) {
	size_t		wmo_len;
	int		wmo_offset;
	char		*dptr		= (char *) data;
	size_t		isz		= *sz;
	size_t		slen		= isz < CHECK_DEPTH ? isz : CHECK_DEPTH;

	if (*sz < MIN_PRODUCT_SIZE) {	/* Don't check for a header in a product that's smaller than the minimum header size */
		return dptr;
	}

	if (slen < *sz) {	/* Don't check beyond the end of the product */
		slen = *sz;
	}

	if ((!memcmp (dptr, "\001\015\015\012", 4) &&
	    isdigit(dptr[4]) && isdigit(dptr[5]) && isdigit(dptr[6]) &&
	    !memcmp (&dptr[7], "\040\015\015\012", 4))) {
		dptr += SIZE_SBN_HDR;
		*sz -= (SIZE_SBN_HDR + SIZE_SBN_TLR);
		log_debug("Stripping LDM header/trailer");
	}

	if ((wmo_offset = getWmoOffset (dptr, slen, &wmo_len)) >= 0) {
		dptr += (wmo_offset + wmo_len);
		*sz -= (wmo_offset + wmo_len);

		log_debug("Stripping WMO header at offset %d, length %d with initial product size %d and final product size %d",
			wmo_offset, wmo_len, isz, *sz);
	} else {
		log_debug("WMO header not found in product with length %d", *sz);
	}

	return (void *) dptr;
}

/* Begin UNIXIO */
static int str_cmp(
        fl_entry*             entry,
        const int             argc,
        char** const restrict argv)
{
    const char* path;

    log_assert(argc > 0);
    log_assert(argv[argc -1] != NULL);
    log_assert(*argv[argc -1] != 0);

    path = argv[argc - 1];
    return (strcmp(path, entry->path));
}

/*
 * Opens an output-file for the FILE action.
 *
 * Arguments:
 *      entry   Pointer to the relevant entry in the pattern/action list.
 *      ac      The number of argument.
 *      av      Pointer to pointers to arguments.
 * Returns:
 *      -1      Failure.  An error-message is logged.
 *      else    The file descriptor of the output-file.
 */
static int unio_open(
        fl_entry *entry,
        int ac,
        char **av)
{
    char* path;
    int flags = (O_WRONLY | O_CREAT);
    int writeFd = -1; /* failure */

    log_assert(ac > 0);
    log_assert(av[ac -1] != NULL);
    log_assert(*av[ac -1] != 0);

    unsigned nopt = decodeOptions(entry, ac, av, &OPT_OVERWRITE, &OPT_STRIP,
            &OPT_METADATA, &OPT_LOG, &OPT_EDEX, &OPT_STRIPWMO, &OPT_FLUSH, &OPT_CLOSE, NULL);
    ac -= nopt;
    av += nopt;

    if (entry_isFlagSet(entry, FL_OVERWRITE))
            flags |= O_TRUNC;

    path = av[ac - 1];
    entry->handle.fd = -1;

    while ((writeFd = mkdirs_open(path, flags, 0666)) == -1 &&
			(errno == EMFILE || errno == ENFILE) && thefl->size > 0)
		fl_closeLru(0); // 0 => unconditional removal

    if (-1 == writeFd) {
    	log_clear();
        log_syserr("Couldn't open file \"%s\"", path);
    }
    else {
        /*
         * Ensure that the file descriptor will close upon execution
         * of an exec(2) family function because no child processes should
         * inherit it.
         */
        int status = ensureCloseOnExec(writeFd);
        if (status) {
            log_add_syserr("ensureCloseOnExec() failure on file \"%s\"", path);
        }
        else {
            if (!(flags & O_TRUNC)) {
                if (lseek(writeFd, 0, SEEK_END) < 0) {
                    /*
                     * The "file" must be a pipe or FIFO.
                     */
                    log_add_syserr("lseek() failure on file \"%s\"", path);
                }
            }

            entry->handle.fd = writeFd;
            strncpy(entry->path, path, PATH_MAX);
            entry->path[PATH_MAX - 1] = 0; /* just in case */

            log_debug("%d %s", entry->handle.fd, entry->path);
        } /* output-file set to close_on_exec */

        if (status) {
            (void) close(writeFd);
            writeFd = -1;
        }
    } /* "writeFd" open */

    return writeFd;
}

static void unio_close(
        fl_entry *entry)
{
    log_debug("%d", entry->handle.fd);
    if (entry->handle.fd != -1) {
        if (close(entry->handle.fd) == -1) {
            log_syserr("close: %s", entry->path);
        }
    }

    entry->handle.fd = -1;
}

static int unio_sync(
        fl_entry *entry,
        int block)
{
    log_debug("%d %s", entry->handle.fd, block ? "" : "non-block");

    if (0 == fsync(entry->handle.fd)) {
        entry_unsetFlag(entry, FL_NEEDS_SYNC);
        return 0;
    }
    if (!block && EAGAIN == errno) {
        return 0;
    }
    if (EINTR != errno) {
        log_add_syserr("Couldn't flush I/O to file \"%s\"", entry->path);
        // disable flushing on I/O error
        entry_unsetFlag(entry, FL_NEEDS_SYNC);
    }

    return errno;
}

/*ARGSUSED*/
static int unio_put(
        fl_entry *entry,
        const char *ignored,
        const void *data,
        size_t sz)
{
    if (sz) {
        fl_makeHead(entry);
        log_debug("handle: %d size: %d", entry->handle.fd, sz);

#if 0
        double nbits = 8*sz;
		time_t start;
		(void)time(&start);
#endif

        do {
            ssize_t nwrote = write(entry->handle.fd, data, sz);

            if (-1 != nwrote) {
                sz -= nwrote;
                data = (char*)data + nwrote;
            }
            else {
                if (EINTR != errno) {
                    log_add_syserr("Couldn't write() %zu bytes to file \"%s\"",
                            sz, entry->path);
                    // disable flushing on I/O error
                    entry_unsetFlag(entry, FL_NEEDS_SYNC);
                }

                return -1;
            }
        } while (sz);

#if 0
        unsigned long duration = time(NULL) - start;
        if (nbits/duration < 1000000)
            log_warning("Output rate < 1 MHz");
#endif

        entry_setFlag(entry, FL_NEEDS_SYNC);
    }

    return 0;
}

static struct fl_ops unio_ops = { str_cmp, unio_open, unio_close, unio_sync};

/*
 * Writes the data-product creation-time to the file as
 *     integer portion                  uint64_t
 *     microseconds portion             int32_t
 * ARGUMENTS:
 *     entry    Pointer to file-list entry
 *     creation Pointer to data-product creation-time
 * RETURNS:
 *     0        Success
 *     else     "errno"
 */
static int unio_putcreation(
        fl_entry* entry,
        const timestampt* creation)
{
    int status;
#if SIZEOF_UINT64_T*CHAR_BIT == 64
    uint64_t uint64 = (uint64_t) creation->tv_sec;
    status = write(entry->handle.fd, (void*) &uint64, (u_int) sizeof(uint64_t));
#else
    uint32_t lower32 = (uint32_t) creation->tv_sec;
#   if SIZEOF_LONG*CHAR_BIT <= 32
    uint32_t upper32 = 0;
#   else
    uint32_t upper32 =
            (uint32_t)(((unsigned long)creation->tv_sec) >> 32);
#   endif
#   if WORDS_BIGENDIAN
    uint32_t first32 = upper32;
    uint32_t second32 = lower32;
#   else
    uint32_t first32 = lower32;
    uint32_t second32 = upper32;
#   endif
    status = write(entry->handle.fd, (void*) &first32, (u_int) sizeof(uint32_t));
    if (status != -1) {
        status = write(entry->handle.fd, (void*) &second32,
                (u_int) sizeof(uint32_t));
        if (status != -1) {
            int32_t int32 = (int32_t) creation->tv_usec;
            status = write(entry->handle.fd, (void*) &int32,
                    (u_int) sizeof(int32_t));
        }
    }
#endif
    return status == -1 ? errno : 0;
}

/*
 * Writes the data-product metadata to the file as:
 *      metadata-length in bytes                                 uint32_t
 *      data-product signature (MD5 checksum)                    uchar[16]
 *      data-product size in bytes                               uint32_t
 *      product creation-time in seconds since the epoch:
 *              integer portion                                  uint64_t
 *              microseconds portion                             int32_t
 *      data-product feedtype                                    uint32_t
 *      data-product sequence number                             uint32_t
 *      product-identifier:
 *              length in bytes (excluding NUL)                  uint32_t
 *              non-NUL-terminated string                        char[]
 *      product-origin:
 *              length in bytes (excluding NUL)                  uint32_t
 *              non-NUL-terminated string                        char[]
 *
 * Arguments:
 *      entry   Pointer to the action-entry.
 *      info    Pointer to the data-product's metadata.
 *      sz      The size of the data in bytes.
 * Returns:
 *      0       Success
 *      else    "errno"
 */
static int unio_putmeta(
        fl_entry* entry,
        const prod_info* info,
        uint32_t sz)
{
    int32_t int32;
    uint32_t uint32;
    uint32_t identLen = (uint32_t) strlen(info->ident);
    uint32_t originLen = (uint32_t) strlen(info->origin);
    uint32_t totalLen = 4 + 16 + 4 + 8 + 4 + 4 + 4 + (4 + identLen)
                + (4 + originLen);
    int status;

    status = write(entry->handle.fd, (void*) &totalLen, (u_int) sizeof(totalLen));
    if (status == -1)
        return errno;

    status = write(entry->handle.fd, (void*) &info->signature,
            (u_int) sizeof(info->signature));
    if (status == -1)
        return errno;

    status = write(entry->handle.fd, (void*) &sz, (u_int) sizeof(sz));
    if (status == -1)
        return errno;

    status = unio_putcreation(entry, &info->arrival);
    if (status != 0)
        return status;

    int32 = (int32_t) info->arrival.tv_usec;
    status = write(entry->handle.fd, (void*) &int32, (u_int) sizeof(int32));
    if (status == -1)
        return errno;

    uint32 = (uint32_t) info->feedtype;
    status = write(entry->handle.fd, (void*) &uint32, (u_int) sizeof(uint32));
    if (status == -1)
        return errno;

    uint32 = (uint32_t) info->seqno;
    status = write(entry->handle.fd, (void*) &uint32, (u_int) sizeof(uint32));
    if (status == -1)
        return errno;

    status = write(entry->handle.fd, (void*) &identLen, (u_int) sizeof(identLen));
    if (status == -1)
        return errno;

    status = write(entry->handle.fd, (void*) info->ident, identLen);
    if (status == -1)
        return errno;

    status = write(entry->handle.fd, (void*) &originLen, (u_int) sizeof(originLen));
    if (status == -1)
        return errno;

    status = write(entry->handle.fd, (void*) info->origin, originLen);

    return status == -1 ? errno : ENOERR;
}

static int unio_out(
        fl_entry* entry,
        const product* prodp,
        const void* data,
        const uint32_t sz)
{
    int status = ENOERR;

    if (entry_isFlagSet(entry, FL_METADATA)) {
        status = unio_putmeta(entry, &prodp->info, sz);
        if (status)
            log_add("Couldn't write product metadata to file");
    }
    if (status == ENOERR && !entry_isFlagSet(entry, FL_NODATA)) {
        status = unio_put(entry, prodp->info.ident, data, sz);
        if (status)
            log_add("Couldn't write product data to file");
    }

    return status;
}

/*ARGSUSED*/
int unio_prodput(
        const product* const restrict prodp,
        const int                     argc,
        char** const restrict         argv,
        const void*                   ignored,
        const size_t                  also_ignored)
{
    int		status = -1; /* failure */
    fl_entry	*entry = fl_getEntry(UNIXIO, argc, argv, NULL);
    char	must_free_data = 0;

    log_debug("%d %s", entry == NULL ? -1 : entry->handle.fd,
            prodp->info.ident);

    if (entry == NULL ) {
        log_add("Couldn't get entry for product \"%s\"", prodp->info.ident);
        status = -1;
    }
    else {
        if (entry_isFlagSet(entry, FL_EDEX)) {
            if (shared_id == -1) {
                log_add("Notification specified but shared memory is not "
                        "available.");
            }
            else {
                edex_message* const queue =
                        (edex_message*)shmat(shared_id, (void*)0, 0);
                edex_message* const msg = queue + queue_counter;
                strncpy(msg->filename, entry->path, 4096);
                msg->filename[4096-1] = 0;
                strncpy(msg->ident, prodp->info.ident, 256);
                msg->ident[256-1] = 0;
                if (shmdt((void*)queue) == -1) {
                    log_add_syserr("Detaching shared memory failed.");
                }
            }
        }

        size_t sz = prodp->info.sz;
        void*  data = prodp->data;

        if (entry_isFlagSet (entry, FL_STRIPWMO)) {
            data = stripHeaders (prodp->data, &sz);
        }

        if (entry_isFlagSet(entry, FL_STRIP)) {
            data = dupstrip(data, sz, &sz);
            if (data == NULL) {
                log_add("Couldn't strip control-characters out of product "
                        "\"%s\"", prodp->info.ident);
                status = -1;
            }
            else {
                status = 0;
                must_free_data = 1;
            }
        }
        else {
            status = 0;
        }

        if (status == 0) {
            if (entry_isFlagSet(entry, FL_OVERWRITE)) {
                if (lseek(entry->handle.fd, 0, SEEK_SET) < 0) {
                    /*
                     * The "file" must be a pipe or FIFO.
                     */
                    log_syserr("Couldn't seek to beginning of file %s",
                    		entry->path);
                }
            }

            status = unio_out(entry, prodp, data, sz);
            if (status) {
                log_add("Couldn't write product to file \"%s\"", entry->path);
            }
            else {
                if (entry_isFlagSet(entry, FL_OVERWRITE)) {
                	const off_t fileSize = lseek(entry->handle.fd, 0, SEEK_CUR);

                	if (fileSize == (off_t)-1) {
						log_syserr("Couldn't get position in file %s",
								entry->path);
                	}
                	else {
						(void) ftruncate(entry->handle.fd, fileSize);
                	}
                }

                status = flushIfAppropriate(entry);
                if (status) {
                    log_add("Couldn't flush I/O to file \"%s\"", entry->path);
                }
                else {
                    if (entry_isFlagSet(entry, FL_LOG))
                        log_notice("Filed in \"%s\": %s", argv[argc - 1],
                                s_prod_info(NULL, 0, &prodp->info,
                                        log_is_enabled_debug));
                    if (entry_isFlagSet(entry, FL_EDEX) && shared_id != -1) {
                        semarg.val = queue_counter;
                        (void)semctl(sem_id, 1, SETVAL, semarg);
                        queue_counter = (queue_counter == largest_queue_element)
                                ? 0
                                : queue_counter + 1;
                    }
                }
            } /* data written */

            if (must_free_data)
                free(data);
        } /* data != NULL */

        if (status || entry_isFlagSet(entry, FL_CLOSE))
            fl_removeAndFree(entry, status ? &DR_FAILED : &DR_CLOSED);
    } /* entry != NULL */

    return status ? -1 : 0;
}

/* End UNIXIO */

/* Begin STDIO */
/*
 * Opens an output-file for the STDIOFILE action.
 *
 * Arguments:
 *      entry   Pointer to the relevant entry in the pattern/action list.
 *      ac      Number of arguments.
 *      av      Pointer to pointers to arguments.
 * Returns:
 *      -1      Failure.  An error-message is logged.
 *      else    File descriptor of the output-file.
 */
static int stdio_open(
        fl_entry *entry,
        int ac,
        char **av)
{
    char* path;
    int flags = (O_WRONLY | O_CREAT);
    int fd;
    char* mode = "a";

    log_assert(ac > 0);
    log_assert(av[ac -1] != NULL);
    log_assert(*av[ac -1] != 0);

    entry->handle.stream = NULL;

    unsigned nopt = decodeOptions(entry, ac, av, &OPT_OVERWRITE, &OPT_STRIP,
            &OPT_LOG, &OPT_STRIPWMO, &OPT_FLUSH, &OPT_CLOSE, NULL);
    ac -= nopt;
    av += nopt;

    if (entry_isFlagSet(entry, FL_OVERWRITE)) {
        flags |= O_TRUNC;
        mode = "w";
    }

    path = av[ac - 1];

    while ((fd = mkdirs_open(path, flags, 0666)) == -1 &&
			(errno == EMFILE || errno == ENFILE) && thefl->size > 0)
		fl_closeLru(0); // 0 => unconditional removal

    if (-1 == fd) {
    	log_clear();
        log_syserr("mkdirs_open: %s", path);
    }
    else {
        /*
         * Ensure that the file descriptor will close upon execution of an
         * exec(2) family function because no child processes should inherit it.
         */
        int status = ensureCloseOnExec(fd);
        if (status) {
            log_error_q("Couldn't open STDIOFILE output-file");
        }
        else {
            entry->handle.stream = fdopen(fd, mode);

            if (NULL == entry->handle.stream) {
                log_syserr("fdopen: %s", path);
            }
            else {
                if (!entry_isFlagSet(entry, O_TRUNC)) {
                    if (fseek(entry->handle.stream, 0, SEEK_END) < 0) {
                        /*
                         * The "file" must be a pipe or FIFO.
                         */
                        log_syserr("stdio_open(): Couldn't seek to EOF: %s", path);
                    }
                }

                strncpy(entry->path, path, PATH_MAX);
                entry->path[PATH_MAX - 1] = 0; /* just in case */
                log_debug("%d", fileno(entry->handle.stream));
                status = 0;
            } /* entry->handle.stream allocated */
        } /* output-file set to close-on-exec */

        if (status) {
            (void) close(fd);
            fd = -1;
        }
    } /* "fd" open */

    return fd;
}

static void stdio_close(
        fl_entry *entry)
{
    log_debug("%d",
            entry->handle.stream ? fileno(entry->handle.stream) : -1);
    if (entry->handle.stream != NULL ) {
        if (fclose(entry->handle.stream) == EOF) {
            log_syserr("fclose: %s", entry->path);
        }
    }
    entry->handle.stream = NULL;
}

/*ARGSUSED*/
static int stdio_sync(
        fl_entry *entry,
        int block)
{
    log_debug("%d",
            entry->handle.stream ? fileno(entry->handle.stream) : -1);

    if (fflush(entry->handle.stream) == EOF) {
        if (EINTR != errno) {
            log_syserr("Couldn't flush I/O to file \"%s\"", entry->path);
            // disable flushing on I/O error
            entry_unsetFlag(entry, FL_NEEDS_SYNC);
        }

        return errno;
    }

    entry_unsetFlag(entry, FL_NEEDS_SYNC);

    return 0;
}

/*ARGSUSED*/
static int stdio_put(
        fl_entry *entry,
        const char *ignored,
        const void *data,
        size_t sz)
{
    log_debug("%d", fileno(entry->handle.stream));
    fl_makeHead(entry);

    size_t nwrote = fwrite(data, 1, sz, entry->handle.stream);

    if (nwrote != sz) {
        if (errno != EINTR) {
            log_syserr("fwrite() error: \"%s\"", entry->path);
            // disable flushing on I/O error
            entry_unsetFlag(entry, FL_NEEDS_SYNC);
        }

        return -1;
    }

    entry_setFlag(entry, FL_NEEDS_SYNC);

    return 0;
}

static struct fl_ops stdio_ops = { str_cmp, stdio_open, stdio_close, stdio_sync};

/*ARGSUSED*/
int stdio_prodput(
        const product* const restrict prodp,
        const int                     argc,
        char** const restrict         argv,
        const void* const restrict    ignored,
        const size_t                  also_ignored)
{
    int		status = -1; /* failure */
    fl_entry*	entry = fl_getEntry(STDIO, argc, argv, NULL);
    char	must_free_data = 0;

    log_debug("%d %s",
            entry == NULL ? -1 : fileno(entry->handle.stream), prodp->info.ident);

    if (entry != NULL ) {
        size_t	sz = prodp->info.sz;
        void	*data = prodp->data;

        if (entry_isFlagSet (entry, FL_STRIPWMO)) {
            data = stripHeaders (data, &sz);
        }

        if (entry_isFlagSet(entry, FL_STRIP)) {
            data = dupstrip(data, sz, &sz);
            if (data == NULL) {
        	log_add("Couldn't strip control-characters out of product "
                        "\"%s\"", prodp->info.ident);
                status = -1;
            } else {
                status = 0;
                must_free_data = 1;
            }
        }

        if (data != NULL ) {
            if (entry_isFlagSet(entry, FL_OVERWRITE)) {
                if (fseek(entry->handle.stream, 0, SEEK_SET) < 0) {
                    /*
                     * The "file" must be a pipe or FIFO.
                     */
                    log_syserr("Couldn't seek to beginning of file %s",
                    		entry->path);
                }
            }

            status = stdio_put(entry, prodp->info.ident, data, sz);

            if (status == 0) {
                if (entry_isFlagSet(entry, FL_OVERWRITE)) {
                	const off_t fileSize = ftello(entry->handle.stream);

                	if (fileSize == (off_t)-1) {
						log_syserr("Couldn't get position in file %s",
								entry->path);
                	}
                	else {
						(void) ftruncate(fileno(entry->handle.stream),
								fileSize);
                	}
                }

                status = flushIfAppropriate(entry);

                if ((status == 0) && entry_isFlagSet(entry, FL_LOG))
                    log_notice("StdioFiled in \"%s\": %s", argv[argc - 1],
                            s_prod_info(NULL, 0, &prodp->info,
                                    log_is_enabled_debug));
            } /* data written */

            if (must_free_data)
                free(data);
        } /* data != NULL */

        if (status || entry_isFlagSet(entry, FL_CLOSE))
            fl_removeAndFree(entry, status ? &DR_FAILED : &DR_CLOSED);
    } /* entry != NULL */

    return status ? -1 : 0;
}

/* End STDIO */

/* Begin PIPE */

/*
 * Concatenates arguments into one, long, NUL-terminated string.
 *
 * Arguments:
 *      buf     Point to buffer into which to concatenate arguments.
 *      len     Maximum number of characters that can be put into buffer
 *              excluding terminating NUL.
 *      argc    Number of arguments.
 *      argv    Pointer to "argc" pointers to the arguments.
 * Returns:
 *      Number of characters placed in "buf" excluding terminating NUL.
 */
static int argcat(
        char *buf,
        int len,
        int argc,
        char **argv)
{
    int cnt = 0;
    char *cp;

    while (argc-- > 0 && (cp = *argv++) != NULL ) {
        if (len <= cnt)
            break;

        if (cnt)
            buf[cnt++] = ' ';

        while (*cp != 0) {
            if (len <= cnt)
                break;
            buf[cnt++] = *cp++;
        }
    }
    buf[cnt] = 0;
    return cnt;
}

static int argcat_cmp(
        fl_entry *entry,
        int argc,
        char **argv)
{
    char buf[PATH_MAX];

    log_assert(argc > 0);
    log_assert(argv[0] != NULL);
    log_assert(*argv[0] != 0);

    argcat(buf, sizeof(buf) - 1, argc, argv);
    return (strcmp(buf, entry->path));
}

/*
 * Set to non-root privilege if possible.
 * Do it in such a way that it is safe to fork.
 * TODO: this is duplicated from ../server/priv.c
 */
void endpriv(
        void)
{
    const uid_t euid = geteuid();
    const uid_t uid = getuid();

    /* if either euid or uid is unprivileged, use it */
    if (euid > 0)
        (void)setuid(euid);
    else if (uid > 0)
        (void)setuid(uid);

    /* else warn??? or set to nobody??? */
}

/* 
 * Open a pipe to a child decoder process.
 *
 * Arguments:
 *      entry   Pointer to the entry in the pattern/action list.
 *      argc    Number of arguments of the decoder invocation command.
 *      argv    Pointer to pointers to arguments of the decoder invocation
 *              command.
 * Returns:
 *      -1      Failure.  An error-message is logged.
 *      else    File descriptor of the write-end of the pipe.
 */
static int pipe_open(
        fl_entry *entry,
        int argc,
        char **argv)
{
    int    ac = argc;
    char** av = argv;
    int    pfd[2];
    int    writeFd = -1; /* failure */

    log_assert(argc >= 1);
    log_assert(argv[0] != NULL && *argv[0] != 0);
    log_assert(argv[argc] == NULL);

    entry->handle.pbuf = NULL;
    entry_setFlag(entry, FL_NOTRANSIENT);

    unsigned nopt = decodeOptions(entry, ac, av, &OPT_TRANSIENT, &OPT_STRIP,
            &OPT_METADATA, &OPT_NODATA, &OPT_STRIPWMO, &OPT_FLUSH, &OPT_CLOSE, NULL);
    // ac -= nopt; // not used
    av += nopt;

    if (entry_isFlagSet(entry, FL_NODATA))
        entry_setFlag(entry, FL_METADATA);

    /*
     * Create a pipe into which the parent pqact(1) process will write one or
     * more data-products and from which the child decoder process will read.
     */
    int status;
    while ((status = pipe(pfd)) == -1 && (errno == EMFILE || errno == ENFILE) &&
    		thefl->size > 0)
		fl_closeLru(0); // 0 => unconditional removal

    if (status == -1) {
    	log_clear();
        log_syserr("Couldn't create pipe");
    }
    else {
        /*
         * Ensure that the write-end of the pipe will close upon execution
         * of an exec(2) family function because no child processes should
         * inherit it.
         */
        status = ensureCloseOnExec(pfd[1]);
        if (status) {
            log_error_q("Couldn't set write-end of pipe to close on exec()");
        }
        else {
            pid_t pid;

            while ((pid = ldmfork()) == -1 && errno == EAGAIN &&
            		thefl->size > 0) {
				fl_closeLru(0); // Too many child processes
				log_clear();
            }

            if (-1 == pid) {
                log_syserr("Couldn't fork(2) PIPE process");
            }
            else {
                if (0 == pid) {
                    /*
                     * Child process.
                     */
                    (void)signal(SIGTERM, SIG_DFL);
                    (void)pq_close(pq);
                    pq = NULL;

                    /*
                     * This process is made its own process-group leader to
                     * isolate it from signals sent to the LDM process-group
                     * (e.g., SIGCONT, SIGINT, SIGTERM, SIGUSR2, SIGUSR2).
                     */
                    if (setpgid(0, 0) == -1) {
                        log_warning_q(
                                "Couldn't make decoder a process-group leader");
                    }

                    /*
                     * It is assumed that the standard output and error streams
                     * are correctly established and should not be modified.
                     */

                    /*
                     * Associate the standard input stream with the read-end of
                     * the pipe.
                     */
                    if (STDIN_FILENO != pfd[0]) {
                        if (-1 == dup2(pfd[0], STDIN_FILENO)) {
                            log_syserr("Couldn't redirect standard input to "
                                    "read-end of pipe: pfd[0]=%d", pfd[0]);
                        }
                        else {
                            (void)close(pfd[0]);
                            pfd[0] = STDIN_FILENO;
                        }
                    }

                    if (STDIN_FILENO == pfd[0]) {
                        endpriv();
                        log_info_q("Executing decoder \"%s\"", av[0]);
                        (void)execvp(av[0], &av[0]);
                        log_syserr("Couldn't execute decoder \"%s\"; "
                                "PATH=%s", av[0], getenv("PATH"));
                    }

                    exit(EXIT_FAILURE); // cleanup() calls log_fini()
                } /* child process */
                else {
                    /*
                     * Parent process.
                     *
                     * Close the read-end of the pipe because it won't be used.
                     */
                    (void) close(pfd[0]);

                    /*
                     * Create a pipe-buffer with pfd[1] as the output file
                     * descriptor.
                     */
                    #ifdef PIPE_BUF
                        entry->handle.pbuf = new_pbuf(pfd[1], PIPE_BUF);
                    #else
                        entry->handle.pbuf = new_pbuf(pfd[1], _POSIX_PIPE_BUF);
                    #endif

                    if (NULL == entry->handle.pbuf) {
                        log_add_syserr("Couldn't create pipe-buffer");
                        log_flush_error();
                    }
                    else {
                        entry->private = pid;
                        writeFd = pfd[1]; /* success */

                        argcat(entry->path, PATH_MAX - 1, argc, argv);
                        log_debug("%d %d", writeFd, pid);
                    }
                } /* parent process */
            } /* fork() success */
        } /* write-end of pipe is FD_CLOEXEC */

        if (-1 == writeFd) {
            (void) close(pfd[1]);
            (void) close(pfd[0]);
        }
    } /* pipe() success */

    return writeFd;
}

static int pipe_sync(
        fl_entry *entry,
        int block)
{
    log_debug("%d %s", entry->handle.pbuf->pfd, block ? "" : "non-block");

    int status = pbuf_flush(entry->handle.pbuf, block, pipe_timeo, entry->path);

    if (0 == status) {
        entry_unsetFlag(entry, FL_NEEDS_SYNC);
        return 0;
    }
    if (EAGAIN == status) {
        return 0;
    }
    if (EINTR != status) {
        log_add("Couldn't flush I/O to decoder: pid=%lu, cmd=\"%s\"",
                entry->private, entry->path);
        // disable flushing on I/O error
        entry_unsetFlag(entry, FL_NEEDS_SYNC);
    }

    return status;
}

static void pipe_close(
        fl_entry *entry)
{
    pid_t pid = (pid_t) entry->private;
    int pfd = -1;

    log_debug("%d, %d",
            entry->handle.pbuf ? entry->handle.pbuf->pfd : -1, pid);
    if (entry->handle.pbuf != NULL ) {
        if (pid >= 0 && entry_isFlagSet(entry, FL_NEEDS_SYNC)) {
            (void) pipe_sync(entry, TRUE);
        }
        pfd = entry->handle.pbuf->pfd;
        free_pbuf(entry->handle.pbuf);
    }
    if (pfd != -1) {
        if (close(pfd) == -1) {
            log_syserr("pipe close: %s", entry->path);
        }
        /*
         * The close should cause termination of the child
         * as the child reads EOF. The child is wait()'ed
         * upon synchronously in a loop in main().
         */
    }
    entry->handle.pbuf = NULL;
}

/*
 * N.B. New return convention:
 * returns ENOERR (0) or, on failure, the errno.
 */
/*ARGSUSED*/
static int pipe_put(
        fl_entry *entry,
        const char *ignored,
        const void *data,
        size_t sz)
{
    int status;

    //log_debug("%d",
            //entry->handle.pbuf ? entry->handle.pbuf->pfd : -1);
    fl_makeHead(entry);

    if (entry->handle.pbuf == NULL ) {
        log_add("NULL pipe-buffer");
        status = EINVAL;
    }
    else {
        if (entry_isFlagSet(entry, FL_NODATA)) {
            status = 0;
        }
        else {
            status = pbuf_write(entry->handle.pbuf, data, sz, pipe_timeo,
                    entry->path);

            if (status && status != EINTR) {
                /* don't waste time syncing an errored entry */
                entry_unsetFlag(entry, FL_NEEDS_SYNC);
            }
            else {
                entry_setFlag(entry, FL_NEEDS_SYNC);
            }
        }
    }

    return status;
}

static struct fl_ops pipe_ops = { argcat_cmp, pipe_open, pipe_close, pipe_sync};

/*
 * Writes the data-product creation-time to the pipe as
 *     integer portion                  uint64_t
 *     microseconds portion             int32_t
 * ARGUMENTS:
 *     entry    Pointer to file-list entry
 *     creation Pointer to data-product creation-time
 * RETURNS:
 *     ENOERR   Success
 *     !ENOERR  Failure
 */
static int pipe_putcreation(
        fl_entry* entry,
        const timestampt* creation)
{
    int status;
#if SIZEOF_UINT64_T*CHAR_BIT == 64
    uint64_t uint64 = (uint64_t) creation->tv_sec;
    status = pbuf_write(entry->handle.pbuf, (void*) &uint64,
            (u_int) sizeof(uint64_t), pipe_timeo, entry->path);
#else
    uint32_t lower32 = (uint32_t) creation->tv_sec;
#   if SIZEOF_LONG*CHAR_BIT <= 32
    uint32_t upper32 = 0;
#   else
    uint32_t upper32 =
            (uint32_t)(((unsigned long)creation->tv_sec) >> 32);
#   endif
#   if WORDS_BIGENDIAN
    uint32_t first32 = upper32;
    uint32_t second32 = lower32;
#   else
    uint32_t first32 = lower32;
    uint32_t second32 = upper32;
#   endif
    status = pbuf_write(entry->handle.pbuf, (void*) &first32,
            (u_int) sizeof(uint32_t), pipe_timeo);
    if (status == ENOERR) {
        status = pbuf_write(entry->handle.pbuf, (void*) &second32,
                (u_int) sizeof(uint32_t), pipe_timeo);
        if (status == ENOERR) {
            int32_t int32 = (int32_t) creation->tv_usec;
            status = pbuf_write(entry->handle.pbuf, (void*) &int32,
                    (u_int) sizeof(int32_t), pipe_timeo);
        }
    }
#endif
    return status;
}

/**
 * Writes the data-product metadata to the pipe as:
 *    - metadata-length in bytes                                 uint32_t
 *    - data-product signature (MD5 checksum)                    uchar[16]
 *    - data-product size in bytes                               uint32_t
 *    - product creation-time in seconds since the epoch:
 *            - integer portion                                  uint64_t
 *            - microseconds portion                             int32_t
 *    - data-product feedtype                                    uint32_t
 *    - data-product sequence number                             uint32_t
 *    - product-identifier:
 *            - length in bytes (excluding NUL)                  uint32_t
 *            - non-NUL-terminated string                        char[]
 *    - product-origin:
 *            - length in bytes (excluding NUL)                  uint32_t
 *            - non-NUL-terminated string                        char[]
 *
 * @param[in] entry  Open-file list entry.
 * @param[in] info   Data-product metadata.
 * @param[in] sz     Size of the data in bytes.
 * @retval    0      Success.
 * @return           `errno` error-code.
 */
static int pipe_putmeta(
        fl_entry* entry,
        const prod_info* info,
        uint32_t sz)
{
    int32_t int32;
    uint32_t uint32;
    uint32_t identLen = (uint32_t) strlen(info->ident);
    uint32_t originLen = (uint32_t) strlen(info->origin);
    uint32_t totalLen = 4 + 16 + 4 + 8 + 4 + 4 + 4 + (4 + identLen)
                + (4 + originLen);
    int status;

    status = pbuf_write(entry->handle.pbuf, (void*) &totalLen,
            (u_int) sizeof(totalLen), pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    status = pbuf_write(entry->handle.pbuf, (void*) &info->signature,
            (u_int) sizeof(info->signature), pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    status = pbuf_write(entry->handle.pbuf, (void*) &sz, (u_int) sizeof(sz),
            pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    status = pipe_putcreation(entry, &info->arrival);
    if (status != ENOERR)
        return status;

    int32 = (int32_t) info->arrival.tv_usec;
    status = pbuf_write(entry->handle.pbuf, (void*) &int32,
            (u_int)sizeof(int32), pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    uint32 = (uint32_t) info->feedtype;
    status = pbuf_write(entry->handle.pbuf, (void*) &uint32, (u_int) sizeof(uint32),
            pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    uint32 = (uint32_t) info->seqno;
    status = pbuf_write(entry->handle.pbuf, (void*) &uint32,
            (u_int)sizeof(uint32), pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    status = pbuf_write(entry->handle.pbuf, (void*) &identLen,
            (u_int)sizeof(identLen), pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    status = pbuf_write(entry->handle.pbuf, (void*) info->ident, identLen,
            pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    status = pbuf_write(entry->handle.pbuf, (void*) &originLen,
            (u_int) sizeof(originLen), pipe_timeo, entry->path);
    if (status != ENOERR)
        return status;

    status = pbuf_write(entry->handle.pbuf, (void*) info->origin, originLen,
            pipe_timeo, entry->path);

    return status;
}

/**
 * Writes a data-product to the file of an open-file list entry.
 *
 * @param[in] entry  Open-file list entry.
 * @param[in] info   Data-product metadata.
 * @param[in] data   Data to write.
 * @param[in] sz     Amount of data in bytes.
 * @retval    0      Success.
 * @return           `errno` error-code.
 */
static int pipe_out(
        fl_entry* const restrict        entry,
        const prod_info* const restrict info,
        const void* restrict            data,
        const uint32_t                  sz)
{
    int status = ENOERR;

    if (entry_isFlagSet(entry, FL_METADATA)) {
        status = pipe_putmeta(entry, info, sz);
        if (status)
            log_add("Couldn't write product metadata to pipe");
    }
    if (status == ENOERR && !entry_isFlagSet(entry, FL_NODATA)) {
        status = pipe_put(entry, info->ident, data, sz);
    }

    return status;
}

/**
 * Sends a data-product to a decoder via a pipe.
 *
 * @param[in] prodp         Data-product to be sent.
 * @param[in] argc          Number of decoder command arguments.
 * @param[in] argv          Decoder command arguments.
 * @param[in] ignored       Ignored.
 * @param[in] also_ignored  Ignored.
 * @retval    0             Success.
 * @retval    -1            Couldn't create relevant entry.
 * @retval    -1            Couldn't strip control-characters from data.
 * @return                  `errno` error code.
 */
/*ARGSUSED*/
int pipe_prodput(
        const product* const restrict prodp,
        const int                     argc,
        char** const restrict         argv,
        const void* const restrict    ignored,
        const size_t                  also_ignored)
{
    int		status = 0;
    size_t	sz = prodp->info.sz;
    bool	isNew;
    fl_entry*	entry = fl_getEntry(PIPE, argc, argv, &isNew);
    char	must_free_data = 0;

    if (entry == NULL ) {
        log_add("Couldn't get entry for product \"%s\"", prodp->info.ident);
        status = -1;
    }
    else {
        log_debug("%d %s",
                entry->handle.pbuf ? entry->handle.pbuf->pfd : -1,
                prodp->info.ident);

        void	*data = prodp->data;

        if (entry_isFlagSet (entry, FL_STRIPWMO)) {
            data = stripHeaders (data, &sz);
        }

        if (entry_isFlagSet(entry, FL_STRIP)) {
            data = dupstrip(data, sz, &sz);
            if (data == NULL) {
                log_add("Couldn't strip control-characters out of product "
                        "\"%s\"", prodp->info.ident);
                status = -1;
            } else {
                status = 0;
                must_free_data = 1;
            }
        }

        if (0 == status) {
            status = pipe_out(entry, &prodp->info, data, sz);

            if (EPIPE == status && !isNew) {
                /*
                 * The entry's decoder, which was started by a previous
                 * invocation, terminated prematurely (which shouldn't have
                 * happened). Remove the entry, free its resources, and try
                 * again -- once.
                 */
                fl_removeAndFree(entry, &DR_FAILED);
                entry = fl_getEntry(PIPE, argc, argv, &isNew);
                if (entry == NULL ) {
                    log_add("Couldn't get entry for product \"%s\"",
                            prodp->info.ident);
                    status = -1;
                }
                else {
                    status = pipe_out(entry, &prodp->info, data, sz);
                    if (status)
                        log_add("Couldn't re-pipe product to decoder \"%s\"",
                                entry->path);
                }
            }

            if (status == 0) {
                status = flushIfAppropriate(entry);
                if (status)
                    log_add("Couldn't flush pipe to decoder \"%s\"",
                            entry->path);
            }

            if (must_free_data)
                free(data);
        }       // `data` possibly allocated

        if (entry && (status || entry_isFlagSet(entry, FL_CLOSE)))
            fl_removeAndFree(entry, status ? &DR_FAILED : &DR_CLOSED);
    }   // Got initial entry

    return status ? -1 : 0;
}

/*ARGSUSED*/
int spipe_prodput(
        const product* const restrict prod,
        const int                     argc,
        char** const restrict         argv,
        const void* const restrict    ignored,
        const size_t                  also_ignored)
{
    fl_entry *entry;
    char *buffer;
    size_t len;
    unsigned long offset;
    int status = ENOERR;

    typedef union {
        unsigned long ulong;
        char cu_long[sizeof(unsigned long)];
    } conv;
    conv key_len;
    conv data_len;
    conv sync;

    entry = fl_getEntry(PIPE, argc, argv, NULL);
    log_debug("%d %s",
            (entry != NULL && entry->handle.pbuf) ? entry->handle.pbuf->pfd : -1,
                    prod->info.ident);
    if (entry == NULL )
        return -1;

    /*
     **---------------------------------------------------------
     ** Place the following information into dbuf_val for
     ** writing to the pipe:
     **
     ** unsigned long SPIPE_SYNC
     ** unsigned long key_len
     ** char *key
     ** unsigned long data_len  (this includes ETX/RS makers)
     ** char *data
     ** char SPIPE_ETX
     ** char SPIPE_RS
     **
     ** First, get lengths of key and data to allocate space
     ** in a temporary buffer.
     **
     **---------------------------------------------------------
     */
#ifndef SPIPE_SYNC
#define SPIPE_SYNC 0x1DFCCF1A
#endif /* !SPIPE_SYNC */

#ifndef SPIPE_ETX
#define SPIPE_ETX '\003'
#endif /* !SPIPE_ETX */

#ifndef SPIPE_RS
#define SPIPE_RS '\036'
#endif /* !SPIPE_ETX */

    key_len.ulong = strlen(prod->info.ident);
    data_len.ulong = prod->info.sz + 2;
    sync.ulong = SPIPE_SYNC;

    len = (unsigned) (sizeof(unsigned long) + sizeof(key_len.cu_long)
            + strlen(prod->info.ident) + sizeof(data_len.cu_long) + prod->info.sz
            + 2);

    buffer = calloc(1, len);

    /*---------------------------------------------------------
     ** Now place the individual items into the buffer
     **-------------------------------------------------------*/

    offset = 0;

    memcpy(buffer + offset, sync.cu_long, sizeof(sync.cu_long));
    offset = offset + sizeof(unsigned long);

    memcpy(buffer + offset, key_len.cu_long, sizeof(key_len.cu_long));
    offset = offset + sizeof(key_len);

    memcpy(buffer + offset, prod->info.ident, (size_t) key_len.ulong);
    offset = offset + key_len.ulong;

    memcpy(buffer + offset, data_len.cu_long, sizeof(data_len.cu_long));
    offset = offset + sizeof(data_len);

    memcpy(buffer + offset, prod->data, prod->info.sz);

    /*---------------------------------------------------------
     ** Terminate the message with ETX & RS
     **-------------------------------------------------------*/
    buffer[len - 2] = SPIPE_ETX;
    buffer[len - 1] = SPIPE_RS;

    log_debug("size = %d\t%d %d %d", prod->info.sz, buffer[len - 3],
            buffer[len - 2], buffer[len - 1]);

    /*---------------------------------------------------------
     ** Send this stuff and tidy up
     **-------------------------------------------------------*/
    status = pipe_put(entry, prod->info.ident, buffer, len);
    if (status == EPIPE) {
        /*
         * In case the decoder exited and we haven't yet reaped,
         * try again once.
         */
        fl_removeAndFree(entry, &DR_FAILED);
        log_error_q("trying again");
        entry = fl_getEntry(PIPE, argc, argv, NULL);
        if (entry == NULL )
            return -1;
        status = pipe_put(entry, prod->info.ident, buffer, len);
    }
    free(buffer);

    if (0 == status)
        status = flushIfAppropriate(entry);

    if (status || entry_isFlagSet(entry, FL_CLOSE))
        fl_removeAndFree(entry, status ? &DR_FAILED : &DR_CLOSED);

    return status ? -1 : 0;
}

int xpipe_prodput(
        const product* const restrict prod,
        const int                     argc,
        char** const restrict         argv,
        const void* const restrict    xprod,
        const size_t                  xlen)
{
    int status = ENOERR;
    fl_entry *entry;

    entry = fl_getEntry(PIPE, argc, argv, NULL);
    log_debug("%d %s",
            (entry != NULL && entry->handle.pbuf) ? entry->handle.pbuf->pfd : -1,
                    prod->info.ident);
    if (entry == NULL )
        return -1;

    status = pipe_put(entry, prod->info.ident, xprod, xlen);
    if (status == EPIPE) {
        /*
         * In case the decoder exited and we haven't yet reaped,
         * try again once.
         */
        fl_removeAndFree(entry, &DR_FAILED);
        log_error_q("trying again");
        entry = fl_getEntry(PIPE, argc, argv, NULL);
        if (entry == NULL )
            return -1;
        status = pipe_put(entry, prod->info.ident, xprod, xlen);
    }

    if (0 == status)
        status = flushIfAppropriate(entry);

    if (status || entry_isFlagSet(entry, FL_CLOSE))
        fl_removeAndFree(entry, status ? &DR_FAILED : &DR_CLOSED);

    return status ? -1 : 0;
}
/* End PIPE */

#ifndef NO_DB
# ifdef USE_GDBM
/* namespace conflict with gdbm_open, etc, so using prefix ldmdb_ */

/*
 * called in gdbm when it tries to punt
 * If we didn't provide this function, gdbm would print the
 * message and call exit(-1).
 */
static void ldmdb_fatal(
        const char * str)
{
    log_add_syserr("ldmdb_fatal(): %s", str);
    log_flush_error();
}

/*
 * two or 3 args:
 *      pathname flag [dblocksize]
 *      if flag is 0 open read/write/create, otherwise open readonly
 */
static int ldmdb_open(
        fl_entry *entry,
        int argc,
        char **argv)
{
    char *path;
    GDBM_FILE db;
    /* default: choose to optimize for space over time */
#define DEFAULT_DBLOCKSIZE 512
    int dblocksize = DEFAULT_DBLOCKSIZE;

    entry->handle.db = NULL;
    path = argv[0];
    int read_write = atoi(argv[1]);

    if (argc > 2) {
        long tmp = atoi(argv[2]);

        if (0 < tmp) {
            dblocksize = (int) tmp;
        }
        else {
            log_error_q("%s: -dblocksize %s invalid", path, argv[1]);
        }
    }

    if (read_write != GDBM_READER) /* not read only */
    {
        /* create directories if needed */
        if (diraccess(path, (R_OK | W_OK), !0) == -1) {
            log_add_syserr("Couldn't access directories leading to %s", path);
            log_flush_error();
            return -1;
        }
    }

    /*
     * NB: It would be nice to set the GDBM file descriptor to close
     * on exec(), but that doesn't appear to be possible.
     */

    while ((db = gdbm_open(path, dblocksize, read_write, 0664, ldmdb_fatal)) ==
    		NULL && (errno == EMFILE || errno == ENFILE) && thefl->size > 0)
		fl_closeLru(0); // 0 => unconditional removal

    if (db == NULL ) {
    	log_clear();
        log_syserr("gdbm_open: %s", path);
        return -1;
    }
    entry->handle.db = db;
    entry->private = read_write;
    strncpy(entry->path, path, PATH_MAX);
    entry->path[PATH_MAX - 1] = 0; /* just in case */
    log_debug("%s", entry->path);
    return 0;
}

static void ldmdb_close(
        fl_entry *entry)
{
    log_debug("%s", entry->path);
    if (entry->handle.db != NULL )
        gdbm_close(entry->handle.db);
    entry->private = 0;
    entry->handle.db = NULL;
}

static int ldmdb_cmp(
        fl_entry *entry,
        int argc,
        char **argv)
{
    char *path;
    int read_write;
    int cmp;

    log_assert(argc > 1);
    log_assert(argv[0] != NULL);
    log_assert(*argv[0] != 0);

    path = argv[0];
    read_write = atoi(argv[1]);

    cmp = strcmp(path, entry->path);
    if (cmp == 0) {
        if (read_write != GDBM_READER && read_write != entry->private) {
            /*
             * the flags don't match, so close and reopen
             */
            ldmdb_close(entry);
            if (ldmdb_open(entry, argc, argv) < 0)
                cmp = -1;
        }
    }
    return cmp;
}

/*ARGSUSED*/
static int ldmdb_sync(
        fl_entry *entry,
        int block)
{
    /* there is no gdbm_sync */
    log_debug("%s", entry->handle.db ? entry->path : "");
    entry_unsetFlag(entry, FL_NEEDS_SYNC);
    return (0);
}

/*ARGSUSED*/
static int ldmdb_put(
        fl_entry *entry,
        const char *keystr,
        const void *data,
        size_t sz)
{
    datum key, content;
    int status;

    key.dptr = (char *) keystr /* N.B. cast away const */;
    key.dsize = (int) strlen(key.dptr) + 1; /* include the \0 */

    content.dptr = (char *) data; /* N.B. cast away const */
    content.dsize = (int) sz;

#if defined(DB_CONCAT) && !DB_XPROD
    /* concatenate duplicate keys  */
    /*
     * Code for concatenating data when the key is a duplicate.
     * Contributed 9/17/91 JCaron/PNeilley/LCarson
     * Wrecks idea of "product" when applied at this layer, so
     * only define DB_CONCAT when DB_XPROD is not defined.
     */

    status = gdbm_store(entry->handle.db, key, content, GDBM_INSERT);
    if (status == 1) {
        int size;
        datum old_stuff, new_stuff;
        old_stuff = gdbm_fetch(entry->handle.db, key);
        log_debug("\tConcatenating data under key %s", key.dptr);
        if (NULL == old_stuff.dptr) {
            log_syserr("Inconsistent Duplicate Key storage");
            return -1;
        }
        size = content.dsize + old_stuff.dsize;
        if (NULL == (new_stuff.dptr = malloc(size))) {
            log_syserr("malloc failed");
            free(old_stuff.dptr);
            return -1;
        }
        memcpy(new_stuff.dptr, old_stuff.dptr, old_stuff.dsize);
        memcpy(&new_stuff.dptr[old_stuff.dsize], content.dptr, content.dsize);
        new_stuff.dsize = size;
        status = gdbm_store(entry->handle.db, key, new_stuff, GDBM_REPLACE);
        free(new_stuff.dptr);
        free(old_stuff.dptr);
    }

#else
    /* TODO: replace flag */
    status = gdbm_store(entry->handle.db, key, content, GDBM_REPLACE);
#endif
    return status;
}

# else /*USE_GDBM*/

/*
 * two or 3 args:
 *      pathname flag [dblocksize]
 *      if flag is 0 open read/write/create, otherwise open readonly
 */
static int
ldmdb_open(fl_entry *entry, int ac, char **av)
{
    const char *path;
    int flags = (O_WRONLY|O_CREAT);

    log_assert(ac > 0);
    log_assert(av[ac -1] != NULL);
    log_assert(*av[ac -1] != 0);

    entry->handle.db = NULL;

    unsigned nopt = decodeOptions(entry, ac, av, &OPT_OVERWRITE, &OPT_STRIP,
            &OPT_FLUSH, &OPT_CLOSE, NULL);
    ac -= nopt;
    av += nopt;

    if (entry_isFlagSet(entry, FL_OVERWRITE))
        flags |= O_TRUNC;

    path = av[ac-1];

    /* create directories if needed */
    if(diraccess(path, (R_OK | W_OK), 1) == -1)
    {
        log_add_syserr("Couldn't access directories leading to %s", path);
        log_flush_error();
        return -1;
    }

    /*
     * NB: It would be nice to set the DBM file descriptor to close
     * on exec(), but that doesn't appear to be possible.
     */

    entry->handle.db = dbm_open(path, flags, 0666);
    if(entry->handle.db == NULL)
    {
        if(errno == EMFILE || errno == ENFILE)
        {
            /* Too many open files */
            fl_closeLru(0);
            fl_closeLru(0);
            fl_closeLru(0);
            fl_closeLru(0);
        }
        log_syserr("ldmdb_open: %s", path);
        return -1;
    }
    strncpy(entry->path, path, PATH_MAX);
    entry->path[PATH_MAX-1] = 0; /* just in case */
    log_debug("%s", entry->path);
    return 0;
}

static void
ldmdb_close(fl_entry *entry)
{
    log_debug("%s", entry->path);
    if(entry->handle.db != NULL)
        dbm_close(entry->handle.db);
    entry->private = 0;
    entry->handle.db = NULL;
}

static int
ldmdb_cmp(fl_entry *entry, int argc, char **argv)
{
    return str_cmp(entry, argc, argv);
}

/*ARGSUSED*/
static int
ldmdb_sync(fl_entry *entry, int block)
{
    /* there is no dbm_sync */
    log_debug("%s",
            entry->handle.db ? entry->path : "");
    entry_unsetFlag(entry, FL_NEEDS_SYNC);
    return(0);
}

/*ARGSUSED*/
static int
ldmdb_put(fl_entry *entry, const char *keystr,
        const void *data, size_t sz)
{
    datum key, content;
    int status;

    key.dptr = (char *) keystr /* N.B. cast away const */;
    key.dsize = (int) strlen(key.dptr) + 1; /* include the \0 */

    content.dptr = (char *) data; /* N.B. cast away const */
    content.dsize = (int)sz;

#if defined(DB_CONCAT) && !DB_XPROD
    /* concatenate duplicate keys  */
    /*
     * Code for concatenating data when the key is a duplicate.
     * Contributed 9/17/91 JCaron/PNeilley/LCarson
     * Wrecks idea of "product" when applied at this layer, so
     * only define DB_CONCAT when DB_XPROD is not defined.
     */

    status = dbm_store(entry->handle.db, key, content, DBM_INSERT);
    if (status == 1 )
    {
        int size;
        datum old_stuff, new_stuff;
        old_stuff = dbm_fetch(entry->handle.db, key);
        log_debug("\tConcatenating data under key %s", key.dptr);
        if (NULL == old_stuff.dptr)
        {
            log_syserr("Inconsistent Duplicate Key storage");
            return -1;
        }
        size = (int)(content.dsize+old_stuff.dsize);
        if (NULL == (new_stuff.dptr = malloc(size)))
        {
            log_syserr("malloc failed");
            free (old_stuff.dptr);
            return -1;
        }
        memcpy(new_stuff.dptr, old_stuff.dptr, old_stuff.dsize);
        memcpy(&((char *)new_stuff.dptr)[old_stuff.dsize],
                content.dptr, content.dsize);
        new_stuff.dsize = size;
        status = dbm_store(entry->handle.db, key, new_stuff, DBM_REPLACE);
        free (new_stuff.dptr);
        free (old_stuff.dptr);
    }

#else
    /* TODO: replace flag */
    status = dbm_store(entry->handle.db, key, content, DBM_REPLACE);
#endif
    return status;
}
# endif /*USE_GDBM*/

static struct fl_ops ldmdb_ops = { ldmdb_cmp, ldmdb_open, ldmdb_close,
        ldmdb_sync};

/*ARGSUSED*/
int ldmdb_prodput(
        const product *prod,
        int ac,
        char **av,
        const void *xp,
        size_t xlen)
{
    fl_entry *entry;
    int status;
    int closeflag = 0;

    const char *keystr;
    char *dblocksizep = NULL;
    char *gdbm_wrcreat = "2";

    for (; ac > 1 && *av[0] == '-'; ac--, av++) {
        if (strncmp(*av, "-close", 3) == 0)
            closeflag = 1;
        else if (strncmp(*av, "-dblocksize", 3) == 0) {
            ac--;
            av++;
            dblocksizep = *av;
        }
        else
            log_error_q("Invalid argument %s", *av);

    }

    {
        /* set up simple argc, argv for ldmdb_open */
        int argc = 0;
        char *argv[4];
        argv[argc++] = av[0];
        argv[argc++] = gdbm_wrcreat;
        if (dblocksizep != NULL )
            argv[argc++] = dblocksizep;
        argv[argc] = NULL;
        entry = fl_getEntry(FT_DB, argc, argv, NULL);
        log_debug("%s %s", entry == NULL ? "" : entry->path,
                prod->info.ident);
        if (entry == NULL )
            return -1;
    }

    ac--;
    av++;

    if (ac >= 0 && av[0] != NULL && *av[0] != 0) {
        /* use command line arg as key */
        keystr = av[0];
    }
    else {
        /* use product->ident */
        keystr = prod->info.ident;
    }

#if DB_XPROD
    status = ldmdb_put(entry, keystr, xp, xlen);
#else
    status = ldmdb_put(entry, keystr, prod->data, prod->info.sz);
#endif

    if (status == -1) {
        log_error_q("%s error for %s, dbkey %s", entry->path, prod->info.ident,
                keystr);
    }
    if (closeflag || status == -1) {
        fl_removeAndFree(entry, -1 == status ? &DR_FAILED : &DR_CLOSED);
    }

    return status ? -1 : 0;
}

#endif /* !NO_DB */

static fl_entry *
entry_new(
        const ft_t   type,
        const int    argc,
        char** const argv)
{
    fl_entry*      entry;
    struct fl_ops* ops;

    switch (type) {
    case UNIXIO:
        ops = &unio_ops;
        break;
    case STDIO:
        ops = &stdio_ops;
        break;
    case PIPE:
        ops = &pipe_ops;
        break;
    case FT_DB:
        #ifndef NO_DB
            ops = &ldmdb_ops;
        #else
            log_add("DB type not enabled");
            ops = NULL;
        #endif
        break;
    default:
        log_add("unknown type %d", type);
        ops = NULL;
    }

    if (NULL == ops) {
        entry = NULL;
    }
    else {
        entry = Alloc(1, fl_entry);

        if (NULL == entry) {
            log_add_syserr("malloc() failure");
        }
        else {
            entry->ops = ops;
            entry->flags = 0;
            entry->type = type;
            entry->next = NULL;
            entry->prev = NULL;
            entry->path[0] = 0;
            entry->private = 0;
            entry->lastUse = time(NULL);

            if (entry->ops->open(entry, argc, argv) == -1) {
                free(entry);
                entry = NULL;
            }
        } // `entry` allocated
    } // `ops` set

    return entry;
}

/**
 * Sets the number of available file descriptors.
 *
 * @param[in] fdCount         The number of available file descriptors.
 * @retval    0               Success.
 * @retval    -1              Failure.  Reason is logged.
 */
int
set_avail_fd_count(
        unsigned fdCount)
{
    int error;

    if (fdCount <= 1) {
        log_error_q("Invalid file-descriptor count: %ld", fdCount);
        error = -1;
    }
    else {
        /*
         * Ensure that two file descriptors will be available to the last entry
         * in the list because, if this entry is a PIPE action, then it will
         * need two because it uses the pipe(2) system-call.
         */
        maxEntries = fdCount - 1;
        error = 0;
    }

    return error;
}

int set_shared_space(
        int shid,
        int semid,
        unsigned size)
{
    int error;
    if (shid == -1 || semid == -1) {
        log_error_q("Shared memory is not available.  Notification system disabled.");
        error = -1;
    }
    else {
        shared_id = shid;
        sem_id = semid;
        shared_size = size;
        semarg.val = size;
        semctl(sem_id, 0, SETVAL, semarg);
        semarg.val = -1;
        semctl(sem_id, 1, SETVAL, semarg);
        largest_queue_element = shared_size - 1;
        error = 0;
    }
    return error;
}

/*
 * Returns the maximum number of file-descriptors that one process can have 
 * open at any one time.
 *
 * NOTE: Under FreeBSD 4.9-RELEASE-p11, OPEN_MAX is 64 but 
 * sysconf(_SC_OPEN_MAX) returns 11095!
 */
long openMax()
{
    static long max = 0;

    if (0 == max) {
#       ifdef OPEN_MAX
        max = OPEN_MAX; /* minimum value: 20 */
#       else
        /*
         * The value must be determined using sysconf().
         */
        max = sysconf(_SC_OPEN_MAX);

        if (-1 == max) {
            /*
             * The value can't be determined.  Fallback to the Standard
             * UNIX value.
             */
            max = _POSIX_OPEN_MAX; /* 16 by definition */
        }
#       endif
    }

    return max;
}

/*
 * Waits-upon one or more child processes.
 *
 * Arguments:
 *      pid             The PID of the process upon which to wait.  If 
 *                      (pid_t)-1, then any child process is waited-upon.
 *      options         Bitwise or of WCONTINUED, WNOHANG, or WUNTRACED.
 *                      See waitpid().
 * Returns:
 *      -1              Failure.  "errno" is set.
 *      0               "options" & WNOHANG is true and status isn't available
 *                      for process "pid".
 *      else            PID of the waited-upon process.
 */
pid_t reap(
        const pid_t pid,
        const int options)
{
    int status = 0;
    const pid_t wpid = waitpid(pid, &status, options);

    if (wpid == -1) {
        if (!(errno == ECHILD && pid == -1)) {
            /*
             * Unwaited-for child processes exist.
             */
            log_syserr("waitpid()");
        }
    }
    else if (wpid != 0) {
        fl_entry* const entry = fl_findByPid(wpid);
        const char*     cmd;
        const char*     childType;
        int             isExec = 0;

        /*
         * "entry" will be NULL if
         *     * The process corresponding to `wpid` is not a `PIPE` decoder;
         *       or
         *     * The corresponding process is a `PIPE` decoder and the
         *       corresponding entry was removed by `fl_removeAndFree()`
         *       because
         *         - The `-close` option was specified; or
         *         - The entry was deleted by `fl_closeLru()`; or
         *         - An I/O error occurred writing to the pipe.
         */
        if (NULL != entry) {
            cmd = entry->path;
            childType = TYPE_NAME[entry->type];
        }
        else {
            cmd = cm_get_command(execMap, wpid);

            if (NULL == cmd) {
                childType = "";
            }
            else {
                childType = "EXEC";
                isExec = 1;
            }
        }

        // `entry != NULL` => `isExec == false`
        // `isExec == true` => `entry == NULL`
        // `isExec == false` => `entry != NULL || cmd == NULL`

        if (WIFSTOPPED(status)) {
            log_notice_q(cmd
                        ? "child %d stopped by signal %d (%s %s)"
                        : "child %d stopped by signal %d",
                    wpid, WSTOPSIG(status), childType, cmd);
        }
        else if (WIFSIGNALED(status)) {
        	log_flush_warning();
            log_warning("Child %d terminated by signal %d", wpid,
                    WTERMSIG(status));

            if (!isExec) {
                fl_removeAndFree(entry, &DR_SIGNALED); // NULL `entry` safe
            }
            else {
                log_warning("Deleting %s EXEC entry \"%s\"",
                		DR_SIGNALED.adjective, cmd);
                (void)cm_remove(execMap, wpid);
            }
        }
        else if (WIFEXITED(status)) {
            const int exitStatus = WEXITSTATUS(status);
            const int logLevel = exitStatus ? LOG_LEVEL_WARNING : LOG_LEVEL_DEBUG;

            log_flush(logLevel);
            log_log(logLevel, "Child %d exited with status %d", wpid,
            		exitStatus);

            const DeleteReason* dr = exitStatus ? &DR_FAILED : &DR_TERMINATED;

            if (!isExec) {
                fl_removeAndFree(entry, dr);    // NULL `entry` safe
            }
            else {
                log_log(logLevel, "Deleting %s EXEC entry \"%s\"",
                        dr->adjective, cmd);
                (void)cm_remove(execMap, wpid);
            }
        }
    } /* wpid != -1 && wpid != 0 */

    return wpid;
}
