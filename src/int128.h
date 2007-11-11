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
#ifndef KADC_INT128_H
#define KADC_INT128_H

/* Primitives to manipulate 128-bit integers like, er, MD4 hashes...
   They are mapped over 16-byte arrays. The first byte (buf[0]) is
   the most significant, and its bit 0 (the one with weight 2**7)
   is the most significant bit. */
#include <stdio.h>

typedef unsigned char *int128;
#define int128_bitnum(n, bit) (((n)[(bit)/8] >> (7-((bit)%8))) & 1)

/**
 * Moves an int128 from src to dest 
 * @return Returns dest
 */
int128
int128move( int128 dest, const int128 src);

/**
 * A strdup lookalike for int128
 * This function allocates memory to hold a copy of int, copies int to it, and returns the pointer
 * You must free() the pointer after you're finished with it.
 * 
 * @param org The int128 to copy
 * @return A copy of int
 */
int128
int128dup( const int128 org );

/**
 * Compare int128, and returns a qsort()-compatible int
 * This function compare two int128, and return the result of the comparison
 *
 * @param i1, i2 The two int128 to compare
 * @return -1 if i1 < i2, 1 if i1 > i2, 0 otherwise
 */
int
int128cmp( const void* i1, const void* i2 );

/**
 * Return an int128 that is the exculsive-OR of the arguments
 * This function takes three parameters, the destination of the XOR, 
 * two operands for it, and will return dest.
 
 * @param dest The result of opn1 XOR opn2
 * @param opn1 An int128
 * @param opn2 An int128
 * @return @see dest.
 */
int128
int128xor( int128 dest, int128 opn1, int128 opn2 );

/**
 * Returns the log value of an int128
 * This function returns the position of the most significant bit of op
 * (from 0 to 127) to be set to 1: in other words, the integer
 * part of its log in base 2. If op is zero it returns -1
 * meaning "error" (log(0) is undefined).
 *
 * @param op The int128 from which to get the logarithmic value
 * @return The log as an int.
 */
int
int128log( int128 op );

/**
 * Return the log value of the XOR between two int128
 * This function combines int128xor & int128log.
 *
 * @param opn1 An int128, will be passed to int129xor
 * @param opn2 An int128, will be passed to int129xor
 * @return The log of the XOR of the operands
 */
int
int128xorlog( int128 opn1, int128 opn2 );

/**
 * Returns a random()-ized int128.
 * This functions allocates an int128, then calls random() on it, and return the result.
 * The returned pointer must be free()d.
 *
 * @return A malloc()ed pointer to the randomized int128
 */
int128
int128random( void );

/**
* Returns a srandom()-ized int128.
 * This functions allocates an int128, calls srandom( seed ), then
 * calls random() on it, and return the result.
 * The returned pointer must be free()d.
 *
 * @see int128random
 * @return A malloc()ed pointer to the randomized int128
 */
int128
int128srandom( unsigned long seed );

#if 0 /* UNUSED */
int128
int128random_r( int128 i128, unsigned int *seed );
#endif

/* Conversions between eMule's and KadC's 128-bit integers.
   eMule uses arrays of 4 long int, whereas KadC uses arrays
   of unsigned chars. eMule's format is endianity-dependent;
   here, assume i386-style little-endian architecture, so
   that we may convert the result of reading into long int[4]
   data saved by eMule 0.4x in preferencesK.dat and nodes.dat
 */
int128
int128eMule2KadC( int128 kadc128int, unsigned long int *emule128int );

unsigned long int *
int128KadC2eMule( unsigned long int *emule128int, int128 kadc128int );

void
int128print( FILE *fd, int128 i128 );

/* if s is NULL or points to an odd-length string, returns NULL
   else it truncates the string or pads it on its RIGHT side
   with zeroes, and converts it as hex string into a int128,
   returning the address of that int128 */
int128
string2int128( int128 i128, char *s );

char *
int128sprintf( char *s, int128 i128 );

#endif /* KADC_INT128_H */