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

/* Those are defaults for normal behavior, as per spec */
/* Setting one of the kc_parameters values to 0 will make the DHT use the default */

#define KADC_EXPIRATION_DELAY   86410   /* in s, the ttl of a node */
#define KADC_REFRESH_DELAY      3600    /* in s, the delay after which a bucket must be refreshed */
#define KADC_REPLICATION_DELAY  3600    /* in s, interval between Kademlia replication events */
#define KADC_REPUBLISH_DELAY    86400   /* in s, the delay after which a key/value must be republished */

#define KADC_PROBE_PARALLELISM  3       /* The number of concurrent probes agains the dht, 
                                         * alpha in Kademlia terminology */
#define KADC_PROBE_DELAY        5       /* The delay to wait between each alpha probes */

#define MESSAGE_QUEUE_SIZE      400     /* Maximum number of queued messages in a session */
#define MAX_SESSION_COUNT       128     /* Maximum number of concurrent "connections" */
#define SESSION_TIMEOUT         10      /* in s, the ttl of a session */
#define MAX_MESSAGE_PER_PULSE   0       /* Unused */

#define BUCKET_COUNT            128     /* Our bucket count */

#include <event.h>
#include "internal.h"

#pragma mark Events

void dhtReplicate( int fd, short event_type, void * arg )
{
    kc_logVerbose( "Replicate time!" );
}

void readcb( struct bufferevent * event, void * arg )
{
    kc_logDebug( "Got read event" );
}

void writecb( struct bufferevent * event, void * arg )
{
    kc_logDebug( "Got write event" );
}

void errorcb( struct bufferevent * event, short what, void * arg )
{
    kc_logAlert( "Got libevent error: %d", what );
}

static void
kc_dhtLock( kc_dht * dht )
{
    pthread_mutex_lock( &dht->lock );
}

static void
kc_dhtUnlock( kc_dht * dht )
{
    pthread_mutex_unlock( &dht->lock );
}

void
eventLog( int severity, const char *msg );

void *
eventLoop( void * arg );

