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

/** @file kc_hash.h
 * This file contains primitives to manipulate 128-bit integers like, er, MD4 hashes...
 *
 * Internally they are mapped over 16-byte arrays. The first byte (buf[0]) is the most significant,
 * and its bit 0 (the one with weight 2**7) is the most significant bit.
 */

typedef struct _kc_hash kc_hash;

//#define int128_bitnum(n, bit) (((n)[(bit)/8] >> (7-((bit)%8))) & 1)

kc_hash *
kc_hashInit( int length );

void
kc_hashFree( kc_hash * hash );

int
kc_hashLength( const kc_hash * hash);

/** 
 * Moves an kc_hash from src to dest.
 * @return Returns dest
 */
kc_hash *
kc_hashMove( kc_hash * dest, const kc_hash * src);

/** 
 * A strdup() for kc_hash.
 *
 * This function allocates memory to hold a copy of int, copies int to it, and returns the pointer
 * You must free() the pointer after you're finished with it.
 * 
 * @param org The kc_hash to copy
 * @return A copy of int
 */
kc_hash *
kc_hashDup( const kc_hash * org );

/** 
 * Compare two kc_hash, and returns a qsort()-compatible int.
 *
 * This function compare two kc_hash, and return the result of the comparison
 *
 * @param i1, i2 The two kc_hash to compare
 * @return -1 if i1 < i2, 1 if i1 > i2, 0 otherwise
 */
int
kc_hashCmp( const void* i1, const void* i2 );

/** 
 * Return an kc_hash that is the exculsive-OR of the arguments.
 *
 * This function takes three parameters, the destination of the XOR, 
 * two operands for it, and will return dest.
 
 * @param dest The result of opn1 XOR opn2
 * @param opn1 An kc_hash
 * @param opn2 An kc_hash
 * @return @see dest.
 */
kc_hash *
kc_hashXor( kc_hash * dest, const kc_hash * opn1, const kc_hash * opn2 );

/** 
 * Returns the log value of an kc_hash.
 *
 * This function returns the position of the most significant bit of op
 * (from 0 to 127) to be set to 1: in other words, the integer
 * part of its log in base 2. If op is zero it returns -1
 * meaning "error" (log(0) is undefined).
 *
 * @param op The kc_hash from which to get the logarithmic value
 * @return The log as an int.
 */
int
kc_hashLog( kc_hash * op );

/** 
 * Return the log value of the XOR between two kc_hash.
 *
 * This function combines int128xor & int128log.
 *
 * @param opn1 An kc_hash, will be passed to int129xor
 * @param opn2 An kc_hash, will be passed to int129xor
 * @return The log of the XOR of the operands
 */
int
kc_hashXorlog( const kc_hash * opn1, const kc_hash * opn2 );

/** 
 * Returns a random()-ized kc_hash.
 *
 * This functions allocates an kc_hash, then calls random() on it, and return the result.
 * The returned pointer must be free()d.
 *
 * @return A malloc()ed pointer to the randomized kc_hash
 */
kc_hash *
kc_hashRandom( int length );

/** 
 * Returns a srandom()-ized kc_hash.
 *
 * This functions allocates an kc_hash, calls srandom( seed ), then
 * calls random() on it, and return the result.
 * The returned pointer must be free()d.
 *
 * @see int128random
 * @return A malloc()ed pointer to the randomized kc_hash
 */
kc_hash *
kc_hashSrandom( int length, unsigned long seed );

#if 0 /* UNUSED */
kc_hash
int128random_r( kc_hash i128, unsigned int *seed );
#endif

/* Conversions between eMule's and KadC's 128-bit integers.
   eMule uses arrays of 4 long int, whereas KadC uses arrays
   of unsigned chars. eMule's format is endianity-dependent;
   here, assume i386-style little-endian architecture, so
   that we may convert the result of reading into long int[4]
   data saved by eMule 0.4x in preferencesK.dat and nodes.dat
 */
kc_hash *
kc_hasheMule2KadC( kc_hash * kadc128int, unsigned long int *emule128int );

unsigned long int *
kc_hashKadC2eMule( unsigned long int *emule128int, kc_hash * kadc128int );

void
kc_hashPrint( FILE *fd, kc_hash * i128 );

/**
 * Create an kc_hash from an ASCII string.
 * 
 * This function parses an ASCII string into an kc_hash.
 * It will return NULL if s points to an odd-length string,
 * else it truncates the string or pads it on its RIGHT side
 * with zeroes, and converts it as hex string into a kc_hash,
 * returning the address of that kc_hash
 * 
 * @return A pointer to a malloc()ed kc_hash. You are responsible of free()ing this.
 * @param s A const pointer to a C string containing hexadecimal ASCII characters.
 */
kc_hash *
atohash( const char *s );

char *
hashtoa( const kc_hash * hash );

char *
kc_hashSprintf( char *s, const kc_hash * hash );

kc_hash *
gethashn( kc_hash * hash, const char **ppb );

char *
puthashn( char *ppb, kc_hash * hash );

#endif /* KADC_INT128_H */