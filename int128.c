/****************************************************************\

Copyright 2004 Enzo Michelangeli

This file is part of the KadC library.

KadC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

KadC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with KadC; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

In addition, closed-source licenses for this software may be granted
by the copyright owner on commercial basis, with conditions negotiated
case by case. Interested parties may contact Enzo Michelangeli at one
of the following e-mail addresses (replace "(at)" with "@"):

 em(at)em.no-ip.com
 em(at)i-t-vision.com

\****************************************************************/

/* Primitives to manipulate 128-bit integers like, er, MD4 hashes...
   They are mapped over 16-byte arrays. The first byte (buf[0]) is
   the most significant, and its bit 0 is the most significant bit. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <int128.h>
#include <pthread.h>

int128 int128move(int128 dest, int128 src) {
	int i;
	int128 dest1 = dest;
	for(i=0; i<16; i++)
		*dest++ = *src++;
	return dest1;
}

int int128eq(int128 i1, int128 i2) {
#if 0
	int i;
	for(i=0; i<16; i++) {
 		if(*i1++ != *i2++)
			return 0;
	}
	return 1;
#else
	return (memcmp(i1, i2, 16) == 0);
#endif
}

int int128lt(int128 i1, int128 i2) {
	int i;
	for(i=0; i<16; i++, i1++, i2++) {
 		if(*i1 != *i2)
			return *i1 < *i2;	/* break returning 1 when first is < second */
	}
	return 0;	/* if they are equal, return 0 */
}


/* dest may also coincide with opn1 or opn2 */
int128 int128xor(int128 dest, int128 opn1, int128 opn2) {
	int i;
	int128 dest1 = dest;
	for(i=0; i<16; i++)
		*dest++ = *opn1++ ^ *opn2++;
	return dest1;
}

const static char logtable[256] = {
   -1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};

/* returns the position of the most significant bit of op
   (from 0 to 127) to be set to 1: in other words, the integer
   part of its log in base 2. If op is zero it returns -1
   meaning "error" (log(0) is undefined). */
int int128log(int128 op) {
	int i;
	int l = 120;
	for(i=0; i < 16; i++, op++) {
		if(*op != 0)
		{
			return l + logtable[*op];
		}
		l -= 8;
	}
	return -1; /* all bytes were zero */
}

int int128xorlog(int128 opn1, int128 opn2) {
	unsigned char buf[16];
	int128xor((int128)buf, opn1, opn2);
	return int128log(buf);
}

int128 int128setrandom(int128 i128) {
	int i;
	for(i=0; i<16; i++)
		i128[i] = rand();
	return i128;
}

int128 int128setrandom_r(int128 i128, unsigned int *seed) {
	int i;
	for(i=0; i<16; i++)
		i128[i] = rand_r(seed);
	return i128;
}

int128 int128eMule2KadC(int128 kadc128int, unsigned long int *emule128int) {
	int i, ii=0;
	for (i = 0; i<4; i++) {
		kadc128int[ii++] = (unsigned char)(emule128int[i] >> 24);
		kadc128int[ii++] = (unsigned char)(emule128int[i] >> 16);
		kadc128int[ii++] = (unsigned char)(emule128int[i] >>  8);
		kadc128int[ii++] = (unsigned char)(emule128int[i] >>  0);
	}
	return kadc128int;
}



unsigned long int *int128KadC2eMule(unsigned long int *emule128int, int128 kadc128int) {
	; /* NOT IMPLEMENTED YET */
	return emule128int;
}

void int128print(FILE *fd, int128 i128) {
	int i;
	if(i128 == NULL)
		fprintf(fd, "(NULL)");
	else
		for(i=0; i<16; i++)
			fprintf(fd, "%02x", i128[i]);
}

/* NOTE: s MUST have space for 33 characters (32 + termin. zero) */
char *int128sprintf(char *s, int128 i128) {
	int i;
	char *p = s;
	if(i128 == NULL)
		strcpy(p,"(NULL)");
	else {
		for(i=0; i<16; i++, p += 2)
			sprintf(p, "%02x", i128[i]);
		*p = 0;
	}
	return s;
}

int128 string2int128(int128 i128, char *s) {
	int i, n;
	unsigned int u;
	if(s == NULL)				/* NULL string */
		return NULL;	/* error */

	if(((n=strlen(s))&1) != 0 ) {	/* odd length string */
		n--;
	}

	for(i=0; i<16; i++)
		i128[i] = 0;

	for(i=0; i<16 && n > 0; i++, s += 2, n -= 2) {
		if(sscanf(s, "%2x", &u ) != 1)
			return NULL;	/* invalid hex char */
		i128[i] = u;
	}
	return i128;	/* OK */
}
