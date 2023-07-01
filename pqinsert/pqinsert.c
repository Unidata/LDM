/**
 * Inserts files into an LDM product-queue as data-products.
 *
 * Copyright 2023, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

/* 
 * Convert files to ldm "products" and insert in local que
 */
#include <config.h>

#if defined(NO_MMAP) || !defined(HAVE_MMAP)
    #define USE_MMAP 0
#else
    #define USE_MMAP 1
#endif

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <signal.h>
#if USE_MMAP
    #include <sys/mman.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "ldm.h"
#include "pq.h"
#include "globals.h"
#include "remote.h"
#include "atofeedt.h"
#include "ldmprint.h"
#include "inetutil.h"
#include "log.h"
#include "md5.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

enum ExitCode {
    exit_success = 0,   /* all files inserted successfully */    ///< exit_success
    exit_system = 1,    /* operating-system failure */           ///< exit_system
    exit_pq_open = 2,   /* couldn't open product-queue */        ///< exit_pq_open
    exit_infile = 3,    /* couldn't process input file */        ///< exit_infile
    exit_dup = 4,       /* input-file already in product-queue *////< exit_dup
    exit_md5 = 6        /* couldn't initialize MD5 processing */ ///< exit_md5
};

#define DEF_STDIN_SIZE 1000000  ///< Default initial number of bytes for standard input buffer
#define DEF_STDIN_IDENT "STDIN" ///< Default product-identifier when reading from standard input

        /* N.B.: assumes hostname doesn't change during program execution :-) */
static char             myname[HOSTNAMESIZE];
static feedtypet        feedtype = EXP;
#if !USE_MMAP
    static struct pqe_index pqeIndex;
#endif
static int              seq_start = 0;
static int              useProductID = FALSE;
static char*            productID = NULL;
static int              signatureFromId = FALSE;
static int              numFiles;
static char* const*     pathnames;
static MD5_CTX*         md5ctxp = NULL;
static bool             useStdin = false;
static size_t           stdinSize = DEF_STDIN_SIZE;
static product          prod;

static void
usage(const char* const progname)
{
    log_add(
"Usage:\n"
"    %s [options] [<file> ...]\n"
"Where:\n"
"    -i              Compute product signature (MD5 checksum) from product ID.\n"
"                    Default is to compute it from the product.\n"
"    -f <feedtype>   Set the feed type as <feedtype>. Default: \"EXP\"\n"
"    -l <dest>       Log to <dest>. One of: \"\" (system logging daemon), \"-\"\n"
"                    (standard error), or file `dest`. Default is \"%s\"\n"
"    -n <size>       Initial size guess, in bytes, for the product read from\n"
"                    standard input. Ignored if file operands are specified.\n"
"                    Default is %d.\n"
"    -p <productID>  Use <productID> in product-identifier. Default for standard\n"
"                    input is \"%s\". Default for files is the filename. With\n"
"                    multiple files, product-ID becomes <productID>.<seqno>.\n"
"    -q <queue>      Use <queue> as product-queue. Default:\n"
"                    \"%s\"\n"
"    -s <seqno>      Set initial product sequence number to <seqno>. Default: 0\n"
"    -v              Verbose, log at the INFO level. Default is NOTE.\n"
"    <file>          Optional files to insert as products. Default is to read a\n"
"                    single product from standard input.",
            progname, log_get_default_destination(), DEF_STDIN_SIZE, DEF_STDIN_IDENT,
            getDefaultQueuePath());
    log_flush_error();
    exit(1);
}

/**
 * Decodes the command line.
 * @param[in] ac  Number of command-line words
 * @param[in] av  Pointers to command-line words
 */
