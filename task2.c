#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG(...) { printf (__VA_ARGS__); fflush (stdout); }

void usage (int argc, const char **argv)
{
    printf ("Usage: %s [i]\n", argv[0]);
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

    //pid_t *childs = (pid_t*) malloc (cnt * sizeof (*childs));
    //if (childs == NULL)
    //{
    //    printf ("Error: asked for too many childs\n");
    //    return -1;
    //}

    char pathname [] = "/tmp/keypath00";
    getPath (pathname);

    if (*pathname == '\0')
    {
        printf ("Can\'t create file for key generating\n");
        return -1;
    }


    for (int i = 1; i <= cnt; i++)
    {
        key_t key = ftok (pathname, i);

        int queueId = msgget (key, IPC_CREAT | 0666);
        if (queueId < 0)
        {
            perror ("Can\'t set up a queue");
            return -1;
        }

        LOG ("Parent: created queue id=%d\n", queueId);

        pid_t pid = fork ();

        if (pid == 0)
        {
            int qqueueId = msgget (key, IPC_CREAT | 0666);
            if (qqueueId < 0)
            {
                perror ("Can\'t set up a fucking queue");
                msgctl (queueId, IPC_RMID, NULL);
                exit (-1);
            }
            
            struct 
            {
                long type;
                char buf [5];
            } msg;
            int rcv = msgrcv (qqueueId, &msg, 5, i, 0);
            if (-1 == rcv)
            {
                perror ("Can\'t read message");
                msgctl (qqueueId, IPC_RMID, NULL);
                msgctl (queueId, IPC_RMID, NULL);
                exit (-1);
            }

            char *message = msg.buf;
            LOG ("Child no %d, pid = %d, message=%s(%02X %02X %02X %02X %02X), msgrcv=%d\n",
             i, getpid (), message, (int)(message[0]), (int)(message[1]), (int)(message[2]), 
             (int)(message[3]), (int)(message[4]), rcv);
            
            msgctl (queueId, IPC_RMID, NULL);
            msgctl (qqueueId, IPC_RMID, NULL);
            exit (0);
        }

        else if (pid == -1)
        {
            perror ("Can\'t fork child");
            msgctl (queueId, IPC_RMID, NULL);
            return -1;
        }

        else
        {
            struct 
            {
                long type;
                char buf [5];
            } msg = { i, "MEOW" };

            int snd = msgsnd (queueId, &msg, 5, 0);
            if (-1 == snd)
            {
                perror ("Can\'t send message");
                msgctl (queueId, IPC_RMID, NULL);
                return -1;
            }

            LOG ("Parent: sent message, msgsnd=%d\n", snd);
            msgctl (queueId, IPC_RMID, NULL);
        }
    }

}




// batch