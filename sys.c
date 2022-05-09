#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>
#include <limits.h>
#include "include/sys.h"

char *oshean_get_user(){
	struct passwd *pw;
	uid_t uid;
	uid = geteuid();
	pw = getpwuid(uid);
	
	if(!pw)
		return NULL;

	return pw->pw_name;
}

char *oshean_get_hostname(){
	char *buff;
	buff = malloc(HOST_NAME_MAX+1);
	gethostname(buff, HOST_NAME_MAX+1);
	return buff;
}
