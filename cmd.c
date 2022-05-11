#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

int cmd_exec_oshean(char *input_cmd_oshean){
	char *cmd;
	pid_t pid_exec;

	cmd = (char*)malloc(80);

	// Memory check
	if(cmd == NULL){
		printf("NULL Memory Allocation\n");
	}
	
	// NULL arguments necesarry for now, will update argv
	char* args[] = {cmd, NULL};
	
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
		printf("What the hell is it?\n");
		exit(1);
	}
	
	// If process has error
	if(pid_exec < 0){
		printf("Error can't fork()\n");
	}
	
	// Success
	else if (pid_exec == 0){
		printf("\n");
		if(execve(cmd, args, NULL) < 0){
			printf("Usage isn't confirmed %d\n", errno);
			kill(getpid(), SIGKILL);
		}
		kill(getpid(), SIGKILL);
	}

	// Parent process
	else {
		
	}	
		return 0;
}