static void
decodeCmdLine(
        int          ac,
        char* const* av)
{
    extern int        optind;
    extern int        opterr;
    extern char*      optarg;
    int               ch;
    const char* const progname = basename(av[0]);

    opterr = 0; /* Suppress getopt(3) error messages */

    while ((ch = getopt(ac, av, ":ivxl:q:f:n:s:p:")) != EOF)
        switch (ch) {
            case 'i':
                signatureFromId = 1;
                break;
            case 'v':
                if (!log_is_enabled_info)
                    (void)log_set_level(LOG_LEVEL_INFO);
                break;
            case 'x':
                (void)log_set_level(LOG_LEVEL_DEBUG);
                break;
            case 'l':
                if (log_set_destination(optarg)) {
                    log_syserr("Couldn't set logging destination to \"%s\"", optarg);
                    usage(progname);
                }
                break;
            case 'n': {
                if (sscanf(optarg, "%zu", &stdinSize) != 1) {
                    log_error("Couldn't decode size-guess for standard-input product: \"%s\"",
                            optarg);
                    usage(progname);
                }
                if (stdinSize == 0) {
                    log_error("Size-guess for standard-input product is zero");
                    usage(progname);
                }
                break;
            }
            case 'q':
                setQueuePath(optarg);
                break;
            case 's':
                seq_start = atoi(optarg);
                break;
            case 'f':
                feedtype = atofeedtypet(optarg);
                if(feedtype == NONE) {
                    fprintf(stderr, "Unknown feedtype \"%s\"\n", optarg);
                    usage(progname);
                }
                break;
            case 'p':
                useProductID = TRUE;
                productID = optarg;
                break;
            case ':': {
                log_add("Option \"-%c\" requires an operand", optopt);
                usage(progname);
            }
            /* no break */
            default:
                log_add("Unknown option: \"%c\"", optopt);
                usage(progname);
                /* no break */
        } // Option `switch`

    ac -= optind; av += optind ;

    if (ac == 0) {
        useStdin = true;
    }
    else {
        useStdin = false;
        numFiles = ac;
        pathnames = av;
    }
}

void
cleanup(void)
{
    if (pq) {
#if !USE_MMAP
        if (!pqeIsNone(pqeIndex))
            (void)pqe_discard(pq, pqeIndex);
#endif

        (void) pq_close(pq);
        pq = NULL;
    }

    log_fini();
}


static void
signal_handler(int sig)
{
    switch(sig) {
      case SIGINT :
         exit(1);
      case SIGTERM :
         done = 1;
         return;
      case SIGUSR1 :
         log_refresh();
         return;
    }
}


static void
set_sigactions(void)
{
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Ignore the following */
    sigact.sa_handler = SIG_IGN;
    (void) sigaction(SIGALRM, &sigact, NULL);
    (void) sigaction(SIGCHLD, &sigact, NULL);

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /* Don't restart the following */
    (void) sigaction(SIGINT, &sigact, NULL);

    /* Restart the following */
    sigact.sa_flags |= SA_RESTART;
    (void) sigaction(SIGTERM, &sigact, NULL);
    (void) sigaction(SIGUSR1, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGALRM);
    (void)sigaddset(&sigset, SIGCHLD);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}


#if !USE_MMAP
static int
fd_md5(MD5_CTX *md5ctxp, int fd, off_t st_size, signaturet signature)
{
        int           nread;
        unsigned char buf[8192];

        MD5Init(md5ctxp);
        for(; st_size > 0; st_size -= nread )
        {
                nread = read(fd, buf, sizeof(buf));
                if(nread <= 0)
                {
                        log_add_syserr("fd_md5: read");
                        log_flush_error();
                        return -1;
                } /* else */
                MD5Update(md5ctxp, buf, nread);
                (void)exitIfDone(1);
        }
        MD5Final(signature, md5ctxp);
        return 0;
}
#else
static int
mm_md5(MD5_CTX *md5ctxp, void *vp, size_t sz, signaturet signature)
{
        MD5Init(md5ctxp);

        MD5Update(md5ctxp, vp, sz);

        MD5Final((unsigned char*)signature, md5ctxp);
        return 0;
}
#endif

/**
 * Inserts a data-product into the product-queue.
 * @retval 0            Success
 * `log_add()` is called for all of the following:
 * @retval exit_dup
 * @retval exit_infile
 * @retval exit_system
 */
