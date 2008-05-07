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

int
dhtSessionCmp( const void *a, const void *b )
{
	const dhtSession *pa = a;
	const dhtSession *pb = b;
    
    int contactCmp = kc_contactCmp( pa->contact, pb->contact );
	if( contactCmp != 0 )
        return contactCmp;
    
    if( pa->incoming != pb->incoming )
        return ( pa->incoming < pb->incoming ? -1 : 1 );
    
    if( pa->type != pb->type )
        return ( pa->type < pb->type ? -1 : 1 );
    
    return 0;
}

void sessionReadCB( struct bufferevent * event, void * arg )
{
    dhtSession * session = arg;
    kc_logError( "Session read for contact: %s", kc_contactPrint( session->contact ) );
}

void sessionWriteCB( struct bufferevent * event, void * arg )
{
    dhtSession * session = arg;
    kc_logError( "Session write for contact: %s", kc_contactPrint( session->contact ) );
}

void sessionErrorCB( struct bufferevent * event, short what, void * arg )
{
    dhtSession * session = arg;
    kc_logError( "Session error for contact %s: %d", kc_contactPrint( session->contact ), what );
}

dhtSession *
dhtSessionInit( const kc_dht * dht, kc_contact * bindContact, kc_contact * connectContact,
                 kc_messageType type, int incoming, kc_sessionCallback callback, int sessionTimeout )
{
    dhtSession * self = malloc( sizeof(dhtSession) );
    if( self == NULL )
    {
        kc_logError( "dhtSessionInit: Failed malloc()ing" );
        return NULL;
    }
    
    self->contact = connectContact;
    self->type = type;
    self->incoming = incoming;
    self->callback = callback;
    
    int contactType = kc_contactGetType( connectContact );
    
    self->socket = kc_netOpen( contactType, SOCK_DGRAM );
    if( self->socket == -1 )
    {
        kc_logError( "Unable to open socket to %s", kc_contactPrint( connectContact ) );
        free( self );
        return NULL;
    }
    
    int status;
    status = kc_netBind( self->socket, bindContact );
    if( status == -1 )
    {
        kc_logError( "Unable to bind socket" );
        free( self );
        return NULL;
    }
    
    status = kc_netConnect( self->socket, connectContact );
    if( status == -1 )
    {
        kc_logError( "Unable to connect socket" );
        free( self );
        return NULL;
    }
    
    self->bufferEvent = bufferevent_new( self->socket, sessionReadCB, sessionWriteCB, sessionErrorCB, self );
    if( self->bufferEvent == NULL )
    {
        kc_logError( "Failed creating event buffer" );
        free( self );
        return NULL;
    }
    if( bufferevent_base_set( dht->eventBase, self->bufferEvent ) != 0 )
    {
        kc_logError( "Failed setting event buffer base" );
        free( self );
        return NULL;
    }
    bufferevent_settimeout( self->bufferEvent, dht->parameters->sessionTimeout, dht->parameters->sessionTimeout );
    bufferevent_enable( self->bufferEvent, EV_READ | EV_WRITE );
    
    return self;
}

void
dhtSessionFree( dhtSession * session )
{
    bufferevent_free( session->bufferEvent );
    free( session );
}
