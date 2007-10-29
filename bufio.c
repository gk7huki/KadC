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

#include <int128.h>
#include <bufio.h>

/* functions to get/put short/long unsigned integers
   and peernode structures from/to Kademlia packets.
   In Overnet, the byte order is generally
   Intel host (i.e., little-endian), not network...
   The byte order of IP addresses and MD4 hashes, however,
   is big-endian (0123)...
   In eMuleKAD, IP addresses are little-endian as well,
   but hashes are in a weird mixed order (32107654BA98...)

   ppb is a pointer to the pointer in the buffer,
   which gets incremented as the buffer bytes are
   consumed or produced.
 */

/* get an unsigned short stored as little endian */
unsigned short int getushortle(unsigned char **ppb) {
	unsigned short u;
	u = *(*ppb)++;
	u += (*(*ppb)++)<<8 ;
	return u;
}

/* get an unsigned long stored as little endian */
unsigned long int getulongle(unsigned char **ppb) {
	unsigned long u;
	u = *(*ppb)++;
	u += (*(*ppb)++)<<8;
	u += (*(*ppb)++)<<16;
	u += (*(*ppb)++)<<24;
	return u;
}

/* get an IP address stored in network byte order (big endian) */
unsigned long int getipn(unsigned char **ppb) {
	unsigned long u;
	u = *(*ppb)++;
	u = u<<8;
	u += *(*ppb)++;
	u = u<<8;
	u += *(*ppb)++;
	u = u<<8;
	u += *(*ppb)++;
	return u;
}


/* get an int128 stored in network byte order (big endian) */
int128 getint128n(int128 hash, unsigned char **ppb) {
	int i;
	for(i=0; i<16; i++)
		hash[i] = *(*ppb)++;
	return hash;
}

/* store an unsigned short as little endian */
unsigned char *putushortle(unsigned char **ppb, unsigned short int u) {
	*(*ppb)++ = (unsigned char)u; u = u>>8;
	*(*ppb)++ = (unsigned char)u;
	return *ppb;
}

/* store an unsigned long as little endian */
unsigned char *putulongle(unsigned char **ppb, unsigned long int u) {
	*(*ppb)++ = (unsigned char)u; u = u>>8;
	*(*ppb)++ = (unsigned char)u; u = u>>8;
	*(*ppb)++ = (unsigned char)u; u = u>>8;
	*(*ppb)++ = (unsigned char)u;
	return *ppb;
}

/* store an IP address in network byte order (big endian) */
unsigned char *putipn(unsigned char **ppb, unsigned long int u) {
	*(*ppb)++ = (unsigned char)(u>>24);
	*(*ppb)++ = (unsigned char)(u>>16);
	*(*ppb)++ = (unsigned char)(u>>8);
	*(*ppb)++ = (unsigned char)u;
	return *ppb;
}

/* store an int128 in network byte order (big endian) */
unsigned char *putint128n(unsigned char **ppb, int128 hash) {
	int i;
	for(i=0; i<16; i++)
		*(*ppb)++ = hash[i];
	return *ppb;
}
