#define _XOPEN_SOURCE 700
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t sigContReceived;

static void
sigContHandler(const int sig)
{
    sigContReceived = 1;
}

void
timedSigContWait(const unsigned seconds)
{
    struct sigaction sigContAction, prevSigContAction;
    sigset_t         sigContMask, prevMask;

    // Block SIGCONT while we set up
    sigemptyset(&sigContMask);
    sigaddset(&sigContMask, SIGCONT);
    (void)pthread_sigmask(SIG_BLOCK, &sigContMask, &prevMask);

    // Set up SIGCONT handler and save previous
    sigemptyset(&sigContAction.sa_mask);
    sigContAction.sa_flags = 0;
    sigContAction.sa_handler = sigContHandler;
    (void)sigaction(SIGCONT, &sigContAction, &prevSigContAction);

    // Unblock SIGCONT
    sigContReceived = 0;
    (void)pthread_sigmask(SIG_UNBLOCK, &sigContMask, &prevMask);

    // Sleep
    if (!sigContReceived)
        sleep(seconds); // Small but finite window of vulnerability

    // Restore previous state
    (void)sigaction(SIGCONT, &prevSigContAction, NULL );
    (void)pthread_sigmask(SIG_SETMASK, &prevMask, NULL);
}