kc_dht*
kc_dhtInit( kc_hash * hash, kc_dhtParameters * parameters )
{
    assert( parameters != NULL );
    
    kc_logVerbose( "kc_dhtInit: dht init" );
    kc_dht * dht = malloc( sizeof( kc_dht ) );
    if ( dht == NULL )
    {
		kc_logError( "kc_dhtInit: malloc failed!");
        return NULL;
    }
    
    kc_logVerbose( "kc_dhtInit: parameters init" );
    /* Initialize our parameters to defaults if unspecified */
    dht->parameters = malloc( sizeof(kc_dhtParameters) );
    if( dht->parameters == NULL )
    {
        kc_logError( "kc_dhtInit: parameter malloc failed" );
        free( dht );
        return NULL;
    }
    
    kc_logVerbose( "kc_dhtInit: parameters copy" );
    memcpy( dht->parameters, parameters, sizeof(kc_dhtParameters) );
    /* Mandatory parameters */
    if ( dht->parameters->hashSize == 0 ||
         dht->parameters->bucketSize == 0 ||
         dht->parameters->callbacks.parseCallback == NULL || 
         dht->parameters->callbacks.readCallback == NULL ||
         dht->parameters->callbacks.writeCallback == NULL )
    {
        kc_logError( "Missing mandatory parameters in passed parameter structure" );
        kc_dhtFree( dht );
        return NULL;
    }
    
    kc_logVerbose( "kc_dhtInit: parameters defaulting" );
#define setToDefault( x, y ) dht->parameters->x = ( dht->parameters->x != 0 ? dht->parameters->x : y )
    setToDefault( expirationDelay, KADC_EXPIRATION_DELAY );
    setToDefault( refreshDelay, KADC_REFRESH_DELAY );
    setToDefault( replicationDelay, KADC_REPLICATION_DELAY );
    setToDefault( republishDelay, KADC_REPUBLISH_DELAY );
    
    setToDefault( lookupParallelism, KADC_PROBE_PARALLELISM );
//    setToDefault( lookupDelay, KADC_PROBE_DELAY );
    
    setToDefault( maxQueuedMessages, MESSAGE_QUEUE_SIZE );
    setToDefault( maxSessionCount, MAX_SESSION_COUNT );
    setToDefault( maxMessagesPerPulse, MAX_MESSAGE_PER_PULSE );
    setToDefault( sessionTimeout, SESSION_TIMEOUT );
#undef setToDefault
    
    kc_logVerbose( "kc_dhtInit: mutex init" );
    if ( ( pthread_mutex_init( &dht->lock, NULL ) != 0 ) )
    {
        kc_logAlert( "kc_dhtInit: mutex init failed" );
        kc_dhtFree( dht );
        return NULL;
    }
    
    kc_logVerbose( "kc_dhtInit: event base init" );
    dht->eventBase = event_init();
    if( dht->eventBase == NULL )
    {
        kc_logError( "kc_dhtInit: libevent initialization failed" );
        kc_dhtFree( dht );
        return NULL;
    }
    
    event_set_log_callback( eventLog );
    
    kc_logVerbose( "kc_dhtInit: hash init" );
    if( hash == NULL )
        dht->hash = kc_hashRandom( parameters->hashSize );
    else
    {
        if( kc_hashLength( hash ) != parameters->hashSize )
        {
            kc_logError( "Passed an %d-bit hash while parameters asks an %d-bit hash", kc_hashLength( hash ), parameters->hashSize );
            kc_dhtFree( dht );
            return NULL;
        }
        dht->hash = kc_hashDup( hash );
    }
    
    kc_logVerbose( "kc_dhtInit: identities init" );
    dht->identities = malloc( sizeof(dhtIdentity*) );
    *dht->identities = NULL;
    
    kc_logVerbose( "kc_dhtInit: keys init" );
    dht->keys = rbtNew( kc_hashCmp );
    if( dht->keys == NULL )
    {
        kc_logAlert( "kc_dhtInit: failed creating Keys RBT" );
        kc_dhtFree( dht );
        return NULL;
    }
    
    kc_logVerbose( "kc_dhtInit: sessions init" );
    dht->sessions = rbtNew( dhtSessionCmp );
    if( dht->sessions == NULL )
    {
        kc_logAlert( "kc_dhtInit: failed creating Sessions RBT" );
        kc_dhtFree( dht );
        return NULL;
    }
    
    kc_logVerbose( "kc_dhtInit: buckets init" );
    dht->buckets = calloc( sizeof(dhtBucket*), BUCKET_COUNT );
    int i;
    for( i = 0; i < BUCKET_COUNT; i++ )
    {
        dht->buckets[i] = dhtBucketInit( parameters->bucketSize );
        if ( dht->buckets[i] == NULL )
        {
            kc_logAlert( "kc_dhtInit: bucket init failed" );
            kc_dhtFree( dht );
            return NULL;
        }
    }
    
    kc_logVerbose( "kc_dhtInit: replication timer init" );
    dht->replicationTimer = malloc( sizeof(struct event) );
    if( dht->replicationTimer == NULL ) {
        kc_logAlert( "kc_dhtInit: failed creating Replication Timer" );
        kc_dhtFree( dht );
        return NULL;
    }
    struct timeval tv;
    tv.tv_sec = dht->parameters->replicationDelay;
    tv.tv_usec = 0;
    event_set( dht->replicationTimer, -1, EV_TIMEOUT | EV_PERSIST, dhtReplicate, dht );
    event_base_set( dht->eventBase, dht->replicationTimer );
    if( evtimer_add( dht->replicationTimer, &tv ) != 0)
    {
        kc_logAlert( "kc_dhtInit: failed adding replication timer" );
        kc_dhtFree( dht );
        return NULL;
    }
    
    kc_logVerbose( "kc_dhtInit: pthread init" );
    pthread_create( &dht->eventThread, NULL, eventLoop, dht );
    if( dht->eventThread == NULL )
    {
        kc_logAlert( "kc_dhtInit: thread init failed" );
        kc_dhtFree( dht );
        return NULL;
    }
    
    return dht;
}

void
kc_dhtFree( kc_dht * dht )
{
    /* We stop the background thread */

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    event_base_loopexit( dht->eventBase, &tv );
    
    if( dht->eventThread != NULL )
        pthread_join( dht->eventThread, NULL );
    
    event_base_free( dht->eventBase );
    
    if( dht->identities != NULL )
    {
        dhtIdentity * identity;
        for( identity = *dht->identities; identity != NULL; identity++ )
        {
            dhtIdentityFree( identity );
        }
        free( dht->identities );
    }
    
    if( dht->sessions != NULL )
        rbtDelete( dht->sessions );
    if( dht->keys != NULL )
        rbtDelete( dht->keys );
    
    if( dht->buckets != NULL )
    {
        int i;
        for( i = 0; i < BUCKET_COUNT; i++ )
            dhtBucketFree( dht->buckets[i] );
        free( dht->buckets );
    }
    
    if( dht->parameters )
        free( dht->parameters );
    if( dht->hash )
        kc_hashFree( dht->hash );
    if( &dht->lock )
        pthread_mutex_destroy( &dht->lock );
    
    free( dht );
}

void
eventLog( int severity, const char *msg )
{
    kc_logLevel lvl;
    if( severity == _EVENT_LOG_ERR )
        lvl = KADC_LOG_ERROR;
    else
        lvl = KADC_LOG_DEBUG;
    kc_logPrint( lvl, msg );
}

void *
eventLoop( void * arg )
{
    kc_dht * dht = arg;
    kc_logVerbose( "eventLoop: started with DHT %p", arg );
    
    while( 1 )
    {
        if( dht->eventBase == NULL )
        {
            break;
        }
        
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        event_base_loopexit( dht->eventBase, &tv );
        int status = event_base_loop( dht->eventBase, 0 );
        if( status != 0 )
        {
            kc_logAlert( "eventLoop: exiting with error %d", status );
            return (void*)1;
        }
//        kc_logVerbose( "eventLoop: looping!" );
    }
    kc_logVerbose( "eventLoop: exiting" );
    return 0;
}

