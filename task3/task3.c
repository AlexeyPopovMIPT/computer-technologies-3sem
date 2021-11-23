#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/wait.h>

#if defined (ENABLE_LOGGING)
#define LOG(v1, ...) { fprintf (stderr, "%d: ", getpid ()); fprintf (stderr, v1, __VA_ARGS__); }
#else
#define LOG(...) ;
#endif

#define ASSERTED(action, message) \
if (!(action)) { perror (message); exitcode = -1; goto cleanup; }


int sendProcess (const char *path);
int receiveProcess (pid_t pid);
#if 1
int sendProcesses (const char *path1, const char *path2);
int receiveProcesses (void);
#endif


#define SHMSIZE 4096
#define SHMBUFSIZE (SHMSIZE - 2 * sizeof(short) - sizeof (int))
const char *KEYPATH = "tmp/igh8734yg8hius87fiuhqf78yg348yg7uvihefviuhoh97ghoruehpiuvfsho8qrhe7fvhvourhv8";
enum Semaphore 
{ 
    BUF = 0, /* mutual exclusion for SharedSegment::bytesCount and SharedSegment::buffer */
    META = 1 /* mutual exclusion for SharedSegment::senderSelected and SharedSegment::receiverSelected */
};
enum Semop
{
    ENTRY = -1,
    EXIT  = +1
};

struct SharedSegment
{
    short senderSelected;
    short receiverSelected;
    unsigned bytesCount;
    char buffer [SHMBUFSIZE];
};