static int
insertProd(void)
{
    int status = pq_insert(pq, &prod);

    switch (status) {
    case ENOERR:
        /* no error */
        log_info("%s", s_prod_info(NULL, 0, &prod.info, log_is_enabled_debug)) ;
        break;
    case PQUEUE_DUP:
        log_add("Product already in queue: %s", s_prod_info(NULL, 0, &prod.info, 1));
        status = exit_dup;
        break;
    case PQUEUE_BIG:
        log_add("Product too big for queue: %s", s_prod_info(NULL, 0, &prod.info, 1));
        status = exit_infile;
        break;
    case ENOMEM:
        log_add("queue full?");
        status = exit_system;
        break;
    case EINTR:
#if defined(EDEADLOCK) && EDEADLOCK != EDEADLK
    case EDEADLOCK:
        /*FALLTHROUGH*/
#endif
    case EDEADLK:
        /* TODO: retry ? */
        /*FALLTHROUGH*/
    default:
        log_add("pq_insert: %s", status > 0 ? strerror(status) : "Internal error");
        break;
    }

    return status;
}

/**
 * Reads bytes from a file descriptor. Unlike `read()`, this function will not stop reading until
 * the given number of bytes has been read or no more bytes can be read.
 * @param[in]  fd         File descriptor from which to read bytes
 * @param[out] buf        Input buffer for the bytes. Caller must ensure that it can hold
 *                        `numToRead` bytes.
 * @param[in]  numToRead  Number of bytes to read
 * @retval     -1         Error. `log_add()` called.
 * @retval      0         End of file/transmission
 * @return                Number of bytes read. If less than `numToRead`, then no more bytes can be
 *                        read.
 */
static ssize_t
readFd( const int fd,
        char*     buf,
        size_t    numToRead)
{
    ssize_t status = -1; // Error

    if (numToRead > SSIZE_MAX) {
        log_add("Number of bytes to read (%zu) is greater than SSIZE_MAX (%zd)", numToRead,
                SSIZE_MAX);
    }
    else {
        ssize_t numRead = 0;

        while (numToRead) {
            status = read(fd, buf, numToRead);

            if (status == -1) {
                log_add_syserr("Couldn't read from file descriptor %d", fd);
                break;
            }

            if (status == 0) {
                status = numRead;
                break;
            }

            numRead   += status;
            numToRead -= status;
        }
    }

    return status;
}

/**
 * Reads a data-product from standard input.
 * @retval 0            Success. `*data` and `*size` are set.
 * @retval exit_system  O/S failure. `log_add()` called.
 * @retval exit_infile  Couldn't process standard input. `log_add()` called.
 */
static int
readStdin(void)
{
    enum ExitCode status = exit_infile;

    if (stdinSize == 0) {
        log_add("Initial buffer size of zero is invalid");
    }
    else {
        char*  buf = NULL;   // Input buffer
        size_t numTotal = 0; // Number of bytes in buffer
        size_t numToRead = (stdinSize < SSIZE_MAX) ? stdinSize : SSIZE_MAX;

        for (;;) {
            char* cp = realloc(buf, stdinSize);

            if (cp == NULL) {
                log_syserr("Couldn't allocate %zu bytes for standard input buffer", stdinSize);
                status = exit_system;
                break;
            }
            else {
                buf = cp;

                ssize_t numRead = readFd(0, buf+numTotal, numToRead);
                if (numRead == -1)
                    break;

                if (numRead < numToRead || numRead == 0) {
                    // All done
                    prod.data = buf;
                    prod.info.sz = numTotal;
                    status = exit_success;
                    break;
                }

                numTotal += numRead;
                if (numTotal >= UINT32_MAX) {
                    log_add("Product is too large because it has at least %zu bytes", numTotal);
                    break;
                }

                numToRead = (numTotal < SSIZE_MAX) ? numTotal : SSIZE_MAX;
                if (stdinSize + numToRead < stdinSize)
                    numToRead = SIZE_MAX - numTotal; // New buffer size would overflow otherwise
                stdinSize += numToRead;
            }  // Buffer re-allocated
        } // For loop

        if (!status)
            free(buf); // NULL safe
    } // Valid arguments

    return status;
}

/**
 * Sets the creation-time of the data-product.
 * @retval 0            Success. `prod.info.arrival` is set.
 * @retval exit_system  System failure. `log_add()` called.
 */
static int
setCreationTime(void)
{
    int status = set_timestamp(&prod.info.arrival);

    if (status) {
        log_add_syserr("set_timestamp() failure");
        status = exit_system;
    }

    return status;
}

