#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h> // for exit ()
#include <string.h> // for strcmp ()
#include <time.h> // for time ()

#define ASSERTED(action, message) if (!(action)) { fprintf (stderr, "%d: ", getpid ()); perror (message); exitcode = -1; goto cleanup; }


#define RESPONSE_WAIT_TIME 10000
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
#endif


/* -open for 1st process, -write for second */
int main (int argc, const char **argv)
{
    if (sizeof (struct SharedSegment) != SHMSIZE)
        abort ();
    
    if (argc == 1)
    {
        fprintf (stderr, "Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }

    if (strcmp (argv[1], "-open") == 0)
        return sendProcess (argv[2]);

    else if (strcmp (argv[1], "-write") == 0)
        return receiveProcess ();

    #if 1
    else if (strcmp (argv[1], "-open2") == 0)
        return sendProcesses (argv[2], argv[3]);
    
    else if (strcmp (argv[1], "-write2") == 0)
        return receiveProcesses ();
    #endif

    else
    {
        fprintf (stderr, "%s: Unknown option: %s\n", argv[0], argv[1]);
        fprintf (stderr, "Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }
}


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
    // 1. Получаем mutex для взаимоисключения на время выполнения этой функции

    int semGetIpc = semget (ftok (KEYPATH, 0), 2, 0666 | IPC_CREAT);

    if (semGetIpc == -1)
    {
        perror ("Error while getting mutex for getIpc sync");
        return -1;
    }

    {
        struct sembuf oper [2];
        oper[0].sem_num = (enum Semaphore) MUTEX;
        oper[0].sem_flg = 0;
        oper[0].sem_op = 0;
        oper[1].sem_num = (enum Semaphore) MUTEX;
        oper[1].sem_flg = SEM_UNDO;
        oper[1].sem_op = 1;
        semop (semGetIpc, oper, 2);
        putchar (1);
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
        *semid = semget (key, 4, 0666 | IPC_CREAT);

        // Проверить, созданы ли семафоры & shared memory по этому ключу
        if (*semid == -1)
        {
            perror ("SKIP Cannot get semaphores");
            //* Не повезло */
            continue;

        }

        else if (isSemZero (*semid, (enum Semaphore) CORRECT))
        {
            // Действительно не созданы

            if (uncreatedIndex == -1) uncreatedIndex = i;
            semctl (*semid, 0, IPC_RMID);
            *semid = -1;

            continue;
        }

        else
        {
                // созданы
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
            perror (NULL);
            continue;
        }

        P (*semid, (enum Semaphore) META, SEM_UNDO);

        struct SharedSegment *shmem = shmat (*shmid, NULL, 0);

        if ((iAmSender ? shmem->senderSelected : shmem->receiverSelected) == 0)
        {
            if (iAmSender) shmem->senderSelected = 1;
            else         shmem->receiverSelected = 1;

            // if (!iAmSender) kill (getpid(), SIGKILL);

            struct sembuf oper [2];
            oper[0].sem_flg = 0;
            oper[0].sem_num = iAmSender ? (enum Semaphore) NEED_READ : (enum Semaphore) NEED_WRITE;
            oper[0].sem_op = +10;
            oper[1].sem_flg = SEM_UNDO;
            oper[1].sem_num = iAmSender ? (enum Semaphore) NEED_READ : (enum Semaphore) NEED_WRITE;
            oper[1].sem_op = -10;
            semop (*semid, oper, 2);

            V (*semid, (enum Semaphore) META, SEM_UNDO);

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
        
            V (*semid, (enum Semaphore) META, SEM_UNDO);

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
    *semid = semget (key, 4, 0666 | IPC_CREAT | IPC_EXCL);
    *shmid = shmget (key, SHMSIZE, 0666 | IPC_CREAT);

    struct sembuf oper [5];
    oper[0].sem_flg = 0;
    oper[0].sem_num = (enum Semaphore) CORRECT;
    oper[0].sem_op = +1;
    oper[1].sem_flg = 0;
    oper[1].sem_num = (enum Semaphore) META;
    oper[1].sem_op = +1;
    oper[2].sem_flg = SEM_UNDO;
    oper[2].sem_num = (enum Semaphore) META;
    oper[2].sem_op = -1;
    oper[3].sem_flg = 0;
    oper[3].sem_num = iAmSender ? (enum Semaphore) NEED_READ : (enum Semaphore) NEED_WRITE;
    oper[3].sem_op = +10;
    oper[4].sem_flg = SEM_UNDO;
    oper[4].sem_num = iAmSender ? (enum Semaphore) NEED_READ : (enum Semaphore) NEED_WRITE;
    oper[4].sem_op = -10;
    
    semop (*semid, oper, 5);

    struct SharedSegment *shmem = shmat (*shmid, NULL, 0);
    shmem->senderSelected = iAmSender;
    shmem->receiverSelected = !iAmSender;
    shmem->bytesCount = 0;


    V (*semid, (enum Semaphore) NEED_WRITE, 0);
    V (*semid, (enum Semaphore) META, 0);

    cleanup:
    
        P (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);
        return exitcode;

}

int sendProcess (const char *path)
{
    int exitcode = 0, shmid = -1, semid = -1, fd_src = -1, semGetIpc = -1;
    
    ASSERTED (getIpc (&shmid, &semid, 1) == 0, "Cannot get ipc for межпроцессное вз-е");
    fprintf (stderr, "%d\n", semid);
    

    struct SharedSegment *shmem;
    ASSERTED ((shmem = shmat (shmid, NULL, 0)) != NULL, "Cannot attach shared memory segment");

    ASSERTED ((fd_src = open (path, O_RDONLY)) != -1, "Cannot open requested file")

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
        
            semop (semid, oper, 3);
        }

        if (shmem->bytesCount != 0)
        {
            fprintf (stderr, "%d: ", getpid ());
            fprintf (stderr, "Logical error: expected bytesCount=0, got %d\n", shmem->bytesCount);
            exitcode = -1;

            CRIT_SECTION_EXIT
            goto cleanup;
        }

        if ((bytesRead = read (fd_src, shmem->buffer, SHMBUFSIZE)) == -1)
        {
            fprintf (stderr, "%d: ", getpid ());
            perror ("Cannot read from file");
            exitcode = -1;

            CRIT_SECTION_EXIT
            goto cleanup;
        }

        shmem->bytesCount = bytesRead;

        CRIT_SECTION_EXIT

        if (bytesRead == 0) break;

    }

    #undef CRIT_SECTION_EXIT

    cleanup:

        semGetIpc = semget (ftok (KEYPATH, 0), 2, 0666);
        {
            struct sembuf oper [2];
            oper[0].sem_num = (enum Semaphore) MUTEX;
            oper[0].sem_flg = 0;
            oper[0].sem_op = 0;
            oper[1].sem_num = (enum Semaphore) MUTEX;
            oper[1].sem_flg = SEM_UNDO;
            oper[1].sem_op = 1;

            semop (semGetIpc, oper, 2);
        }
        if (shmid != -1)  shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1)  semctl (semid, 0, IPC_RMID);
        P (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);
        if (fd_src != -1) close (fd_src);

    return exitcode;
}

int receiveProcess ()
{
    int exitcode = 0, shmid = -1, semid = -1, semGetIpc = -1;

    ASSERTED (getIpc (&shmid, &semid, 0) == 0, "Cannot get ipc for межпроцессное вз-е");
    fprintf (stderr, "%d\n", semid);

    struct SharedSegment *shmem;
    ASSERTED ((shmem = shmat (shmid, NULL, 0)) != NULL, "Can\'t attach shared memory segment");
    
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

            semop (semid, oper, 3);
        }

        if (shmem->bytesCount == 0)
        {
            CRIT_SECTION_EXIT
            goto cleanup;
        }

        if (write (1, shmem->buffer, shmem->bytesCount) != shmem->bytesCount)
        {
            fprintf (stderr, "%d: ", getpid ());
            perror ("Cannot write data to stdout");
            exitcode = -1;

            CRIT_SECTION_EXIT
            goto cleanup;

        }

        shmem->bytesCount = 0;

        CRIT_SECTION_EXIT
    }

    #undef CRIT_SECTION_EXIT

    cleanup:
    
        semGetIpc = semget (ftok (KEYPATH, 0), 2, 0666);
        {
            struct sembuf oper [2];
            oper[0].sem_num = (enum Semaphore) MUTEX;
            oper[0].sem_flg = 0;
            oper[0].sem_op = 0;
            oper[1].sem_num = (enum Semaphore) MUTEX;
            oper[1].sem_flg = SEM_UNDO;
            oper[1].sem_op = 1;
            semop (semGetIpc, oper, 2);
        }
        if (shmid != -1)  shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1)  semctl (semid, 0, IPC_RMID);
        P (semGetIpc, (enum Semaphore) MUTEX, SEM_UNDO);

    return exitcode;
}


