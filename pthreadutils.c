/****************************************************************\

Copyright 2004 Enzo Michelangeli

This file is part of the KadC library.

KadC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

KadC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with KadC; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

In addition, closed-source licenses for this software may be granted
by the copyright owner on commercial basis, with conditions negotiated
case by case. Interested parties may contact Enzo Michelangeli at one
of the following e-mail addresses (replace "(at)" with "@"):

 em(at)em.no-ip.com
 em(at)i-t-vision.com

\****************************************************************/

#include <pthread.h>
#include <config.h>
#include <Debug_pthreads.h>
#ifdef __WIN32__
#include <windows.h>
#else
#include <sys/time.h>
#include <errno.h>
#endif
#include <pthreadutils.h>

void ts_set(struct timespec *ts)  {
#ifndef __WIN32__
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ts->tv_nsec = tv.tv_usec * 1000;
	ts->tv_sec = tv.tv_sec;
#else
	FILETIME ft;
	LONGLONG llft;
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
	llft = ((LONGLONG)ft.dwHighDateTime << 32) + (LONGLONG)ft.dwLowDateTime;
	ts->tv_sec = (long int)((llft - TS_TO_FT_OFFSET) / 10000000);
	ts->tv_nsec = (long int)((llft - TS_TO_FT_OFFSET -
					((LONGLONG)ts->tv_sec * (LONGLONG)10000000)) * 100);
#endif
	return;
}

/* returns ts1 - ts2 in milliseconds */
long int millisdiff(struct timespec *pts1, struct timespec *pts2) {

	long int diff, ms1, ms2;
	ms1 = (pts1->tv_nsec + 500000L)/1000000L;
	ms2 = (pts2->tv_nsec + 500000L)/1000000L;
	diff = ms1 - ms2 + 1000 * (pts1->tv_sec - pts2->tv_sec);
	return diff;
}

/* adds millis to ts1 */
struct timespec *millisadd(struct timespec *pts, long int millis) {

	/* add millisec offset */
	pts->tv_nsec += (millis % 1000) * 1000000;
	if((long int)pts->tv_nsec > 1000000000) {
		pts->tv_sec += (pts->tv_nsec / 1000000000);
		pts->tv_nsec = (pts->tv_nsec % 1000000000);
	} else if((long int)pts->tv_nsec < 0) {
		pts->tv_sec += (pts->tv_nsec / 1000000000 - 1);
		pts->tv_nsec = ((1000000000+pts->tv_nsec) % 1000000000);
	}
	pts->tv_sec += (millis / 1000);
	return pts;
}

int pthread_cond_incrtimedwait(pthread_cond_t *pcond, pthread_mutex_t *pmutex, unsigned long int millisdelay) {
	struct timespec ts;
	int status;

	/* set ts to now() */
	ts_set(&ts);
	/* add millisec offset */
	millisadd(&ts, millisdelay);
	do {
		status = pthread_cond_timedwait(pcond, pmutex, &ts);
	} while(status == EINTR);
	return status;
}

/*
Recursive mutexes are a PITA . There are two basic issues:

a) static vs dynamic mutex initialization.
b) _NP suffixes.

- Older versions of Win32-Pthreads and Linux RH 7.0 do not support static
  initialization (as in "mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER")
- GNU Pth defaults to recursive, has pthread_mutex_init() but returns
  error on pthread_mutex_init(PTHREAD_MUTEX_RECURSIVE)
- Linux RH 7.0 uses the "_NP" (non-portable) suffix, Cygwin does not
- PTHREAD_MUTEX_RECURSIVE is defined in an enum, not a #define, so it
  can't be tested by an #ifdef

All this makes it impossible to get it right automatically without a
"configure" kind of tool. That's why we use the script tinyconfig
to create a config setting  __MUTEX_RECURSIVE_INITIALIZER and
__MUTEX_RECURSIVE_ATTRIBUTE to the values consistent with the
current platform.

*/


void pthreadutils_mutex_init_recursive(pthread_mutex_t *pmutex){

#ifdef __MUTEX_RECURSIVE_INITIALIZER
	static const pthread_mutex_t recursive_mutex_initializer = __MUTEX_RECURSIVE_INITIALIZER;
	*pmutex = recursive_mutex_initializer;
#else
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, __MUTEX_RECURSIVE_ATTRIBUTE);
	pthread_mutex_init(pmutex, &attr);
	pthread_mutexattr_destroy(&attr);
#endif
}

unsigned int pthread_rand_seed() {
	return (time(NULL) ^ (unsigned)pthread_self());
}
