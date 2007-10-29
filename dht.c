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
    in_addr_t       addr;       /* IP address of this node */
    in_port_t       port;       /* UDP port of this node */
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
    RbtHandle         * nodes;          /* red-black tree of nodes */
    dhtBucket         * buckets;        /* array of buckets */
    dhtNode           * us;             /* pointer to ourself */
    kc_dhtReadCallback  readCallback;   /* callback used when we need reading something */
    kc_dhtWriteCallback writeCallback;  /* callback used when we need to send something... */
    kc_udpIo          * io;
    
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
	if(pkb != NULL) {
        /* if static initialization of recursive mutexes is available, use it;
        otherwise, hope that dynamic initialization is available... */

//   		pthreadutils_mutex_init_recursive( &pkb->mutex );

		pkb->nodes = rbtNew( dhtNodeCmp );
		pkb->availableSlots = size;
	}
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
    
    dht->buckets = rbtNew( dhtNodeCmp );
    if ( dht->buckets == NULL )
    {
        free( dht );
        return NULL;
    }
    
    int128 hash = int128random();
    dht->us = dhtNodeInit( addr, port, hash );
    free( hash );
    
    dht->bucketSize = bucketMaxSize;
    
    dht->readCallback = readCallback;
    dht->writeCallback = writeCallback;
    
    rbtInsert( dht->buckets, dht->us->nodeID, dht->us );
    
    if ( ( pthread_mutex_init( &dht->lock, NULL ) != 0 ) )
    {
        kc_logPrint( KADC_LOG_DEBUG, "kc_dhtInit: mutex init failed\n" );
        free( dht );
        return NULL;
    }
    
    dht->io = kc_udpIoInit( addr, port, 1024, ioCallback, dht );
    if ( dht->io == NULL )
    {
        kc_logPrint( KADC_LOG_DEBUG, "kc_dhtInit: kc_udpIo failed\n" );
        rbtDelete( dht->buckets );
        pthread_mutex_destroy( &dht->lock );
        free( dht );
        return NULL;
    }
    
    pthread_create( &dht->thread, NULL, dhtPulse, dht );
    
    return dht;
}

void
kc_dhtFree( kc_dht * dht )
{
    kc_udpIoFree( dht->io );
    
    rbtDelete( dht->buckets );
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
    
    dhtNode * node = dhtNodeInit( addr, port, hash );
    
    if ( node != NULL )
        rbtInsert( dht->buckets, hash, node );
}

void
kc_dhtCreateNode( const kc_dht * dht, in_addr_t addr, in_port_t port)
{
    assert( dht != NULL);
    
    performPing( dht, addr, port );
}

in_addr_t
kc_dhtGetOurIp( const kc_dht * dht )
{
    return dht->us->addr;
}

in_port_t
kc_dhtGetOurPort( const kc_dht * dht )
{
    return dht->us->port;
}

int128
kc_dhtGetOurHash( const kc_dht * dht )
{
    return dht->us->nodeID;
}

#if 0
void setup_kba(KadEngine *pKE, int kbsize) {
	int i;
	for(i=0; i<128; i++) {
		pKE->kb[i] = (void *)kc_kBucketInit( kbsize );
	}
	pKE->kspace = rbtNew(int128cmp);
}

void destroy_kba(KadEngine *pKE) {
	int i;
	void *iter;
	RbtStatus rbt_status;

	pthread_mutex_lock(&pKE->mutex);

	/* destroy the kspace table */
	/* empty pKE->kspace; do not deallocate knodes, they will be later */
	for(;;) {
		if((iter = rbtBegin(pKE->kspace)) == NULL)
			break;
		rbtErase(pKE->kspace, iter);
	}
	rbt_status = rbtDelete(pKE->kspace);
	assert(rbt_status == RBT_STATUS_OK);

	for(i=0; i<128; i++) {
		kc_kBucketFree((kbucket *)pKE->kb[i]);
	}
	pthread_mutex_unlock(&pKE->mutex);
}

