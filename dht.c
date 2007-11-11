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

struct _kc_dhtNode {
    in_addr_t       addr;       /* IP address of this node, in network-byte order */
    in_port_t       port;       /* UDP port of this node, in network-byte order */
    int128          nodeID;
    
	time_t          lastSeen;	/* Last time we heard of it */
//    time_t          rtt;        /* Round-trip-time to it */
};

typedef struct dhtBucket {
	RbtHandle         * nodes;              /* Red-Black tree of nodes */
    
    unsigned char       availableSlots;     /* available slots in bucket */
    
    time_t              lastChanged;        /* Last time this bucket changed */
    pthread_mutex_t     mutex;
	
} dhtBucket;

struct _kc_dht {
    dhtBucket         * buckets[128];   /* array of buckets */
    
    kc_dhtNode        * us;             /* pointer to ourself */
    
    kc_dhtReadCallback  readCallback;   /* callback used when we need reading something */
    kc_dhtWriteCallback writeCallback;  /* callback used when we need to send something... */
    
    kc_udpIo          * io;             /* Our UDP network layer */
    
    unsigned char       bucketSize;     /* This DHT buckets size
                                         * Also used to stop the bg thread, by setting to 0 */
    
    pthread_t           thread;
    pthread_mutex_t     lock;
};

/* Management of the kbuckets/kspace table */
static int
dhtNodeCmp( const void *a, const void *b )
{
	const kc_dhtNode *pa = a;
	const kc_dhtNode *pb = b;
    
	if( pa->lastSeen != pb->lastSeen)
        return ( pa->lastSeen < pb->lastSeen ? -1 : 1 );
    
    return 0;
}

static kc_dhtNode *
dhtNodeInit( in_addr_t addr, in_port_t port, const int128 hash )
{
	kc_dhtNode *pkn = malloc( sizeof(kc_dhtNode) );
    
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
dhtNodeFree( kc_dhtNode *pkn )
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

    pthreadutils_mutex_init_recursive( &pkb->mutex );

    pkb->nodes = rbtNew( dhtNodeCmp );
	pkb->availableSlots = size;
    
	return pkb;
}

static void
dhtBucketLock( dhtBucket *pkb )
{
    pthread_mutex_lock( &pkb->mutex );
}

static void
dhtBucketUnlock( dhtBucket *pkb )
{
    pthread_mutex_unlock( &pkb->mutex );
}

static void
dhtBucketFree( dhtBucket *pkb )
{
	void *iter;
	dhtBucketLock( pkb );

	/* empty pkb->rbt */
	while ( ( iter = rbtBegin( pkb->nodes ) ) != NULL)
    {
		kc_dhtNode *pkn;

		rbtKeyValue( pkb->nodes, iter, NULL, (void**)&pkn );
        
		rbtErase( pkb->nodes, iter ); /* ?? */
        
		dhtNodeFree( pkn );	/* deallocate knode */
	}
    
	rbtDelete( pkb->nodes );

	dhtBucketUnlock( pkb );
	pthread_mutex_destroy( &pkb->mutex );
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
            kc_dhtNode * value;
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
    kc_dht    * dht = ref;
    kc_udpMsg * answer = malloc( sizeof(kc_udpMsg) );
    int status;
    
    status = dht->readCallback( dht, msg, answer );
    
    if ( status == 0 )
    {
        status = kc_udpIoSendMsg( dht->io, msg );
        if ( status < 0 )
        {
            kc_logPrint( KADC_LOG_ALERT, "Couldn't send message, err %d", errno );
        }
    }
    
/*    if( answer->payload )
        free( answer->payload );*/
    free( answer );
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
    
    int128 hash = int128random();
    dht->us = dhtNodeInit( addr, port, hash );
    free( hash );
    
    dht->readCallback = readCallback;
    dht->writeCallback = writeCallback;
    
    dht->bucketSize = bucketMaxSize;
    
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
    
    pthread_create( &dht->thread, NULL, dhtPulse, dht );
    
    return dht;
}

void
kc_dhtFree( kc_dht * dht )
{
    /* We stop the background thread */
    dht->bucketSize = 0;
    pthread_join( dht->thread, NULL );
    
    kc_udpIoFree( dht->io );
  
    int i;
    for( i = 0; i < 128; i++ )
        dhtBucketFree( dht->buckets[i] );

    pthread_mutex_destroy( &dht->lock );
    
    dhtNodeFree( dht->us );
    free( dht );
}

void
kc_dhtLock( kc_dht * dht )
{
    pthread_mutex_lock( &dht->lock );
}

void
kc_dhtUnlock( kc_dht * dht )
{
    pthread_mutex_unlock( &dht->lock );
}

