#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include "include/std.h"

int cmd_exec_oshean(char *input_cmd_oshean, char **args){
	char *cmd;
	pid_t pid_exec;
	
	// Arguments for executing
	int i = 1;

	cmd = (char*)malloc(strlen(input_cmd_oshean)+9+1+i+1);

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
		if (execve(cmd, args, NULL) < 0){	
			printf("I'm in here cmd.c:68\n");
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
