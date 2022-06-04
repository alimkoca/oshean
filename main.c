#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include "include/sh.h"

int main(){
	setlocale(LC_ALL, "");
	spawn_oshean();
}
