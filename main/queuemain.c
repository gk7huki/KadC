/* simple test for queues */
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <queue.h>


#ifndef __WIN32__
# define Sleep(n) usleep(1000*(n))
#else
# include <windows.h>
#endif
#include <stdio.h>

static queue *q; /* shared among threads */

void *t1c(void *p) {
	int i;
	char *s;
	int t;

	srand(11);
	for(i=0; i<5; i++) {
		s = malloc(10);
		assert(s != NULL);
		t = rand() & ((1<<10)-1);
		Sleep(t);
		sprintf(s, "t1: %d t=%d", i, t);
		q->enq(q, s);
	}
	return(NULL);
}

void *t2c(void *p) {
	int i;
	char *s;
	int t;

	srand(22);
	for(i=0; i<5; i++) {
		s = malloc(10);
		assert(s != NULL);
		t = rand() & ((1<<10)-1);
		Sleep(t);
		sprintf(s, "t2: %d t=%d", i, t);
		q->enq(q, s);
	}
	return(NULL);
}

void *t3c(void *p) {
	int i;
	char *s;
	int t;

	for(i=0; i<5; i++)
		q->enq(q, NULL);
	srand(33);
	for(i=0; i<5; i++) {
		s = malloc(10);
		assert(s != NULL);
		t = rand() & ((1<<10)-1);
		Sleep(t);
		sprintf(s, "t3: %d t=%d", i, t);
		q->enq(q, s);
	}
	return(NULL);
}

int main(int ac, char *av[]) {
	int i;

	pthread_t t1, t2, t3;
	char *s;

	q = new_queue(20);

	/* try first single-thread testing */

	for(i=0; i<5; i++) {
		s = malloc(10);
		assert(s != NULL);
		sprintf(s, "t0: %d", i);
		q->enq(q, s);
	}


	for(i=0; i<5; i++){
		char *s = (char *)q->deqtw(q, i * 300);
		printf("%s\n", s);
		free(s);
	}

	/* now try inter-thread communications */

	pthread_create(&t1, NULL, t1c, NULL);
	pthread_create(&t2, NULL, t2c, NULL);
	pthread_create(&t3, NULL, t3c, NULL);

	for(i=0; i<15; ){
		char *s = (char *)q->deqtw(q, 200);
		if(s == NULL) {
			printf("TIMEOUT\n");
		} else {
			printf("%s\n", s);
			free(s);
			i++;
		}
	}
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	pthread_join(t3, NULL);

	q->destroy(q);

	return 0;
}
