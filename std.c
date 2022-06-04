#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "include/std.h"

char *ltrim(char *s){
	while(isspace(*s))
		s++;
	return s;
}

char *rtrim(char *s){
	char* back = s + strlen(s)+1;

	while(isspace(*--back))
		*(back+1) = '\0';

	return s;
}

char *osh_trim(char *s){
	return rtrim(ltrim(s));
}

char *osh_set_args(char **arg, char *s){
	int i = 1;
	
	arg[0] = strtok(s, " ");

	while (arg[i] = strtok(NULL, " ")){
		i++;
	}

	arg[i+1] = NULL;
}