void dump_kba(KadEngine *pKE) {	/* dump kbucket array */
	int i;
	int estusers;
	kbucket **pkb = (kbucket **)pKE->kb;

#ifdef VERBOSE_DEBUG
	time_t now=time(NULL);
#endif

	kc_logPrint("-- pKE->kb[127..0] (in brackets: age in seconds):\n");
	for(i=127; i >= 0; i--) {
		int nodes_in_this_bucket = 0;
		void *iter = rbtBegin(pkb[i]->rbt);
		if(iter == NULL)
			continue;	/* skip empty buckets */
		kc_logPrint(">>%3d: ", i);
		pthread_mutex_lock(&pkb[i]->mutex); 	/* \\\\\\ LOCK \\\\\\ */
		for(; iter != NULL; iter = rbtNext(pkb[i]->rbt, iter)) {
			knode *pkn;
			pkn = rbtValue(rbt, iter);
			nodes_in_this_bucket++;
#ifdef VERBOSE_DEBUG
			kc_logPrint("%s:%d.%d(%ld) ", htoa(pkn->pn.ip), pkn->pn.port, pkn->pn.type, now - pkn->lastseen);
#endif
		}
		if(pkb[i]->avail > 2) {
			estusers = (1 << (128 - i)) * nodes_in_this_bucket;
			kc_logPrint("bucket %2d holds %2d nodes => estimated online users: %9d\n",
					i, nodes_in_this_bucket, estusers);
		} else {
			kc_logPrint("bucket %2d holds %2d nodes\n",
					i, nodes_in_this_bucket);
		}
		pthread_mutex_unlock(&pkb[i]->mutex);	/* ///// UNLOCK ///// */
	}

}

void dump_kspace(KadEngine *pKE) {	/* dump kspace rbt */
	void *iter;
	time_t now=time(NULL);

	kc_logPrint("-- pKE->kspace (in brackets: age in seconds):\n");
	pthread_mutex_lock(&pKE->mutex); 	/* \\\\\\ LOCK \\\\\\ */
	for(iter = rbtBegin(pKE->kspace); iter != NULL; iter = rbtNext(pKE->kspace, iter)) {
		knode *pkn = rbtValue(rbt, iter);
		int128 pknkey = rbtKey(rbt, iter);
		assert(pknkey == pkn->pn.hash);
		KadC_int128flog(stdout, pkn->pn.hash);
		kc_logPrint(" %s:%d (%ld) ", htoa(pkn->pn.ip), pkn->pn.port, now - pkn->lastSeen);
		kc_logPrint("\n");
	}
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK ///// */
}

/* Try and add peer to kbuckets+kspace (caller should check it's not there)
   return 0 if OK, 1 if kbucket full but peer valid, -1 otherwise */
