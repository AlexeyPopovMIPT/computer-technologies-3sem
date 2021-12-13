#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>


#define FIFODIR "/tmp"
#define FIFONAME_SAMPLE (FIFODIR "/fifo---")
#define FIFONAME_LEN 12
#define MAX_FIFO_NUM 999
#define PREFIX_LEN 9


#ifdef ENABLE_LOGGING
#define LOG(v1, ...) { fprintf (stderr, "%d: ", getpid ()); fprintf (stderr, v1, __VA_ARGS__); }
#else
#define LOG(...) ;
#endif

#define ASSERTED(action, message) \
if (!(action)) { perror (message); exitcode = -1; goto cleanup; }

int sendProcess (const char *path);
int receiveProcess (pid_t pid);
#if 0
int sendProcesses (const char *path1, const char *path2);
int receiveProcesses (void);
#endif


/* -open for 1st process, -write for second */
int main (int argc, const char **argv)
{
    if (argc == 1)
    {
        printf ("Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }

    if (strcmp (argv[1], "-open") == 0)
        return sendProcess (argv[2]);
    #if 0
    else if (strcmp (argv[1], "-open2") == 0)
        return sendProcesses (argv[2], argv[3]);
    
    else if (strcmp (argv[1], "-write2") == 0)
        return receiveProcesses ();
    #endif

    else if (strcmp (argv[1], "-write") == 0)
        return receiveProcess (getpid());

    else
    {
        printf ("%s: Unknown option: %s\n", argv[0], argv[1]);
        printf ("Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }
}

void setFifoName (char fifoname [FIFONAME_LEN], int num)
{
    sprintf (fifoname + PREFIX_LEN, "%03d", num);
}

int createNamedFifo (char *fifoname)
{
    if (mkdir (FIFODIR, 0777) == -1)
        if (errno != EEXIST)
        {
            perror ("Cannot create directory for fifos");
            return -1;
        }

    for (int num = 0; num <= MAX_FIFO_NUM; num++)
    {
        setFifoName (fifoname, num);

        if (mkfifo (fifoname, 0666) != -1)
            return num;

    }
}

int sendProcess (const char *path)
{
    LOG ("%s", "Process started for reading\n");

    int fd_default = -1, fd_unique = -1, fd_src = -1, exitcode = 0;
    char fifoname [FIFONAME_LEN + 1] = FIFONAME_SAMPLE;

    // creating directory for fifo
    ASSERTED (mkdir (FIFODIR, 0777) != -1 || errno == EEXIST, "Cannot create directory for fifo")


    // making and opening default fifo
    ASSERTED (mkfifo (FIFONAME_SAMPLE, 0666) != -1 || errno == EEXIST, "Cannot make default fifo")
    LOG ("%s\n", "Made default fifo\n");
    ASSERTED ((fd_default = open (FIFONAME_SAMPLE, O_RDWR | O_NONBLOCK)) != -1, "Cannot open default fifo")
    LOG ("%s\n", "Opened default fifo\n");

    // making unique fifo
    int fifoN;
    ASSERTED ((fifoN = createNamedFifo (fifoname)) != -1, "")
    LOG ("Made unique fifo no %d\n", fifoN)

    // trying to write fifoN to default fifo
    ASSERTED (write (fd_default, &fifoN, sizeof (fifoN)) == sizeof (fifoN), "Cannot write fifoN to default fifo")
    LOG ("%s", "Wrote fifoN to default fifo\n")

    // opening unique fifo
    int fd_fool = open (fifoname, O_RDONLY | O_NONBLOCK);
    ASSERTED (fd_fool != -1, "Cannot open unique fifo as reader");
    ASSERTED ((fd_unique = open (fifoname, O_WRONLY | O_NONBLOCK)) != -1, "Cannot open unique fifo");
    close (fd_fool);
    LOG ("Created unique fifo no %d\n", fifoN);

    ASSERTED (fcntl (fd_unique, F_SETFL, O_WRONLY) != -1, "Cannot reset O_NONBLOCK from write side");
    sleep (10);

    // opening requested file
    ASSERTED ((fd_src = open (path, O_RDONLY)) != -1, "Cannot open requested file")

    char buffer [PIPE_BUF];
    int bytesRead;

    while (1)
    {
        ASSERTED ((bytesRead = read (fd_src, buffer, PIPE_BUF)) != -1, "Error while reading from file")
        if (bytesRead == 0)
            break;

        // write file to unique fifo
        ASSERTED (write (fd_unique, buffer, bytesRead) == bytesRead, "Error while writing to unique fifo")
    }

    LOG ("Wrote file %s to fifo no %d\n", path, fifoN)


    cleanup:

        if (fd_default != -1) close (fd_default);
        if (fd_unique  != -1) close (fd_unique);
        if (fd_src     != -1) close (fd_src);
        unlink (FIFONAME_SAMPLE);
        unlink (fifoname); 


    return exitcode;
}


int receiveProcess (pid_t pid)
{
    int exitcode = 0, fd_default = -1, fd_unique = -1;
    char fifoname [FIFONAME_LEN + 1] = FIFONAME_SAMPLE;

    // creating directory for fifo
    ASSERTED (mkdir (FIFODIR, 0777) != -1 || errno == EEXIST, "Cannot create directory for fifo")
    

    // creating default fifo
    ASSERTED (mkfifo (FIFONAME_SAMPLE, 0666) != -1  || errno == EEXIST, "Cannot make default fifo")
    LOG ("%s\n", "Made default fifo\n");
    ASSERTED ((fd_default = open (FIFONAME_SAMPLE, O_RDONLY | O_NONBLOCK)) != -1, "Cannot open default fifo")
    LOG ("%s\n", "Opened default fifo\n");

    // getting unique fifo number
    int fifoN = -1;
    int readCnt = read (fd_default, &fifoN, sizeof (fifoN));
    ASSERTED (readCnt != -1, "Cannot read unique fifo number")
    if (readCnt != sizeof (fifoN))
    {
        fprintf (stderr, "Error: %zu bytes from fifo estimated, got %d. fifoN is 0x%X\n", sizeof (fifoN), readCnt, fifoN);
        exitcode = -1;
        goto cleanup;
    }
    LOG ("Got fifoN=%d", fifoN)

    // generating unique fifo name
    setFifoName (fifoname, fifoN);

    // opening unique fifo
    ASSERTED ((fd_unique = open (fifoname, O_RDONLY | O_NONBLOCK)) != -1, "Cannot open unique fifo")
    ASSERTED (fcntl (fd_unique, F_SETFL, O_RDONLY) != -1, "Cannot reset O_NONBLOCK from write side");

    // transferring data from fifo to stdout
    char buffer [PIPE_BUF];
    int bytesRead;

    while (1)
    {
        ASSERTED ((bytesRead = read (fd_unique, buffer, PIPE_BUF)) != -1, "Error while reading from fifo")
        if (bytesRead == 0)
            break;

        ASSERTED (write (1, buffer, bytesRead) == bytesRead, "Error while writing to stdout")
    }

    cleanup:

        if (fd_default != -1) close (fd_default);
        if (fd_unique  != -1) close (fd_unique);
        unlink (FIFONAME_SAMPLE);
        unlink (fifoname);


    return exitcode;

}

#if 0
int sendProcesses (const char *path1, const char *path2)
{
    LOG ("%s", "Parent\n");
    pid_t pid = fork ();
    sendProcess (pid ? path1 : path2);
    if (pid > 0) waitpid (pid, 0, 0);
    return 0;
}

int receiveProcesses ()
{
    LOG ("%s", "Parent\n");
    pid_t pid = fork ();
    receiveProcess (pid);
    if (pid > 0) waitpid (pid, 0, 0);
    return 0;
}

#endif
