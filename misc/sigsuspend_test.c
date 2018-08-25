// This file tests the atomicity of sigpause(2) or sigsuspend(2)
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

static void handleSigTerm(int sig)
{
    //puts("handleSigTerm() called"); // Commenting-out makes no difference
}

/*
 * Whether to use sighold(2), sigset(2), and sigpause(2) instead of
 * sigprocmask(2), sigaction(2), and sigsuspend(2).
 */
#define USE_OBSOLETE_FUNCS 0

int main()
{
#if USE_OBSOLETE_FUNCS
    sighold(SIGTERM);
#else
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, NULL);
#endif
#if USE_OBSOLETE_FUNCS
    sigset(SIGTERM, handleSigTerm);
#else
    struct sigaction sigact = {};
    sigact.sa_handler = handleSigTerm;
    sigaction(SIGTERM, &sigact, NULL);
#endif
    pid_t forkPid = fork();
    if (forkPid == 0) {
        // Child process
#if USE_OBSOLETE_FUNCS
        sigpause(SIGTERM);
#else
        sigdelset(&mask, SIGTERM);
        sigsuspend(&mask);
#endif
        exit(0);
    }
#if USE_OBSOLETE_FUNCS
    //usleep(100000); // Corrects non-atomicity of sigpause()
#endif
    kill(forkPid, SIGTERM);
    wait(NULL); // If this hangs, then there's a problem
    return 0;
}
