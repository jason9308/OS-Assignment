#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <semaphore.h>
#include <time.h>
#define MAX 1025
typedef struct 
{
    int flag;      // 1 for message passing, 2 for shared memory
    union
    {
        int msqid; //for system V api. You can replace it with struecture for POSIX api
        char* shm_addr;
    }storage;
} mailbox_t;


typedef struct 
{
    long mtype;       // 訊息類型 
    char data[MAX];  // 訊息內容
    /*  TODO: 
        Message structure for wrapper
    */
} message_t;


union semun 
{
    int val;                // Value for SETVAL
    struct semid_ds *buf;   // Buffer for IPC_STAT, IPC_SET
    unsigned short *array;  // Array for GETALL, SETALL
    struct seminfo *__buf;  // Buffer for IPC_INFO (Linux-specific)
};

void send(message_t message, mailbox_t* mailbox_ptr);