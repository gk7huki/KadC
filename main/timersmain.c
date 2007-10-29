#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef __WIN32__
#include <windows.h>
#define sleep(n) Sleep(1000*(n))
#endif
#include <timers.h>

typedef struct _arg_t {
	int i;
} arg_t;

void callback(void *par) {
	printf("callback called with arg %d\n", ((arg_t *)par)->i);
}

int main(int ac, char *av[]){
	int i, status;
	void *ptimer[30];
	arg_t args[30];

	tsparam *ptsp = startTimersThread(30);
	if(ptsp == NULL) {
		printf("startTimersThread() returned NULL\n");
			exit(1);
	}

	for(i=0; i<30; i++) {

		args[i].i = i;
		ptimer[i] = newTimer(ptsp, callback, (void *)&args[i]);
		if(ptimer[i] == NULL) {
			printf("%d-th newTimer() returned NULL\n", i);
			exit(2);
		}
	}

	for(i=0; i<30; i+=3) {
		status = destroyTimer(ptsp, ptimer[i]);
		if(status != 0) {
			printf("destroyTimer(ptsp, ptimer[%d]) returned %d\n", i, status);
			exit(3);
		}
	}

	for(i=0; i<30; i++) {
		int delay = rand() % 3;
		status = startTimer(ptimer[i], delay);
		if(status != 0) {
			printf("startTimer() returned %d\n", status);
			exit(3);
		}
	}

	sleep(1);
	for(i=0; i<30; i+=2) {
		status = destroyTimer(ptsp, ptimer[i]);
		if(status != 0) {
			printf("destroyTimer(ptsp, ptimer[%d]) returned %d\n", i, status);
			/* exit(3); */
		}
	}

	ptimer[0] = newTimer(ptsp, callback, (void *)&args[0]);
	if(ptimer[0] == NULL) {
		printf("%d-th newTimer() returned NULL\n", 0);
		exit(2);
	}

	status = startTimer(ptimer[0], 2);
	if(status != 0) {
		printf("startTimer() returned %d\n", status);
		exit(3);
	}

	sleep(2);

	status = stopTimersThread(ptsp);
	if(status != 0) {
		printf("stopTimersThread() returned %d\n", status);
		exit(3);
	}
	sleep(3);

	return 0;
}



