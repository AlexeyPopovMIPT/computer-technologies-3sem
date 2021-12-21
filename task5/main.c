#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h> // for pow ()
#include <limits.h>
#include <sys/wait.h>
#include <string.h> // for memcpy ()

#define ASSERTED(cond, ...)                    \
if (!(cond))                                   \
{                                              \
    fprintf (stderr, __VA_ARGS__); perror (0); \
    exitcode = -1;                             \
    goto cleanup;                              \
}

struct Connection
{
    void *buffer;
    int readFd;
    int writeFd;
    pid_t pid;
    int bufferSize;
    int occupiedFrom;
    int occupiedTo;
};

#define MIN_SIZE (128 * 1024)
#define MAX_SIZE (32 * 1024 * 1024)

int childCode (int i, int n, int readFd, int writeFd);

void usage (const char* callie)
{
    fprintf (stderr, "Usage: %s [n] [path]\n", callie);
}

int calculateSize (int i, int n)
{
    double res = pow (3.0, n - i + 4);
    if (res > MAX_SIZE) return MAX_SIZE;
    if (MIN_SIZE > res) return MIN_SIZE;
    return res + 0.5;
}

int main (int argc, const char **argv)
{
    int exitcode = 0;

    if (argc != 3)
    {
        usage (argv[0]);
        return 0;
    }

    char *end = NULL;
    int n = strtol (argv[1], &end, 10);
    if (*end)
    {
        fprintf (stderr, "Not an integer: %s\n", argv[1]);
        usage (argv[0]);
        return 0;
    }

    struct Connection *connections =
    (struct Connection *) calloc (n, sizeof (struct Connection));

    ASSERTED (connections != NULL, "Memory error\n");

    fd_set readFds, writeFds;
    FD_ZERO (&readFds);
    FD_ZERO (&writeFds);
    int maxFd = 0;

    for (int i = 0; i < n; i++)
    {
        int parentToChild [2], childToParent [2];

        ASSERTED (pipe (parentToChild) != -1
               && pipe (childToParent) != -1, "Can\'t create pipe\n")

        connections[i].bufferSize = calculateSize (i, n);

        connections[i].pid = fork ();
        ASSERTED (connections[i].pid != -1, "Cannot fork: ")
        if (connections[i].pid == 0)
        {
            int readFd, writeFd;

            if (i == 0)
            {
                readFd = open (argv[2], O_RDONLY);
                close (parentToChild[0]);
            }
            else
            {
                readFd  = parentToChild[0];
            }
            close (parentToChild[1]);

            if (i == n - 1)
            {
                writeFd = 1;
                close (childToParent[1]);
            }
            else
            {
                writeFd = childToParent[1];
            }
            close (childToParent[0]);


            for (int j = 0; j < i; j++)
            {
                close (connections[j].readFd);
                close (connections[j].writeFd);
            }

            free (connections);
            return childCode (i, n, readFd, writeFd);
        }

        ASSERTED ((connections[i].buffer = malloc (connections[i].bufferSize)) != NULL, "Memory error\n");

        if (i == n - 1)
        {
            connections[i].readFd = -1;
            close (childToParent[0]);
        }
        else
        {
            connections[i].readFd = childToParent[0];
            fcntl (connections[i].readFd, F_SETFL, O_RDONLY | O_NONBLOCK);
            FD_SET (connections[i].readFd, &readFds);

            if (maxFd < connections[i].readFd)
                maxFd = connections[i].readFd;
        }
        close (childToParent[1]);


        if (i == 0)
        {
            connections[i].writeFd = -1;
            close (parentToChild[1]);
        }
        else
        {
            connections[i].writeFd = parentToChild[1];
            fcntl (connections[i].writeFd, F_SETFL, O_WRONLY | O_NONBLOCK);
            FD_SET (connections[i].writeFd, &writeFds);

            if (maxFd < connections[i].writeFd)
                maxFd = connections[i].writeFd;
        }
        close (parentToChild[0]);

    }

    struct timeval delay =
    {
        .tv_sec = 15,
        .tv_usec = 0
    };

    for (;;)
    {
        fd_set readyToRead;
        memcpy (&readyToRead, (const void*)&readFds, sizeof (fd_set));
        fd_set readyToWrite;
        memcpy (&readyToWrite, (const void*)&writeFds, sizeof (fd_set));

        int ready = select (maxFd + 1,
                            &readyToRead,
                            &readyToWrite,
                            NULL,
                            &delay);

        ASSERTED (ready != -1, "Error in select\n")
        ASSERTED (ready !=  0, "Waiting for the responce from child processes has timed out\n")

        maxFd = 0;
        int i;
        for (i = 0; i < n; i++)
        {
            if (connections[i].writeFd != -1
                && FD_ISSET (connections[i].writeFd, &readyToWrite))
            {
                --ready;

                int bytesWritten = write (
                    connections[i].writeFd,
                    connections[i].buffer + connections[i].occupiedFrom,
                    connections[i].occupiedTo - connections[i].occupiedFrom);

                connections[i].occupiedFrom += bytesWritten;

                if (connections[i].occupiedFrom == connections[i].occupiedTo)
                    connections[i].occupiedFrom = (connections[i].occupiedTo = 0);
            }

            if (ready == 0) break;

            if (connections[i].readFd != -1
                && FD_ISSET (connections[i].readFd, &readyToRead)
                && connections[i+1].occupiedTo == 0)
            {
                --ready;

                connections[i+1].occupiedTo = read (connections[i].readFd,
                    connections[i+1].buffer, connections[i+1].bufferSize);

                if (connections[i+1].occupiedTo == 0)
                {
                    close (connections[i].readFd);
                    FD_CLR (connections[i].readFd, &readFds);
                    connections[i].readFd = -1;

                    close (connections[i+1].writeFd);
                    FD_CLR (connections[i+1].writeFd, &writeFds);

                    connections[i+1].writeFd = -1;

                    if (i + 1 == n - 1)
                        goto cleanup;

                }

            }

            if (ready == 0) break;

            if (maxFd < connections[i].readFd)
                maxFd = connections[i].readFd;
            if (maxFd < connections[i].writeFd)
                maxFd = connections[i].writeFd;

        }

        for (; i < n; i++)
        {
            if (maxFd < connections[i].readFd)
                maxFd = connections[i].readFd;
            if (maxFd < connections[i].writeFd)
                maxFd = connections[i].writeFd;
        }

    }

    cleanup:

        for (int i = 0; i < n; i++)
        {
            if (connections[i].pid > 0)
                waitpid (connections[i].pid, 0, 0);
        }

        return exitcode;

}

int childCode (int i, int n, int readFd, int writeFd)
{
    void *buffer = malloc (PIPE_BUF);

    int bytesRead;

    int ttlRead = 0, ttlWrote = 0;

    for (;;)
    {
        ttlRead += (bytesRead = read (readFd, buffer, PIPE_BUF));

        if (bytesRead == 0)
            break;

        if (bytesRead == -1)
        {
            perror ("Error in read");
            break;
        }

        ttlWrote += write (writeFd, buffer, bytesRead);
    }

    close (readFd);
    close (writeFd);
    free (buffer);

}
