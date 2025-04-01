#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../include/command.h"
#include "../include/builtin.h"

// ======================= requirement 2.3 =======================
/**
 * @brief 
 * Redirect command's stdin and stdout to the specified file descriptor
 * If you want to implement ( < , > ), use "in_file" and "out_file" included the cmd_node structure
 * If you want to implement ( | ), use "in" and "out" included the cmd_node structure.
 *
 * @param p cmd_node structure
 * 
 */
void redirection(struct cmd_node *p){
    // printf("********************************");
    // 檢查有沒有指定重定向輸入 
    if (p->in_file) {
        // 用唯讀模式開啟指定的檔案，並返回一個檔案描述符
        int in_fd = open(p->in_file, O_RDONLY);
        if (in_fd == -1) {
            perror("open in_file failed");
            return;
        }
        // 重定向 STDIN 到  in_fd
        dup2(in_fd, STDIN_FILENO);
        // 關閉 in_fd
        close(in_fd);
    }
    
    // 檢查有沒有指定重定向輸出 
    if (p->out_file) {
        // 用唯寫模式 開啟或創建指定的檔案 並返回一個檔案描述符
        int out_fd = open(p->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd == -1) {
            perror("open out_file failed");
            return;
        }
        // 重定向 STDOUT 到  out_fd
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }
	
}
// ===============================================================

// ======================= requirement 2.2 =======================
/**
 * @brief 
 * Execute external command
 * The external command is mainly divided into the following two steps:
 * 1. Call "fork()" to create child process
 * 2. Call "execvp()" to execute the corresponding executable file
 * @param p cmd_node structure
 * @return int 
 * Return execution status
 */

int spawn_proc(struct cmd_node *p)
{
    pid_t pid = fork();
    
    // 檢查 fork 是否成功 
    if (pid < 0) { 
        perror("fork failed");
        return -1;
    }
    
    // pid==0 代表是子進程
    else if (pid == 0) { 
    
        // 在子進程中執行外部指令
        if (execvp(p->args[0], p->args) == -1) {
            perror("execvp failed");
            exit(EXIT_FAILURE); // 如果 execvp 失敗，退出子進程
        }
    }
    
    // pid>0 代表是父進程 
    else { 
        // 等待子進程完成的 singal 
        int status;
        waitpid(pid, &status, 0);

        // WEXITSTATUS(status) 會返回子進程的返回值，正常結束的話是 0 
        //return WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        // 但是回傳0會跟exit的status相同，所以要改成回傳1 
        return WIFEXITED(status) ? 1 : -1;
    }
    
  	return 1;
}

// ===============================================================


// ======================= requirement 2.4 =======================
/**
 * @brief 
 * Use "pipe()" to create a communication bridge between processes
 * Call "spawn_proc()" in order according to the number of cmd_node
 * @param cmd Command structure  
 * @return int
 * Return execution status 
 */
int fork_cmd_node(struct cmd *cmd) 
{
    int pipefd[2];   // 用來儲存 pipe 的文件描述符
    int in_fd = 0;   // 用來追蹤標準輸入的文件描述符，初始化為 0 (標準輸入)
    struct cmd_node *current = cmd->head;
    pid_t pid;

    while (current != NULL) 
    {
        // 建立管道
        if (current->next != NULL) 
        {  
            // 只有在有下一個命令時才建立管道 (每一組指令間的 pipe是獨立的) 
            if (pipe(pipefd) == -1) 
            {
                perror("pipe failed");
                return -1;
            }
        }

        // 創建子進程
        pid = fork();
        if (pid == -1) 
        {
            perror("fork failed");
            return -1;
        } 
        else if (pid == 0) 
        {
            // 子進程

            // 如果是第一個命令，檢查是否需要輸入重定向
            if (in_fd == 0) 
            {
                redirection(current); // 執行輸入重定向（如果有指定的 < 文件）
            }

            // 重定向標準輸入
            if (in_fd != 0) 
            {  
                // 如果不是第一個命令，in_fd 應該是上一個管道的輸出端(讀取端) 
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }

            // 重定向標準輸出
            if (current->next != NULL) 
            {  // 如果有下一個命令，重定向到 pipefd[1](當前管道的輸入端) 
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
            }
            else 
            {
                // 如果是最後一個命令，執行重導向
                redirection(current);
            }

            // 執行命令
            if (execvp(current->args[0], current->args) == -1) 
            {
                perror("execvp failed");
                exit(EXIT_FAILURE);
            }
        } 
        else 
        {
            // 父進程
            // 關閉不需要的文件描述符
            if (in_fd != 0) close(in_fd);  // 關閉先前的讀端
            close(pipefd[1]);              // 關閉當前的寫入端，保留讀取端給下一個命令使用
            in_fd = pipefd[0];             // 更新 in_fd 為當前的讀取端 

            // 不等待子進程馬上完成，因為有多個子進程
        }

        // 移動到下一個命令
        current = current->next;
    }

    // 等待所有子進程完成
    while (wait(NULL) > 0);

    return 1;
}
// ===============================================================


void shell()
{
	while (1) {
		printf(">>> $ ");
		char *buffer = read_line();
		if (buffer == NULL)
			continue;

		/////////////////// ADD THIS LINE ///////////////
		// 直接在讀取輸入後檢查是否為 "exit"
        // if (strcmp(buffer, "exit") == 0) {
        //     free(buffer);
        //     break;  // 結束 shell
        // }

		struct cmd *cmd = split_line(buffer);
		
		int status = -1;
		// only a single command
		struct cmd_node *temp = cmd->head;
		
		if(temp->next == NULL){
			status = searchBuiltInCommand(temp);
            
			if (status != -1){
				int in = dup(STDIN_FILENO), out = dup(STDOUT_FILENO);
				if( in == -1 || out == -1)
					perror("dup");

                //printf("********************************\n");
				redirection(temp);
				status = execBuiltInCommand(status,temp);

				// recover shell stdin and stdout
				if (temp->in_file)  dup2(in, 0);
				if (temp->out_file){
					dup2(out, 1);
				}
				close(in);
				close(out);
			}
			else{
				//external command
                // modify here
                int in = dup(STDIN_FILENO), out = dup(STDOUT_FILENO);
                if( in == -1 || out == -1)
                    perror("dup");
                redirection(temp);
				status = spawn_proc(cmd->head);
                if (temp->in_file)  dup2(in, 0);
                if (temp->out_file){
                    dup2(out, 1);
			    }
			}
		}
		// There are multiple commands ( | )
		else{

			status = fork_cmd_node(cmd);
		}
		// free space
		while (cmd->head) {
			
			struct cmd_node *temp = cmd->head;
      		cmd->head = cmd->head->next;
			free(temp->args);
   	    	free(temp);
   		}
		free(cmd);
		free(buffer);
		
		if (status == 0)  
			break;
	}
}
