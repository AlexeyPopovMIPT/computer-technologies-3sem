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


#ifdef USE_UNDO
#define SEM_UNDO_ SEM_UNDO
#else
#define SEM_UNDO_ 0
#endif


#ifdef ENABLE_LOGGING
#define LOG(v1, ...) { fprintf (stderr, "%d: ", getpid ()); fprintf (stderr, v1, __VA_ARGS__); }
#else
#define LOG(...) ;
#endif


#define ASSERTED(action, message) if (!(action)) { fprintf (stderr, "%d: ", getpid ()); perror (message); exitcode = -1; goto cleanup; }


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


int P (int semid, int sem_num);
int V (int semid, int sem_num);

int sendProcess (const char *path);
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
        return receiveProcess (getpid());

    else
    {
        printf ("%s: Unknown option: %s\n", argv[0], argv[1]);
        printf ("Usage: %s [-open <FILE>] or [-write]\n", argv[0]);
        return 0;
    }
}

#ifndef ENABLE_LOGGING
void printSemaphoreValues (int semid)
{
    /*fprintf (stderr, "%d: ", semid);
    struct sembuf oper = { };
    oper.sem_flg = IPC_NOWAIT;
    oper.sem_num = (enum Semaphore) META;
    oper.sem_op = (enum Semop) 0;
    fprintf (stderr, "META=%d, ", semop (semid, &oper, 1) == -1 ? 1 : 0);

    oper.sem_num = (enum Semaphore) NEED_READ;
    fprintf (stderr, "NEED_READ=%d, ", semop (semid, &oper, 1) == -1 ? 1 : 0);

    oper.sem_num = (enum Semaphore) NEED_WRITE;
    fprintf (stderr, "NEED_WRITE=%d\n", semop (semid, &oper, 1) == -1 ? 1 : 0);*/
}
#else
void printSemaphoreValues (int semid) {}
#endif


int P (int semid, int sem_num)
{
    struct sembuf oper = { };
    oper.sem_flg = SEM_UNDO_;
    oper.sem_num = sem_num;
    oper.sem_op = -1;
    return semop (semid, &oper, 1);
}

