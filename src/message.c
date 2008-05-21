/*
 *  message.c
 *  KadC
 *
 *  Created by Etienne on 14/02/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include "message.h"

/** 
 * A structure holding a message.
 */
struct kc_message {
    /* General routing info */
    kc_contact        * contact;     /**< Represents the remote host */
    kc_messageType      type;       /**< The message type. @see kc_messageType */
    
    /* A buffer containing the contents of the message */
    int                 size;
    char              * data;
    
    /* Protocol-specific stuff */
    void              * protocolStuff;
};

kc_message *
kc_messageInit( kc_contact * contact, kc_messageType type, size_t length, char* data )
{
    kc_message * self = malloc( sizeof(kc_message) );
    if( !self )
        return NULL;
    self->contact = contact;
    self->type = type;
    self->size = length;
    if( data != NULL )
    {
        memcpy( self->data, data, length);
    }
    else
    {
        self->data = calloc( length, sizeof(char*) );
    }
    return self;
}

kc_message *
kc_messageInitFromEvBuffer( kc_contact * contact, kc_messageType type, struct evbuffer * buffer )
{
    while( 0 );
    
    
    return NULL;
}

void
kc_messageFree( kc_message * message )
{
    assert( message != NULL );
    free( message->data );
    free( message );
}

const kc_contact *
kc_messageGetContact( const kc_message * message )
{
    assert( message != NULL );
    return message->contact;
}

void
kc_messageSetContact( kc_message * message, kc_contact * contact )
{
    assert( message != NULL );
    assert( contact != NULL );
    message->contact = contact;
}

kc_messageType
kc_messageGetType( const kc_message * message )
{
    assert( message != NULL );
    return message->type;
}

void
kc_messageSetType( kc_message * message, kc_messageType type )
{
    assert( message != NULL );
    message->type = type;
    /* TODO: Error-check */
}

char *
kc_messageGetData( kc_message * message )
{
    assert( message != NULL );
    return message->data;
}

size_t
kc_messageGetSize( kc_message * message )
{
    assert( message != NULL );
    return message->size;
}

int
kc_messageSetData( kc_message * message, void * data, size_t size )
{
    assert( message != NULL );
    if( data == NULL )
    {
        assert( size != 0 );
        free( message->data );
        message->data = NULL;
        message->size = 0;
        return 0;
    }
    
    void * tmp = realloc( message->data, size );
    if( tmp == NULL )
    {
        kc_logAlert( "Failed reallocating message buffer" );
        return -1;
    }
    message->data = tmp;
    
    memcpy( message->data, data, size );
    message->size = size;
    return 0;
}

int
kc_messageWriteToBufferEvent( kc_message * message, struct bufferevent * bufevent )
{
    assert( bufevent != NULL );
    assert( message != NULL );
    
    int status;
    
    kc_logVerbose( "kc_messageWriteToBufferEvent: Writing message in bufferEvent (size: %d)", kc_messageGetSize( message ) );
    status = bufferevent_write( bufevent, kc_messageGetData( message ), kc_messageGetSize( message ) );
    if( status != 0 )
    {
        kc_logAlert( "Failed writing evbuffer to buffer event, err %d", status );
        return -1;
    }
    
    return 0;
}
