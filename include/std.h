#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

char *ltrim(char *s){
	while(isspace(*s)) 
		s++;
	return s;
}

char *rtrim(char *s){
	char* back = s + strlen(s);
	while(isspace(*--back));
		*(back+1) = '\0';
	return s;
}

char *osh_trim(char *s){
	return rtrim(ltrim(s)); 
}