int V (int semid, int sem_num)
{
    struct sembuf oper = { };
    oper.sem_flg = SEM_UNDO_;
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
    if ((semGetIpc = semget (ftok (KEYPATH, 0), 1, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
    {
        // Оптимистичный вариант - семафоры уже созданы

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

        P (semGetIpc, 0);
        
        // TODO: выход из крит. секции в конце функции
    }

    else
    {
        // Семафоры только что созданы, вход в к.с. оформлен
        ;
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

                // созданы
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


        // Проверить, требуется ли sender/receiver

        *shmid = shmget (key, SHMSIZE, 0666);
        if (*shmid == -1)
        {
            /* Перестала существовать shared memory - вероятно, sender и receiver оба были,
             * но завершились. Не повезло
             * Ищем другое i
             */
            *semid = -1;
            LOG ("i=%d, SKIP Shared memory disappeared: ", i)
            #ifdef ENABLE_LOGGING
            perror ("");
            #endif
            continue;
        }

        P (*semid, META);

        struct SharedSegment *shmem = shmat (*shmid, NULL, 0);

        if ((iAmSender ? shmem->senderSelected : shmem->receiverSelected) == 0)
        {
            if (iAmSender) shmem->senderSelected = 1;
            else         shmem->receiverSelected = 1;

            LOG ("Line %d: ", __LINE__)
            printSemaphoreValues (*semid);
            V (*semid, META);

            LOG ("Got existed semaphores and shared memory no %d\n", i);

            goto cleanup;
        }

        else
        {
            LOG ("Line %d: ", __LINE__)
            printSemaphoreValues (*semid);
            V (*semid, META);

            *semid = -1;
            *shmid = -1;

            LOG ("i=%d, SKIP Resource already busy\n", i)

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

    V (*semid, NEED_WRITE);
    V (*semid, META);

    cleanup:

        V (semGetIpc, 0);
        return exitcode;

    #if 0
    for (int i = 1; i < 256; i++)
    {
        assert (*shmid == -1);
        assert (*semid == -1);

        /* Попытаться создать _новый_ сегмент разделяемой памяти.
         * Если получилось, инициализировать его поля. 
         * Если нет, значит поля уже кто-то проинициализировал;
         * проверить, требуется ли sender
         */

        key = ftok (KEYPATH, i);


        *semid = semget (key, 3, 0666 | IPC_CREAT | IPC_EXCL);
        if (*semid == -1)
        {
            if (errno != EEXIST)
            {
                LOG ("i=%d, ", i)
                perror ("SKIP Cannot get semaphores");
                /* Не повезло */
                continue;
            }
                

            /* Семафоры с ключом по i существовали на момент вызова semget */
            
            *semid = semget (key, 3, 0666);

            if (*semid == -1)
            {
                /* Вероятно, они перестали существовать - процессы, использовавшие их,
                 * завершились. Ищем другое i */
                LOG ("i=%d, SKIP Semaphores disappeared\n", i)
                continue;
            }

            *shmid = shmget (key, SHMSIZE, 0666);
            if (*shmid == -1)
            {
                /* Перестала существовать shared memory - также процессы завершились.
                 * Ищем другое i
                 */
                *semid = -1;
                LOG ("i=%d, SKIP Shared memory disappeared", i)
                perror ("");
                continue;
            }

            /* Получили семафоры и сегмент разделяемой памяти. Проверим, заняты ли они другим
             * sender-ом
             */

            struct sembuf oper = { };
            oper.sem_flg = SEM_UNDO_;
            oper.sem_num = (enum Semaphore) META;
            oper.sem_op = (enum Semop) ENTRY; /*-1*/


            semop (*semid, &oper, 1);

            struct SharedSegment *shmem = shmat (*shmid, NULL, 0);

            if ((iAmSender ? shmem->senderSelected : shmem->receiverSelected) == 0)
            {
                if (iAmSender) shmem->senderSelected = 1;
                else         shmem->receiverSelected = 1;

                oper.sem_op = (enum Semop) EXIT; /*+1*/
                LOG ("Line %d: ", __LINE__)
                printSemaphoreValues (*semid);
                semop (*semid, &oper, 1);
                LOG ("Got existed semaphores and shared memory no %d\n", i);
                return 0;
            }

            else
            {
                //shmctl (*shmid, IPC_RMID, NULL);
                oper.sem_op = (enum Semop) EXIT; /*+1*/
                LOG ("Line %d: ", __LINE__)
                printSemaphoreValues (*semid);
                semop (*semid, &oper, 1);
                *semid = -1;
                *shmid = -1;
                LOG ("i=%d, SKIP Resource already busy\n", i)
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

                //semctl (*semid, 0, IPC_RMID);
                *semid = -1;
                return -1;
            }

            struct SharedSegment *shmem = shmat (*shmid, NULL, 0);

            shmem->senderSelected = iAmSender;
            shmem->receiverSelected = !iAmSender;
            shmem->bytesCount = 0;
            
            struct sembuf oper [2] = {{ }, { }};
            oper[0].sem_flg = SEM_UNDO_;
            oper[0].sem_num = (enum Semaphore) NEED_WRITE;
            oper[0].sem_op = (enum Semop) EXIT; /*+1*/
            oper[1].sem_flg = SEM_UNDO_;
            oper[1].sem_num = (enum Semaphore) META;
            oper[1].sem_op = (enum Semop) EXIT; /*+1*/


            LOG ("Line %d: ", __LINE__)
            printSemaphoreValues (*semid);
            semop (*semid, oper, 2);
            LOG ("Line %d: ", __LINE__)
            printSemaphoreValues (*semid);


            LOG ("Got new semaphores and shared memory no %d\n", i);
            return 0;

        }

        abort ();

    }

    return -1;

    #endif
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
    oper.sem_flg = SEM_UNDO_;

    while (1)
    {
        oper.sem_num = (enum Semaphore) NEED_WRITE;
        oper.sem_op = (enum Semop) ENTRY; /*-1*/
        LOG ("Line %d: ", __LINE__)
        printSemaphoreValues (semid);
        semop (semid, &oper, 1);
        LOG ("Line %d: ", __LINE__)
        printSemaphoreValues (semid);

        if (shmem->bytesCount != 0)
        {
            fprintf (stderr, "%d: ", getpid ());
            fprintf (stderr, "Logical error: expected bytesCount=0, got %d\n", shmem->bytesCount);
            exitcode = -1;
            oper.sem_num = (enum Semaphore) NEED_READ;
            LOG ("Line %d: ", __LINE__)
            printSemaphoreValues (semid);
            oper.sem_op = (enum Semop) EXIT; /*+1*/
            semop (semid, &oper, 1);
            goto cleanup;
        }

        if ((bytesRead = read (fd_src, shmem->buffer, SHMBUFSIZE)) == -1)
        {
            fprintf (stderr, "%d: ", getpid ());
            perror ("Cannot read from file");
            exitcode = -1;
            oper.sem_num = (enum Semaphore) NEED_READ;
            oper.sem_op = (enum Semop) EXIT; /*+1*/
            LOG ("Line %d: ", __LINE__)
            printSemaphoreValues (semid);
            semop (semid, &oper, 1);
            goto cleanup;
        }

        shmem->bytesCount = bytesRead;

        oper.sem_num = (enum Semaphore) NEED_READ;
        LOG ("Line %d: ", __LINE__)
        printSemaphoreValues (semid);
        oper.sem_op = (enum Semop) EXIT; /*+1*/
        semop (semid, &oper, 1);

        if (bytesRead == 0) break;

    }

    cleanup:
        LOG ("%s", "Cleaning up\n")
        if (shmid != -1) shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1) 
        {
            semctl (semid, 0, IPC_RMID);
            semctl (semid, 1, IPC_RMID);
            semctl (semid, 2, IPC_RMID);
        }
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
    oper.sem_flg = SEM_UNDO_;


    while (1)
    {
        oper.sem_num = (enum Semaphore) NEED_READ;
        oper.sem_op = (enum Semop) ENTRY; /*-1*/
        LOG ("Line %d: ", __LINE__)
        printSemaphoreValues (semid);
        semop (semid, &oper, 1);
        LOG ("Line %d: ", __LINE__)
        printSemaphoreValues (semid);

        if (shmem->bytesCount == 0)
        {
            oper.sem_num = (enum Semaphore) NEED_WRITE;
            LOG ("Line %d: ", __LINE__)
            printSemaphoreValues (semid);
            oper.sem_op = (enum Semop) EXIT; /*+1*/
            semop (semid, &oper, 1);
            goto cleanup;
        }

        if (write (1, shmem->buffer, shmem->bytesCount) != shmem->bytesCount)
        {
            fprintf (stderr, "%d: ", getpid ());
            perror ("Cannot write data to stdout");
            exitcode = -1;
            oper.sem_num = (enum Semaphore) NEED_WRITE;
            LOG ("Line %d: ", __LINE__)
            printSemaphoreValues (semid);
            oper.sem_op = (enum Semop) EXIT; /*+1*/
            semop (semid, &oper, 1);
            goto cleanup;

        }

        shmem->bytesCount = 0;

        oper.sem_num = (enum Semaphore) NEED_WRITE;
        LOG ("Line %d: ", __LINE__)
        printSemaphoreValues (semid);
        oper.sem_op = (enum Semop) EXIT; /*+1*/
        semop (semid, &oper, 1);
    }


    cleanup:
        LOG ("%s\n", "Cleaning up")
        if (shmid != -1) shmctl (shmid, IPC_RMID, NULL);
        if (semid != -1) 
        {
            semctl (semid, 0, IPC_RMID);
            semctl (semid, 1, IPC_RMID);
            semctl (semid, 2, IPC_RMID);
        }

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

int stressTest (const char *path)
{
    fork(); fork(); fork(); fork();
    fork(); fork(); fork(); fork();
    sendProcess (path);
}

#endif

