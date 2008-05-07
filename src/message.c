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
    kc_contact    * contact;     /**< Represents the remote host */
    
    /* Protocol-specific stuff */
    kc_messageType  type;       /**< The message type. @see kc_messageType */
    int             size;       /**< The size of the buffer */
	char          * payload;    /**< A buffer containing the payload of the message */
};

kc_message *
kc_messageInit( kc_contact * contact, kc_messageType type, size_t length, char* data )
{
    kc_message * self = malloc( sizeof(kc_message) );
    if( !self )
        return NULL;
    self->payload = NULL;
    kc_messageSetContact( self, contact );
    kc_messageSetType( self, type );
    kc_messageSetSize( self, length );
    kc_messageSetPayload( self, data );
    return self;
}

void
kc_messageFree( kc_message * message )
{
    free( message->payload );
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

const char *
kc_messageGetPayload( const kc_message * message )
{
    assert( message != NULL );
    return message->payload;
}

void
kc_messageSetPayload( kc_message * message, char * payload )
{
    assert( message != NULL );
    if( payload == NULL )
    {
        message->payload = NULL;
        return;
    }
    assert( message->size != 0 );
    memcpy( message->payload, payload, message->size );
}

size_t
kc_messageGetSize( const kc_message * message )
{
    assert( message != NULL );
    return message->size;
}

void
kc_messageSetSize( kc_message * message, size_t size )
{
    assert( message != NULL );
    message->payload = realloc( message->payload, sizeof(char) * size );
    message->size = size;
}