#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <curses.h>
#include <string.h>
#include "include/sh.h"
#include "include/cmd.h"
#include "include/sys.h"
#include "include/std.h"
#include "include/linenoise.h"

int spawn_oshean(){
	// Variables needed by program
	size_t size = 0;
	ssize_t chars;
	char *user, *hostname;
	char *input_cmd_oshean_bf_tr;
	char *prompt;
	int n;

	prompt = malloc(40);

	// User check, if gets error -> exit
	if ((user = oshean_get_user()) == 0){
		printf("Exiting due to user %s\n", user);
		exit(1);
	}
	// Hostname check like over user check
	if ((hostname = oshean_get_hostname()) == 0){
		printf("Exiting due to hostname %s\n", hostname);
		exit(1);
	}

	if (sprintf(prompt, "<\033[0;34m%s@%s\033[0;37m> ", user, hostname)  < 0){
		printf("Exiting due to sprintf\n");
		exit(1);
	}

	while((input_cmd_oshean_bf_tr = linenoise(prompt)) != NULL){
		char *input_cmd_oshean = osh_trim(input_cmd_oshean_bf_tr);
		
		if (!strcmp(input_cmd_oshean, ""))
			continue;

		// shell username and hostname print and get input with fgets in down,
		// Check errors and execute command
		if((n = cmd_exec_oshean(input_cmd_oshean)) != 0){
			printf("RET: %d\n", n);
		}

		linenoiseFree(input_cmd_oshean_bf_tr);
	}
}
