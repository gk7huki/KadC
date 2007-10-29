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

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *tc(void *p) {
	int i;
	char *s;
	int t;
	char *tname = p;

	for(i=0; i<5; i++)
		q->enq(q, NULL);
	for(i=0; i<5; i++) {
		s = malloc(100);
		assert(s != NULL);
		t = rand() & ((1<<10)-1);
		Sleep(t);
		pthread_mutex_lock(&mutex);
		sprintf(s, "In tc: %s: %d t=%d", tname, i, t);
		pthread_mutex_unlock(&mutex);
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

	pthread_create(&t1, NULL, tc, "t1");
	pthread_create(&t2, NULL, tc, "t2");
	pthread_create(&t3, NULL, tc, "t3");

	for(i=0; i<15; ){
		char *s = (char *)q->deqtw(q, 200);
		if(s == NULL) {
			printf("TIMEOUT\n");
		} else {
			pthread_mutex_lock(&mutex);
			printf("Got %s\n", s);
			pthread_mutex_unlock(&mutex);
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
