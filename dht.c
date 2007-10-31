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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <sys/time.h>

//#include "Debug_pthreads.h"
#include "pthreadutils.h"
//#include "KadCalloc.h"
//#include "int128.h"
//#include "queue.h"
#include "rbt.h"

//#include "inifiles.h"
//#include "KadCthread.h"
//#include "opcodes.h"
#include "logging.h"

#include "dht.h"

/* This is based on the Kademlia design specification available at
 * http://xlattice.sourceforge.net/components/protocol/kademlia/specs.html
 */

#define KADC_EXPIRATION_DELAY   86410   /* in s, the ttl of a node */
#define KADC_REFRESH_DELAY      3600    /* in s, the delay after which a bucket must be refreshed */
#define KADC_REPLICATION_DELAY  3600    /* in s, interval between Kademlia replication events */
#define KADC_REPUBLISH_DELAY    86400   /* in s, the delay after which a key/value must be republished */

typedef struct _dhtNode {
    in_addr_t       addr;       /* IP address of this node, in network-byte order */
    in_port_t       port;       /* UDP port of this node, in network-byte order */
    int128          nodeID;
    
	time_t          lastSeen;	/* Last time we heard of it */
//    time_t          rtt;        /* Round-trip-time to it */
} dhtNode;

typedef struct dhtBucket {
	RbtHandle         * nodes;              /* Red-Black tree of nodes */
//	pthread_mutex_t     mutex;              /* protect access */
	unsigned char       availableSlots;     /* available slots in bucket */
} dhtBucket;

struct _kc_dht {
    dhtBucket         * buckets[128];   /* array of buckets */
    
    dhtNode           * us;             /* pointer to ourself */
    
    kc_dhtReadCallback  readCallback;   /* callback used when we need reading something */
    kc_dhtWriteCallback writeCallback;  /* callback used when we need to send something... */
    
    kc_udpIo          * io;             /* Our UDP network layer */
    
    unsigned char       bucketSize;
    
    pthread_t           thread;
    pthread_mutex_t     lock;
};

/* Management of the kbuckets/kspace table */
static int
dhtNodeCmp( const void *a, const void *b )
{
	const dhtNode *pa = a;
	const dhtNode *pb = b;
    
	if( pa->lastSeen != pb->lastSeen)
        return ( pa->lastSeen < pb->lastSeen ? -1 : 1 );
    
    return 0;
}

static dhtNode *
dhtNodeInit( in_addr_t addr, in_port_t port, const int128 hash )
{
	dhtNode *pkn = malloc( sizeof(dhtNode) );
    
    if ( pkn == NULL )
    {
		kc_logPrint(KADC_LOG_DEBUG, "kc_kNodeInit failed!\n");
        return NULL;
    }
    
    pkn->addr = htonl( addr );
    pkn->port = htons( port );
    pkn->nodeID = int128dup( hash );    /* copy dereferenced data */
	pkn->lastSeen = 0;
    
	return pkn;
}

static void
dhtNodeFree( dhtNode *pkn )
{
    free( pkn->nodeID );
	free( pkn );
}

static dhtBucket *
dhtBucketInit( int size )
{
	dhtBucket *pkb = calloc( 1, sizeof(dhtBucket) );
	if(pkb == NULL)
    {
        kc_logPrint( KADC_LOG_DEBUG, "dhtBucketInit: malloc failed !" );
        return NULL;
    }
    
    /* if static initialization of recursive mutexes is available, use it;
     * otherwise, hope that dynamic initialization is available... */

//   pthreadutils_mutex_init_recursive( &pkb->mutex );

    pkb->nodes = rbtNew( dhtNodeCmp );
	pkb->availableSlots = size;
    
	return pkb;
}

