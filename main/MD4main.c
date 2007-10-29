#include <MD4.h>
#include <stdio.h>
#include <string.h>
/* from RFC1320:

A.5 Test suite

   The MD4 test suite (driver option "-x") should print the following
   results:

MD4 test suite:
MD4 ("") = 31d6cfe0d16ae931b73c59d7e0c089c0
MD4 ("a") = bde52cb31de33e46245e05fbdbd6fb24
MD4 ("abc") = a448017aaf21d8525fc10ae87aa6729d
MD4 ("message digest") = d9130a8164549fe818874806e1c7014b
MD4 ("abcdefghijklmnopqrstuvwxyz") = d79e1c308aa5bbcdeea8ed63df412da9
MD4 ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") =
043f8582f241db351ce627e153e7f0e4
MD4 ("123456789012345678901234567890123456789012345678901234567890\
12345678901234567890") = e33b4ddc9c38f2199c3e7b164fcc0536

*/

static char *test[] = {
"",
"a",
"abc",
"message digest",
"abcdefghijklmnopqrstuvwxyz",
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
"123456789012345678901234567890123456789012345678901234567890"\
"12345678901234567890",
NULL
};

#include <int128.h>

int main(int ac, char *av[]) {
	unsigned char digest[16], digest1[16];
	int i;
	unsigned char flip_data[16];
    int128 flip = flip_data;

	for(i=0; i<16; i++)
		flip[i] = 0;
	flip[0] = 0x55;

	for(i=0; (test[i]) != NULL; i++) {
		int j;

		MD4(digest, (unsigned char *)test[i], strlen(test[i]));
		printf("MD4 (\"%s\") = ", test[i]);
		for(j=0; j<16; j++)
			printf("%02x", digest[j]);
		printf("\n");
	}


	printf("last digest most significant byte (bits 0-7):\n");

	for(i=0; i<8; i++)
		printf("%d", int128_bitnum((int128)digest, 0+i) );

	int128xor(digest1, digest, flip);

	printf(" and after xoring with 0x55: ");

	for(i=0; i<8; i++)
		printf("%d", int128_bitnum((int128)digest1, 0+i) );
	printf("\n");


	printf("XOR dist between old and new digest: %d\n",
			int128xorlog(digest, digest1));

	for(;;) {
		int j;
		char line[80];
		char *l;
		printf("Enter string: "); fflush(stdout);
		l = fgets(line, sizeof(line)-1, stdin);
		if(l == NULL)
			break;
		if((l = strrchr(line, '\n')) != NULL) *l = 0;
		if((l = strrchr(line, '\r')) != NULL) *l = 0;
		MD4(digest, (unsigned char *)line, strlen(line));
		printf("MD4 (\"%s\") = ", line);
		for(j=0; j<16; j++)
			printf("%02x", digest[j]);
		printf("\n");
	}
	return 0;

}
