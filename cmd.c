#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_exec_oshean(char *input_cmd_oshean){
	if(!strcmp(input_cmd_oshean, "echo")){
		printf("Echo test\n");
	}
	if(!strcmp(input_cmd_oshean, "clear")){
		printf("\e[1;1H\e[2J");
	}
	if(!strcmp(input_cmd_oshean, "exit")){
		printf("Exit: 0\n");
		exit(0);
	}
	if(input_cmd_oshean < 0 || input_cmd_oshean == NULL){
		printf("NULL input\n");
		exit(1);
	}	
	return 0;
}