static void
dhtBucketFree( dhtBucket *pkb )
{
	void *iter;
//	pthread_mutex_lock( &pkb->mutex );

	/* empty pkb->rbt */
	while ( ( iter = rbtBegin( pkb->nodes ) ) != NULL)
    {
		dhtNode *pkn;

		pkn = rbtValue( pkb->nodes, iter );
        
		rbtErase( pkb->nodes, iter ); /* ?? */
        
		dhtNodeFree( pkn );	/* deallocate knode */
	}
    
	rbtDelete( pkb->nodes );

//	pthread_mutex_unlock( &pkb->mutex );
//	pthread_mutex_destroy( &pkb->mutex );
	free( pkb );
}

static void
dhtPrintBucket( const dhtBucket * bucket )
{
    RbtIterator iter = rbtBegin( bucket->nodes );
    if( iter != NULL )
    {
        do
        {
            int128  key;
            dhtNode * value;
            char    buf[33];
            
            rbtKeyValue( bucket->nodes, iter, (void*)&key, (void*)&value );
            
            assert( key != NULL );
            assert( value != NULL );
            
            struct in_addr ad;
            ad.s_addr = value->addr;
            kc_logPrint( KADC_LOG_NORMAL, "%s at %s:%d", int128sprintf( buf, key ), inet_ntoa( ad ), ntohs( value->port ) );
            
        } while ( ( iter = rbtNext( bucket->nodes, iter ) ) );
    }
}

void *
dhtPulse( void * arg );

void ioCallback( void * ref, kc_udpIo * io, kc_udpMsg *msg )
{
    kc_dht  * dht = ref;
    
    dht->readCallback( dht, msg );
}

kc_dht*
kc_dhtInit( in_addr_t addr, in_port_t port, int bucketMaxSize, kc_dhtReadCallback readCallback, kc_dhtWriteCallback writeCallback )
{    
    kc_dht * dht = malloc( sizeof( kc_dht ) );
    
    if ( dht == NULL )
    {
		kc_logPrint( KADC_LOG_DEBUG, "kc_dhtInit: malloc failed!\n");
        return NULL;
    }
    
/*    dht->nodes = rbtNew( dhtNodeCmp );
    if ( dht->nodes == NULL )
    {
		kc_logPrint( KADC_LOG_DEBUG, "kc_dhtInit: nodes malloc failed!\n");
        free( dht );
        return NULL;
    }*/
    
    int128 hash = int128random();
    dht->us = dhtNodeInit( addr, port, hash );
    free( hash );
    
    dht->readCallback = readCallback;
    dht->writeCallback = writeCallback;
    
    dht->bucketSize = bucketMaxSize;
    
//    rbtInsert( dht->buckets, dht->us->nodeID, dht->us );
    
    if ( ( pthread_mutex_init( &dht->lock, NULL ) != 0 ) )
    {
        kc_logPrint( KADC_LOG_DEBUG, "kc_dhtInit: mutex init failed\n" );
        dhtNodeFree( dht->us );
        free( dht );
        return NULL;
    }
    
    dht->io = kc_udpIoInit( addr, port, 1024, ioCallback, dht );
    if ( dht->io == NULL )
    {
        kc_logPrint( KADC_LOG_DEBUG, "kc_dhtInit: kc_udpIo failed\n" );
        dhtNodeFree( dht->us );
        pthread_mutex_destroy( &dht->lock );
        free( dht );
        return NULL;
    }
    int i;
    for( i = 0; i < 128; i++ )
        dht->buckets[i] = dhtBucketInit( dht->bucketSize );
    
//    pthread_create( &dht->thread, NULL, dhtPulse, dht );
    
    return dht;
}

void
kc_dhtFree( kc_dht * dht )
{
    kc_udpIoFree( dht->io );
  
    int i;
    for( i = 0; i < 128; i++ )
        dhtBucketFree( dht->buckets[i] );

    pthread_mutex_destroy( &dht->lock );
    
    dhtNodeFree( dht->us );
    free( dht );
}

void *
dhtPulse( void * arg )
{
    kc_dht * dht = arg;
    
    return dht;
}

