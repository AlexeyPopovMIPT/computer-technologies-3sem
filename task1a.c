#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

// #define ENABLE_LOGGING

#define FIFODIR "/tmp/myfifo"
#define FIFONAME_SAMPLE (FIFODIR "/fifo----------")
#define FIFONAME_LEN 27
#define PREFIX_LEN 16

#define PID_TR_LEN 16
#define MAX_FIFOS 999999

#define INPUT_BUFFER_LEN 2 * 1024 * 1024



#if defined (ENABLE_LOGGING)
#define LOG(v1, ...) { fprintf (stderr, "%d: ", getpid ()); fprintf (stderr, v1, __VA_ARGS__); }
#define PERROR(condition) { if (!(condition)) { fprintf (stderr, "%d: Line %d: ", getpid (), __LINE__); perror (0); } }
#else
#define LOG(...) ;
#define PERROR(condition) { if (!(condition)) { perror ("ERROR"); } }
#endif

int openProcess (const char *filename);
int openProcesses (const char *filename1, const char *filename2);
int writeProcess (int pid);
int writeProcesses (void);


/* -open for 1st process, -write for second */
int main (int argc, const char **argv)
{
    if (argc == 1)
    {
        printf ("Usage: %s [-open <string>] or [-write]\n", argv[0]);
        return 0;
    }

    if (strcmp (argv[1], "-open") == 0)
        return openProcess (argv[2]);
    
    else if (strcmp (argv[1], "-open2") == 0)
        return openProcesses (argv[2], argv[3]);
    
    else if (strcmp (argv[1], "-write2") == 0)
        return writeProcesses ();

    else if (strcmp (argv[1], "-write") == 0)
        return writeProcess (getpid ());

    else
    {
        printf ("%s: Unknown option: %s\n", argv[0], argv[1]);
        printf ("Usage: %s [-open <string>] or [-write]\n", argv[0]);
        return 0;
    }
}


int getFifoNumber ()
{
    int ret = fork ();
    if (ret == 0)
        exit (0);
    return ret;
}

void createFifoName (char *sample, int num)
{
    strcpy (sample, FIFONAME_SAMPLE);
    sprintf (sample + FIFONAME_LEN - 11, "%10d", num);
}


int openProcess (const char *filename)
{
    LOG ("%s", "Process started for reading\n");

    // creating directory for fifos
    PERROR (mkdir (FIFODIR, S_IRWXG | S_IRWXO | S_IRWXU) != -1 || errno == EEXIST);

    // creating unique name for fifo
    int fifoNumber = getFifoNumber ();
    char fifoname [FIFONAME_LEN];
    createFifoName (fifoname, fifoNumber);
    mkfifo (fifoname, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

    // creating default fifo
    mkfifo (FIFONAME_SAMPLE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    int descrip = open (FIFONAME_SAMPLE, O_WRONLY);
    PERROR (descrip != -1);
    LOG ("%s", "Opened default fifo\n");

    // writing unique fifo number to default fifo
    write (descrip, &fifoNumber, sizeof (fifoNumber));
    LOG ("Printed %d to default fifo\n", fifoNumber);

    // creating fifo with unique name
    int descripUnique = open (fifoname, O_WRONLY);
    PERROR (descripUnique != -1);
    LOG ("Created %s\n", fifoname);

    // writing filename to unique fifo
    char filenameBuffer [5004];
    strcpy (filenameBuffer, filename);
    write (descripUnique, filenameBuffer, 5004);

    LOG ("Wrote %s to %s\n", filenameBuffer, fifoname);

    //close (descrip);
    //close (descripUnique);

    return 0;
}


void printFile (const char *filename)
{
    int descrip = open (filename, O_RDONLY);
    if (descrip <= 0)
        LOG ("Cannot open %s\n", filename);
    
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


int getFifoName (int pid)
{
    mkfifo (FIFONAME_SAMPLE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    int descrip = open (FIFONAME_SAMPLE, O_RDONLY);
    if (descrip <= 0)
        LOG ("Cannot open %s\n", FIFONAME_SAMPLE);
    LOG ("Opened %s\n", FIFONAME_SAMPLE);

    int num = 0;
    read (descrip, &num, sizeof (num));
    LOG ("Got file num %d\n", num);

    //close (descrip);
    return num;
}


int writeProcess (int pid)
{
    if (pid != 0) sleep (3);
    /* после этого ребёнок виснет, т.к. закрылись все дескрипторы на запись. */
    /* зависание без слипа - успевает открыть фифо, но не может читать */
    /* вариант решения - не закрывать фифо, но анлинкать? */
    
    // Reading unique fifo name
    int num = getFifoName (pid);

    // Generating unique fifo name
    char fifoname [FIFONAME_LEN];
    createFifoName (fifoname, num);

    // Opening unique fifo    
    mkfifo (fifoname, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    int descripUnique = open (fifoname, O_RDONLY);
    if (descripUnique <= 0)
        LOG ("Cannot open %s\n", fifoname);
    LOG ("Opened %s\n", fifoname);

    // Getting file name
    char filename [5004] = {1, 2, 3, 4};
    read (descripUnique, filename, 5004);
    LOG ("Got filename %s(%X...)\n", filename, *(int*)filename);

    // Priniting file to console
    printFile (filename);
    //close (descripUnique);

    unlink (fifoname);
    unlink (FIFONAME_SAMPLE);
    return 0;

}


int openProcesses (const char *filename1, const char *filename2)
{
    LOG ("%s", "Parent\n");
    pid_t pid = fork ();
    openProcess (pid ? filename1 : filename2);
    if (pid > 0) waitpid (pid, 0, 0);
    return 0;
}

int writeProcesses ()
{
    LOG ("%s", "Parent\n");
    pid_t pid = fork ();
    writeProcess (pid);
    if (pid > 0) waitpid (pid, 0, 0);
    return 0;
}
