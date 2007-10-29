#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <Debug_pthreads.h>
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_init

int Debug_pthread_mutex_lock(pthread_mutex_t *m, char *file, int line) {
	int status = pthread_mutex_lock(m);
	if(status != 0)
		printf("*** %s:%d pthread_mutex_lock(0x%lx) returned error %d (%s)\n",
				file, line, (unsigned long int)m, status, strerror(status));
	return status;
}

int Debug_pthread_mutex_unlock(pthread_mutex_t *m, char *file, int line) {
	int status = pthread_mutex_unlock(m);
	if(status != 0)
		printf("*** %s:%d pthread_mutex_unlock(0x%lx) returned error %d (%s)\n",
				file, line, (unsigned long int)m, status, strerror(status));
	return status;
}

int Debug_pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr, char *file, int line) {
	int status = pthread_mutex_init(m, attr);
	if(status != 0)
		printf("*** %s:%d pthread_mutex_init() returned error %d (%s)\n",
				file, line, status, strerror(status));
	return status;
}