void
performPing( const kc_dht * dht, in_addr_t addr, in_port_t port )
{
    kc_udpMsg * msg = malloc( sizeof( kc_udpMsg ) );
    int         status;
    
    msg->remoteIp = addr;
    msg->remotePort = port;
    
    status = dht->writeCallback( dht, DHT_RPC_PING, msg );
    
    if ( status == 0 )
    {
        status = kc_udpIoSendMsg( dht->io, msg );
        if ( status < 0 )
        {
            kc_logPrint( KADC_LOG_ALERT, "Couldn't send message, err %d", errno );
        }
    }
    
    free( msg->payload );
    free( msg );
}

void
kc_dhtAddNode( const kc_dht * dht, in_addr_t addr, in_port_t port, int128 hash )
{
    assert( dht != NULL );
    
    int logDist = int128xorlog( dht->us->nodeID, hash );
    if( logDist < 0 )
    {
        kc_logPrint( KADC_LOG_DEBUG, "Trying to add our own node. Ignoring..." );
        return;
    }
    
    /* Get this node's bucket */
    dhtBucket * bucket = dht->buckets[logDist];
    dhtNode   * node;
    
    RbtIterator nodeIter = rbtFind( bucket->nodes, hash );
    if( nodeIter != NULL )
    {
        char msg[33];
        // This node is already in our bucket list, let's update it's info */
        kc_logPrint( KADC_LOG_DEBUG, "Node %s already in our bucket, updating...", int128sprintf( msg, hash ) );
        rbtKeyValue( bucket->nodes, nodeIter, NULL, (void**)&node );
        node->addr = addr;
        node->port = port;
        node->lastSeen = time(NULL);
        /* FIXME: handle node type */
//        node->type = 0;
        return;
    }
    
    /* We don't have it, allocate one */
    node = dhtNodeInit( addr, port, hash );
    
    if( node == NULL )
    {
        kc_logPrint( KADC_LOG_ALERT, "kc_dhtAddNode: dhtNodeInit failed !");
        return;
    }
    
    dhtNode   * oldNode;
    
    if( bucket->availableSlots == 0 )
    {
        // This bucket is full, get the first node in this bucket, (the least-heard-of one)
        /* Here we need to ping it to make sure it's down, then if it replies we move up
         * in the bucket until one of them fails to reply...
         * - If one fails, we remove it and add the new one at the end
         * - If they all replied, we store it in our "backup nodes" list 
         */
        /* FIXME: Right now, we just consider the first one didn't replied */
        RbtIterator first = rbtBegin( bucket->nodes );
        assert( first != NULL );
        rbtKeyValue( bucket->nodes, first, NULL, (void**)&oldNode );
        assert( oldNode != NULL );
        /* We remove it */
        rbtErase( bucket->nodes, first );
        dhtNodeFree( oldNode );
        bucket->availableSlots++;
    }
    
    /* We mark it as seen just now */
    node->lastSeen = time(NULL);
    /* FIXME: Handle node type here */
    
    /* We add it to this bucket */
    rbtInsert( bucket->nodes, hash, node );
    bucket->availableSlots--;
}

void
kc_dhtCreateNode( const kc_dht * dht, in_addr_t addr, in_port_t port)
{
    assert( dht != NULL);
    
    performPing( dht, addr, port );
}

void
kc_dhtPrintTree( const kc_dht * dht )
{
    int i;
    for( i = 0; i < 128; i++ )
        dhtPrintBucket( dht->buckets[i] );
}

int
kc_dhtNodeCount( kc_dht *dht )
{
    int total = 0;
    int i;
    for( i = 0; i < 128; i++)
        total += dht->bucketSize - dht->buckets[i]->availableSlots;
    
    return abs( total );
}

in_addr_t
kc_dhtGetOurIp( const kc_dht * dht )
{
    return ntohl( dht->us->addr );
}

in_port_t
kc_dhtGetOurPort( const kc_dht * dht )
{
    return ntohs( dht->us->port );
}

int128
kc_dhtGetOurHash( const kc_dht * dht )
{
    return dht->us->nodeID;
}
