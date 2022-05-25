#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

int cmd_exec_oshean(char *input_cmd_oshean){
	char *cmd;
	pid_t pid_exec;

	cmd = (char*)malloc(strlen(input_cmd_oshean)+9+1);

	// Memory check
	if(cmd == NULL){
		printf("NULL Memory Allocation\n");
	}
	
	// Arguments for executing
	int i = 1;
	char *args[8];
	args[0] = strtok(input_cmd_oshean, " ");
	
	while((args[i] = strtok(NULL, " "))){
		i++;
	}

	args[i+1] = NULL;
	
	if(!strcmp(input_cmd_oshean, "exit")){
		printf("Exit: 0\n");
		exit(0);
	}
	
	if(input_cmd_oshean < 0 || input_cmd_oshean == NULL){
		printf("NULL input\n");
		exit(1);
	}

	// Fork process
	pid_exec = fork();

	// Format string
	if(sprintf(cmd, "/usr/bin/%s", input_cmd_oshean) < 0){
		printf("What the hell is it?: %s\n", cmd);
		exit(1);
	}
	
	// If process has error
	if(pid_exec < 0){
		printf("Error can't fork()\n");
	}
	
	// Success
	else if (pid_exec == 0){
		if(execve(cmd, args, NULL) < 0){
			printf("What the hell is it?: %s\n", cmd);
			printf("Command not found or something happened, errn %d, %s\n", errno, strerror(errno));
		}
	}

	// Parent process
	else {
		wait(NULL);
	}	
		return 0;
}