static int
kc_kBucketAdd(peernode *ppn, KadEngine *pKE)
{
	kc_kNode   * pkn = NULL;		/* pointer to K-node */
	kc_kNode   * pkno = NULL;	/* pointer to old K-node */
//	kc_kBucket * pkb = NULL;	/* pointer to k-bucket */
    
	int previousavail;
	RbtStatus rbt_status;
	int retval = 0;

	/* We have an alive peer here. See if we can put it in its K-bucket. */

	pkn = kc_kNodeInit( ppn->ip, ppn->port, ppn->hash );
	if(pkn == NULL) {
		return -1;
	}
    
	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK KE \\\\\\ */
//	pkb = pKE->kb[pkn->bucketNum];

	pthread_mutex_lock(&pkb->mutex); /* \\\\\\ LOCK k-bucket \\\\\\ */

	previousavail = pkb->avail;

	if(pkb->avail == 0) {	/* if k-bucket full... */
		/* see if oldest peer replies: if not, evict it */
		void *iter = rbtBegin(pkb->rbt);	/* get oldest */
		assert(iter != NULL);				/* if full can't be empty!! */
		pkno = rbtValue(pkb->rbt, iter);				/* save data by pointing it with pkno */
		rbt_status = rbtErase(pkb->rbt, iter);	/* remove provisionally from k-bucket... */
		assert(rbt_status == RBT_STATUS_OK);
		rbt_status = rbtEraseKey(pKE->kspace, pkno->pn.hash);	/* ...and from k-space as well */
		assert(rbt_status == RBT_STATUS_OK);
		pkb->avail++;
	}

	/* now pkn points to the new Knode, pkno to the oldest (or NULL if it wasn't full) */

	if(pkno != NULL) {
		/* Petar says to preserve old nodes when possible, but we just kick'em out
		To be nice would require being able to ping the old node here,
		but that's awkward so we don't do that now. Maybe later... */
#ifdef VERBOSE_DEBUG
		kc_logPrint("evicting old peer %s:%d (type %d), last seen %d s ago\n",
			htoa(pkno->pn.ip), pkno->pn.port, pkno->pn.type, time(NULL) - pkno->lastseen);
#endif
        kc_kNodeFree( pkno ); /* throw away unresponsive old knode */
		pkno = NULL;
	}

	if(pkno != NULL) {	/* it means old knode is not to be killed, yet */
		rbt_status = rbtInsert(pkb->rbt, pkno, pkno);	/* put it back */
		assert(rbt_status == RBT_STATUS_OK);
		rbt_status = rbtInsert(pKE->kspace, pkno->pn.hash, pkno);
		assert(rbt_status == RBT_STATUS_OK);	/* we just removed it, can't be there! */
		pkb->avail--;
#ifdef VERBOSE_DEBUG
		kc_logPrint("kept old peer %s:%d\n", htoa(pkno->pn.ip), pkno->pn.port);
#endif
		free(pkn);	/* and throw away new node (maybe we should keep it aside for booting purposes...)*/
		retval = 1;
	} else {
        void *iter1;
		kc_kNode *pkn1 = NULL;

		pkn->lastSeen = time(NULL);	/* set pkn's age as "seen now" */
		pkn->pn.type = 0;			/* hey, it just replied after all */

		/* if a knode with that same hash already there, remove it */
        iter1 = rbtFind(pKE->kspace, pkn->pn.hash);
		if(iter1 != NULL) {
			void *iter2 = NULL;
			pkn1 = rbtValue(pKE->kspace, iter1);
			/* now look for it scanning its K-bucket */
			for(iter2=rbtBegin(pkb->rbt); iter2 != NULL; iter2 = rbtNext(pkb->rbt, iter2)) {
				if(rbtValue(pkb->rbt, iter2) == pkn1) {
					rbtErase(pkb->rbt, iter2); /* remove from k-bucket */
					break;
				}
			}
			assert(iter2 != NULL);	/* must have found it in kbucket! */
			rbtErase(pKE->kspace, iter1); /* remove also from k-space */
#ifdef VERBOSE_DEBUG
		kc_logPrint("removed peer %s:%d with same hash as new\n", htoa(pkn1->pn.ip), pkn1->pn.port);
#endif
			free(pkn1);	/* destroy it as well */
			pkb->avail++;
		}
		/* now add new node */
		rbt_status = rbtInsert(pKE->kspace, pkn->pn.hash, pkn);
		assert(rbt_status == RBT_STATUS_OK);	/* we just removed it, can't be there! */
		rbt_status = rbtInsert(pkb->rbt, pkn, pkn);
		assert(rbt_status == RBT_STATUS_OK);
		pkb->avail--;
#ifdef VERBOSE_DEBUG
		kc_logPrint("added new peer %s:%d\n", htoa(pkn->pn.ip), pkn->pn.port);
#endif
		retval = 0;
	}

	pthread_mutex_unlock(&pkb->mutex); /* ////// UNLOCK k-bucket ////// */
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
	return retval;
}

/* Seek a peernode in kspace and kbuckets.
   If not in kbuckets and kspace,
   			if(isalive) try to add it.

   If it's already there:
   			If isalive reset type to 0;
   			If ! isalive , increment type;
   			If type >= NONRESPONSE_THRESHOLD, remove the corresponding knode from both tables and free it

   In any case, ppn is left alone (not free'd).
   Returns:

   	-1 our own node or non-routable address: nothing done

	if isalive:
	 0 added or left there
     1 could no be added because bucket full

   	if ! isalive:
   	 0 it was, and was left there, or was not there already
   	 1 it was there, but it was removed due to type >= NONRESPONSE_THRESHOLD

 */
