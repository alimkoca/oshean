#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include "include/linenoise.h"
#include "include/std.h"
#include "include/env.h"

extern char **environ;

int cmd_exec_oshean(char *input_cmd_oshean, char **args){
	char *cmd;
	char *env_val[100];
	pid_t pid_exec;
		
	set_env_var(env_val, environ);

	cmd = (char*)malloc(strlen(input_cmd_oshean)+9+1+sizeof(args)+1);

	// Memory check
	if (cmd == NULL){
		printf("NULL Memory Allocation\n");
	}

	if (!strcmp(input_cmd_oshean, "exit")){
		exit(0);
	}

	if (input_cmd_oshean < 0 || input_cmd_oshean == NULL){
		printf("NULL input\n");
		exit(1);
	}

	// Fork process
	pid_exec = fork();

	// Format string
	if (sprintf(cmd, "/usr/bin/%s", input_cmd_oshean) < 0){
		printf("What the hell is it?: %s\n", cmd);
		exit(1);
	}
	
	// If process has error
	if (pid_exec < 0){
		printf("Error can't fork()\n");
	}
	
	// Success
	else if (pid_exec == 0){
		if (execve(cmd, args, env_val) < 0){
			printf("%s: %s\n", strerror(errno), cmd);
			kill(getpid(), SIGKILL);
		}
	}

	// Parent process
	else {
		free(cmd);
		wait(NULL);
	}	
		return 0;
}
