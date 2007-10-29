
#include <stdio.h>
#include <pthread.h>
#include <pthreadutils.h>

#ifdef __WIN32__
#include <windows.h>
#define sleep(n) Sleep(1000*(n))
#else
#include <sys/time.h>
#include <unistd.h>
#endif

int main(int ac, char *av[]){

	struct timespec ts1;
	struct timespec ts2;
	long int diff;

	ts_set(&ts1);
	printf("ts_set(&ts1) set ts1 = %lu.%09lu s\n",
			ts1.tv_sec, ts1.tv_nsec);
	sleep(3);
	ts_set(&ts2);
	printf("ts_set(&ts2) set ts2 = %lu.%09lu s\n",
			ts2.tv_sec, ts2.tv_nsec);
	diff = millisdiff(&ts1, &ts2);
	printf("millisdiff(&ts1, &ts2) = %ld\n",
			diff);
	millisadd(&ts2, diff);
	printf("millisadd(&ts2, diff) set ts2 back to ts1 = %lu.%09lu s\n",
			ts2.tv_sec, ts2.tv_nsec);


	return 1;
}

