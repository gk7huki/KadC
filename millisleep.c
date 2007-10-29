#ifdef __WIN32__
#include <windows.h>
#else
#include <time.h>
#endif

#include <stdio.h>

#include <millisleep.h>

void millisleep(unsigned long int millistimeout) {
#ifdef __WIN32__
	Sleep(millistimeout);
#else
	struct timespec ts;

	ts.tv_sec  = millistimeout / 1000;
	ts.tv_nsec = (millistimeout % 1000)*1000000;

	while(nanosleep(&ts, &ts) != 0);
#endif
}
