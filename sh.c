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

char *hints(const char *buff, int *color, int *bold){
	if (!strcmp(buff, "l")){
		*color = 2;
		*bold = 0;
		return "s";
	}
	return NULL;
}

void completion(const char *buff, linenoiseCompletions *lc){
	if (!strcmp(&buff[0], "l")){
		linenoiseAddCompletion(lc, "ls");
	}
}

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

	// Prompt formatting
	if (sprintf(prompt, "<\033[0;34m%s@%s\033[0;37m> ", user, hostname)  < 0){
		printf("Exiting due to sprintf\n");
		exit(1);
	}

	linenoiseSetHintsCallback(hints);
	linenoiseSetCompletionCallback(completion);

	// Prompt input
	while((input_cmd_oshean_bf_tr = linenoise(prompt)) != NULL){
		// Trim the string and return address
		char *input_cmd_oshean = osh_trim(input_cmd_oshean_bf_tr);
	
		linenoiseHistoryAdd(input_cmd_oshean);

		// Space check	
		if (!strcmp(input_cmd_oshean, ""))
			continue;

		// Check errors and execute command
		if((n = cmd_exec_oshean(input_cmd_oshean)) != 0){
			printf("RET: %d\n", n);
		}

		free(input_cmd_oshean_bf_tr);
	}
}
