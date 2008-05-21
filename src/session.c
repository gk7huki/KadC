/*
 *  session.c
 *  KadC
 *
 *  Created by Etienne on 16/05/08.
 *  Copyright 2008 Etienne Samson. All rights reserved.
 *
 */

#include "session.h"
#include "internal.h"

struct _kc_session {
    kc_contact            * contact;
    
    kc_messageType          type;
    int                     incoming;
    
    int                     socket;
    struct bufferevent    * bufferEvent;
    
    kc_sessionCallback      callback;
    
    kc_dht                * dht;
};


int
kc_sessionCmp( const void *a, const void *b )
{
	const kc_session *pa = a;
	const kc_session *pb = b;
    
    int contactCmp = kc_contactCmp( pa->contact, pb->contact );
	if( contactCmp != 0 )
        return contactCmp;
    
    if( pa->incoming != pb->incoming )
        return ( pa->incoming < pb->incoming ? -1 : 1 );
    
    if( pa->type != pb->type )
        return ( pa->type < pb->type ? -1 : 1 );
    
    return 0;
}

static void
sessionReadCB( struct bufferevent * event, void * arg )
{
    kc_session * session = arg;
    kc_logNormal( "Session read for contact: %s", kc_contactPrint( session->contact ) );
    /* Means we have recieved data from this contact */
}

static void
sessionWriteCB( struct bufferevent * event, void * arg )
{
    kc_session * session = arg;
    kc_logNormal( "Session write for contact: %s", kc_contactPrint( session->contact ) );
}

static void
sessionErrorCB( struct bufferevent * event, short what, void * arg )
{
    kc_session * session = arg;
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
    
    kc_dhtDeleteSession( session->dht, session );
    kc_sessionFree( session );
}

kc_session *
kc_sessionInit( kc_dht * dht, kc_contact * connectContact, kc_messageType type,
               int incoming, kc_sessionCallback callback )
{
    kc_session * self = malloc( sizeof(kc_session) );
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
    
    self->dht = dht;
    
    kc_logVerbose( "Successfully inited session %p to %s", self, kc_contactPrint( connectContact ) );
    
    return self;
}

void
kc_sessionFree( kc_session * session )
{
    if( session->bufferEvent != NULL )
        bufferevent_free( session->bufferEvent );
    if( session->socket != -1 )
        kc_netClose( session->socket );
    
    free( session );
}

int
kc_sessionStart( kc_session * session )
{
    dhtIdentity * id = kc_dhtIdentityForContact( session->dht, session->contact );
    if( id == NULL )
    {
        kc_logError( "No identity available to start session to contact %s", kc_contactPrint( session->contact ) );
        return -1;
    }
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
    if( bufferevent_base_set( session->dht->eventBase, session->bufferEvent ) != 0 )
    {
        kc_logError( "Failed setting event buffer base" );
        return 1;
    }
    
    int timeout = session->dht->parameters->sessionTimeout;
    bufferevent_settimeout( session->bufferEvent, timeout, timeout );
    
    if( bufferevent_enable( session->bufferEvent, EV_READ | EV_WRITE ) != 0 )
    {
        kc_logError( "Failed enabling event buffer" );
        return 1;
    }
    return 0;
}

static void
evbufferCB(struct evbuffer * evbuf, size_t oldSize, size_t newSize, void * arg )
{
    kc_logVerbose( "evbufferCB: buffer %p modified %d -> %d", evbuf, oldSize, newSize );
}

int
kc_sessionSend( kc_session * session, kc_message * message )
{
    assert( session != NULL );
    return kc_messageWriteToBufferEvent( message, session->bufferEvent );
}

int
kc_sessionRecieved( kc_session * session, kc_message * message )
{
    assert( session != NULL );
    assert( message != NULL );
    
    int status;
    status = kc_messageWriteToBufferEvent( message, session->bufferEvent );
    if( status != 0 )
    {
        kc_logAlert( "failed adding message buffer to session with error %d", status);
        return -1;
    }
    return 0;
}

const kc_contact *
kc_sessionGetContact( const kc_session * session )
{
    assert( session != NULL );
    return session->contact;
}

kc_messageType
kc_sessionGetType( const kc_session * session )
{
    assert( session != NULL );
    return session->type;
}

char *
kc_sessionPrint( const kc_session * session )
{
    assert( session != NULL );
    
    static char * contactStr = NULL;
    if( contactStr != NULL )
        free( contactStr );
    asprintf( &contactStr, "%s session to %s", ( session->incoming ? "Incoming" : "Outgoing" ), kc_contactPrint( session->contact ) );
    assert( contactStr != NULL );
    
    return contactStr;
}