#if 1
int sendProcesses (const char *path1, const char *path2)
{
    pid_t pid = fork ();
    int ret = sendProcess (pid ? path2 : path1);
    return ret;
}


int receiveProcesses ()
{
    pid_t pid = fork ();
    int ret = receiveProcess ();
    return ret;
}

int stressTest (const char *path)
{
    fork(); fork(); fork(); fork();
    fork(); fork(); fork(); fork();
    sendProcess (path);
}

#endif





// Don't pay attention to this please :)

#if 0
key = ftok (KEYPATH, i);

        if (uncreatedIndex == -1)
        {
            // Проверить, созданы ли семафоры & shared memory по этому ключу
            if ((*semid = semget (key, 4, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
            {
                if (errno != EEXIST)
                {
                    
                    perror ("SKIP Cannot get semaphores");
                    //* Не повезло */
                    continue;
                }

            }

            else
            {
                // Действительно не созданы

                uncreatedIndex = i;
                semctl (*semid, 0, IPC_RMID);
                *semid = -1;

                continue;
            }

            
        }

        *semid = semget (key, 4, 0666);

        if (*semid == -1)
        {
            /* Не существуют, но создавать их не требуется */
            continue;
        }

        if (isSemZero (*semid, (enum Semaphore) CORRECT))
        {
            /* Создавший семафоры процесс слишком быстро завершился, по факту ситуация эквивалентна той что выше */
            semctl (*semid, 0, IPC_RMID);
            continue;
        }
#endif

/*
1) сендеры и ресиверы за созданные семафоры & shared memory
2) в каждой паре сендер и ресивер за поля
3) где аларм

*/

