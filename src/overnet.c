/*
 *  overnet.c
 *  KadC
 *
 *  Created by Etienne on 16/01/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include "internal.h"
#include "overnet.h"
#include "overnet_opcodes.h"

char *
ov_writePing( const kc_dht * dht, size_t * size )
{
    kc_contact * contact = kc_dhtGetOurContact( dht, AF_INET );
    kc_hash * ourHash = kc_dhtGetOurHash( dht );
    if( kc_hashLength( ourHash ) != 128 )
    {
        /* We doesn't work with non-128 hashes */
        *size = 0;
        return NULL;
    }
    
    const struct in_addr * addr = kc_contactGetAddr( contact );
    
    *size = sizeof(struct ov_connect);
    struct ov_connect * msg = malloc( *size );
    msg->protoType = OP_EDONKEYHEADER;
    msg->messageType = OVERNET_CONNECT;
    
    
    puthashn( msg->node.hash, ourHash );
    /* FIXME: External IP needed here */
    memcpy( msg->node.ip, addr, sizeof(struct in_addr) );
    msg->node.port = kc_contactGetPort( contact );
    msg->node.unused = 0;

    
    return (void*)msg;
}

char *
ov_writePingReply( const kc_dht * dht, size_t * size )
{
//    *size = 4; /* Proto type (1) + opcode (1) */
    
    *size = sizeof(struct ov_connect_reply);
    struct ov_connect_reply * msg = malloc( *size );
    
    msg->protoType = OP_EDONKEYHEADER;
    msg->messageType = OVERNET_CONNECT_REPLY;
    
    /* We try to get nodes out of our DHT */
    int nodeCount;
    kc_dhtNode ** nodes;
    
    nodes = kc_dhtGetNodes( dht, NULL, &nodeCount );

    int i; /* Node count inside our msg->nodes array, index of our last (empty) node */
    kc_dhtNode * currentNode; /* Pointer to our current node from the DHT */
    
    for( currentNode = *nodes, i = 0; currentNode != NULL; currentNode++ )
    {
        kc_contact * contact = kc_dhtNodeGetContact( currentNode );
        if( kc_contactGetType( contact ) != AF_INET )
        {
            /* Overnet only handles IPv4 nodes */
            continue;
        }
        
        /* - 4 is for nodes pointer in ov_connect_reply */
        msg = realloc( msg, sizeof(struct ov_connect_reply) + sizeof(struct ov_node) * i );
        
        puthashn( msg->nodes[i].hash, kc_dhtNodeGetHash( currentNode ) );
        memcpy( msg->nodes[i].ip, kc_contactGetAddr( contact ), sizeof(struct in_addr) );
        msg->nodes[i].port = kc_contactGetPort( contact );
        msg->nodes[i].unused = 0;
        
        /* We advance in our msg->nodes array */
        i++;
    }
    
    msg->nodeCount = i;
    *size = sizeof(struct ov_connect_reply) + 8 * sizeof(struct ov_node);
    return (void*)msg;
}

char *
ov_writePublicize( const kc_dht * dht, size_t * size )
{
    return NULL;
}

int
ov_parseCallback( const kc_dht * dht, const kc_message * msg )
{
    /* TODO: */
    return -1;
}