int
kc_dhtAddIdentity( kc_dht * dht, kc_contact * contact )
{
    assert( dht != NULL );
    assert( contact != NULL );
    
    kc_logVerbose( "Adding identity %s to DHT %p", kc_contactPrint( contact ), dht );
    
    int type = kc_contactGetType( contact );
    kc_contact * existingContact = kc_dhtGetOurContact( dht, type );
    if( existingContact != NULL )
    {
        kc_logAlert( "We already use an identity for contact type %d, ignoring...", type );
        return -1;
    }
    
    /* Let's create a new identity */
    dhtIdentity * identity = dhtIdentityInit( dht, contact );
    
    if( identity == NULL )
    {
        kc_logAlert( "Failed creating new identity %s", kc_contactPrint( contact ) );
        return -1;
    }
    
    kc_dhtLock( dht );
    
    int identityCount;
    dhtIdentity ** lastIdentity;
    for( identityCount = 0, lastIdentity = dht->identities; *lastIdentity != NULL; identityCount++, lastIdentity++ )
        /* We loop until we get the NULL sentinel */
        ;

    *lastIdentity = identity;
    
    void *tmp;
    
    if( ( tmp = realloc( dht->identities, sizeof(kc_contact*) * ( identityCount + 1 ) ) ) == NULL )
    {
        kc_logAlert( "Failed realloc()ating identities array !" );
        kc_dhtUnlock( dht );
        return -1;
    }
    dht->identities = tmp;
     
    dht->identities[identityCount + 1] = NULL;
    
    kc_dhtUnlock( dht );
    return 0;
}

static dhtBucket *
dhtBucketForHash( const kc_dht * dht, const kc_hash * hash )
{
    int logDist = kc_hashXorlog( dht->hash, hash );
    if( logDist <= 0 )
        return NULL;
    
    /* Get this node's bucket */
    return dht->buckets[logDist];
    
}

static kc_dhtNode *
dhtNodeForContact( const kc_dht * dht, const kc_contact * contact )
{
    /* TODO: Make this work */
    return NULL;
}

dhtIdentity *
kc_dhtIdentityForContact( const kc_dht * dht,  kc_contact * contact )
{
    dhtIdentity * identity;
    for( identity = *dht->identities; identity != NULL; identity++ )
    {
        if( kc_contactGetType( identity->us ) == kc_contactGetType( contact ) )
        {
            return identity;
        }
    }
    return NULL;
}

static dhtSession *
dhtSessionForMsg( const kc_dht * dht, kc_message * msg )
{
    RbtIterator sessIter;
    for( sessIter = rbtBegin( dht->sessions ); sessIter != NULL; sessIter = rbtNext( dht->sessions, sessIter ) )
    {
        dhtSession * session;
        kc_dhtNode * node;
        rbtKeyValue( dht->sessions, sessIter, (void*)&session, NULL );
        
        if( kc_contactCmp( node->contact, kc_messageGetContact( msg ) ) && session->type == kc_messageGetType( msg ) )
        {
            return session;
        }
    }
    return NULL;
}

static kc_dhtNode *
dhtNodeForHash( const kc_dht * dht, kc_hash * hash )
{
    int logDist = kc_hashXorlog( dht->hash, hash );
    if( logDist < 0 )
    {
        return NULL;
    }
    
    /* Get this node's bucket */
    dhtBucket     * bucket = dht->buckets[logDist];
    kc_dhtNode    * node;
    
    RbtIterator nodeIter = rbtFind( bucket->nodes, hash );
    if( nodeIter == NULL )
        return NULL;
    
    rbtKeyValue( bucket->nodes, nodeIter, NULL, (void**)&node );
    return node;
}

static int
asyncCallback( const kc_dht * dht, const kc_message * msg )
{
    assert( dht != NULL );
    assert( msg != NULL );
    
    switch( kc_messageGetType( msg ) )
    {
        case DHT_RPC_PING:
        {
            kc_dhtNode * node = dhtNodeForContact( dht, kc_messageGetContact( msg ) );
            dhtBucket * bucket = dhtBucketForHash( dht, node->hash );
            bucket->lastChanged = time( NULL );
            node->lastSeen = time( NULL );
            return 1;
            break;
        }
            
        default:
            break;
    }
    return -1;
}

