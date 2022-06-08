#define UTF8

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
#include "include/utf8.h"

// hints and completions
char *hints(const char *buff, int *color, int *bold){
	*color = 2;
	*bold = 0;

	if (!strcmp(buff, "l")){
		return "s";
	}

	if (!strcmp(buff, "p")){
		return "wd";
	}

        if (!strcmp(buff, "c")){
		return "d";
	}

	if (!strcmp(buff, "v")){
		return "im";
	}

	if (!strcmp(buff, "na")){
		return "no";
	}
				
	return NULL;
}

void completion(const char *buff, linenoiseCompletions *lc){
	if (!strcmp(&buff[0], "l")){
		linenoiseAddCompletion(lc, "ls");
	}

	if (!strcmp(&buff[0], "p")){
		linenoiseAddCompletion(lc, "pwd");
	}

	if (!strcmp(&buff[0], "c")){
		linenoiseAddCompletion(lc, "cd");
	}

	if (!strcmp(&buff[0], "v")){
		linenoiseAddCompletion(lc, "vim");
	}

	if (!strcmp(buff, "na")){
		linenoiseAddCompletion(lc, "nano");
	}
}

int spawn_oshean(){
	// Size equals 0
	size_t size = 0;
	// Character number
	ssize_t chars;
	// User and hostname
	char *user, *hostname;
	// Input before space trimming
	char *input_cmd_oshean_bf_tr;
	// Prompt value
	char *prompt;
	// Home path
	char *home_p;
	// Regular n value used in loop
	int n;
	// Command line arguments
	char *args[80];

	// memory allocation
	prompt = (char*)malloc(40);
	home_p = (char*)malloc(40);

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
		printf("Exiting due to sprintf, sh.c:88\n");
		exit(1);
	}

	if (sprintf(home_p, "/home/%s", user) < 0){
		printf("Exiting due to sprintf, sh.c:93\n");
		exit(1);
	}

	if (chdir(home_p) < 0){
		printf("%s\n", strerror(errno));
	}

	// linenoise settings
	linenoiseSetMultiLine(1);
	linenoiseSetHintsCallback(hints);
	linenoiseSetCompletionCallback(completion);
	linenoiseHistorySetMaxLen(100);

	// UTF-8 encoding functions
#ifdef UTF8
	linenoiseSetEncodingFunctions(
		linenoiseUtf8PrevCharLen,
		linenoiseUtf8NextCharLen,
		linenoiseUtf8ReadCode);
#endif

	// Prompt input
	for (;;){
		errno = 0;

		input_cmd_oshean_bf_tr = linenoise(prompt);

		if (!input_cmd_oshean_bf_tr){
			// Equalivent to CTRL-D
			if (errno != EAGAIN)
				break;
			// CTRL-C, NULL
			continue;
		}

		// Space check again after trim 
		if (!strcmp(input_cmd_oshean_bf_tr, ""))
			continue;

		// Trim the string and return address
		char *input_cmd_oshean = osh_trim(input_cmd_oshean_bf_tr);

		// Add command to history
		linenoiseHistoryAdd(input_cmd_oshean);

		// Set args[80] value
		osh_set_args(args, input_cmd_oshean);

		// Space check again after trim
		if (!strcmp(input_cmd_oshean, ""))
			continue;

		// cd builtin command
		if (!strcmp(args[0], "cd")){
			if (chdir(args[1]) < 0){
				printf("%s\n", strerror(errno));
			}
			continue;
		}

		// ZWFzdGVyIGVnZy4uLg==	
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

		// clear screen, basically
		if (!strcmp(input_cmd_oshean, "clear")){
			linenoiseClearScreen();
			continue;
		}
		
		// Check errors and execute command
		if ((n = cmd_exec_oshean(input_cmd_oshean, args)) != 0){
			printf("RET: %d\n", n);
		}

		// Free the memory
		free(input_cmd_oshean_bf_tr);
	}

	// avoid memory leaks
	free(prompt);
	free(home_p);
	free(user);
	free(hostname);
	free(input_cmd_oshean_bf_tr);
}
