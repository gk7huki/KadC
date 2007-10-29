#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	int i, n;
	void **p;
	if(argc < 2) {
		printf("usage: %s number_of_allocs\n", argv[0]);
		exit(1);
	}
	n = atoi(argv[1]);

	if((p=malloc(n * sizeof(void *))) == NULL) {
		printf("can't alloc %d pointers\n", n);
		exit(2);
	}

	for(i=0; i<n; i++) {
		if((p[i]=malloc(31)) == NULL) {
			printf("can't alloc of %d-th integer failed\n", i);
			exit(2);
		}
	}

	for(i=0; i<n; i++) {
		free(p[i]);
	}

	return 0;
}