static int
dhtSendMessage( kc_dht * dht, kc_messageType type, kc_contact * contact )
{
    int status;
    assert( dht != NULL );
    assert( contact != NULL );
    
    dhtSession * session = dhtSessionInit( dht, contact, type, 0, asyncCallback );
    
    kc_dhtLock( dht );
    RbtIterator sessIter = rbtFind( dht->sessions, session );
    if( sessIter == NULL )
    {
        kc_logVerbose( "Session not found in DHT, inserting..." );
        status = rbtInsert( dht->sessions, session, NULL );
        if( status != RBT_STATUS_OK )
        {
            kc_logAlert( "Failed inserting session for outgoing message, err %d", status );
            if( status == RBT_STATUS_DUPLICATE_KEY )
                kc_logVerbose( "err 2 is duplicate key" );
            kc_dhtUnlock( dht );
            return -1;
        }
        if( dhtSessionStart( session, dht ) != 0 )
        {
            kc_logAlert( "Failed starting session" );
            kc_dhtUnlock( dht );
            return -1;
        }
    }
    else
    {
        kc_logVerbose( "A session was found..." );
        dhtSessionFree( session );
        rbtKeyValue( dht->sessions, sessIter, (void*)&session, NULL );
    }
    
    kc_message * answer = kc_messageInit( session->contact, session->type, 0, NULL );
    
    kc_logVerbose( "dhtSendMessage: Writing message type: %d", session->type );
    status = dht->parameters->callbacks.writeCallback( dht, NULL, answer );
    if( status )
    {
        kc_logAlert( "Failed writing message type %d, err %d", session->type, status );
        kc_dhtUnlock( dht );
        return -1;
    }
    
    status = dhtSessionSend( session, answer );
    if( status )
    {
        kc_logAlert( "Failed sending message type %d, err %d", session->type, status );
        kc_dhtUnlock( dht );
        return -1;
    }
    
    kc_dhtUnlock( dht );
    
    if( session->callback == NULL )
    {
#if 0
        kc_message * answer = kc_queueDequeueTimeout( session->queue, SESSION_TIMEOUT );
        if( answer == NULL )
        {
            kc_logAlert( "Timeout" );
            return 1;
        }
        assert( kc_messageGetType( answer ) != type );
#endif
        kc_logAlert( "FIXME: Redo sync send" );
    }
    return 0;
}

static int
dhtPingByIP( kc_dht * dht, kc_contact * contact, kc_hash * hash, int sync )
{
    kc_dhtNode * node;
    int         status;
    
    kc_logNormal( "PING %s (%s) %s", hashtoa( hash ), kc_contactPrint( contact ), ( sync ? "synchronously" : "asynchronously" ) );
    status = dhtSendMessage( dht, DHT_RPC_PING, contact );    
    if( status != 0 )
    {
        kc_logAlert( "Failed to send message for message type DHT_RPC_PING: %d", status );
        free( node );
        return -1;
    }
    
    if( sync )
    {
        /* TODO: Is there something special I should do here ? Update maybe ? */
        return 0;
    }
    return 0;
}

static int
dhtPingByHash( kc_dht * dht, kc_hash * hash, int sync )
{
    assert( dht != NULL );
    
    int logDist = kc_hashXorlog( dht->hash, hash );
    if( logDist < 0 )
    {
        kc_logDebug( "Trying to ping our own node. Ignoring..." );
        return -1;
    }
    
    /* Get this node's bucket */
    dhtBucket     * bucket = dht->buckets[logDist];
    kc_dhtNode    * node;
    
    RbtIterator nodeIter = rbtFind( bucket->nodes, hash );
    if( nodeIter == NULL )
    {
        kc_logDebug( "Can't find a node with hash %s. Ignoring...", hashtoa( hash ) );
        return -1;
    }
    
    rbtKeyValue( bucket->nodes, nodeIter, NULL, (void**)&node );
    
    return dhtPingByIP( dht, node->contact, node->hash, sync );
}

static int
dhtStore( kc_dht * dht, void * key, dhtValue * value )
{
    kc_dhtNode ** nodes;
    kc_dhtNode * node;
    int status;
    int count;
    
    assert( dht != NULL );
    assert( key != NULL );
    assert( value != NULL );
    
    nodes = kc_dhtGetNodes( dht, key, &count );
    if( count == 0 )
    {
        kc_logDebug( "No known nodes to republish to. Ignoring..." );
        return 0;
    }
    
    for( node = nodes[0]; node != NULL; node++ )
    {
        status = dhtSendMessage( dht, DHT_RPC_STORE, node->contact );
        if( status != 0 )
        {
            kc_logAlert( "Failed writing DHT_RPC_STORE message" );
            return -1;
        }
    }
    
    free( nodes );
    return 0;
}

