#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <curses.h>
#include <string.h>
#include "include/sh.h"
#include "include/cmd.h"
#include "include/sys.h"

void int_handler(){
	printf("Exiting due to CTRL-C\n");
	exit(0);
}

int spawn_oshean(){
	// CTRL-C Interrupt for exit
	signal(SIGINT, int_handler);

	// Variables needed by program
	size_t size = 0;
	ssize_t chars;
	char *user, *hostname;
	char *input_cmd_oshean = NULL;
	int n;

	// User check, if gets error -> exit
	if((user = oshean_get_user()) == 0){
		printf("Exiting due to user %s\n", user);
		exit(1);
	}
	// Hostname check like over user check
	if((hostname = oshean_get_hostname()) == 0){
		printf("Exiting due to hostname %s\n", hostname);
		exit(1);
	}
	for(;;){
		// shell username and hostname print and get input with fgets in down,
                printf("%s@%s$ ", user, hostname);

		if((chars = getline(&input_cmd_oshean, &size, stdin)) == -1){
			break;
		}
		
		if(!strcmp(input_cmd_oshean, "\n")){
			continue;
		}

		if(input_cmd_oshean[chars - 1] == '\n'){
			input_cmd_oshean[chars - 1] = '\0';
		}

		// Check errors and execute command
		if((n = cmd_exec_oshean(input_cmd_oshean)) != 0){
			printf("RET: %d\n", n);
		}

	}
}
