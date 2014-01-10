/**
 * This program experiments with sharable and exclusive record-locking.
 */

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>

typedef enum {
    EXIT = 0,
    ERROR,
    READ_LOCK,
    WRITE_LOCK,
    UNLOCK
} Command;

/**
 * Sets a lock on a file-descriptor.
 *
 * @param fd        The file-descriptor.
 * @param flock     Template file-locking structure.
 * @param type      Type of lock. One of F_RDLCK, F_WRLCK, or F_UNLCK.
 * @return 0        Success.
 * @retval -1       Error. An error-message is printed.
 */
static int
setFileLock(
    const int           fd,
    const struct flock* flock,
    short               type)
{
    struct flock    flck = *flock;

    flck.l_type = type;

    if (fcntl(fd, F_SETLKW, &flck) == -1) {
        (void)fprintf(stderr, "Couldn't set lock on file: %s",
                strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * Executes a command on a file-descriptor.
 *
 * @param fd    File-descriptor of the file to apply the command.
 * @param cmd   Command to apply. One of READ_LOCK, WRITE_LOCK, UNLOCK.
 * @param flock Template file-locking structure.
 * @retval 0    Success.
 * @retval -1   Error. An error-message is printed.
 */
static int
executeCommand(
    const int           fd,
    const Command       cmd,
    const struct flock* flock)
{
    if (cmd == READ_LOCK)
        return setFileLock(fd, flock, F_RDLCK);
    if (cmd == WRITE_LOCK)
        return setFileLock(fd, flock, F_WRLCK);
    if (cmd == UNLOCK)
        return setFileLock(fd, flock, F_UNLCK);
    (void)fprintf(stderr, "Invalid command: %d\n", cmd);
    return -1;
}

/**
 * Opens a file for reading and writing -- creating it if necessary.
 *
 * @param pathname  Pathname of the file.
 * @retval -1       An error occurred. An error-message is printed.
 * @return          File-descriptor to the opened file.
 */
static int
openFile(
    const char* pathname)
{
    int fd = open(pathname, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

    if (fd == -1) {
        (void)fprintf(stderr, "Unable to open file \"%s\": %s", pathname,
                strerror(errno));
    }

    return fd;
}

static void
initFlock(
    struct flock*   flock)
{
    flock->l_whence = SEEK_SET;
    flock->l_start = 0; /* BOF */
    flock->l_len = 0; /* to EOF */
}

/**
 * Returns the first character of the next input line on the standard input
 * stream. The input line is entirely consumed.
 *
 * @retval EOF  End of input.
 * @retval '\n' Empty input line.
 * @return      The first character of the next line of input on the standard
 *              input stream. The stream is consumed up to and including the
 *              terminating newline.
 */
static int
getFirstChar(void)
{
    int first = fgetc(stdin);
    int c;

    for (c = first; c != '\n' && c != EOF; c = fgetc(stdin))
        ;

    return first;
}

/**
 * Gets the next command.
 *
 * @retval 0    Success;
 */
static Command
getCommand(void)
{
    for (;;) {
        int c;

        if (fputs("(r)ead-lock, (w)rite-lock, (u)nlock, (e)xit: ", stdout) < 0) {
            (void)perror("fputs() failure");
            return ERROR;
        }
        c = getFirstChar();
        if (c == 'r') return READ_LOCK;
        if (c == 'w') return WRITE_LOCK;
        if (c == 'u') return UNLOCK;
        if (c == 'e') return EXIT;
        if (c == EOF) return EXIT;
    }
}

/**
 * Runs this program.
 *
 * @param pathname  The pathname of the file to be locked.
 * @retval 0        Success.
 * @retval 1        Unable to open file. An error-message is printed.
 * @retval 2        Unable to execute command.
 */
static int
run(
    const char* pathname)
{
    int             fd = openFile(pathname);
    struct flock    flock;
    Command         cmd;

    if (fd == -1)
        return 1;

    initFlock(&flock);

    for (cmd = getCommand(); cmd != EXIT && cmd != ERROR; cmd = getCommand()) {
        if (executeCommand(fd, cmd, &flock) != 0)
            break;
    }

    return cmd == EXIT ? 0 : 2;
}

/**
 * Returns the pathname of the file to be locked.
 *
 * @param argc      Number of command-line arguments.
 * @param argv      Command-line arguments.
 * @retval NULL     If an error occurs while decoding the command-line. An
 *                  error-message is printed.
 * @return          The pathname of the file to be locked.
 */
static const char*
getPathname(
    int                 argc,
    const char* const   argv[])
{
    if (argc != 2) {
        (void)fprintf(stderr, "Incorrect number of arguments: %d\n", argc);
        return NULL;
    }
    return argv[1];
}

int
main(
    int                 argc,
    const char* const   argv[])
{
    const char* pathname = getPathname(argc, argv);

    return (pathname == NULL) ? 1 : run(pathname);
}
