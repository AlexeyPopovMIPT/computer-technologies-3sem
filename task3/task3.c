#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <assert.h>
#include <stdlib.h> // for exit ()
#include <string.h> // for strcmp ()
#include <time.h> // for time ()


#ifdef ENABLE_LOGGING
#define LOG(v1, ...) { fprintf (stderr, "%d: ", getpid ()); fprintf (stderr, v1, __VA_ARGS__); }
#else
#define LOG(...) ;
#endif


#define ASSERTED(action, message) if (!(action)) { fprintf (stderr, "%d: ", getpid ()); perror (message); exitcode = -1; goto cleanup; }


#define RESPONSE_WAIT_TIME 10
#define SHMSIZE 4096
#define SHMBUFSIZE (SHMSIZE - 2 * sizeof(short) - sizeof (int))
const char *KEYPATH = "/tmp/igh8734yg8hius87fiuhqf78yg348yg7uvihefviuhoh97ghoruehpiuvfsho8qrhe7fvhvourhv9";
enum Semaphore 
{ 
    META = 0, /* mutual exclusion for SharedSegment::bytesCount and SharedSegment::buffer */
    NEED_WRITE = 1,
    NEED_READ = 2
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


int P (int semid, int sem_num, int sem_flg);
int V (int semid, int sem_num, int sem_flg);

void sigalrm_handler (int sig);

int sendProcess (const char *path, pid_t pid);
int receiveProcess (pid_t pid);
#if 1
int sendProcesses (const char *path1, const char *path2);
int receiveProcesses (void);
int stressTest (const char *path);
#endif



/* -open for 1st process, -write for second */
int main (int argc, const char **argv)
{
    if (sizeof (struct SharedSegment) != SHMSIZE)
        exit (-1);
    
    if (argc == 1)
    {
        printf ("Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }

    if (strcmp (argv[1], "-open") == 0)
        return sendProcess (argv[2], getpid ());
    #if 1
    else if (strcmp (argv[1], "-open2") == 0)
        return sendProcesses (argv[2], argv[3]);
    
    else if (strcmp (argv[1], "-write2") == 0)
        return receiveProcesses ();
    else if (strcmp (argv[1], "-stress") == 0)
        return stressTest (argv[2]);
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

#ifdef ENABLE_SEM_LOGGING
void printSemaphoreValues (int semid, int line)
{
    fprintf (stderr, "Line %d: %d: ", line, semid);
    struct sembuf oper = { };
    oper.sem_flg = IPC_NOWAIT;
    oper.sem_num = (enum Semaphore) META;
    oper.sem_op = (enum Semop) 0;
    fprintf (stderr, "META=%d, ", semop (semid, &oper, 1) == -1 ? 1 : 0);

    oper.sem_num = (enum Semaphore) NEED_READ;
    fprintf (stderr, "NEED_READ=%d, ", semop (semid, &oper, 1) == -1 ? 1 : 0);

    oper.sem_num = (enum Semaphore) NEED_WRITE;
    fprintf (stderr, "NEED_WRITE=%d\n", semop (semid, &oper, 1) == -1 ? 1 : 0);
}
#else
void printSemaphoreValues (int semid, int line) {}
#endif
#define printSemaphoreValues(semid) printSemaphoreValues (semid, __LINE__)


int P (int semid, int sem_num, int sem_flg)
{
    struct sembuf oper = { };
    oper.sem_flg = sem_flg;
    oper.sem_num = sem_num;
    oper.sem_op = -1;
    return semop (semid, &oper, 1);
}

int V (int semid, int sem_num, int sem_flg)
{
    struct sembuf oper = { };
    oper.sem_flg = sem_flg;
    oper.sem_num = sem_num;
    oper.sem_op = +1;
    return semop (semid, &oper, 1);
}

int getIpc (int *shmid, int *semid, int iAmSender)
{
    assert (iAmSender == 0 || iAmSender == 1);
    int exitcode = 0;

    creat (KEYPATH, 0666);

    //////////
    // 1. Получаем mutex для взаимоисключения на время выполнения этой функции

    int semGetIpc;
    int needUndoForSemGetIpc = -1;
    if ((semGetIpc = semget (ftok (KEYPATH, 0), 1, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
    {
        // Семафоры уже были созданы

        if (errno != EEXIST)
        {
            perror ("Error while getting mutex for getIpc sync");
            return -1;
        }

        if ((semGetIpc = semget (ftok (KEYPATH, 0), 1, 0666)) == -1)
        {
            perror ("Semaphores disappeared");
            return -1;
        }

        // Нужно повиснуть на входе в к.с.
        needUndoForSemGetIpc = 1;
        P (semGetIpc, 0, SEM_UNDO);

    }

    else
    {
        needUndoForSemGetIpc = 0;
    }


    //////////
    // 2. Среди всех возможных 255 семафоров & shared memory ищем первые несозданные
    // и первые созданные, но незаполненные senderом/receiverом. Приоритетно подключаемся
    // к созданным.

    key_t key;
    int uncreatedIndex = -1;

    for (int i = 1; i < 256; i++)
    {
        key = ftok (KEYPATH, i);

        if (uncreatedIndex == -1)
        {
            // Проверить, созданы ли семафоры & shared memory по этому ключу
            if ((*semid = semget (key, 3, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
            {
                if (errno != EEXIST)
                {
                    LOG ("i=%d, ", i)
                    perror ("SKIP Cannot get semaphores");
                    /* Не повезло */
                    continue;
                }

            }

            else
            {
                // Действительно не созданы

                uncreatedIndex = i;
                semctl (*semid, 0, IPC_RMID);
                *semid = -1;

                LOG ("Set uncreatedIndex = %d\n", i);

                continue;
            }
            
        }

        *semid = semget (key, 3, 0666);

        if (*semid == -1)
        {
            /* Не существуют, но создавать их не требуется - uncreatedIndex уже заполнен */
            LOG ("i=%d, SKIP Semaphores are not exist\n", i)
            continue;
        }

        struct semid_ds sem_info = { };
        semctl (*semid, 0, IPC_STAT, &sem_info);


        // Проверить, требуется ли sender/receiver

        *shmid = shmget (key, SHMSIZE, 0666);
        if (*shmid == -1)
        {
            /* Перестала существовать shared memory - вероятно, sender и receiver оба были,
             * но завершились. Вообще такого быть не должно
             */
            *semid = -1;
            fprintf (stderr, "Unexpected error while getting shared memory: ");
            perror ("");
            continue;
        }

        P (*semid, META, SEM_UNDO);

        struct SharedSegment *shmem = shmat (*shmid, NULL, 0);

        if ((iAmSender ? shmem->senderSelected : shmem->receiverSelected) == 0)
        {
            if (iAmSender) shmem->senderSelected = 1;
            else         shmem->receiverSelected = 1;

            printSemaphoreValues (*semid);
            V (*semid, META, SEM_UNDO);

            LOG ("Got existed semaphores and shared memory no %d\n", i);

            goto cleanup;
        }

        else
        {
            // Возможно, sender и receiver, занимавшие эти семафоры & shared memory, некорректно
            // завершились и не удалили семафоры
            
            if (time (NULL) - sem_info.sem_otime > RESPONSE_WAIT_TIME)
            {
                // Действительно так, значит, эти семафоры & shared memory по сути незанятые, и
                // их нужно обработать как uncreated
                shmctl (*shmid, IPC_RMID, 0);
                semctl (*semid, 0, IPC_RMID);
                *shmid = -1;
                *semid = -1;
                i--;
                continue;
            }
            printSemaphoreValues (*semid);
            
            LOG ("i=%d, SKIP Resource already busy\n", i)
            V (*semid, META, SEM_UNDO);

            *semid = -1;
            *shmid = -1;

            continue;
        }
        
    }

    //////////
    // 3. Сюда попали, если не нашли созданных семафоров & shared memory.
    // Нужно создать и проинициальзировать новые

    if (uncreatedIndex == -1)
    {
        // не нашли незанятых
        exitcode = -1;
        goto cleanup;
    }

    key = ftok (KEYPATH, uncreatedIndex);
    *semid = semget (key, 3, 0666 | IPC_CREAT | IPC_EXCL);
    *shmid = shmget (key, SHMSIZE, 0666 | IPC_CREAT);

    struct SharedSegment *shmem = shmat (*shmid, NULL, 0);
    shmem->senderSelected = iAmSender;
    shmem->receiverSelected = !iAmSender;
    shmem->bytesCount = 0;

    V (*semid, NEED_WRITE, 0);
    V (*semid, META, 0);

    cleanup:
    
        assert (needUndoForSemGetIpc != -1);
        V (semGetIpc, 0, needUndoForSemGetIpc ? SEM_UNDO : 0);
        return exitcode;

}

int sendProcess (const char *path, pid_t pid)
{
    int exitcode = 0, shmid = -1, semid = -1, fd_src = -1;
    
    ASSERTED (getIpc (&shmid, &semid, 1) == 0, "Cannot get ipc for межпроцессное вз-е");
    

    struct SharedSegment *shmem;
    ASSERTED ((shmem = shmat (shmid, NULL, 0)) != NULL, "Cannot attach shared memory segment");

    ASSERTED ((fd_src = open (path, O_RDONLY)) != -1, "Cannot open requested file")

    struct sigaction act = { };
    act.sa_handler = sigalrm_handler;
    sigaction (SIGALRM, &act, NULL);

    int bytesRead;
    struct sembuf oper[3];

    #define CRIT_SECTION_EXIT                        \
    oper[0].sem_op  = +1;                            \
    oper[0].sem_num = (enum Semaphore) NEED_WRITE;   \
    oper[0].sem_flg = 0;                             \
    oper[1].sem_op  = -1;                            \
    oper[1].sem_num = (enum Semaphore) NEED_WRITE;   \
    oper[1].sem_flg = SEM_UNDO;                      \
    oper[2].sem_op  = +1;                            \
    oper[2].sem_num = (enum Semaphore) NEED_READ;    \
    oper[2].sem_flg = 0;                             \
    semop (semid, oper, 3);


    while (1)
    {
        printSemaphoreValues (semid);
        // P (semid, NEED_WRITE, SEM_UNDO);
        {
            oper[0].sem_op  = (enum Semop) ENTRY;
            oper[0].sem_num = (enum Semaphore) NEED_WRITE;
            oper[0].sem_flg = 0;
            oper[1].sem_op  = +1;
            oper[1].sem_num = (enum Semaphore) NEED_READ;
            oper[1].sem_flg = SEM_UNDO;
            oper[2].sem_op  = -1;
            oper[2].sem_num = (enum Semaphore) NEED_READ;
            oper[2].sem_flg = 0;
        
            alarm (RESPONSE_WAIT_TIME);
            semop (semid, oper, 3);
            alarm (0);
        }
        printSemaphoreValues (semid);

        if (shmem->bytesCount != 0)
        {
            fprintf (stderr, "%d: ", getpid ());
            fprintf (stderr, "Logical error: expected bytesCount=0, got %d\n", shmem->bytesCount);
            exitcode = -1;
            printSemaphoreValues (semid);
            //V (semid, NEED_READ, SEM_UNDO);
            CRIT_SECTION_EXIT
            goto cleanup;
        }

        if ((bytesRead = read (fd_src, shmem->buffer, SHMBUFSIZE)) == -1)
        {
            fprintf (stderr, "%d: ", getpid ());
            perror ("Cannot read from file");
            exitcode = -1;
            printSemaphoreValues (semid);
            // V (semid, NEED_READ, SEM_UNDO);
            CRIT_SECTION_EXIT
            goto cleanup;
        }

        shmem->bytesCount = bytesRead;

        printSemaphoreValues (semid);
        // V (semid, NEED_READ, SEM_UNDO);
        CRIT_SECTION_EXIT

        if (bytesRead == 0) break;

    }

    #undef CRIT_SECTION_EXIT

    cleanup:

        LOG ("%s", "Cleaning up\n")
        int semGetIpc = semget (ftok (KEYPATH, 0), 1, 0666);
        P (semGetIpc, 0, SEM_UNDO);
        if (shmid != -1)  shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1)  semctl (semid, 0, IPC_RMID);
        V (semGetIpc, 0, SEM_UNDO);
        if (fd_src != -1) close (fd_src);

    return exitcode;
}

int receiveProcess (pid_t pid)
{
    int exitcode = 0, shmid = -1, semid = -1;

    ASSERTED (getIpc (&shmid, &semid, 0) == 0, "Cannot get ipc for межпроцессное вз-е");
    

    struct SharedSegment *shmem;
    ASSERTED ((shmem = shmat (shmid, NULL, 0)) != NULL, "Can\'t attach shared memory segment");

    struct sigaction act = { };
    act.sa_handler = sigalrm_handler;
    sigaction (SIGALRM, &act, NULL);
    
    struct sembuf oper[3];

    #define CRIT_SECTION_EXIT                        \
    oper[0].sem_op  = +1;                            \
    oper[0].sem_num = (enum Semaphore) NEED_READ;    \
    oper[0].sem_flg = 0;                             \
    oper[1].sem_op  = -1;                            \
    oper[1].sem_num = (enum Semaphore) NEED_READ;    \
    oper[1].sem_flg = SEM_UNDO;                      \
    oper[2].sem_op  = +1;                            \
    oper[2].sem_num = (enum Semaphore) NEED_WRITE;   \
    oper[2].sem_flg = 0;                             \
    semop (semid, oper, 3);

    while (1)
    {
        printSemaphoreValues (semid);
        // P (semid, NEED_READ, 0);
        {
            oper[0].sem_op  = (enum Semop) ENTRY;
            oper[0].sem_num = (enum Semaphore) NEED_READ;
            oper[0].sem_flg = 0;
            oper[1].sem_op  = +1;
            oper[1].sem_num = (enum Semaphore) NEED_WRITE;
            oper[1].sem_flg = SEM_UNDO;
            oper[2].sem_op  = -1;
            oper[2].sem_num = (enum Semaphore) NEED_WRITE;
            oper[2].sem_flg = 0;
            alarm (RESPONSE_WAIT_TIME);
            semop (semid, oper, 3);
            alarm (0);
        }
        
        printSemaphoreValues (semid);

        if (shmem->bytesCount == 0)
        {
            printSemaphoreValues (semid);
            //V (semid, NEED_WRITE, 0);
            CRIT_SECTION_EXIT
            goto cleanup;
        }

        if (write (1, shmem->buffer, shmem->bytesCount) != shmem->bytesCount)
        {
            fprintf (stderr, "%d: ", getpid ());
            perror ("Cannot write data to stdout");
            exitcode = -1;
            printSemaphoreValues (semid);
            //V (semid, NEED_WRITE);
            CRIT_SECTION_EXIT
            goto cleanup;

        }

        shmem->bytesCount = 0;

        printSemaphoreValues (semid);
        //V (semid, NEED_WRITE);
        CRIT_SECTION_EXIT
    }

    #undef CRIT_SECTION_EXIT

    cleanup:
        LOG ("%s\n", "Cleaning up")
        int semGetIpc = semget (ftok (KEYPATH, 0), 1, 0666);
        P (semGetIpc, 0, SEM_UNDO);
        if (shmid != -1) shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1) semctl (semid, 0, IPC_RMID);
        V (semGetIpc, 0, SEM_UNDO);

    return exitcode;
}

void sigalrm_handler (int sig)
{
    fprintf (stderr, "Waiting for a response from a pair process has timed out\n");
    exit (-1);
}


#if 1
int sendProcesses (const char *path1, const char *path2)
{
    pid_t pid = fork ();
    int ret = sendProcess (pid ? path2 : path1, pid);
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

int stressTest (const char *path)
{
    fork(); fork(); fork(); fork();
    fork(); fork(); fork(); fork();
    sendProcess (path, getpid ());
}

#endif

