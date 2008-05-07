/*
 *  message.h
 *  KadC
 *
 *  Created by Etienne on 14/02/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef __KADC_MESSAGE_H__
#define __KADC_MESSAGE_H__

typedef struct kc_message kc_message;

/**
 * A DHT message type.
 *
 * This is used in the kc_dhtMsg struct to tell the callback what kind of message to write
 * @see kc_dhtMsg
 */
typedef enum {
    DHT_RPC_UNKNOWN = -1,   /* Used when we can't parse this message */
    DHT_RPC_PING,
    DHT_RPC_STORE,
    DHT_RPC_FIND_NODE,
    DHT_RPC_FIND_VALUE,
    DHT_RPC_PROTOCOL_SPECIFIC   /* Used for extended (aka non-Kademlia) messages */
} kc_messageType;

kc_message *
kc_messageInit( kc_contact * contact, kc_messageType type, size_t length, char* data );

void
kc_messageFree( kc_message * message );

const kc_contact *
kc_messageGetContact( const kc_message * message );

kc_messageType
kc_messageGetType( const kc_message * message );

const char *
kc_messageGetPayload( const kc_message * message );

size_t
kc_messageGetSize( const kc_message * message );

void
kc_messageSetContact( kc_message * message, kc_contact * contact );

void
kc_messageSetType( kc_message * message, kc_messageType type );

void
kc_messageSetPayload( kc_message * message, char * payload );

void
kc_messageSetSize( kc_message * message, size_t size );

#endif