/* -open for 1st process, -write for second */
int main (int argc, const char **argv)
{
    if (sizeof (struct SharedSegment) != SHMSIZE)
        abort ();
    
    if (argc == 1)
    {
        printf ("Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }

    if (strcmp (argv[1], "-open") == 0)
        return sendProcess (argv[2]);
    #if 1
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

int getIpc (int *shmid, int *semid, int iAmSender)
{
    assert (iAmSender == 0 || iAmSender == 1);
    key_t key;
    for (int i = 0; i < 256; i++)
    {
        assert (*shmid == -1);
        assert (*semid == -1);

        /* Попытаться создать _новый_ сегмент разделяемой памяти.
         * Если получилось, инициализировать его поля. 
         * Если нет, значит поля уже кто-то проинициализировал;
         * проверить, требуется ли sender
         */

        key = ftok (KEYPATH, i);


        *semid = semget (key, 2, 0666 | IPC_CREAT | IPC_EXCL);
        if (*semid == -1)
        {
            if (errno != EEXIST)
                /* Не повезло */
                continue;

            /* Семафоры с ключом по i существовали на момент вызова semget*/
            
            *semid = semget (key, 2, 0666);

            if (*semid == -1)
                /* Вероятно, они перестали существовать - процессы, использовавшие их,
                 * завершились. Ищем другое i */
                continue;

            *shmid = shmget (key, SHMSIZE, 0666);
            if (*shmid == -1)
            {
                /* Перестала существовать shared memory - также процессы завершились.
                 * Ищем другое i
                 */
                semctl (*semid, 0, IPC_RMID);
                *semid = -1;
                continue;
            }

            /* Получили семафоры и сегмент разделяемой памяти. Проверим, заняты ли они другим
             * sender-ом
             */

            struct sembuf oper = { };
            oper.sem_flg = SEM_UNDO;
            oper.sem_num = (enum Semaphore) META;
            oper.sem_op = (enum Semop) ENTRY; /*-1*/


            semop (*semid, &oper, 1);

            struct SharedSegment *shmem = shmat (*shmid, NULL, 0);

            if ((iAmSender ? shmem->senderSelected : shmem->receiverSelected) == 0)
            {
                if (iAmSender) shmem->senderSelected = 1;
                else         shmem->receiverSelected = 1;

                oper.sem_op = (enum Semop) EXIT; /*+1*/
                semop (*semid, &oper, 1);
                return 0;
            }

            else
            {
                shmctl (*shmid, IPC_RMID, NULL);
                oper.sem_op = (enum Semop) EXIT; /*+1*/
                semop (*semid, &oper, 1);
                semctl (*semid, 0, IPC_RMID);
                *semid = -1;
                *shmid = -1;
                continue;
            }

            
        }

        else
        {
            /* мы - первые, кто создал семафоры с этим номером */
            /* пока в семафорах 0, не беспокоимся, что кто-то залезет в shared memory */

            *shmid = shmget (key, SHMSIZE, 0666 | IPC_CREAT);
            if (*shmid == -1)
            {
                perror ("Cannot get shared memory segment");
                semctl (*semid, 0, IPC_RMID);
                *semid = -1;
                return -1;
            }

            struct SharedSegment *shmem = shmat (*shmid, NULL, 0);

            shmem->senderSelected = iAmSender;
            shmem->receiverSelected = !iAmSender;
            shmem->bytesCount = 0;
            
            struct sembuf oper = { };
            oper.sem_flg = SEM_UNDO;
            oper.sem_num = (enum Semaphore) BUF;
            oper.sem_op = (enum Semop) EXIT; /*+1*/

            semop (*semid, &oper, 1);

            oper.sem_num = (enum Semaphore) META;
            semop (*semid, &oper, 1);

            return 0;

        }

        abort ();

    }

    return -1;
}

int sendProcess (const char *path)
{
    int exitcode = 0, shmid = -1, semid = -1, fd_src = -1;
    
    ASSERTED (getIpc (&shmid, &semid, 1) == 0, "Cannot get ipc for межпроцессное вз-е");
    

    struct SharedSegment *shmem;
    ASSERTED ((shmem = shmat (shmid, NULL, 0)) != NULL, "Can\'t attach shared memory segment");

    ASSERTED ((fd_src = open (path, O_RDONLY)) != -1, "Cannot open requested file")

    int bytesRead;

    struct sembuf oper = { };
    oper.sem_flg = SEM_UNDO;
    oper.sem_num = (enum Semaphore) BUF;
    oper.sem_op = (enum Semop) ENTRY; /*-1*/

    while (1)
    {
        semop (semid, &oper, 1);
        if (shmem->bytesCount != 0)
        {
            printf ("Logical error: expected bytesCount=0, got %d\n", shmem->bytesCount);
            exitcode = -1;
            goto cleanup;
        }

        ASSERTED ((bytesRead = read (fd_src, shmem->buffer, SHMBUFSIZE)) != -1, "Cannot read from file")

        shmem->bytesCount = bytesRead;
        oper.sem_op = (enum Semop) EXIT; /*+1*/
        semop (semid, &oper, 1);

    }
    


    cleanup:

        if (shmid != -1) shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1) semctl (semid, 0, IPC_RMID);
        if (fd_src != -1) close (fd_src);

    return exitcode;
}

int receiveProcess (pid_t pid)
{
    int exitcode = 0, shmid = -1, semid = -1;

    ASSERTED (getIpc (&shmid, &semid, 0) == 0, "Cannot get ipc for межпроцессное вз-е");
    

    struct SharedSegment *shmem;
    ASSERTED ((shmem = shmat (shmid, NULL, 0)) != NULL, "Can\'t attach shared memory segment");


    struct sembuf oper = { };
    oper.sem_flg = SEM_UNDO;
    oper.sem_num = (enum Semaphore) BUF;
    oper.sem_op = (enum Semop) ENTRY; /*-1*/


    while (1)
    {
        semop (semid, &oper, 1);

        if (shmem->bytesCount == 0)
            goto cleanup;

        ASSERTED (write (1, shmem->buffer, shmem->bytesCount) == shmem->bytesCount, "Cannot write data to stdout");
        shmem->bytesCount = 0;

        oper.sem_op = (enum Semop) EXIT; /*+1*/
    }


    cleanup:
        if (shmid != -1) shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1) semctl (semid, 0, IPC_RMID);

    return exitcode;
}


#if 1
int sendProcesses (const char *path1, const char *path2)
{
    pid_t pid = fork ();
    int ret = sendProcess (pid ? path2 : path1);
    if (pid > 0) waitpid (pid, 0, 0);
    return ret;
}


int receiveProcesses ()
{
    pid_t pid = fork ();
    int ret = receiveProcess (pid);
    if (pid > 0) waitpid (pid, 0, 0);
    return ret;
}

#endif

