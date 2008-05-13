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
    char * errStr = calloc( 15, sizeof(char) );
    if( what & EVBUFFER_READ )
        sprintf( errStr, "%s", "Read" );
    if( what & EVBUFFER_WRITE )
        sprintf( errStr, "%s", "Write" );
    if( what & EVBUFFER_EOF )
        sprintf( errStr, "%s %s", errStr, "EOF" );
    if( what & EVBUFFER_ERROR )
        sprintf( errStr, "%s %s", errStr, "error" );
    if( what & EVBUFFER_TIMEOUT )
        sprintf( errStr, "%s %s", errStr, "timeout" );
    
    kc_logError( "%s for contact %s", errStr, kc_contactPrint( session->contact ) );
    free( errStr );
}

dhtSession *
dhtSessionInit( const kc_dht * dht, kc_contact * connectContact, kc_messageType type,
               int incoming, kc_sessionCallback callback )
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

    self->socket = -1;
    self->bufferEvent = NULL;
    
    kc_logVerbose( "Successfully inited session %p to %s", self, kc_contactPrint( connectContact ) );
    
    return self;
}

void
dhtSessionFree( dhtSession * session )
{
    if( session->bufferEvent != NULL )
        bufferevent_free( session->bufferEvent );
    if( session->socket != -1 )
        kc_netClose( session->socket );
    
    free( session );
}

int
dhtSessionStart( dhtSession * session, kc_dht * dht )
{
    dhtIdentity * id = kc_dhtIdentityForContact( dht, session->contact );
    kc_contact * bindContact = id->us;
    
    int contactType = kc_contactGetType( session->contact );
    
    session->socket = kc_netOpen( contactType, SOCK_DGRAM );
    if( session->socket == -1 )
    {
        kc_logError( "Unable to open socket to %s", kc_contactPrint( session->contact ) );
        return 1;
    }
    
    int status;
    status = kc_netBind( session->socket, bindContact );
    if( status == -1 )
    {
        kc_logError( "Unable to bind socket" );
        return 1;
    }
    
    status = kc_netConnect( session->socket, session->contact );
    if( status == -1 )
    {
        kc_logError( "Unable to connect socket" );
        return 1;
    }
    
    session->bufferEvent = bufferevent_new( session->socket, sessionReadCB, sessionWriteCB, sessionErrorCB, session );
    if( session->bufferEvent == NULL )
    {
        kc_logError( "Failed creating event buffer" );
        return 1;
    }
    if( bufferevent_base_set( dht->eventBase, session->bufferEvent ) != 0 )
    {
        kc_logError( "Failed setting event buffer base" );
        return 1;
    }
    bufferevent_settimeout( session->bufferEvent, dht->parameters->sessionTimeout, dht->parameters->sessionTimeout );
    
    if( bufferevent_enable( session->bufferEvent, EV_READ | EV_WRITE ) != 0 )
    {
        kc_logError( "Failed enabling event buffer" );
        return 1;
    }
    return 0;
}

void
evbufferCB(struct evbuffer * evbuf, size_t oldSize, size_t newSize, void * arg )
{
    kc_logVerbose( "evbufferCB: buffer %p modified %d -> %d", evbuf, oldSize, newSize );
}

int
dhtSessionSend( dhtSession * session, kc_message * message )
{
    assert( session != NULL );
    assert( message != NULL );
    
    int status;
    
    kc_logVerbose( "dhtSessionSend: Creating a new evbuffer" );
    struct evbuffer * evbuf = evbuffer_new();
    evbuffer_setcb( evbuf, evbufferCB, NULL );
    
    kc_logVerbose( "dhtSessionSend: converting kc_message to evbuffer" );
    status = evbuffer_add( evbuf, kc_messageGetPayload( message ), kc_messageGetSize( message ) );
    if( status != 0 )
    {
        kc_logAlert( "Failed putting message data in evbuffer for message, err %d", status );
        return -1;
    }
    
    kc_logVerbose( "dhtSessionSend: Writing evbuffer in session bufferEvent" );
    status = bufferevent_write_buffer( session->bufferEvent, evbuf );
    if( status != 0 )
    {
        kc_logAlert( "Failed writing evbuffer to buffer event, err %d", status );
        return -1;
    }
    evbuffer_free( evbuf );
    
    return 0;
}
