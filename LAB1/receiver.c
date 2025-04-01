#include "receiver.h"
#define CYAN "\033[36m"
#define RED  "\033[31m"
#define RESET "\033[0m"
struct sembuf sb = {0, -1, 0};
void receive(message_t* message_ptr, mailbox_t* mailbox_ptr)
{
    if(mailbox_ptr->flag == 1)
    {
        // 從 message queue 接收訊息 
        msgrcv(mailbox_ptr->storage.msqid, message_ptr, sizeof(*message_ptr), 1, 0);
    }
    else if(mailbox_ptr->flag == 2)
    {
        // 從 shared memory 複製訊息內容 
        strcpy(message_ptr->data, mailbox_ptr->storage.shm_addr);
    }
    /*  TODO: 
        1. Use flag to determine the communication method
        2. According to the communication method, receive the message
    */
}

int main(int argc, char* argv[])
{
    int mechanism = atoi(argv[1]);
    mailbox_t mailbox;
    mailbox.flag = mechanism;
    key_t key = ftok("progfile", 65);
    
    if(mechanism == 1)
    {    
        //int shmid = shmget(key, MAX, 0666 | IPC_CREAT);
        mailbox.storage.msqid = msgget(key, 0666 | IPC_CREAT);
    }

    else if(mechanism == 2)
    {
        mailbox.storage.shm_addr = shmat(shmget(key, MAX, 0666 | IPC_CREAT), NULL, 0);
    }

    else
    {
        printf("Invalid mechanism\n");
        exit(1);
    }
    
    message_t message;
    struct timespec start, end;
    double time_spent = 0.0;
     
    int semid = semget(key, 2, 0666 | IPC_CREAT);
    /* 
    sb.sem_num = 0;
    sb.sem_op = 1;
    semop(semid, &sb, 1);
    sb.sem_num = 1;
    sb.sem_op = -1;
    semop(semid, &sb, 1);
    */
    printf("\nMessage Passing \n\n");
    //sleep(1.5);

    while(1)
    {
        // sb.sem_num = 0;
        // sb.sem_op = 1;
        // semop(semid, &sb, 1);
        // sb.sem_num = 1;
        // sb.sem_op = -1;
        // semop(semid, &sb, 1);
        
        clock_gettime(CLOCK_MONOTONIC, &start);
        receive(&message, &mailbox);
        clock_gettime(CLOCK_MONOTONIC, &end);
        time_spent += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        if (strcmp(message.data, "EOF") == 0)
        {
            printf(RED"\nSender exit!\n\n");
            break;
        }

        printf(CYAN"Received message:" RESET"%s\n", message.data);
        usleep(250000);

        sb.sem_num = 0;
        sb.sem_op = 1;
        semop(semid, &sb, 1);
        sb.sem_num = 1;
        sb.sem_op = -1;
        semop(semid, &sb, 1);
    }
    printf(RESET"Total time taken in receiving msg: %.9f s\n", time_spent);
    return 0;

    /*  TODO: 
        1) Call receive(&message, &mailbox) according to the flow in slide 4
        2) Measure the total receiving time
        3) Get the mechanism from command line arguments
            ‧ e.g. ./receiver 1
        4) Print information on the console according to the output format
        5) If the exit message is received, print the total receiving time and terminate the receiver.c
    */
}