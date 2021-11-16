#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>


#ifndef NDEBUG
#define LOG(...) { printf (__VA_ARGS__); fflush (stdout); }
#else
#define LOG(...) ;
#endif

#define ALARM_TIME 5
void usage (int argc, const char **argv)
{
    printf ("Usage: %s [d]\n", argv[0]);
}

void handler (void)
{
    printf ("Out of time\n");
    exit (-1);
}


int childStuff (int i, int cnt, int queueId)
{
    int exitcode = 0;
            
    struct 
    {
        long type;
        char buf [5];
    } msg;

    alarm (ALARM_TIME);

    int rcv = msgrcv (queueId, &msg, 5, i, 0);
    if (-1 == rcv)
    {
        LOG ("Child no %d: ", i);
        perror ("Can\'t read message");
        exitcode = -1;
        goto cleanup;
    }

    char *message = msg.buf;
    LOG ("Child no %d, pid = %d, message=%s(%02X %02X %02X %02X %02X), msgrcv=%d\n",
          i, getpid (), message, (int)(message[0]), (int)(message[1]), (int)(message[2]), 
          (int)(message[3]), (int)(message[4]), rcv);
    printf ("Child no %d, pid = %d\n", i, getpid ());

    msg.type++;
    if (i == 2) goto cleanup;
    
    msgsnd (queueId, &msg, 5, 0);

    cleanup:
    exit (exitcode);
}


int main (int argc, const char **argv)
{
    if (argc != 2)
    {
        usage (argc, argv);
        return 0;
    }

    char *end = NULL;
    int cnt = strtol (argv[1], &end, 10); // cnt < 256
    if (*end)
    {
        usage (argc, argv);
        return 0;
    }
    
    int queueId = msgget (IPC_PRIVATE, IPC_CREAT | 0666);
    if (queueId < 0)
    {
        perror ("Can\'t set up a queue");
        return -1;
    }

    LOG ("Parent: created queue id=%d\n", queueId);

    struct sigaction act = { };
    act.sa_handler = (sig_t) handler;
    sigaction (SIGALRM, &act, NULL);

    int exitcode = 0;

    for (int i = 1; i <= cnt; i++)
    {
        pid_t pid = fork ();

        if (pid == 0)
        {
            childStuff (i, cnt, queueId); 
            exit (0);  
        }

        else if (pid == -1)
        {
            perror ("Can\'t fork child");
            exitcode = -1;
            goto cleanup;
        }
    }

    struct 
    {
        long type;
        char buf [5];
    } msg = { 1, "DOIT" };

    int snd = msgsnd (queueId, &msg, 5, 0);
    if (-1 == snd)
    {
        perror ("Parent: Can\'t send message");
        exitcode = -1;
        goto cleanup;
    }

    LOG ("Parent: sent message, msgsnd=%d\n", snd);

    alarm (ALARM_TIME);

    int rcv = msgrcv (queueId, &msg, 5, cnt + 1, 0);
    if (-1 == rcv)
    {
        LOG ("%s", "Parent: ");
        perror ("Can\'t read message");
        exitcode = -1;
        goto cleanup;
    }

    LOG ("Parent: reseived message, msgrcv=%d\n", rcv);

    cleanup:
    msgctl (queueId, IPC_RMID, NULL);
    return exitcode;

}




// batch
