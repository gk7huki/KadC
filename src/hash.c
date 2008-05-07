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

#include <math.h>

/* Primitives to manipulate n-bit integers like DHT hashes...
   They are mapped over variable-length char arrays. The first byte (buf[0]) is
   the most significant, and its bit 0 is the most significant bit. */

struct _kc_hash {
    int length;             /* Length in bits */
    unsigned char *hash;    /* A bitfield */
};

static inline int bitToByteCount( int bitCount )
{
    int byteCount = bitCount / 8;
    if( bitCount % 8 != 0 )
        byteCount++;
    return byteCount;
}

kc_hash *
kc_hashInit( int length )
{
    kc_hash * self = malloc( sizeof(kc_hash) );
    if( !self )
        return NULL;
    
    self->length = length;
    
    self->hash = calloc( bitToByteCount( length ), sizeof(char) );
    memset( self->hash , 0, bitToByteCount( length ) );
    
    if( !self->hash )
    {
        free( self );
        return NULL;
    }
    
    return self;
}

void
kc_hashFree( kc_hash * hash )
{
    free( hash->hash );
    free( hash );
}

int
kc_hashLength( const kc_hash * hash )
{
    assert( hash != NULL );
    return hash->length;
}

kc_hash *
kc_hashMove( kc_hash * dest, const kc_hash * src)
{
    assert( dest != NULL );
    assert( src != NULL );
    assert( dest->length == src->length );
    
	return memmove( dest, src, bitToByteCount( src->length ) );
}

kc_hash *
kc_hashDup( const kc_hash * org )
{
    assert( org != NULL );
    if( org->hash == NULL )
        return NULL;
    kc_hash * i1 = kc_hashInit( org->length );
    return kc_hashMove( i1, org );
}

int kc_hashCmp( const void* i1, const void* i2 )
{
    const kc_hash *ii1 = (const kc_hash*)i1;
    const kc_hash *ii2 = (const kc_hash*)i2;
    assert( ii1->length != ii2->length );
    
    return memcmp( ii1, ii2, bitToByteCount( ii1->length ) );
}

