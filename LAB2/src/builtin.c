#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include "../include/builtin.h"

/**
 * @brief 
 * Determine whether cmd is a built-in command
 * @param cmd Command structure
 * @return int 
 * If command is built-in command return function number
 * If command is external command return -1 
 */
int searchBuiltInCommand(struct cmd_node *cmd)
{
	for (int i = 0; i < num_builtins(); ++i){
		if (strcmp(cmd->args[0], builtin_str[i]) == 0){
			return i;
		}
	}
	return -1;
}
/**
 * @brief Execute built-in command
 * 
 * @param status Choose which built-in command to execute
 * @param cmd Command structure
 * @return int 
 * Return execution status
 */
int execBuiltInCommand(int status,struct cmd_node *cmd){
	status = (*builtin_func[status])(cmd->args);
	return status;
}

int help(char **args)
{
	int i;
    printf("--------------------------------------------------\n");
  	printf("My Little Shell!!\n");
	printf("The following are built in:\n");
	for (i = 0; i < num_builtins(); i++) {
    	printf("%d: %s\n", i, builtin_str[i]);
  	}
    printf("--------------------------------------------------\n");
	return 1;
}
// ======================= requirement 2.1 =======================
int cd(char **args)
{
    // 如果沒有提供路徑，只有cd，返回 home 
    if (args[1] == NULL) {
        char *home = getenv("HOME");
        // 用chdir返回 home ，成功的話會回傳 0 
        if (home != NULL) {
            if (chdir(home) != 0) {
                perror("cd");
                return -1;
            }
        } 
        else {
            fprintf(stderr, "cd: HOME not set\n");
            return -1;
        }
    } 
    
    // 如果路徑以 ~ 開頭，替換成 home 
    else if (args[1][0] == '~') {
        // 取得 home 的路徑
        char *home = getenv("HOME");
        if (home != NULL) {
            // 組合新的路徑字串：home 路徑 + `args[1]` 去掉 `~` 的部分
            char new_path[1024];
            snprintf(new_path, sizeof(new_path), "%s%s", home, args[1] + 1);  // args[1] + 1 跳過 `~`
            
            // 切換到新路徑
            if (chdir(new_path) != 0) {
                perror("cd");
                return -1;
            }
        } 
        else {
            fprintf(stderr, "cd: HOME not set\n");
            return -1;
        }
    }
    // 切換到指定的路徑
    else {
        if (chdir(args[1]) != 0) {
            perror("cd");
            return -1;
        }
    }
    return 1;  // 成功執行 cd 返回 1
}
// ===============================================================

int pwd(char **args)
{
	char cwd[BUF_SIZE];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
    return 1;
}

int echo(char **args)
{
	bool newline = true;
	for (int i = 1; args[i]; ++i) {
		if (i == 1 && strcmp(args[i], "-n") == 0) {
			newline = false;
			continue;
		}
		printf("%s", args[i]);
		if (args[i + 1])
			printf(" ");
	}
	if (newline)
		printf("\n");

	return 1;
}

int exit_shell(char **args)
{
	return 0;
}

int record(char **args)
{
	if (history_count < MAX_RECORD_NUM) {
		for (int i = 0; i < history_count; ++i)
			printf("%2d: %s\n", i + 1, history[i]);
	} else {
		for (int i = history_count % MAX_RECORD_NUM; i < history_count % MAX_RECORD_NUM + MAX_RECORD_NUM; ++i)
			printf("%2d: %s\n", i - history_count % MAX_RECORD_NUM + 1, history[i % MAX_RECORD_NUM]);
	}
	return 1;
}

const char *builtin_str[] = {
 	"help",
 	"cd",
	"pwd",
	"echo",
 	"exit",
 	"record",
};

const int (*builtin_func[]) (char **) = {
	&help,
	&cd,
	&pwd,
	&echo,
	&exit_shell,
  	&record,
};

int num_builtins() {
	return sizeof(builtin_str) / sizeof(char *);
}