int
ov_readCallback( kc_dht * dht, const kc_message * msg )
{
    const kc_contact * contact = kc_messageGetContact( msg );
    kc_logDebug( "readCallback: got a message from %s", kc_contactPrint( contact ) );
    kc_logDebug( "readCallback: message len %d", kc_messageGetSize( msg ) );
    const char* bp = kc_messageGetPayload( msg );
    
    if( *bp++ != (char)OP_EDONKEYHEADER )
    {
        kc_logAlert( "readCallback: unknown protocol type %X", *--bp );
        return 1;
    }
    
    switch( *bp++ )
    {
        case (char)OVERNET_CONNECT:
            kc_logDebug( "readCallback: got a OVERNET_CONNECT" );
            
/*            *answer = malloc( sizeof(kc_dhtMsg) );
            (*answer)->node = msg->node;
            (*answer)->type = DHT_RPC_PING;*/
            
            return 0;
            break;
            
        case (char)OVERNET_CONNECT_REPLY:
            kc_logDebug( "readCallback: got a OVERNET_CONNECT_REPLY" );
            
            int nodeCount;
            nodeCount = getushortle( &bp );
            
            if( kc_messageGetSize( msg ) != 4 + nodeCount * 23 )
            {
                kc_logAlert( "readCallback: OVERNET_CONNECT_REPLY with bad size" );
                return -1;
            }
            
            int i;
            for( i = 0; i < nodeCount; i++ )
            {
                kc_hash    * hash = kc_hashRandom( 0 /* FIXME: */ );
                int type;
                struct in_addr addr;
                in_port_t port;
                
                gethashn( hash, &bp );
                
                addr = getipn( &bp );
                port = getushortle( &bp );
                type = *bp++;
                kc_contact * newContact = kc_contactInit( &addr, sizeof(struct in_addr), port );
                type = kc_dhtAddNode( dht, newContact, hash );
                if( type == 0 )
                    kc_dhtCreateNode( dht, newContact );
            }
            return 0;
            
            break;
            case (char)OVERNET_PUBLICIZE:
            kc_logDebug( "readCallback: got a OVERNET_PUBLICIZE" );
            
/*            *answer = malloc( sizeof(kc_dhtMsg) );
            (*answer)->node = msg->node;
            (*answer)->type = DHT_RPC_PROTOCOL_SPECIFIC;
            (*answer)->payload = ov_writePublicize( dht, &(*answer)->payloadSize );*/
            return 0;
            break;
            
            case (char)OVERNET_PUBLICIZE_ACK:
            kc_logDebug( "readCallback: got a OVERNET_PUBLICIZE_ACK" );
            break;
            
            case (char)OVERNET_SEARCH:
            kc_logDebug( "readCallback: got a OVERNET_SEARCH" );
            break;
            
            case (char)OVERNET_PUBLISH:
            kc_logDebug( "readCallback: got a OVERNET_PUBLISH" );
            break;
            
            default:
            kc_logAlert( "readCallback: unknown message type %d", kc_messageGetPayload( msg )[1] );
            break;
    }
    return DHT_RPC_UNKNOWN;
}

int
ov_writeCallback( const kc_dht * dht, kc_message * msg, kc_message * answer )
{
    if( msg == NULL )
    {
        assert( answer );
        /* This is a session start */
        switch( kc_messageGetType( answer ) )
        {
            case DHT_RPC_PING:
                ;
                size_t size;
                char * payload;
                
                payload = ov_writePing( dht, &size );
                kc_messageSetSize( answer, size );
                kc_messageSetPayload( answer, payload );
                
                return ( payload == NULL );
                break;
            default:
                assert(0);
                break;
        }
    }
    else
    {
        switch ( kc_messageGetType( msg ) )
        {
            case DHT_RPC_PING:
            {
                const kc_contact * contact = kc_messageGetContact( msg );
                answer = kc_messageInit( contact, DHT_RPC_PING, 0, NULL );

                size_t size;
                char * payload;
                payload = ov_writePingReply( dht, &size );
                kc_messageSetSize( answer, size );
                kc_messageSetPayload( answer, payload );
                
                return ( payload != NULL );
            } 
                break;
                
            default:
                break;
        }
    }
    
    return 0;
}

#pragma mark ov_parameters
kc_dhtParameters ov_parameters = {
    0,/*int expirationDelay;*/
    0,/*int refreshDelay;*/
    0,/*int replicationDelay;*/
    0,/*int republishDelay;*/
    
    0,/*int lookupParallelism;*/
    
    0,/*int maxQueuedMessages;*/
    0,/*int maxSessionCount;*/
    0,/*int maxMessagesPerPulse;*/
    0,/*int sessionTimeout;*/
    
    128,/*int hashSize;*/
    20,/*int bucketSize;*/
    {
        ov_parseCallback,
        ov_readCallback,
        ov_writeCallback
    }
};