static void*
dhtPerformLookup( const kc_dht * dht, kc_hash * hash, int value )
{
    /* We select alpha contacts from the non-empty closest bucket to the key.
     * We can spill outside the bucket if fewer than alpha contacts, and closestNode must be noted
     */
    kc_dhtNode ** nodes;
    kc_dhtNode * node;
    kc_dhtNode * closestNode;
    int count = KADC_PROBE_PARALLELISM;
    int status;
    nodes = kc_dhtGetNodes( dht, hash, &count );
    if( count == 0 )
    {
        kc_logDebug( "No known nodes to perform lookup from. Ignoring..." );
        return 0;
    }
    
    if( count < KADC_PROBE_PARALLELISM )
    {
        /* FIXME: Spill ! */
    }
    
    qsort( nodes, count, sizeof(kc_dhtNode), dhtNodeCmpHash );
    closestNode = nodes[0];
    
    /* We create a shortlist by issuing a FIND_* to the selected contacts.
     * A contact failing to answer is removed from the shortlist */
    kc_dhtNode **tempNode;
    for( tempNode = nodes; tempNode != NULL; tempNode++ )
    {
        
    }
    
    /* TODO: Reselect alpha contacts from the shortlist and resend a FIND_* to them => PARALLEL SEARCH.
     * We shouldn't re-send to already contacted contacts.
     * We update closestNode accordingly.
     * We continue until we can't find a node closer than closestNode, or we have k contacts */
    /* TODO: If searching a value, the search end as soon as the value is found,
     * and the value is stored at the closest node which did not return the value */
    
    /* Now send those messages ! */
    for( node = nodes[0]; node != NULL; node++ )
    {
        kc_message * answer = kc_messageInit( node->contact, ( value ? DHT_RPC_FIND_VALUE : DHT_RPC_FIND_NODE ), 0, NULL );

        status = dht->parameters->callbacks.writeCallback( dht, NULL, answer );
        if( status != 0 )
        {
            kc_logAlert( "Failed writing %s message", ( value ? "DHT_RPC_FIND_VALUE" : "DHT_RPC_FIND_NODE" ) );
            return NULL;
        }
        
        status = kc_queueEnqueue( dht->sndQueue, answer );
        if( status != 0 )
        {
            kc_logAlert( "Failed enqueing %s message", ( value ? "DHT_RPC_FIND_VALUE" : "DHT_RPC_FIND_NODE" ) );
            return NULL;
        }
    }
    
    free( nodes );
    return NULL;
}

static void*
dhtFindNode( const kc_dht * dht, kc_hash * hash )
{
    return dhtPerformLookup( dht, hash, 0 );
}