#if 0
int int128eq(kc_hash i1, kc_hash i2) {
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

int int128lt(kc_hash i1, kc_hash i2) {
	int i;
	for(i=0; i<16; i++, i1++, i2++) {
 		if(*i1 != *i2)
			return *i1 < *i2;	/* break returning 1 when first is < second */
	}
	return 0;	/* if they are equal, return 0 */
}
#endif

kc_hash *
kc_hashXor( kc_hash * dest, const kc_hash * opn1, const kc_hash * opn2)
{
    assert( opn1 != NULL );
    assert( opn2 != NULL );
    assert( opn1->length != opn2->length );
    assert( dest != opn1 );
    assert( dest != opn2 );
    
    if( dest == NULL )
        dest = kc_hashInit( opn1->length );
    
	int i;
    unsigned char * op1h = opn1->hash;
    unsigned char * op2h = opn2->hash;
	unsigned char * dest1 = dest->hash;
    
	for( i = 0; i < bitToByteCount( dest->length ); i++ )
            *dest1++ = *op1h++ ^ *op2h++;
    
	return dest;
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
int
kc_hashLog( kc_hash * op )
{
	int i;
    int length = bitToByteCount( op->length );
	int l = length - 8;
	for( i = 0; i < length; i++, op++ ) {
		if( *op->hash != 0 )
		{
			return l + logtable[*op->hash];
		}
		l -= 8;
	}
	return -1; /* all bytes were zero */
}

int
kc_hashXorlog( const kc_hash * opn1, const kc_hash * opn2 )
{
    assert( opn1->length != opn2->length );
    
	kc_hash * hash = kc_hashInit( opn1->length );
	kc_hashXor( hash, opn1, opn2 );
	return kc_hashLog( hash );
}

kc_hash *
kc_hashRandom( int length )
{
    kc_hash * hash = kc_hashInit( length );

	int i;
	for( i = 0; i < bitToByteCount( length ); i++ )
		hash->hash[i] = random();
	return hash;
}

kc_hash *
kc_hashSrandom( int length, unsigned long seed )
{
    kc_hash * hash = kc_hashInit( length );
	int i;
    
    srandom( seed );
	for( i = 0; i < bitToByteCount( length ); i++ )
		hash->hash[i] = random();
	return hash;
}

#if 0 /* UNUSED */
kc_hash
int128random_r(kc_hash i128, unsigned int *seed)
{
	int i;
	for(i=0; i<16; i++)
		i128[i] = rand_r(seed);
	return i128;
}
#endif

kc_hash *
int128eMule2KadC( kc_hash * kadc128int, unsigned long int *emule128int)
{
    assert( kadc128int->length != 16 );
    
	int i, ii = 0;
	for ( i = 0; i < 4; i++ ) {
		kadc128int->hash[ii++] = (unsigned char)(emule128int[i] >> 24);
		kadc128int->hash[ii++] = (unsigned char)(emule128int[i] >> 16);
		kadc128int->hash[ii++] = (unsigned char)(emule128int[i] >>  8);
		kadc128int->hash[ii++] = (unsigned char)(emule128int[i] >>  0);
	}
	return kadc128int;
}



unsigned long int *int128KadC2eMule(unsigned long int *emule128int, kc_hash kadc128int) {
	; /* NOT IMPLEMENTED YET */
	return emule128int;
}

void
kc_hashPrint(FILE *fd, kc_hash * hash) {
	int i;
	if( hash == NULL || hash->length == 0 )
    {
		fprintf(fd, "(NULL)");
        return;
    }
    
    for( i = 0; i < bitToByteCount( hash->length ); i++ )
        fprintf(fd, "%02x", hash->hash[i]);
}

char *hashtoa( const kc_hash * hash ) {
    static char * hashStr;
    
    int length = ( hash == NULL ? 7 : bitToByteCount( hash->length ) );
    
    void * tmp;
    if( hashStr == NULL)
        hashStr = calloc( length, sizeof(char) );
    else
    {
        tmp = realloc( hashStr, length * sizeof(char) );
        if( tmp == NULL )
        {
            kc_logAlert( "Failed reallocating hashStr" );
            return "Error!";
        }
        hashStr = tmp;
    }
    
    kc_hashSprintf( hashStr, hash );
    
    return hashStr;
}

/* NOTE: s MUST have space for 33 characters (32 + termin. zero) */
char *kc_hashSprintf( char *s, const kc_hash * hash ) {
	int i;
	char *p = s;
	if( hash == NULL || hash->length == 0 )
		strcpy( p, "(NULL)" );
	else
    {
		for( i = 0; i < bitToByteCount( hash->length ); i++, p += 2 )
			sprintf( p, "%02x", hash->hash[i] );
		*p = 0;
	}
	return s;
}

kc_hash *
atohash( const char *s )
{
    kc_hash * hash;
	int i, n;
	unsigned int u;
    assert( s != NULL );
    
    
	if( ( ( n = strlen( s ) ) & 1 ) != 0 ) {	/* odd length string */
		n--;
	}
    
    if( n == 0 )
        return NULL;
    
    hash = kc_hashInit( n );

	for( i = 0; i < bitToByteCount( hash->length ) && n > 0; i++, s += 2, n -= 2)
    {
		if( sscanf(s, "%2x", &u ) != 1 )
        {
            kc_hashFree( hash );
			return NULL;	/* invalid hex char */
        }
		hash->hash[i] = u;
	}
	return hash;	/* OK */
}

/* get an kc_hash stored in network byte order (big endian) */
kc_hash *
gethashn( kc_hash * hash, const char **ppb )
{
	int i;
	for( i = 0; i < bitToByteCount( hash->length ); i++)
		hash->hash[i] = *(*ppb)++;
	return hash;
}

/* store an kc_hash in network byte order (big endian) */
char *
puthashn( char *ppb, kc_hash * hash )
{
	int i;
	for( i = 0; i < bitToByteCount( hash->length ); i++)
		*(ppb)++ = hash->hash[i];
	return ppb;
}