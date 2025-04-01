#include "sender.h"
#define CYAN "\033[36m"
#define RED  "\033[31m"
#define RESET "\033[0m"
struct sembuf sb = {0, -1, 0}; // 用來存 信號量的編號、操作、標誌 

void send(message_t message, mailbox_t* mailbox_ptr)
{
    if(mailbox_ptr->flag == 1)
    {
        // 發送訊息的函式 (queue ID, 訊息, 大小, 操作標誌) 
        msgsnd(mailbox_ptr->storage.msqid, &message, sizeof(message.data), 0);
    }
    else if(mailbox_ptr->flag == 2)
    {
        // 將訊息複製到共享記憶體 
        strcpy(mailbox_ptr->storage.shm_addr, message.data);
    }
    /*  TODO: 
        1. Use flag to determine the communication method
        2. According to the communication method, send the message
    */
}

int main(int argc, char* argv[])
{
    int mechanism = atoi(argv[1]); // 讀取命令列參數 (1 or 2) 
    char *input_file = argv[2]; // 讀取命令列參數(檔案名稱)
    FILE *fp = fopen(input_file, "r");
    mailbox_t mailbox;
    mailbox.flag = mechanism;
    
    // 生成 key，作為識別的符號 
    key_t key = ftok("progfile", 65);

    if(mechanism == 1)
    {    
        //int shmid = shmget(key, MAX, 0666 | IPC_CREAT);
        
        // 創建一個 message queue，並使用 key來識別  (msggt返回 id)
        mailbox.storage.msqid = msgget(key, 0666 | IPC_CREAT);
    }

    else if(mechanism == 2)
    {
        // 創建共享記憶體(shmget), 並附加到當前進程(shmat) 
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
    
    // 取得一組信號量(有兩個)，並將 id存在 semid 
    int semid = semget(key, 2, 0666 | IPC_CREAT);

    union semun sem_union; // 用來設置信號 
    sem_union.val = 0;
    if (semctl(semid, 0, SETVAL, sem_union) == -1) // 初始化信號 0為 0 
    {
        perror("semctl failed");
        exit(1);
    }
    
    sem_union.val = 0;
    if (semctl(semid, 1, SETVAL, sem_union) == -1) // 初始化信號 1為 0
    {
        perror("semctl failed");
        exit(1);
    }


    // sb.sem_num = 0;
    // sb.sem_op = -1;
    // semop(semid, &sb, 1);
    printf("\nMessage Passing \n\n");
    //sleep(1.5);
//    sb.sem_num = 1;
//    sb.sem_op = 1;
//    semop(semid, &sb, 1);
    

    while (fgets(message.data, MAX, fp) != NULL) 
    {
        // 計時並傳遞訊息
        message.mtype = 1;
        clock_gettime(CLOCK_MONOTONIC, &start);
        send(message, &mailbox);
        clock_gettime(CLOCK_MONOTONIC, &end);
        time_spent += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        printf(CYAN"Sending message:" RESET"%s\n", message.data);
        
        // 啟用 receiver(V操作)
        // 意思是釋放可用資源，所以 SIGNAL1 會加 1 
        sb.sem_num = 1;
        sb.sem_op = 1;
        semop(semid, &sb, 1);
        
        // 凍結 sender(P操作) 
        // 意思是要等待有可用資源，所以要等到 SIGNAL0大於 0才會啟動 
        // 並且會將 SIGNAL0 減 1 
        sb.sem_num = 0;
        sb.sem_op = -1;
        semop(semid, &sb, 1);
    }

    strcpy(message.data, "EOF");
    send(message, &mailbox);
    sb.sem_num = 1;
    sb.sem_op = 1;
    semop(semid, &sb, 1);
    
    printf(RED"\nEnd of input file! exit\n\n");
    printf(RESET"Total sending time: %.9f seconds\n", time_spent);
    fclose(fp);

    if (mechanism == 2) 
    {
        // 解除共享記憶體連接
        shmdt(mailbox.storage.shm_addr);
    }

    return 0;


    /*  TODO: 
        1) Call send(message, &mailbox) according to the flow in slide 4
        2) Measure the total sending time
        3) Get the mechanism and the input file from command line arguments
            ‧ e.g. ./sender 1 input.txt
                    (1 for Message Passing, 2 for Shared Memory)
        4) Get the messages to be sent from the input file
        5) Print information on the console according to the output format
        6) If the message form the input file is EOF, send an exit message to the receiver.c
        7) Print the total sending time and terminate the sender.c
    */
}