static void*
dhtFindValue( const kc_dht * dht, kc_hash * key )
{
    return dhtPerformLookup( dht, key, 1 );
}
#if 0
static void
ioCallback( void * ref, kc_udpIo * io, kc_message *msg )
{
    kc_dht    * dht = ref;
    int status;
    
    status = dht->parameters->callbacks.parseCallback( dht, msg );
    
    const kc_contact * contact = kc_messageGetContact( msg );
    kc_dhtNode * node = dhtNodeForContact( dht, contact );
    if( node != NULL )
    {
        /* Unexpected communication from an unknown node, add it to our nodes */
        /* FIXME: I should parse the hash for this node, if available */
        kc_dhtAddNode( dht, contact, NULL );
    }
    
    dhtSession * session = dhtSessionForMsg( dht, msg );
    if( session != NULL )
    {
        status = kc_queueEnqueue( session->queue, msg );
        if ( status != 0 )
        {
            kc_logAlert( "Couldn't enqueue incoming message, err %d", status );
        }
        return;
    }
    else
    {
        session = dhtSessionInit( contact, kc_messageGetType( msg ), 1, asyncCallback, dht->parameters->maxQueuedMessages );
        
        if( session == NULL )
        {
            kc_logAlert( "Failed creating new session object" );
            return;
        }
        kc_dhtLock( dht );
        if( rbtInsert( dht->sessions, session, NULL ) != RBT_STATUS_OK )
        {
            kc_logAlert( "Failed inserting new session object" );
            kc_dhtUnlock( dht );
            return;
        }
        kc_dhtUnlock( dht );
        
        status = asyncCallback( dht, msg );
        if( status != 0 )
        {
            kc_logAlert( "asyncCallback failed with err %d", status );
        }
    }
    return;
}
#endif
#if 0
void *
dhtPulse( void * arg )
{
    kc_dht * dht = arg;
    
    while( parameters->bucketSize != 0 )
    {
        /* Sessions update */
        RbtIterator sessIter;
        for( sessIter = rbtBegin( dht->sessions );
             sessIter != NULL;
             sessIter = rbtNext( dht->sessions, sessIter ) )
        {
            
            dhtSession * session;
            rbtKeyValue( dht->sessions, sessIter, (void*)session, NULL );
            if( session == NULL )
                continue;
            
            if( session->callback != NULL )
            {
                /* This is a synchronous session, nothing to see here */
                continue;
            }
            
            kc_message * msg = kc_queueDequeueTimeout( session->queue, 0 );
            if( msg == NULL )
            {
                /* No messages here, move on */
                continue;
            }
            int status = session->callback( dht, msg );
            if( status == -1 )
            {
                rbtEraseKey( dht->sessions, session );
                dhtSessionFree( session );
            }
        }
        /* Routing table refresh */
        int i;
        for( i = 0; i < BUCKET_COUNT; i++ )
        {
            dhtBucket   * bucket = dht->buckets[i];
            if( time( NULL ) - bucket->lastChanged > KADC_REFRESH_DELAY )
            {
                dhtBucketLock( bucket );
                
                RbtIterator iter;
                for( iter = rbtBegin( bucket->nodes ); iter != NULL; iter = rbtNext( bucket->nodes, iter ) )
                {
                    kc_dhtNode * node;
                    rbtKeyValue( bucket->nodes, iter, NULL, (void**)&node );
                    if( node != NULL && time( NULL ) - node->lastSeen > KADC_REFRESH_DELAY )
                    {
                        if( dhtPingByHash( dht, node->hash, 1 ) == 0 )
                            continue;
                        else
                        {
                            node->lastSeen = time( NULL );
                            bucket->lastChanged = time( NULL );
                            break;
                        }
                    }
                }
                
                dhtBucketUnlock( bucket );
            }
        }
        
        /* Key/Value republish */
        int replicateAll;
        int needsCleanup = 0;
        replicateAll = ( time( NULL ) - dht->lastReplication >= KADC_REPLICATION_DELAY );
        if( replicateAll )
            kc_logNormal( "Started replicating all our keys..." );
        
        RbtIterator keys;
        for( keys = rbtBegin( dht->keys ); keys != NULL; keys = rbtNext( dht->keys, keys ) )
        {
            void * key;
            dhtValue * value;
            
            rbtKeyValue( dht->keys, keys, (void**)&key, (void**)value);
            assert( key != NULL );
            assert( value != NULL );
            
            int replicateThis = 0;
            time_t now = time( NULL );
            
            /* We need to expire it */
            if( now - value->published >= KADC_EXPIRATION_DELAY )
            {
                /* I'm doing this in two pass because the deletion is likely to change
                 * the RB tree and break the iterators I'm using */
                value->published = 0;
                needsCleanup = 0;
                continue;
            }
            
            /* If this is our value, we need to republish it */
            if( value->mine && now - value->published >= KADC_REPUBLISH_DELAY )
                replicateThis = 1;
                
            if( replicateThis || replicateAll )
            {
                dhtStore( dht, key, value );
                if( replicateThis )
                    value->published = now;
            }
        }
        
        if( replicateAll )
        {
            kc_logNormal( "Replication ended..." );
            dht->lastReplication = time( NULL );
        }
        
        if( needsCleanup )
        {
            kc_logNormal( "Started cleanup of expired keys" );
        
            /* Two pass replication, cleanup our keys */
            for( keys = rbtBegin( dht->keys ); keys != NULL; keys = rbtNext( dht->keys, keys ) )
            {
                void * key;
                dhtValue * value;
                
                rbtKeyValue( dht->keys, keys, (void**)&key, (void**)value);
                assert( key != NULL );
                assert( value != NULL );
                
                if( value->published == 0 )
                {
                    rbtErase( dht->keys, keys );
                    keys = rbtBegin( dht->keys );
                }
            }
            
            kc_logNormal( "Cleanup ended" );
        }
            
        /* Send our messages */
        kc_message * msg;
        i = 0;
        if( time( NULL ) - dht->probeDelay >= KADC_PROBE_DELAY )
        {
            kc_logDebug(" Probe time !" );
            
            dht->probeDelay = time( NULL );
            kc_dhtLock( dht );
            while( ( msg = kc_queueDequeueTimeout( dht->sndQueue, 0 ) ) && i++ < KADC_PROBE_PARALLELISM )
            {
                const kc_contact * contact = kc_messageGetContact( msg );
                kc_logDebug( "Probing %s with %d", kc_contactPrint( contact ), kc_messageGetType( msg ) );
//                int err = kc_udpIoSendMsg( dht->io, msg );
//                if( err < 0 ) {
//                    kc_logAlert( "Failed sending probe: %d", err );
//                }
                
                kc_messageFree( msg );
            }
            kc_dhtUnlock( dht );
        }
            
        sleep( 5 );
    }
    
    return dht;
}
#endif

