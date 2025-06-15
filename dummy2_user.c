#define _GNU_SOURCE
#include <unistd.h>

#ifndef __NR_dummy2
#define __NR_dummy2 468
#endif

int main() {
	syscall(__NR_dummy2);
	return 0;
}
