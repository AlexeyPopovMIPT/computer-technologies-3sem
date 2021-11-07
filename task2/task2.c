#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef NDEBUG
#define LOG(...) { printf (__VA_ARGS__); fflush (stdout); }
#else
#define LOG(...) ;
#endif

void usage (int argc, const char **argv)
{
    printf ("Usage: %s [d]\n", argv[0]);
}

void getPath (char *filename) // "/tmp/keypath00"
{
    for (int i = 0; i < 100; i++)
    {
        sprintf (filename + 12, "%02d", i);
        int fd = creat (filename, 0666);
        if (fd > 0)
        {
            close (fd);
            return;
        }
    }
    *filename = '\0';
}

int childStuff (key_t key, int i, int cnt, int queueId)
{
    int exitcode = 0;
    // int slt = sleep (2);
    // LOG ("Child no %d: underslept %d\n", i, slt);
            
    struct 
    {
        long type;
        char buf [5];
    } msg;

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

    if (i != cnt)
    {
        msg.type++;
        msgsnd (queueId, &msg, 5, 0);
    }

    cleanup:
    if (i == cnt)
        msgctl (queueId, IPC_RMID, NULL);
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

    char pathname [] = "/tmp/keypath00";
    getPath (pathname);

    if (*pathname == '\0')
    {
        printf ("Can\'t create file for key generating\n");
        return -1;
    }

    key_t key = ftok (pathname, 0);

    int queueId = msgget (key, IPC_CREAT | 0666);
    if (queueId < 0)
    {
        perror ("Can\'t set up a queue");
        return -1;
    }

    LOG ("Parent: created queue id=%d\n", queueId);

    for (int i = 1; i <= cnt; i++)
    {
        pid_t pid = fork ();

        if (pid == 0)
        {
            childStuff (key, i, cnt, queueId); 
            exit (0);  
        }

        else if (pid == -1)
        {
            perror ("Can\'t fork child");
            msgctl (queueId, IPC_RMID, NULL);
            return -1;
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
        msgctl (queueId, IPC_RMID, NULL);
        return -1;
    }

    LOG ("Parent: sent message, msgsnd=%d\n", snd);

}




// batch