int dhtRemoveNode( const kc_dht * dht, kc_hash * hash )
{
    assert( dht != NULL );
    
    /* Get this node's bucket */
    kc_dhtNode    * node;
    dhtBucket     * bucket = dhtBucketForHash( dht, hash );
    
    if( bucket == NULL )
        return -1;
    
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
kc_dhtAddNode( kc_dht * dht, kc_contact * contact, kc_hash * hash )
{
    assert( dht != NULL );
    
    /* Get this node's bucket */
    kc_dhtNode    * node;
    dhtBucket     * bucket = dhtBucketForHash( dht, hash );
    
    if( bucket == NULL )
    {
        kc_logVerbose( "Trying to add our own node. Ignoring." );
        return 0;
    }
    
    RbtIterator nodeIter = rbtFind( bucket->nodes, hash );
    if( nodeIter != NULL )
    {
        dhtBucketLock( bucket );
        
        // This node is already in our bucket list, let's update it's info */
        kc_logDebug( "Node %s already in our bucket, updating...", hashtoa( hash ) );
        rbtKeyValue( bucket->nodes, nodeIter, NULL, (void**)&node );
        node->contact = contact;
        node->lastSeen = time(NULL);
        /* FIXME: handle node type */
        //        node->type = 0;
        dhtBucketUnlock( bucket );
        return 1;
    }
    
    /* We don't have it, allocate one */
    node = dhtNodeInit( contact, hash );
    
    if( node == NULL )
    {
        kc_logAlert( "kc_dhtAddNode: dhtNodeInit failed !");
        return -1;
    }
    
    if( bucket->availableSlots == 0 )
    {
        // This bucket is full, get the first node in this bucket, (the least-heard-of one)
        /* Here we need to ping it to make sure it's down, then if it replies we move up
         * in the bucket until one of them fails to reply...
         * - If one fails, we remove it and add the new one at the end
         * - If they all replied, we store it in our "backup nodes" list 
         */
        
        dhtBucketLock( bucket );
        RbtIterator nodeIter;
        
        for( nodeIter = rbtBegin( bucket->nodes ); nodeIter != NULL; nodeIter = rbtNext( bucket->nodes, nodeIter ) )
        {
            kc_dhtNode    * oldNode;
            rbtKeyValue( bucket->nodes, nodeIter, NULL, (void**)&oldNode );
            if( dhtPingByIP( dht, oldNode->contact, oldNode->hash, 1 ) == 0 )
            {
                /* This one replied, try next... */
                oldNode->lastSeen = time( NULL );
                continue;
            } else {
                /* We remove the old one */
                rbtErase( bucket->nodes, nodeIter );
                dhtNodeFree( oldNode );
                bucket->availableSlots++;
            }
        }
        
        if( bucket->availableSlots == 0 )
        {
            /* We just checked every node, and none is bad, add as a backup node */
            /* TODO: Handle backup nodes */
            return 0;
        }
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
kc_dhtCreateNode( kc_dht * dht, kc_contact * contact )
{
    assert( dht != NULL );
    assert( contact != NULL );
    
    dhtPingByIP( dht, contact, NULL, 0 );
}

int
kc_dhtStoreKeyValue( kc_dht * dht, kc_hash * key, void * value )
{
    assert( dht != NULL );
    assert( key != NULL );
    
    dhtValue * dhtVal = malloc( sizeof(dhtValue) );
    dhtVal->value = value; /* FIXME: Copy ? */
    dhtVal->published = 0; 
    dhtVal->mine = 1;
    
    return dhtStore( dht, key, dhtVal );
}

void *
kc_dhtValueForKey( const kc_dht * dht, void * key )
{
    assert( dht != NULL );
    assert( key != NULL );
    
    dhtValue * value = rbtFind( dht->keys, key );
    if( value != NULL )
    {
        return value->value;
    }
    
    kc_logDebug( "Key %s not found, performing lookup", hashtoa( key ) );
    
    return dhtFindValue( dht, key );
}

void
kc_dhtPrintState( const kc_dht * dht )
{
    kc_logNormal( "DHT %p hash : %s", dht, hashtoa( dht->hash ) );
    if( *dht->identities == NULL )
        kc_logNormal( "No identities" );
    else
    {
        kc_logNormal( "Our Identities :" );
        dhtIdentity ** identity;
        for( identity = dht->identities; *identity != NULL ; identity++ )
        {
            kc_logNormal( "%s", kc_contactPrint( (*identity)->us ) );
        }
    }
    
    RbtIterator sessIter;
    if( rbtBegin( dht->sessions ) == NULL )
    {
        kc_logNormal( "No running sessions" );
    }
    else
    {
        kc_logNormal( "Running sessions :" );

        for( sessIter = rbtBegin( dht->sessions ); sessIter != NULL; sessIter = rbtNext( dht->sessions, sessIter ) )
        {
            dhtSession * session;
            rbtKeyValue( dht->sessions, sessIter, (void*)&session, NULL );
            if( session != NULL )
                kc_logNormal( "%s session to %s", ( session->incoming ? "Incoming" : "Outgoing" ), kc_contactPrint( session->contact ) );
        }
    }
}

void
kc_dhtPrintKeys( const kc_dht * dht )
{
    if( rbtBegin( dht->keys ) == NULL )
    {
        kc_logNormal( "DHT has no keys" );
    }
    else
    {
        kc_logNormal( "DHT has following keys stored :" );
        RbtIterator keysIter;
        for( keysIter = rbtBegin( dht->keys ); keysIter != NULL; keysIter = rbtNext( dht->keys, keysIter ) )
        {
            kc_hash * key;
            dhtValue * value;
            
            rbtKeyValue( dht->keys, keysIter, (void*)key, (void*)value );
            kc_logNormal( "Key %s: %x, expires %d", hashtoa( key ), value->value, time( NULL ) - value->published );
        }
    }
}

void
kc_dhtPrintTree( const kc_dht * dht )
{
    int i;
    for( i = 0; i < BUCKET_COUNT; i++ )
    {
        if( dht->parameters->bucketSize - dht->buckets[i]->availableSlots != 0 )
        {
            kc_logNormal( "Bucket %d contains %d nodes :", i, dht->parameters->bucketSize - dht->buckets[i]->availableSlots );
            dhtPrintBucket( dht->buckets[i] );
        }
    }
}

int
kc_dhtNodeCount( const kc_dht *dht )
{
    int total = 0;
    int i;
    for( i = 0; i < BUCKET_COUNT; i++)
        total += dht->parameters->bucketSize - dht->buckets[i]->availableSlots;
    
    return abs( total );
}

kc_dhtNode**
kc_dhtGetNodes( const kc_dht * dht, kc_hash * hash, int * nodeCount )
{
    assert( dht != NULL );
    assert( nodeCount != NULL );
    
    if( hash == NULL )
    {
        /* FIXME: I'm not really sure how to get a correct list of nodes here,
         * I'll get them in order, but they'll be sorted, so maybe it's bad
         */
        int count = kc_dhtNodeCount( dht );
        
        /* We return nodeCount nodes (or parameters->bucketSize if 0), or all our nodes if we don't have enough */
        *nodeCount = ( *nodeCount == 0 ? dht->parameters->bucketSize : *nodeCount );
        *nodeCount = ( count < *nodeCount ? count : *nodeCount );
        
        kc_dhtNode ** nodes = calloc( *nodeCount, sizeof(kc_dhtNode) );
        
        int i = 0;
        int j;
        for( j = 0; j < BUCKET_COUNT; i++ )
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
    else /* hash == NULL */
    {
        dhtBucket * bucket = dhtBucketForHash( dht, hash );
        
        int count = dht->parameters->bucketSize - bucket->availableSlots;
        
        /* We return nodeCount nodes (or parameters->bucketSize if 0), or all our nodes if we don't have enough */
        *nodeCount = ( *nodeCount == 0 ? dht->parameters->bucketSize : *nodeCount );
        *nodeCount = ( count < *nodeCount ? count : *nodeCount );
        
        kc_dhtNode ** nodes = calloc( *nodeCount, sizeof(kc_dhtNode) );
        
        RbtIterator iter;
        int i = 0;
        for( iter = rbtBegin( bucket->nodes );
            i < *nodeCount && iter != NULL;
            iter = rbtNext( bucket->nodes, iter ) )
        {
            rbtKeyValue( bucket->nodes, iter, NULL, (void**)&nodes[i++] );
            
            if( i >= *nodeCount )
                break;
        }
        
        return nodes;
    }
}

kc_contact *
kc_dhtGetOurContact( const kc_dht * dht, int type )
{
    dhtIdentity * identity;
    for( identity = *dht->identities; identity != NULL; identity++ )
    {
        if( kc_contactGetType( identity->us ) == type )
            return identity->us;
    }
    return NULL;
}

kc_hash *
kc_dhtGetOurHash( const kc_dht * dht )
{
    return dht->hash;
}


dhtIdentity *
dhtIdentityInit( kc_dht * dht, kc_contact * contact )
{
    assert( dht != NULL );
    assert( contact != NULL );
    
    int status = 0;
    int type = kc_contactGetType( contact );
    
    dhtIdentity * identity = malloc( sizeof(dhtIdentity) );
    
    identity->fd = kc_netOpen( type, SOCK_DGRAM );
    if( identity->fd == -1 )
    {
        kc_logAlert( "Error opening socket" );
        free( identity );
        return NULL;
    }
    
    status = kc_netBind( identity->fd, contact );
    if( status == -1 )
    {
        kc_logAlert( "Error binding socket to %s", kc_contactPrint( contact ) );
        kc_netClose( identity->fd );
        free( identity );
        return NULL;
    }
    
    identity->inputEvent = bufferevent_new( identity->fd, readcb, NULL, errorcb, dht );
    if( identity->inputEvent == NULL )
    {
        kc_logAlert( "Error creating buffer event for identity %s", kc_contactPrint( contact ) );
        kc_netClose( identity->fd );
        free( identity );
        return NULL;
    }
    
    status = bufferevent_base_set( dht->eventBase, identity->inputEvent );
    if( status != 0 )
    {
        kc_logAlert( "Error setting event buffer base for identity %s", kc_contactPrint( contact ) );
        kc_netClose( identity->fd );
        bufferevent_free( identity->inputEvent );
        free( identity );
        return NULL;
    }
    
    identity->us = kc_contactDup( contact );
    if( identity->us == NULL )
    {
        kc_logAlert( "Failed creating contact for identity %s", kc_contactPrint( contact ) );
        kc_netClose( identity->fd );
        bufferevent_free( identity->inputEvent );
        free( identity );
        return NULL;
    }
    
    return identity;
}

void
dhtIdentityFree( dhtIdentity * identity )
{
    assert( identity != NULL );
    
    kc_contactFree( identity->us );
    bufferevent_free( identity->inputEvent );
    kc_netClose( identity->fd );
    free( identity );
}