/**
 * Reads a data-product from standard input and inserts it into the product-queue.
 * @retval 0            Success
 * @retval exit_dup     Duplicate product
 * @retval exit_system  System error. `log_add()` called.
 * @retval exit_infile  Couldn't process standard input
 */
static int
insertStdin()
{
    int status = readStdin();

    if (status == 0) {
        prod.info.seqno = seq_start;
        prod.info.ident = useProductID ? productID : DEF_STDIN_IDENT;

        status = setCreationTime();

        if(status) {
            log_add("Couldn't set creation-time");
        }
        else {
            signatureFromId
                ? mm_md5(md5ctxp, prod.info.ident, strlen(prod.info.ident), prod.info.signature)
                : mm_md5(md5ctxp, prod.data, prod.info.sz, prod.info.signature);

            status = insertProd();
        }
    }

    return status;
}

/**
 * Inserts files as data-products into the product-queue.
 * @retval 0             Success
 * @retval exit_infile
 * @retval exit_dup
 * @retval exit_system
 */
static int
insertFiles(void)
{
    int       status = exit_success;
    const int multipleFiles = numFiles > 1;
    char      identifier[KEYSIZE];

    for(prod.info.seqno = seq_start ; numFiles > 0 ; pathnames++, numFiles--, prod.info.seqno++) {
        char*       pathname = *pathnames;
        struct stat statb;

        int fd = open(pathname, O_RDONLY, 0);
        if(fd == -1) {
            log_syserr("open: %s", pathname);
            status = exit_infile;
            continue;
        }

        if( fstat(fd, &statb) == -1) {
            log_syserr("fstat: %s", pathname);
            (void) close(fd);
            status = exit_infile;
            continue;
        }

        /* Determine what to use for product identifier */
        if (useProductID) {
            if (multipleFiles) {
                sprintf(identifier,"%s.%d", productID, prod.info.seqno);
                prod.info.ident = identifier;
            }
            else {
              prod.info.ident = productID;
            }
        }
        else {
            prod.info.ident = pathname;
        }

        prod.info.sz = statb.st_size;
        prod.data = NULL;

        /* These members, and seqno, vary over the loop. */
        status = setCreationTime();
        if(status) {
            log_add("Couldn't set creation-time for file \"%s\", pathname");
            log_flush_error();
            status = exit_infile;
            continue;
        }

#if USE_MMAP
        prod.data = mmap(0, prod.info.sz, PROT_READ, MAP_PRIVATE, fd, 0);
        if(prod.data == MAP_FAILED) {
            log_syserr("mmap: %s", pathname);
            (void) close(fd);
            status = exit_infile;
            continue;
        }

        status =
            signatureFromId
                ? mm_md5(md5ctxp, prod.info.ident, strlen(prod.info.ident), prod.info.signature)
                : mm_md5(md5ctxp, prod.data, prod.info.sz, prod.info.signature);

        (void)exitIfDone(1);

        if (status != 0) {
            log_syserr("mm_md5: %s", pathname);
            (void) munmap(prod.data, prod.info.sz);
            (void) close(fd);
            status = exit_infile;
            continue;
        }

        /*
         * Do the deed
         */
        status = insertProd();
        if (status == exit_system) {
            (void)munmap(prod.data, prod.info.sz);
            (void)close(fd);
            break;
        }
        else if (status) {
            log_flush_error();
            (void)munmap(prod.data, prod.info.sz);
            (void)close(fd);
            continue;
        }

        (void) munmap(prod.data, prod.info.sz);
#else // USE_MMAP above; !USE_MMAP below
        status =
            signatureFromId
                ? mm_md5(md5ctxp, prod.info.ident,
                    strlen(prod.info.ident), prod.info.signature)
                : fd_md5(md5ctxp, fd, statb.st_size,
                    prod.info.signature);

        (void)exitIfDone(1);

        if (status != 0) {
                log_add_syserr("xx_md5: %s", pathname);
                log_flush_error();
                (void) close(fd);
                status = exit_infile;
                continue;
        }

        if(lseek(fd, 0, SEEK_SET) == (off_t)-1)
        {
                log_syserr("rewind: %s", pathname);
                (void) close(fd);
                status = exit_infile;
                continue;
        }

        pqeIndex = PQE_NONE;
        status = pqe_new(pq, &prod.info, &prod.data, &pqeIndex);

        if(status != ENOERR) {
            log_add_syserr("pqe_new: %s", pathname);
            log_flush_error();
            status = exit_infile;
        }
        else {
            ssize_t     nread = read(fd, prod.data, prod.info.sz);

            (void)exitIfDone(1);

            if (nread != prod.info.sz) {
                log_syserr("read %s %u", pathname, prod.info.sz);
                status = EIO;
            }
            else {
                status = pqe_insert(pq, pqeIndex);
                pqeIndex = PQE_NONE;

                switch (status) {
                case ENOERR:
                    /* no error */
                    if(ulogIsVerbose())
                        log_info_q("%s", s_prod_info(NULL, 0, &prod.info,
                            log_is_enabled_debug)) ;
                    break;
                case PQUEUE_DUP:
                    log_error_q("Product already in queue: %s",
                        s_prod_info(NULL, 0, &prod.info, 1));
                    status = exit_dup;
                    break;
                case ENOMEM:
                    log_error_q("queue full?");
                    break;
                case EINTR:
#if defined(EDEADLOCK) && EDEADLOCK != EDEADLK
                case EDEADLOCK:
                    /*FALLTHROUGH*/
#endif
                case EDEADLK:
                    /* TODO: retry ? */
                    /*FALLTHROUGH*/
                default:
                    log_error_q("pq_insert: %s", status > 0
                        ? strerror(status) : "Internal error");
                }
            }                   /* data read into `pqeIndex` region */

            if (status != ENOERR) {
                (void)pqe_discard(pq, pqeIndex);
                pqeIndex = PQE_NONE;
            }
        }                       /* `pqeIndex` region allocated */

#endif
        (void) close(fd);
    }                               /* input-file loop */

    return status;
}