int UpdateNodeStatus(peernode *ppn, KadEngine *pKE, int isalive) {
	kc_kNode *pkn;		/* pointer to K-node in kspace */
	kc_kNode *pknb;	/* pointer to K-node in kbucket */
//	kc_kBucket *pkb;	/* pointer to k-bucket */
	void *iterks;
	void *iterkb;
	RbtStatus rbt_status;
	int removed = 0;
	int bucketnum;

	bucketnum = int128xorlog(ppn->hash, pKE->localnode.hash);
	if(bucketnum < 0 ||
		(ppn->ip == pKE->localnode.ip && ppn->port == pKE->localnode.port)) {
#ifdef VERBOSE_DEBUG
		kc_logPrint("not adding our own peer\n");
#endif
		return -1;
	}

	if(isnonroutable(ppn->ip) || ppn->ip == pKE->extip || ppn->ip == pKE->localnode.ip) {
#ifdef VERBOSE_DEBUG
		kc_logPrint("not adding peer with nonroutable or our int/ext IP address\n");
#endif
		return -1;
	}

	pkb =  pKE->kb[bucketnum];
	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK KE \\\\\\ */

	iterks = rbtFind(pKE->kspace, ppn->hash);
	if(iterks == NULL) {	/* not found. Quite normal, for new nodes */
		pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
		/* In this case, if(isalive) we try to add the node */
		if(isalive)
			return kc_kBucketAdd( ppn, pKE );
		else
			return 0;	/* not there and no need to add it */
	}

	assert(iterks != NULL);
	pkn = rbtValue(pKE->kspace, iterks);
	assert(pkn != NULL);	/* must be there! */

	if(isalive)
		pkn->pn.type = 0;
	else
		pkn->pn.type++;

	if(pkn->pn.type >= NONRESPONSE_THRESHOLD) {
		/* remove from kspace */
		rbt_status = rbtErase(pKE->kspace, iterks);
		assert(rbt_status == RBT_STATUS_OK);

		/* now remove from k-bucket as well */
		pthread_mutex_lock(&pkb->mutex); /* \\\\\\ LOCK k-bucket \\\\\\ */
		for(pknb = NULL, iterkb=rbtBegin(pkb->rbt); iterkb != NULL; iterkb = rbtNext(pkb->rbt, iterkb)) {
			pknb = rbtValue(pkb->rbt, iterkb);
			if(memcmp(pknb->pn.hash, ppn->hash, 16) == 0)
				break;
		}
		assert(iterkb != NULL);	/* can't be in kspace and not in k-bucket */
		assert(pknb == pkn);	/* both kbucket and kspace entries point to the same object */
		rbtErase(pkb->rbt, iterkb); /* remove from k-bucket */
		pkb->avail++;
		pthread_mutex_unlock(&pkb->mutex); /* ////// UNLOCK k-bucket ////// */


		/* free the knode */
		free(pkn);

		removed = 1;
#ifdef VERBOSE_DEBUG
		kc_logPrint("UpdateNodeStatus evicted unresponsive old peer %s:%d (type %d) last seen %d s ago\n",
			htoa(pkn->pn.ip), pkn->pn.port, pkn->pn.type,  time(NULL) - pkn->lastseen);
#endif
	}
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
	return removed;
}

int knodes_count(KadEngine *pKE) {
	int nknodes;
	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK KE \\\\\\ */
	nknodes = rbtSize(pKE->kspace);
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
	return nknodes;
}

int erase_knodes(KadEngine *pKE) {
	int i;
	void *iter;
	int nknodes = 0;

	pthread_mutex_lock(&pKE->mutex);

	/* empty pKE->kspace; do not deallocate knodes, they will be later */
	for(;;) {
		if((iter = rbtBegin(pKE->kspace)) == NULL)
			break;
		rbtErase(pKE->kspace, iter);
	}

	for(i=0; i<128; i++) {
		kc_kBucket *pkb = pKE->kb[i];

		pthread_mutex_lock(&pkb->mutex);
		/* empty pkb->rbt */
		for(;;) {
			kc_kNode *pkn;
			iter = rbtBegin(pkb->rbt);
			if(iter == NULL)
				break;
			pkn = rbtValue(pkb->rbt, iter);
			rbtErase(pkb->rbt, iter); /* ?? */
			kc_kNodeFree( pkn );	/* deallocate knode */
			pkb->avail++;
			nknodes++;
		}
		pthread_mutex_unlock(&pkb->mutex);
	}
	pthread_mutex_unlock(&pKE->mutex);
	return nknodes;
}
#endif

void
kc_dhtPrintTree( const kc_dht * dht )
{
    RbtIterator iter = rbtBegin( dht->buckets );
    
    do
    {
        int128  key;
        dhtNode * value;
        char    buf[33];
        
        rbtKeyValue( dht->buckets, iter, (void*)&key, (void*)&value );
        
        assert( key != NULL );
        assert( value != NULL );
        
        struct in_addr ad;
        ad.s_addr = value->addr;
        kc_logPrint( KADC_LOG_NORMAL, "%s:%d, keyed %s\n", inet_ntoa( ad ), ntohs( value->port ), int128sprintf( buf, key ) );
        
    } while ( ( iter = rbtNext( dht->buckets, iter ) ) );
}