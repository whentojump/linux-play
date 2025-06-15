#define _GNU_SOURCE
#include <unistd.h>

#ifndef __NR_dummy
#define __NR_dummy 467
#endif

int main() {
	syscall(__NR_dummy);
	return 0;
}
