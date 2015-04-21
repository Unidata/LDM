#define _XOPEN_SOURCE 600

#define SELECT   1
#define PSELECT  2
#define POLL     3
#define SELECT_TYPE (POLLFUNC == SELECT || POLLFUNC == PSELECT)
#define POLL_TYPE (POLLFUNC == POLL)
#define POLLFUNC POLL

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#if POLL_TYPE
#    include <poll.h>
#endif
#include <pthread.h>
#if SELECT_TYPE
#    include <sys/select.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MAX
    #define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

typedef struct {
    pthread_t thread;
    int       sock;
    int       fds[2];
} Server;

static void termSigHandler(
        const int sig)
{
    (void)printf("Caught signal %d\n", sig);
}

static int setTermSigHandler(void)
{
    struct sigaction newAction;

    (void)sigemptyset(&newAction.sa_mask);
    newAction.sa_flags = 0;
    newAction.sa_handler = termSigHandler;

    return sigaction(SIGTERM, &newAction, NULL)
        ? errno
        : 0;
}

static int serverSock_init(
        int* const sock)
{
    int sck = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int status;

    if (sck == -1) {
        status = errno;
    }
    else {
        struct sockaddr_in addr;

        (void)memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(0); // let O/S assign port

        status = bind(sck, (struct sockaddr*)&addr, sizeof(addr));

        if (0 == status) {
            status = listen(sck, 1);

            if (0 == status)
                *sock = sck;
        }
    }

    return status;
}

static void* server_serve(
        void* const arg)
{
    Server* const server = (Server*)arg;
    const int     sock = server->sock;
    const int     pipeIn = server->fds[0];
    const int     width = MAX(sock, pipeIn) + 1;
    int           status;

    for (;;) {
#if SELECT_TYPE
        fd_set readfds;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(pipeIn, &readfds);
#elif POLL_TYPE
        struct pollfd pfds[2];
        #define SOCK_PFD pfds[0]
        #define PIPE_PFD pfds[1]

        SOCK_PFD.fd = sock;
        SOCK_PFD.events = POLLIN;
        PIPE_PFD.fd = pipeIn;
        PIPE_PFD.events = POLLIN;
#endif

#if POLLFUNC == SELECT
        (void)puts("Calling select()...");
        /* NULL timeout argument => indefinite wait */
        status = select(width, &readfds, NULL, NULL, NULL);
        (void)puts("select() returned");
#elif POLLFUNC == PSELECT
        (void)puts("Calling pselect()...");
        /* NULL timeout argument => indefinite wait */
        status = pselect(width, &readfds, NULL, NULL, NULL, NULL);
        (void)puts("pselect() returned");
#else
        (void)puts("Calling poll()...");
        /* -1 timeout argument => indefinite wait */
        status = poll(pfds, 2, -1);
        (void)puts("poll() returned");
#endif

        if (0 > status)
            break;
#if SELECT_TYPE
        if (FD_ISSET(pipeIn, &readfds)) {
#else
        if (PIPE_PFD.revents & POLLIN || PIPE_PFD.revents & POLLHUP ||
                SOCK_PFD.revents & POLLHUP) {
#endif
            (void)close(pipeIn);
            status = 0;
            break;
        }
#if SELECT_TYPE
        if (FD_ISSET(sock, &readfds)) {
#else
        if (SOCK_PFD.revents & POLLIN) {
#endif
            (void)puts("Calling accept()...");
            const int     sock = accept(server->sock, NULL, NULL);
            (void)puts("accept() returned");

            if (sock == -1) {
                status = errno;
                break;
            }

            close(sock);
        }
    }

    static int staticStatus;
    staticStatus = status;
    return &staticStatus;
}

static int server_init(
        Server* const server)
{
    int   status = pipe(server->fds);

    if (0 == status) {
        status = serverSock_init(&server->sock);

        if (0 == status)
            status = pthread_create(&server->thread, NULL, server_serve,
                    server);
    }

    return status;
}

static int server_destroy(
        Server* const server)
{
    int status = setTermSigHandler();

    if (0 == status) {
        #if 0
            if (close(server->sock)) { // select() & accept() don't return
                status = errno;
            }
            else {
        #elif 0
            status = pthread_kill(server->thread, SIGTERM); // select() & accept() return

            if (0 == status) {
        #elif 0
            if (write(server->fds[1], server->fds, 1) < 0) { // select() returns
                status = errno;
            }
            else {
        #elif 0
            if (close(server->fds[0]) < 0) { // select() doesn't return
                status = errno;
            }
            else {
        #elif 1
            if (close(server->fds[1])) { // (p)select() & poll() return
                status = errno;
            }
            else {
        #endif
                (void)puts("Calling pthread_join()...");
                status = pthread_join(server->thread, NULL);
                (void)puts("pthread_join() returned");
            }
    }

    return status;
}

int main(
        const int          argc,
        const char* const* argv)
{
    Server server;
    int    status = server_init(&server);

    if (0 == status) {
        (void)sleep(1);
        status = server_destroy(&server);
    }

    return status;
}