/**
 * Creates one or more LDM data-products from files or standard input and inserts them into an LDM
 * product-queue.
 * @param[in] ac  Number of command-line words
 * @param[in] av  Pointers to command-line words
 * @retval 0      Success. All products were successfully created and inserted.
 * @retval 1      Operating-system failure
 * @retval 2      Couldn't open product-queue
 * @retval 3      Couldn't process input file
 * @retval 4      Input-file already in product-queue
 * @retval 6      Couldn't initialize MD5 processing
 */
int main(
        int   ac,
        char* av[])
{
    int           status;
    enum ExitCode exitCode = exit_success;

    if (log_init(av[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }

#if !USE_MMAP
    pqeIndex = PQE_NONE;
#endif

    // Decode the command -line
    decodeCmdLine(ac, av);

    // Register the exit handler
    if(atexit(cleanup) != 0) {
        log_syserr("atexit");
        exit(exit_system);
    }

    // Set up signal handling
    set_sigactions();

    // Open the product queue
    const char* const pqfname = getQueuePath();
    if((status = pq_open(pqfname, PQ_DEFAULT, &pq))) {
        if (PQ_CORRUPT == status) {
            log_error_q("The product-queue \"%s\" is inconsistent\n", pqfname);
        }
        else {
            log_error_q("pq_open: \"%s\" failed: %s", pqfname,
                    status > 0 ? strerror(status) : "Internal error");
        }
        exit(exit_pq_open);
    }

    // Allocate an MD5 context
    md5ctxp = new_MD5_CTX();
    if(md5ctxp == NULL) {
        log_syserr("new_md5_CTX failed");
        exit(exit_md5);
    }

    // These members of the product-metadata are constant
    (void) strncpy(myname, ghostname(), sizeof(myname));
    myname[sizeof(myname)-1] = 0;
    prod.info.origin = myname;
    prod.info.feedtype = feedtype;

    exitCode = useStdin
            ? insertStdin()
            : insertFiles();
    if (exitCode)
        log_flush(exitCode == exit_system ? LOG_LEVEL_FATAL : LOG_LEVEL_ERROR);

    free_MD5_CTX(md5ctxp);

    exit(exitCode);
}
