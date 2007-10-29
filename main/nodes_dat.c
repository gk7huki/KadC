#include <stdio.h>

/* Print an IP address in host byte order (LSB first) */
void printip(unsigned int *ip) {
	unsigned char *b = (unsigned char *)ip;
	printf("%d.%d.%d.%d", b[3], b[2], b[1], b[0]);
}

main(int ac, char *av[]){
	int i,j;
	FILE *fn;
	int numContacts;
	unsigned int id[4];
	unsigned int binip;
	unsigned short int udpport;
	unsigned short int tcpport;
	unsigned char type;


	if(ac <= 1) {
		printf("usage: %s nodes.dat\n", av[0]);
		exit(1);
	}


	fn = fopen(av[1], "rb");
	if(fn == NULL) {
		printf("can't open %s\n", av[1]);
		exit(1);
	}

	if(fread(&numContacts, sizeof(numContacts), 1, fn) != 1)
		numContacts = 0;

	if(numContacts == 0) {
		printf("no contacts in %s\n", av[1]);
		exit(1);
	}

	for(i=0; i < numContacts; i++) {
		if(fread(&id, sizeof(id), 1, fn) != 1)
			goto err;
		if(fread(&binip, sizeof(binip), 1, fn) != 1)
			goto err;
		if(fread(&udpport, sizeof(udpport), 1, fn) != 1)
			goto err;
		if(fread(&tcpport, sizeof(tcpport), 1, fn) != 1)
			goto err;
		if(fread(&type, sizeof(type), 1, fn) != 1)
			goto err;

		/* also MD4 in nodes.dat are little endian */
		for(j=0;j<4;j++) printf("%08x", id[j]);
		printf(" ");
		printip(&binip);
		printf("%6u", udpport);
		printf("%6u", tcpport);
		printf("%4u", type);
		printf("\n");
	}
	fclose(fn);
	exit(0);
err:
	printf("premature EOF or error encountered.\n");
	exit(1);
}
