// This file tests for correct interaction between `fork()` and `kill()`.
#include <signal.h>
#include <stdlib.h>

static void handleSigTerm(int sig)
{}

int main()
{
    sighold(SIGTERM);
    sigset(SIGTERM, handleSigTerm);
    pid_t forkPid = fork();
    if (forkPid == 0) {
        // Child process
        sigpause(SIGTERM);
        exit(0);
    }
    //usleep(100000); // Corrects bad fork()/kill() behavior
    sigset(SIGTERM, SIG_DFL);
    sigrelse(SIGTERM);
    kill(forkPid, SIGTERM);
    wait(NULL); // Hangs here if usleep() commented-out
    return 0;
}
