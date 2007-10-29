#include <stdio.h>
main(int ac, char *av) {
	int i,j;
	for(i=0; i<256; i++) {
		for(j=0; j<8; j++) {
			if(i>>(j) == 0)
				break;
		}
		printf("%d", j-1);
		if(((i+1) % 16) != 0)
			printf(", ");
		else
			printf("\n");
	}
}
