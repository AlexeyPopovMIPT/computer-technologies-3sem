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
    CORRECT = 0,
    MUTEX = 1,
    META = 1,
    NEED_WRITE = 2,
    NEED_READ = 3
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

int sendProcess (const char *path);
int receiveProcess ();
#if 1
int sendProcesses (const char *path1, const char *path2);
int receiveProcesses (void);
int stressTest (const char *path);
#endif



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
    else if (strcmp (argv[1], "-stress") == 0)
        return stressTest (argv[2]);
    #endif

    else if (strcmp (argv[1], "-write") == 0)
        return receiveProcess ();

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

int isSemZero (int semid, int sem_num)
{
    struct sembuf oper = { };
    oper.sem_flg = IPC_NOWAIT;
    oper.sem_num = sem_num;
    oper.sem_op = 0;

    return semop (semid, &oper, 1) != -1;
}


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
    // 1. ???????????????? mutex ?????? ???????????????????????????????? ???? ?????????? ???????????????????? ???????? ??????????????

    int semGetIpc;
    if ((semGetIpc = semget (ftok (KEYPATH, 0), 2, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
    {
        
        // ???????????????? ?????? ???????? ??????????????

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

        if (isSemZero (semGetIpc, (enum Semaphore) CORRECT))
        {
            struct sembuf oper [3];
            oper[0].sem_num = (enum Semaphore) MUTEX;
            oper[0].sem_op = +1;
            oper[0].sem_flg = 0;
            oper[1].sem_num = (enum Semaphore) MUTEX;
            oper[1].sem_op = -1;
            oper[1].sem_flg = SEM_UNDO;
            oper[2].sem_num = (enum Semaphore) CORRECT;
            oper[2].sem_op = +1;
            oper[2].sem_flg = 0;
            semop (semGetIpc, oper, 3);
        }

        else
        {
            P (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);
        }


    }

    else
    {
        struct sembuf oper [3];
        oper[0].sem_num = (enum Semaphore) MUTEX;
        oper[0].sem_op = +1;
        oper[0].sem_flg = 0;
        oper[1].sem_num = (enum Semaphore) MUTEX;
        oper[1].sem_op = -1;
        oper[1].sem_flg = SEM_UNDO;
        oper[2].sem_num = (enum Semaphore) CORRECT;
        oper[2].sem_op = +1;
        oper[2].sem_flg = 0;
        semop (semGetIpc, oper, 3);
    }


    //////////
    // 2. ?????????? ???????? ?????????????????? 255 ?????????????????? & shared memory ???????? ???????????? ??????????????????????
    // ?? ???????????? ??????????????????, ???? ?????????????????????????? sender????/receiver????. ?????????????????????? ????????????????????????
    // ?? ??????????????????.

    key_t key;
    int uncreatedIndex = -1;

    for (int i = 1; i < 256; i++)
    {
        key = ftok (KEYPATH, i);

        if (uncreatedIndex == -1)
        {
            // ??????????????????, ?????????????? ???? ???????????????? & shared memory ???? ?????????? ??????????
            if ((*semid = semget (key, 4, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
            {
                if (errno != EEXIST)
                {
                    LOG ("i=%d, ", i)
                    perror ("SKIP Cannot get semaphores");
                    //* ???? ?????????????? */
                    continue;
                }

            }

            else
            {
                // ?????????????????????????? ???? ??????????????

                uncreatedIndex = i;
                semctl (*semid, 0, IPC_RMID);
                *semid = -1;

                LOG ("Set uncreatedIndex = %d\n", i);

                continue;
            }
            
        }

        *semid = semget (key, 4, 0666);

        if (*semid == -1)
        {
            /* ???? ????????????????????, ???? ?????????????????? ???? ???? ?????????????????? */
            continue;
        }

        if (isSemZero (*semid, (enum Semaphore) CORRECT))
        {
            /* ?????????????????? ???????????????? ?????????????? ?????????????? ???????????? ????????????????????, ???? ?????????? ???????????????? ???????????????????????? ?????? ?????? ???????? */
            semctl (*semid, 0, IPC_RMID);
            continue;
        }

        struct semid_ds sem_info = { };
        semctl (*semid, 0, IPC_STAT, &sem_info);


        // ??????????????????, ?????????????????? ???? sender/receiver

        *shmid = shmget (key, SHMSIZE, 0666);
        if (*shmid == -1)
        {
            /* ?????????????????? ???????????????????????? shared memory - ????????????????, sender ?? receiver ?????? ????????,
             * ???? ??????????????????????. ???????????? ???????????? ???????? ???? ????????????
             */
            *semid = -1;
            fprintf (stderr, "Unexpected error while getting shared memory: ");
            perror (NULL);
            continue;
        }

        P (*semid, (enum Semaphore) META, SEM_UNDO);

        struct SharedSegment *shmem = shmat (*shmid, NULL, 0);

        if ((iAmSender ? shmem->senderSelected : shmem->receiverSelected) == 0)
        {
            if (iAmSender) shmem->senderSelected = 1;
            else         shmem->receiverSelected = 1;

            printSemaphoreValues (*semid);
            V (*semid, (enum Semaphore) META, SEM_UNDO);

            LOG ("Got existed semaphores and shared memory no %d\n", i);

            goto cleanup;
        }

        else
        {
            // ????????????????, sender ?? receiver, ???????????????????? ?????? ???????????????? & shared memory, ??????????????????????
            // ?????????????????????? ?? ???? ?????????????? ????????????????
            
            if (time (NULL) - sem_info.sem_otime > RESPONSE_WAIT_TIME)
            {
                // ?????????????????????????? ??????, ????????????, ?????? ???????????????? & shared memory ???? ???????? ??????????????????, ??
                // ???? ?????????? ???????????????????? ?????? uncreated
                shmctl (*shmid, IPC_RMID, 0);
                semctl (*semid, 0, IPC_RMID);
                *shmid = -1;
                *semid = -1;
                i--;
                continue;
            }
            printSemaphoreValues (*semid);
            
            LOG ("i=%d, SKIP Resource already busy\n", i)
            V (*semid, (enum Semaphore) META, SEM_UNDO);

            *semid = -1;
            *shmid = -1;

            continue;
        }
        
    }

    //////////
    // 3. ???????? ????????????, ???????? ???? ?????????? ?????????????????? ?????????????????? & shared memory.
    // ?????????? ?????????????? ?? ?????????????????????????????????????? ??????????

    LOG ("Initializing semaphores no %d\n", uncreatedIndex);

    if (uncreatedIndex == -1)
    {
        // ???? ?????????? ??????????????????
        exitcode = -1;
        goto cleanup;
    }

    key = ftok (KEYPATH, uncreatedIndex);
    *semid = semget (key, 4, 0666 | IPC_CREAT | IPC_EXCL);
    *shmid = shmget (key, SHMSIZE, 0666 | IPC_CREAT);

    struct sembuf oper [3];
    oper[0].sem_flg = 0;
    oper[0].sem_num = (enum Semaphore) CORRECT;
    oper[0].sem_op = +1;
    oper[1].sem_flg = 0;
    oper[1].sem_num = (enum Semaphore) META;
    oper[1].sem_op = +1;
    oper[2].sem_flg = SEM_UNDO;
    oper[2].sem_num = (enum Semaphore) META;
    oper[2].sem_op = -1;
    semop (*semid, oper, 3);

    struct SharedSegment *shmem = shmat (*shmid, NULL, 0);
    shmem->senderSelected = iAmSender;
    shmem->receiverSelected = !iAmSender;
    shmem->bytesCount = 0;


    V (*semid, (enum Semaphore) NEED_WRITE, 0);
    V (*semid, (enum Semaphore) META, 0);

    cleanup:
    
        V (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);
        return exitcode;

}

int sendProcess (const char *path)
{
    int exitcode = 0, shmid = -1, semid = -1, fd_src = -1;
    
    ASSERTED (getIpc (&shmid, &semid, 1) == 0, "Cannot get ipc for ?????????????????????????? ????-??");
    

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
    oper[0].sem_num = (enum Semaphore) NEED_READ;    \
    oper[0].sem_flg = SEM_UNDO;                      \
    oper[1].sem_op  = +1;                            \
    oper[1].sem_num = (enum Semaphore) NEED_WRITE;   \
    oper[1].sem_flg = SEM_UNDO;                      \
    oper[2].sem_op  = -1;                            \
    oper[2].sem_num = (enum Semaphore) NEED_WRITE;   \
    oper[2].sem_flg = 0;                             \
    semop (semid, oper, 3);


    while (1)
    {
        printSemaphoreValues (semid);
        // P (semid, NEED_WRITE, SEM_UNDO);
        {
            oper[0].sem_op  = -1;
            oper[0].sem_num = (enum Semaphore) NEED_WRITE;
            oper[0].sem_flg = SEM_UNDO;
            oper[1].sem_op  = +1;
            oper[1].sem_num = (enum Semaphore) NEED_READ;
            oper[1].sem_flg = 0;
            oper[2].sem_op  = -1;
            oper[2].sem_num = (enum Semaphore) NEED_READ;
            oper[2].sem_flg = SEM_UNDO;
        
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
        int semGetIpc = semget (ftok (KEYPATH, 0), 2, 0666);
        P (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);
        if (shmid != -1)  shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1)  semctl (semid, 0, IPC_RMID);
        V (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);
        if (fd_src != -1) close (fd_src);

    return exitcode;
}

int receiveProcess ()
{
    int exitcode = 0, shmid = -1, semid = -1;

    ASSERTED (getIpc (&shmid, &semid, 0) == 0, "Cannot get ipc for ?????????????????????????? ????-??");
    

    struct SharedSegment *shmem;
    ASSERTED ((shmem = shmat (shmid, NULL, 0)) != NULL, "Can\'t attach shared memory segment");

    struct sigaction act = { };
    act.sa_handler = sigalrm_handler;
    sigaction (SIGALRM, &act, NULL);
    
    struct sembuf oper[3];

    #define CRIT_SECTION_EXIT                        \
    oper[0].sem_op  = +1;                            \
    oper[0].sem_num = (enum Semaphore) NEED_WRITE;   \
    oper[0].sem_flg = SEM_UNDO;                      \
    oper[1].sem_op  = +1;                            \
    oper[1].sem_num = (enum Semaphore) NEED_READ;    \
    oper[1].sem_flg = SEM_UNDO;                      \
    oper[2].sem_op  = -1;                            \
    oper[2].sem_num = (enum Semaphore) NEED_READ;    \
    oper[2].sem_flg = 0;                             \
    semop (semid, oper, 3);

    while (1)
    {
        printSemaphoreValues (semid);
        // P (semid, NEED_READ, 0);
        {
            oper[0].sem_op  = -1;
            oper[0].sem_num = (enum Semaphore) NEED_READ;
            oper[0].sem_flg = SEM_UNDO;
            oper[1].sem_op  = +1;
            oper[1].sem_num = (enum Semaphore) NEED_WRITE;
            oper[1].sem_flg = 0;
            oper[2].sem_op  = -1;
            oper[2].sem_num = (enum Semaphore) NEED_WRITE;
            oper[2].sem_flg = SEM_UNDO;

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
        int semGetIpc = semget (ftok (KEYPATH, 0), 2, 0666);
        P (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);
        if (shmid != -1) shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1) semctl (semid, 0, IPC_RMID);
        V (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);

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
    int ret = sendProcess (pid ? path2 : path1);
    if (pid > 0) waitpid (pid, 0, 0);
    return ret;
}


int receiveProcesses ()
{
    pid_t pid = fork ();
    int ret = receiveProcess ();
    if (pid > 0) waitpid (pid, 0, 0);
    return ret;
}

int stressTest (const char *path)
{
    fork(); fork(); fork(); fork();
    fork(); fork(); fork(); fork();
    sendProcess (path);
}

#endif

