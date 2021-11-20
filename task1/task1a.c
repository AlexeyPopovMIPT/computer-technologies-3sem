/*
NE RABOTAET!!!!11
*/
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
#define FIFONAME (FIFODIR "/fifo----------")


#define INPUT_BUFFER_LEN (2 * 1024 * 1024)



#if defined (ENABLE_LOGGING)
#define LOG(v1, ...) { fprintf (stderr, "%d: ", getpid ()); fprintf (stderr, v1, __VA_ARGS__); }
#define PERROR(condition) { if (!(condition)) { fprintf (stderr, "%d: Line %d: ", getpid (), __LINE__); perror (0); } }
#else
#define LOG(...) ;
#define PERROR(condition) { if (!(condition)) { perror ("ERROR"); } }
#endif

int openProcess (const char *path);
int openProcesses (const char *path1, const char *path2);
int writeProcess (void);
int writeProcesses (void);


/* -open for 1st process, -write for second */
int main (int argc, const char **argv)
{
    if (argc == 1)
    {
        printf ("Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }

    if (strcmp (argv[1], "-open") == 0)
        return openProcess (argv[2]);
    
    else if (strcmp (argv[1], "-open2") == 0)
        return openProcesses (argv[2], argv[3]);
    
    else if (strcmp (argv[1], "-write2") == 0)
        return writeProcesses ();

    else if (strcmp (argv[1], "-write") == 0)
        return writeProcess ();

    else
    {
        printf ("%s: Unknown option: %s\n", argv[0], argv[1]);
        printf ("Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }
}




int openProcess (const char *path)
{
    LOG ("%s", "Process started for reading\n");

    // creating directory for fifo
    PERROR (mkdir (FIFODIR, S_IRWXG | S_IRWXO | S_IRWXU) != -1 || errno == EEXIST);

    // creating fifo
    mkfifo (FIFONAME, 0666);
    int descrip = open (FIFONAME, O_RDWR | O_NONBLOCK);
    PERROR (descrip != -1);
    LOG ("%s", "Opened fifo\n");

    // wait for pair process to open for reading
    sleep (5);

    // trying to write pathname to fifo
    char filenameBuffer [PATH_MAX];
    int pathLen = strlen (path);
    if (pathLen > PATH_MAX)
    {
        printf ("name too long\n");
        goto cleanup;
    }

    memcpy (filenameBuffer, path, pathLen);
    if (pathLen < PATH_MAX) 
        filenameBuffer[pathLen] = '\0';

    // PERROR (fcntl (descrip, F_SETFD, O_RDWR| O_NONBLOCK) != -1);
    PERROR (write (descrip, filenameBuffer, PATH_MAX) != -1);

    LOG ("Wrote %s to %s\n", filenameBuffer, FIFONAME);

    cleanup:
    close (descrip);
    unlink (FIFONAME);  

    return 0;
}


void printFile (const char *path)
{
    int descrip = open (path, O_RDONLY);
    if (descrip <= 0)
        LOG ("Cannot open %s\n", path);
    
    char * const buffer = (char * const) malloc (INPUT_BUFFER_LEN);
    if (buffer == NULL)
    {
        LOG ("%s", "Memory error\n");
        return;
    }

    int bytesRead;

    while (1)
    {
        bytesRead = read (descrip, buffer, INPUT_BUFFER_LEN);
        if (bytesRead == 0)
            break;
        if (bytesRead < 0)
        {
            PERROR (bytesRead >= 0);
            close (descrip);
            return;
        }

        write (1, buffer, bytesRead);

    }

    free (buffer);
    close (descrip);

}


int writeProcess ()
{
    // Opening fifo
    mkfifo (FIFONAME, 0666);
    int descrip = open (FIFONAME, O_RDONLY);
    if (descrip <= 0)
        LOG ("Cannot open %s\n", FIFONAME);
    LOG ("Opened %s\n", FIFONAME);

    // Getting file name
    char filename [PATH_MAX + 1] = {1, 2, 3, 4};
    filename [PATH_MAX] = '\0';
    int bytesRead = read (descrip, filename, PATH_MAX);
    PERROR (bytesRead != -1);
    LOG ("Got filename %s(%X...)\n", filename, *(int*)filename);


    close (descrip);
    unlink (FIFONAME);


    if (bytesRead == 0 || bytesRead == -1)
        printf ("Logical error: pipe is empty\n");
    else
        printFile (filename);


    return 0;

}


int openProcesses (const char *path1, const char *path2)
{
    LOG ("%s", "Parent\n");
    pid_t pid = fork ();
    openProcess (pid ? path1 : path2);
    if (pid > 0) waitpid (pid, 0, 0);
    return 0;
}

int writeProcesses ()
{
    LOG ("%s", "Parent\n");
    pid_t pid = fork ();
    writeProcess ();
    if (pid > 0) waitpid (pid, 0, 0);
    return 0;
}
