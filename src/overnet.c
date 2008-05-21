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

int
ov_writePing( const kc_dht * dht, kc_message * message )
{
    kc_contact * contact = kc_dhtGetOurContact( dht, AF_INET );
    kc_hash * ourHash = kc_dhtGetOurHash( dht );
    if( kc_hashLength( ourHash ) != 128 )
    {
        /* We doesn't work with non-128 hashes */
        return -1;
    }
    
    const struct in_addr * addr = kc_contactGetAddr( contact );
    
    struct ov_connect * msg = malloc( sizeof(struct ov_connect) );
    msg->protoType = OP_EDONKEYHEADER;
    msg->messageType = OVERNET_CONNECT;
    
    puthashn( msg->node.hash, ourHash );
    /* FIXME: External IP needed here */
    memcpy( msg->node.ip, addr, sizeof(struct in_addr) );
    msg->node.port = kc_contactGetPort( contact );
    msg->node.unused = 0;
    
    int status;
    status = kc_messageSetData( message, msg, sizeof(msg) );
    
    free( msg );
    return status;
}

int
ov_writePingReply( const kc_dht * dht, kc_message * message )
{
//    *size = 4; /* Proto type (1) + opcode (1) */
    struct ov_connect_reply * msg = malloc( sizeof(struct ov_connect_reply) );
    
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
    int status;
    size_t size = sizeof(struct ov_connect_reply) + 8 * sizeof(struct ov_node);
    status = kc_messageSetData( message, msg, size );
    free( msg );
    return status;
}

char *
ov_writePublicize( const kc_dht * dht, size_t * size )
{
    return NULL;
}

int
ov_parseCallback( const kc_dht * dht, kc_message * msg )
{
    /* We will only read required parts for routing here,
     * contact info, message type and hash (if available).
     * The remaining data should be left in the buffer and
     * processed by the read callback.
     */
    struct ov_header * header = (struct ov_header*)kc_messageGetData( msg );

    if( header->protoType != OP_EDONKEYHEADER )
    {
        kc_logAlert( "parseCallback: unknown protocol type %X", header->protoType );
        return 1;
    }
    
    switch( header->messageType )
    {
        case OVERNET_CONNECT:
            kc_logDebug( "parseCallback: got a OVERNET_CONNECT" );
            
            kc_messageSetType( msg, DHT_RPC_PING );
            return DHT_RPC_PING;
            break;
            
        case OVERNET_CONNECT_REPLY:
            kc_logDebug( "parseCallback: got a OVERNET_CONNECT_REPLY" );
            
            kc_messageSetType( msg, DHT_RPC_PING );
            return DHT_RPC_PING;
            
            break;
/*            case OVERNET_PUBLICIZE:
            kc_logDebug( "parseCallback: got a OVERNET_PUBLICIZE" );
            
            return 0;
            break;
            
            case OVERNET_PUBLICIZE_ACK:
            kc_logDebug( "parseCallback: got a OVERNET_PUBLICIZE_ACK" );
            break;
            
            case OVERNET_SEARCH:
            kc_logDebug( "parseCallback: got a OVERNET_SEARCH" );
            break;
            
            case OVERNET_PUBLISH:
            kc_logDebug( "parseCallback: got a OVERNET_PUBLISH" );
            break;
            */
            default:
            break;
    }            
    kc_logAlert( "parseCallback: unknown message type %d", header->messageType );
    
    kc_messageSetType( msg, DHT_RPC_UNKNOWN );
    return DHT_RPC_UNKNOWN;
}

int
ov_readCallback( kc_dht * dht, const kc_message * msg )
{
    const kc_contact * contact = kc_messageGetContact( msg );
    kc_logDebug( "readCallback: got a message from %s", kc_contactPrint( contact ) );
#if 0    
    switch( messageType )
    {
        case OVERNET_CONNECT:
            kc_logDebug( "parseCallback: got a OVERNET_CONNECT" );
            
            /*            *answer = malloc( sizeof(kc_dhtMsg) );
             (*answer)->node = msg->node;
             (*answer)->type = DHT_RPC_PING;*/
            
            return 0;
            break;
            
        case OVERNET_CONNECT_REPLY:
            kc_logDebug( "parseCallback: got a OVERNET_CONNECT_REPLY" );
            
            int nodeCount = 0;
            evbuffer_remove( buffer, &nodeCount, sizeof(int) ); //getushortle( &bp );
            // FIXME: API to get an evbuffer current size ?
            if( buffer->off != 4 + nodeCount * 23 )
            {
                kc_logAlert( "parseCallback: OVERNET_CONNECT_REPLY with bad size" );
                return -1;
            }
            
            int i;
            for( i = 0; i < nodeCount; i++ )
            {
                int type;
                struct in_addr addr;
                in_port_t port;
                
                int byteCount = dht->parameters->hashSize / 8;
                if( byteCount % 8 != 0 )
                    byteCount++;
                
                const char * hashPtr = calloc( byteCount, sizeof(char) );
                evbuffer_remove( buffer, (char*)hashPtr, byteCount );
                
                kc_hash    * hash = kc_hashInit( dht->parameters->hashSize );
                
                gethashn( hash, &hashPtr );
                evbuffer_remove( buffer, &addr, sizeof(struct in_addr) );
                //                addr = getipn( &bp );
                //                port = getushortle( &bp );
                evbuffer_remove( buffer, &port, sizeof(short int) );
                evbuffer_remove( buffer, &type, sizeof(int) );
                //                type = *bp++;
                
                kc_contact * newContact = kc_contactInit( &addr, sizeof(struct in_addr), port );
                type = kc_dhtAddNode( dht, newContact, hash );
                if( type == 0 )
                    kc_dhtCreateNode( dht, newContact );
                
            }
            return 0;
            
            break;
            case OVERNET_PUBLICIZE:
            kc_logDebug( "parseCallback: got a OVERNET_PUBLICIZE" );
            
            /*            *answer = malloc( sizeof(kc_dhtMsg) );
             (*answer)->node = msg->node;
             (*answer)->type = DHT_RPC_PROTOCOL_SPECIFIC;
             (*answer)->payload = ov_writePublicize( dht, &(*answer)->payloadSize );*/
            return 0;
            break;
            
            case OVERNET_PUBLICIZE_ACK:
            kc_logDebug( "parseCallback: got a OVERNET_PUBLICIZE_ACK" );
            break;
            
            case OVERNET_SEARCH:
            kc_logDebug( "parseCallback: got a OVERNET_SEARCH" );
            break;
            
            case OVERNET_PUBLISH:
            kc_logDebug( "parseCallback: got a OVERNET_PUBLISH" );
            break;
            
            default:
            kc_logAlert( "parseCallback: unknown message type %d", messageType );
            break;
    }            
#endif
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
            {
                return ov_writePing( dht, answer );
            }
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
                kc_contact * contact = kc_contactDup( kc_messageGetContact( msg ) );
                answer = kc_messageInit( contact, DHT_RPC_PING, 0, NULL );

                return ov_writePingReply( dht, answer );
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