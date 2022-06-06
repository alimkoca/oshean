#include <stdio.h>
#include <stdlib.h>
#include "include/env.h"

void set_env_var(char **env_val, char **envp){
	int i = 0;

	for (char **env = envp; *env != 0; env++){
		env_val[i] = *env;
		i++;
	}

	env_val[i+1] = 	NULL;
}
