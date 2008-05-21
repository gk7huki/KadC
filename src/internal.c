/*
 *  internal.c
 *  KadC
 *
 *  Created by Etienne on 13/02/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include "internal.h"

/* Management of the kbuckets/kspace table */
int
dhtNodeCmpSeen( const void *a, const void *b )
{
	const kc_dhtNode *pa = a;
	const kc_dhtNode *pb = b;
    
    /* TODO: Check I'm really sorting from least-recently seen to most-recently seen */
	if( pa->lastSeen != pb->lastSeen)
        return ( pa->lastSeen < pb->lastSeen ? -1 : 1 );
    
    return 0;
}

int
dhtNodeCmpHash( const void *a, const void *b )
{
	const kc_dhtNode *pa = a;
	const kc_dhtNode *pb = b;
    
	return kc_hashCmp( pa, pb );
}

kc_dhtNode *
dhtNodeInit( kc_contact * contact, const kc_hash * hash )
{
	kc_dhtNode *self = malloc( sizeof(kc_dhtNode) );
    if ( !self )
        return NULL;
    
    self->contact = contact;
    if( hash )
        self->hash = kc_hashDup( hash );    /* copy dereferenced data */
	self->lastSeen = 0;
    
	return self;
}

void
dhtNodeFree( kc_dhtNode *pkn )
{
    kc_hashFree( pkn->hash );
	free( pkn );
}


kc_contact *
kc_dhtNodeGetContact( const kc_dhtNode * node )
{
    return node->contact;
}

void
kc_dhtNodeSetContact( kc_dhtNode * node, kc_contact * contact )
{
    node->contact = contact;
}


kc_hash *
kc_dhtNodeGetHash( const kc_dhtNode * node )
{
    return node->hash;
}

void
kc_dhtNodeSetHash( kc_dhtNode * node, kc_hash * hash )
{
    kc_hashMove( node->hash, hash );
}

dhtBucket *
dhtBucketInit( int size )
{
	dhtBucket *pkb = malloc( sizeof(dhtBucket) );
	if(pkb == NULL)
    {
        kc_logError( "dhtBucketInit: malloc failed !" );
        return NULL;
    }
    
    /* if static initialization of recursive mutexes is available, use it;
     * otherwise, hope that dynamic initialization is available... */
    
    pthreadutils_mutex_init_recursive( &pkb->mutex );
    
    pkb->nodes = rbtNew( dhtNodeCmpSeen );
	pkb->availableSlots = size;
    
	return pkb;
}

void
dhtBucketLock( dhtBucket *pkb )
{
    pthread_mutex_lock( &pkb->mutex );
}

void
dhtBucketUnlock( dhtBucket *pkb )
{
    pthread_mutex_unlock( &pkb->mutex );
}

void
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

void dhtPrintBucket( const dhtBucket * bucket )
{
    RbtIterator iter = rbtBegin( bucket->nodes );
    if( iter != NULL )
    {
        do
        {
            kc_hash * key;
            kc_dhtNode * node;
            
            rbtKeyValue( bucket->nodes, iter, (void*)&key, (void*)&node );
            
            assert( key != NULL );
            assert( node != NULL );
            
            kc_logNormal( "%s at %s", hashtoa( key ), kc_contactPrint( node->contact ) );
            
        } while ( ( iter = rbtNext( bucket->nodes, iter ) ) );
    }
}