void *
dhtPulse( void * arg )
{
    kc_dht * dht = arg;
    
    while( dht->bucketSize != 0 )
    {
        int i;
        for( i = 0; i < 128; i++ )
        {
            dhtBucket   * bucket = dht->buckets[i];
            if( time( NULL) - bucket->lastChanged > KADC_REFRESH_DELAY )
            {
                dhtBucketLock( bucket );
                
                /* FIXME: Need to refresh a bucket here */
                
                dhtBucketUnlock( bucket );
            }
        }
        
//        kc_dhtLock( dht );
        
//        kc_dhtUnlock( dht );
        sleep( 5 );
    }
    
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

int dhtRemoveNode( const kc_dht * dht, int128 hash )
{
    assert( dht != NULL );
    
    int logDist = int128xorlog( dht->us->nodeID, hash );
    if( logDist < 0 )
    {
//        kc_logPrint( KADC_LOG_DEBUG, "Trying to add our own node. Ignoring..." );
        return -1;
    }
    
    /* Get this node's bucket */
    dhtBucket     * bucket = dht->buckets[logDist];
    kc_dhtNode    * node;
    
    // Get the node corresponding to hash
            
    dhtBucketLock( bucket );
    
    RbtIterator first = rbtFind( bucket->nodes, hash );
    if( first == NULL )
    {
        dhtBucketUnlock( bucket );
        return -1;
    }
    rbtKeyValue( bucket->nodes, first, NULL, (void**)&node );
    if( node == NULL )
    {
        dhtBucketUnlock( bucket );
        return -1;
    }
    /* We remove it */
    rbtErase( bucket->nodes, first );
    dhtNodeFree( node );
    bucket->availableSlots++;
    
    dhtBucketUnlock( bucket );
    
    return 0;
}

int
kc_dhtAddNode( const kc_dht * dht, in_addr_t addr, in_port_t port, int128 hash )
{
    assert( dht != NULL );
    
    int logDist = int128xorlog( dht->us->nodeID, hash );
    if( logDist < 0 )
    {
        kc_logPrint( KADC_LOG_DEBUG, "Trying to add our own node. Ignoring..." );
        return -1;
    }
    
    /* Get this node's bucket */
    dhtBucket     * bucket = dht->buckets[logDist];
    kc_dhtNode    * node;
    
    RbtIterator nodeIter = rbtFind( bucket->nodes, hash );
    if( nodeIter != NULL )
    {
        dhtBucketLock( bucket );
        char msg[33];
        // This node is already in our bucket list, let's update it's info */
        kc_logPrint( KADC_LOG_DEBUG, "Node %s already in our bucket, updating...", int128sprintf( msg, hash ) );
        rbtKeyValue( bucket->nodes, nodeIter, NULL, (void**)&node );
        node->addr = addr;
        node->port = port;
        node->lastSeen = time(NULL);
        /* FIXME: handle node type */
        //        node->type = 0;
        dhtBucketUnlock( bucket );
        return 1;
    }
    
    /* We don't have it, allocate one */
    node = dhtNodeInit( addr, port, hash );
    
    if( node == NULL )
    {
        kc_logPrint( KADC_LOG_ALERT, "kc_dhtAddNode: dhtNodeInit failed !");
        return -1;
    }
    
    kc_dhtNode    * oldNode;
    
    if( bucket->availableSlots == 0 )
    {
        // This bucket is full, get the first node in this bucket, (the least-heard-of one)
        /* Here we need to ping it to make sure it's down, then if it replies we move up
         * in the bucket until one of them fails to reply...
         * - If one fails, we remove it and add the new one at the end
         * - If they all replied, we store it in our "backup nodes" list 
         */
        /* FIXME: Right now, we just consider the first one didn't replied */
        
        dhtBucketLock( bucket );
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
    bucket->lastChanged = time( NULL );
    rbtInsert( bucket->nodes, hash, node );
    bucket->availableSlots--;
    
    
    dhtBucketUnlock( bucket );
    return 0;
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
    {
        kc_logPrint( KADC_LOG_NORMAL, "Bucket %d contains %d nodes :", i, dht->bucketSize - dht->buckets[i]->availableSlots );
        dhtPrintBucket( dht->buckets[i] );
        kc_logPrint( KADC_LOG_NORMAL, "" );
    }
}

int
kc_dhtNodeCount( const kc_dht *dht )
{
    int total = 0;
    int i;
    for( i = 0; i < 128; i++)
        total += dht->bucketSize - dht->buckets[i]->availableSlots;
    
    return abs( total );
}

const kc_dhtNode**
kc_dhtGetNode( const kc_dht * dht, int * nodeCount )
{
    assert( dht != NULL );
    assert( nodeCount != NULL );
    
    /* FIXME: I'm not really sure how to get a correct list of node here,
     * I'll get them in order, but they'll be sorted, so maybe it's bad
     */
    
    int count = kc_dhtNodeCount( dht );
    
    /* We return a bucket-worth of nodes, or our max number of nodes if we don't have enough */
    *nodeCount = ( count < *nodeCount ? count : dht->bucketSize );
    
    const kc_dhtNode ** nodes = calloc( *nodeCount, sizeof(kc_dhtNode) );

    int i = 0;
    int j;
    for( j = 0; j < 128; i++ )
    {
        dhtBucket * bucket = dht->buckets[i];
        
        RbtIterator iter;
        for( iter = rbtBegin( bucket->nodes );
             i < *nodeCount && iter != NULL;
             iter = rbtNext( bucket->nodes, iter ) )
        {
            rbtKeyValue( bucket->nodes, iter, NULL, (void**)&nodes[i++] );
        }
        
        if( i >= *nodeCount )
            break;
    }
    
    return nodes;
}

in_addr_t
kc_dhtGetOurIp( const kc_dht * dht )
{
    return kc_dhtNodeGetIp( dht->us );
}

in_port_t
kc_dhtGetOurPort( const kc_dht * dht )
{
    return kc_dhtNodeGetPort( dht->us );
}

int128
kc_dhtGetOurHash( const kc_dht * dht )
{
    return kc_dhtNodeGetHash( dht->us );
}

in_addr_t
kc_dhtNodeGetIp( const kc_dhtNode * node )
{
    return ntohl( node->addr );
}

in_port_t
kc_dhtNodeGetPort( const kc_dhtNode * node )
{
    return ntohs( node->port );
}

int128
kc_dhtNodeGetHash( const kc_dhtNode * node )
{
    return node->nodeID;
}

