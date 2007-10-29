#include <stdio.h>
#include <int128.h>
#include <stdlib.h>

/* Print an IP address in host byte order (LSB first) */
void printip(unsigned int *ip) {
	unsigned char *b = (unsigned char *)ip;
	printf("%d.%d.%d.%d", b[3], b[2], b[1], b[0]);
}

int main(int ac, char *av[]){
	int i,j;
	FILE *fn, *fp;
	unsigned int clientbinip;
	unsigned short int clientudpport;
	unsigned long int clientid[4];

	int numContacts;
	unsigned long int id[4];
	unsigned int binip;
	unsigned short int udpport;
	unsigned short int tcpport;
	unsigned char type;

	unsigned char buf[16], buf1[16];
	int128 clientidint128 = buf;
	int128 idint128 = buf1;

	int commonhist[32];


	if(ac <= 1) {
		char *p;
		char buf[33];
		printf("usage: %s nodes.dat [preferencesK.dat]\n", av[0]);
		printf("or testing conversions:\n");

		p = "de2db47b13029fdd64827ceadf8c246d";
		printf("int128print(stdout,string2int128(idint128, %s)):\n",p);
		int128print(stdout, string2int128(idint128, p));
		printf("\n");
		printf("int128sprintf(buf, string2int128(idint128, %s)):\n", p);
		printf("%s\n", int128sprintf(buf, string2int128(idint128, p)));

		p = "00112233445566778899aabbccddeeff123456";
		printf("int128print(stdout,string2int128(idint128, %s)):\n",p);
		int128print(stdout, string2int128(idint128, p));
		printf("\n");
		printf("int128sprintf(buf, string2int128(idint128, %s)):\n", p);
		printf("%s\n",int128sprintf(buf, string2int128(idint128, p)));

		p = "112233";
		printf("int128print(stdout,string2int128(idint128, %s)):\n",p);
		int128print(stdout, string2int128(idint128, p));
		printf("\n");
		printf("int128sprintf(buf, string2int128(idint128, %s)):\n", p);
		printf("%s\n",int128sprintf(buf, string2int128(idint128, p)));

		p = "11223";
		printf("int128print(stdout,string2int128(idint128, %s)):\n",p);
		int128print(stdout, string2int128(idint128, p));
		printf("\n");
		printf("int128sprintf(buf, string2int128(idint128, %s)):\n", p);
		printf("%s\n",int128sprintf(buf, string2int128(idint128, p)));

		p = "1122z344";
		printf("int128print(stdout,string2int128(idint128, %s)):\n",p);
		int128print(stdout, string2int128(idint128, p));
		printf("\n");
		printf("int128sprintf(buf, string2int128(idint128, %s)):\n", p);
		printf("%s\n",int128sprintf(buf, string2int128(idint128, p)));

		exit(1);
	}

	if(ac > 2) {

		fp = fopen(av[2], "rb");
		if(fp == NULL) {
			printf("can't open %s\n", av[2]);
			exit(1);
		}

		if(fread(&clientbinip, sizeof(clientbinip), 1, fp) != 1)
			goto err2;

		if(fread(&clientudpport, sizeof(clientudpport), 1, fp) != 1)
			goto err2;

		if(fread(&clientid, sizeof(clientid), 1, fp) != 1)
			goto err;

		printf("Client IP: ");
		printip(&binip);
		printf(" port: %6u",clientudpport);
		printf(" ID: ");
		for(i=0;i<4;i++) printf("%08lx", clientid[i]);
		printf("\n\n");
		fclose(fp);

		/* convert into good KadC's int128 format */

		int128eMule2KadC(clientidint128, clientid);

		printf("clientidint128 in KadC format: ");
		for(i=0; i<16; i++)
			printf("%02x", clientidint128[i]);
		printf("\n\n");

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
	} else {
		printf("%s contains %d contacts:\n", av[1], numContacts);
	}

	for(i=0; i < numContacts; i++) {
		int xl;
		unsigned char xb[16];

		printf("%4d: ",i);
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
		for(j=0;j<4;j++) printf("%08lx", id[j]);


		if(ac > 2) { /* if we have a Client ID to compute distance from */
		/* convert into good KadC's int128 format */

			int128eMule2KadC(idint128, id);

			printf(" ");

			int128xor(xb, idint128, clientidint128);

			for(j=0;j<16;j++) printf("%02x", xb[j]);

			xl = int128xorlog(idint128, clientidint128);
			commonhist[127-xl]++;

			printf(" %d", 127-xl);
		}
		printf("%6u", udpport);
		printf("%6u", tcpport);
		printf("%4u", type);
		printf(" ");
		printip(&binip);
		printf("\n");
	}
	fclose(fn);


	exit(0);
err:
	printf("premature EOF or error encountered on %s.\n", av[1]);
	exit(1);
err2:
	printf("premature EOF or error encountered on %s.\n", av[2]);
	exit(1);
}
