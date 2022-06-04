#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *ltrim(char *s);
char *rtrim(char *s);
char *osh_trim(char *s);
char *osh_set_args(char **arg, char *cmd);
