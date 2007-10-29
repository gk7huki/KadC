
#include <pthread.h>
#ifdef __WIN32__
#include <windows.h>
#else
#include <sys/time.h>
#include <errno.h>
#endif

void ts_set(struct timespec *ts)  {
#ifndef __WIN32__
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ts->tv_nsec = tv.tv_usec * 1000;
	ts->tv_sec = tv.tv_sec;
#else
	FILETIME ft;
	SYSTEMTIME st;
#define TS_TO_FT_OFFSET (((LONGLONG)27111902 << 32) + ((LONGLONG)109181 << 15))

	/* replacement for _ftime() that also work on on WinCE: see
	   http://sources.redhat.com/ml/pthreads-win32/1999/msg00085.html */
	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);

	/* Now convert filetime, where the time is expressed
	   in 100 nanoseconds from Jan 1, 1601,to timespec,
	   where the time is expressed in seconds and nanoseconds
	   from Jan 1, 1970 */
	ts->tv_sec = (long int)((*(LONGLONG *)&ft - TS_TO_FT_OFFSET) / 10000000);
	ts->tv_nsec = (long int)((*(LONGLONG *)&ft - TS_TO_FT_OFFSET -
					((LONGLONG)ts->tv_sec * (LONGLONG)10000000)) * 100);
#endif
	return;
}

/* returns ts1 - ts2 in milliseconds */
long int millisdiff(struct timespec *ts1, struct timespec *ts2) {

	long int diff, ms1, ms2;
	ms1 = (ts1->tv_nsec + 500000L)/1000000L;
	ms2 = (ts2->tv_nsec + 500000L)/1000000L;
	diff = ms1 - ms2 + 1000 * (ts1->tv_sec - ts2->tv_sec);
	return diff;
}

#include <stdio.h>
#ifdef __WIN32__
#define sleep(n) Sleep(1000*(n))
#else
#include <unistd.h>
#endif

int main(int ac, char *av[]){

	struct timespec ts1;
	struct timespec ts2;

	ts_set(&ts1);
	printf("ts_set(&ts1) set ts1 = %lu.%09lu s\n",
			ts1.tv_sec, ts1.tv_nsec);
	sleep(3);
	ts_set(&ts2);
	printf("ts_set(&ts2) set ts2 = %lu.%09lu s\n",
			ts2.tv_sec, ts2.tv_nsec);
	printf("millisdiff(&ts1, &ts2) = %ld\n",
			millisdiff(&ts1, &ts2));

	return 1;
}

