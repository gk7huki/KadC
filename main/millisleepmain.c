#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <millisleep.h>


void sighandler(int sig) {
	printf("Signal %d caught\n", sig);
	signal(sig, sighandler);
}

int main(int ac, char *av[]) {
	if(ac < 2) {
		printf("usage: millisleep number_of_milliseconds\n");
		return 1;
	}

	signal(SIGTERM, sighandler);	/* for the TERM signal (15, default for kill command)...  */
	signal(SIGINT, sighandler); 	/* .. the INT signal... (Ctrl-C  hit on keyboard) */

	millisleep(atoi(av[1]));
	return 0;
}
