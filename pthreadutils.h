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

#ifdef __linux__
#ifndef pthread_mutexattr_settype
extern int pthread_mutexattr_settype(pthread_mutexattr_t *__attr, int __kind);
#endif
#endif

/* sets a struct timespec to the current clock, replacing the
   missing clock_settime() (not defined by pthread.h although
   the latter does define struct timespec).
   Under Cygwin we implement ts_set() it in terms of gettimeofday(),
   and under WIN32 in terms of GetSystemTime()+SystemTimeToFileTime()
   which works also under WinCE. Life is tough. */
void ts_set(struct timespec *ts);

/* returns ts1 - ts2 in milliseconds */
long int millisdiff(struct timespec *ts1, struct timespec *ts2);

/* adds millis to ts1 */
struct timespec *millisadd(struct timespec *pts, long int millis);

/* Like Pthreads' standard pthread_cond_timedwait() but passing as third
   argument a delay in milliseconds rather than an absolute timespec.
   The implementation works on both UNIX and WIN32 (including WinCE,
   although the latter wasn't tested). */
int pthread_cond_incrtimedwait(pthread_cond_t *pcond, pthread_mutex_t *pmutex, unsigned long int millisdelay);

/* Initialize a mutex to be recursive, using either static or dynamic
   initialization depending on what's available on the current platform */
void pthreadutils_mutex_init_recursive(pthread_mutex_t *pmutex);

/* Returns a thread specific random seed for use with rand_r */
unsigned int pthread_rand_seed();
