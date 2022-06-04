#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <curses.h>
#include <string.h>
#include <ctype.h>
#include "include/sh.h"
#include "include/cmd.h"
#include "include/sys.h"
#include "include/std.h"
#include "include/linenoise.h"

char *hints(const char *buff, int *color, int *bold){
	*color = 2;
	*bold = 0;

	if (!strcmp(buff, "l")){
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
	char *args[80];

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

	linenoiseSetMultiLine(1);
	linenoiseSetHintsCallback(hints);
	linenoiseSetCompletionCallback(completion);

	// Prompt input
	while((input_cmd_oshean_bf_tr = linenoise(prompt)) != NULL){
		// Space check again after trim 
		if (!strcmp(input_cmd_oshean_bf_tr, ""))
			continue;
		
		// Trim the string and return address
		char *input_cmd_oshean = osh_trim(input_cmd_oshean_bf_tr);

		osh_set_args(args, input_cmd_oshean);

		// Space check again after trim
		if (!strcmp(input_cmd_oshean, ""))
			continue;

		if (!strcmp(args[0], "cd")){
			if (chdir(args[1]) < 0){
				printf("%s\n", strerror(errno));
			}
			continue;
		}
		
		linenoiseHistoryAdd(input_cmd_oshean);

		if (!strcmp(input_cmd_oshean, "Hello")){
                	printf("Hello, hello? Uh, I wanted to record a message for you to help you get settled "
                	"in your tutorial. Um, I actually developer of oshean. "
                        "I'm finishing up my last commits now, as a matter of fact. "
                        "So, I know it can be a bit weird, "
                        "but I'm here to tell you there's nothing to worry about usage. "
                        "Uh, you'll do fine. "
                        "So, let's just focus on getting you through commands. Okay?\n");
			
			continue;
		}

		if (!strcmp(input_cmd_oshean, "clear")){
			linenoiseClearScreen();
			continue;
		}
		
		// Check errors and execute command
		if ((n = cmd_exec_oshean(input_cmd_oshean, args)) != 0){
			printf("RET: %d\n", n);
		}

		free(input_cmd_oshean_bf_tr);
	}

	free(prompt);
	free(user);
	free(hostname);
	free(input_cmd_oshean_bf_tr);
}
