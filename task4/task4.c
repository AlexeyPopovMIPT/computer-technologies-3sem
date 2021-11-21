#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>


#if defined (ENABLE_LOGGING)
#define LOG(v1, ...) { fprintf (stderr, "%d: ", getpid ()); fprintf (stderr, v1, __VA_ARGS__); }
#else
#define LOG(...) ;
#endif

#define ASSERTED(action, message) \
if (!(action)) { perror (message); exitcode = -1; goto cleanup; }


#define SIZE 4096
const int UNDEFINED = 1;
const int WAIT_INTERVAL_SEC = 15;

uint8_t buffer [SIZE];

int pos = 0;
int mask = 1;
int childExitCode = UNDEFINED;


int perform (const char *path);


void sigchld_handler (int sig, siginfo_t *info, void *ucontext)
{
    childExitCode = info->si_status;
}

void sigalrm_handler_child (int sig)
{
    printf ("Waiting for a response from a parent process has timed out\n");
    exit (-1);
}


void sigusr1_handler (int sig)
{
    buffer[pos] |= 0;
    if (mask == (1 << 7))
    {
        mask = 1 << 0;
        pos++;
    }
    else
    {
        mask <<= 1;
    }
}


void sigusr2_handler (int sig)
{
    buffer[pos] |= mask;
    if (mask == (1 << 7))
    {
        mask = 1 << 0;
        pos++;
    }
    else
    {
        mask <<= 1;
    }
}


void sigusr1_handler_child (int sig) 
{}



void usage (const char *pathForCaller)
{
    printf ("Usage: %s [path]\n", pathForCaller);
}


int main (int argc, const char **argv)
{
    if (argc != 2) usage (argv[0]);

    return perform (argv[1]);
}

int childCode (const char *path)
{
    int exitcode = 0, fd_src = -1;

     // block handling of signals
    sigset_t set, old_set;
    sigfillset (&set);
    sigprocmask (SIG_BLOCK, &set, &old_set);
    
    struct sigaction act = { };
    act.sa_handler = sigusr1_handler_child;
    sigaction (SIGUSR1, &act, NULL);

    memset (&act, 0, sizeof (struct sigaction));
    act.sa_handler = sigalrm_handler_child;
    sigaction (SIGALRM, &act, NULL);

    sigset_t sigallow;
    sigfillset (&sigallow); //sigemptyset (&sigallow);
    sigdelset (&sigallow, SIGUSR1); //sigaddset (&sigallow, SIGUSR1);
    sigdelset (&sigallow, SIGALRM);

    ASSERTED ((fd_src = open (path, O_RDONLY)) != -1, "Cannot open requested file")

    int bytesRead;

    while (1)
    {
        ASSERTED ((bytesRead = read (fd_src, buffer, SIZE)) != -1, "Error while reading from file")
        if (bytesRead == 0)
            break;

        for (int i = 0; i < bytesRead; i++)
        {
            size_t mask_ = 1;
            while (1)
            {
                kill (getppid (), buffer[i] & mask_ ? SIGUSR2 : SIGUSR1);
                LOG ("Sent\n");

                alarm (WAIT_INTERVAL_SEC);
                sigsuspend (&sigallow);
                alarm (0);
                LOG ("Sigsuspend returned\n");

                if (mask_ == (1 << 7))  
                    break;

                mask_ <<= 1;
            }
            
        }
    }

    cleanup:
        if (fd_src != -1) close (fd_src);

    return exitcode;

}

// Parent code
void flushBuffer ()
{
    write (1, buffer, pos);
    pos = 0;
    mask = 1;
}

int perform (const char *path)
{
    struct sigaction act = { };
    act.sa_sigaction = sigchld_handler;
    act.sa_flags |= SA_SIGINFO;
    sigaction (SIGCHLD, &act, NULL);

    memset (&act, 0, sizeof (struct sigaction));
    act.sa_handler = sigusr1_handler;
    sigaction (SIGUSR1, &act, NULL);

    act.sa_handler = sigusr2_handler;
    sigaction (SIGUSR2, &act, NULL);


    // block handling of signals
    sigset_t set, old_set;
    sigfillset (&set);
    sigprocmask (SIG_BLOCK, &set, &old_set);

    sigset_t sigallow;
    sigfillset (&sigallow); //sigemptyset (&sigallow);
    sigdelset (&sigallow, SIGUSR1); //sigaddset (&sigallow, SIGUSR1);
    sigdelset (&sigallow, SIGUSR2); //sigaddset (&sigallow, SIGUSR2);
    sigdelset (&sigallow, SIGCHLD); //sigaddset (&sigallow, SIGCHLD);

    pid_t pid = fork ();
    if (pid == 0) 
        return childCode (path);


    while (1)
    {
        sigsuspend (&sigallow);
        LOG ("Received\n");
        if (childExitCode != UNDEFINED)
        {
            flushBuffer ();
            if (childExitCode != 0)
                printf ("Child encountered an error %d, file wasn\'t passed successfully\n", childExitCode);
            return childExitCode;
        }

        if (pos == SIZE)
        {
            flushBuffer ();
        }


        kill (pid, SIGUSR1);
        printf ("Child no %d\n", pid);
    }

}
