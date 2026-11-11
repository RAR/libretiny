/* Copyright (c) Andrew Rankin 2026-06-12. */

// newlib-nano syscalls that abort()/exit() and the signal code reference.
// Exit is a reset; the rest fail with ENOSYS. The builder links libnosys for
// the remaining syscalls (sbrk, read, write, ...); these strong definitions
// take precedence over its stubs.

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <em_device.h>

__attribute__((noreturn)) void _exit(int status) {
	(void)status;
	NVIC_SystemReset();
	while (1) {}
}

int _kill(int pid, int sig) {
	(void)pid;
	(void)sig;
	errno = ENOSYS;
	return -1;
}

int _getpid(void) {
	return 1; // single process
}
