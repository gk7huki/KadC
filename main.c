/*
 *  main.c
 *  KadC
 *
 *  Created by Etienne on 24/10/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "inifiles.h"
#include "logging.h"
#include "dht.h"
#include "int128.h"
#include "opcodes.h"
#include "bufio.h"

void
readCallback( const kc_dht * dht, const kc_udpMsg * msg );

int
writeCallback( const kc_dht * dht, dht_msg_type type, kc_udpMsg * msg );

int
main( int argc, const char* argv[] )
{
    kc_dht * dht;
    FILE* fd;
    in_addr_t * nodeAddr;
    in_port_t * nodePort;
    int nodeCount;
    in_addr_t addr;
    in_port_t port;

    if( argc < 2 )
    {
        kc_logPrint( KADC_LOG_ALERT, "You need to provide a configuration file !" );
        return EXIT_FAILURE;
    }
    
    kc_logPrint( KADC_LOG_DEBUG, "Opening file %s for reading", argv[1] );
    fd = fopen( argv[1], "r" );
    
    kc_iniParseLocalSection( fd, &addr, &port );
    kc_iniParseNodeSection( fd, "[overnet_peers]", &nodeAddr, &nodePort, &nodeCount ); 
    
    dht = kc_dhtInit( addr, port, 20, readCallback, writeCallback );
    if ( dht == NULL )
        return EXIT_FAILURE;
    
    int i;
    for( i = 0; i < nodeCount; i++ )
    {
        kc_dhtCreateNode( dht, nodeAddr[i], nodePort[i] );
    }
    
    
    int k;
    int j = 0;
    while ( j != 1 )
    {
        kc_logPrint( KADC_LOG_NORMAL, "%d nodes available in DHT", kc_dhtNodeCount( dht ) );
        sleep( 1 );
        if( k == 10 )
        {
            kc_dhtPrintTree( dht );
            k = 0;
        }
        k++;
    }
    
    kc_dhtFree( dht );
    
    return EXIT_SUCCESS;
}

char *
writeOvernetPing( const kc_dht * dht, int * size )
{
    char * buf = calloc( 25, sizeof(char*) );
    char * bp = buf;
    *size = 25;
    
	*bp++ = OP_EDONKEYHEADER;
	*bp++ = OVERNET_CONNECT;

    putint128n( &bp, kc_dhtGetOurHash( dht ) );
    /* FIXME : External IP needed here */
    putipn( &bp, kc_dhtGetOurIp( dht ) );
    putushortle( &bp, kc_dhtGetOurPort( dht ) );
    putushortle( &bp, 0 );
    
    return buf;
}

void
readCallback( const kc_dht * dht, const kc_udpMsg * msg )
{
    struct in_addr ad;
    ad.s_addr = msg->remoteIp;
    kc_logPrint( KADC_LOG_NORMAL, "readCallback: got a message from %s:%d", inet_ntoa( ad ), ntohs( msg->remotePort ) );
    kc_logPrint( KADC_LOG_NORMAL, "readCallback: message len %d", msg->payloadSize );
    char* bp = msg->payload;
    
    if( *bp++ != (char)OP_EDONKEYHEADER )
    {
        kc_logPrint( KADC_LOG_NORMAL, "readCallback: unknown protocol type %X", msg->payload[0] );
        return;
    }
    
    switch( *bp++ )
    {
        case (char)OVERNET_CONNECT:
            break;
            
        case (char)OVERNET_CONNECT_REPLY:
            ;
            kc_logPrint( KADC_LOG_DEBUG, "readCallback: got a OVERNET_CONNECT_REPLY" );
            int nodeCount;
            nodeCount = getushortle( &bp );
            
            if( msg->payloadSize != 4 + nodeCount * 23 )
            {
                kc_logPrint( KADC_LOG_ALERT, "readCallback: OVERNET_CONNECT_REPLY with bad size" );
                return;
            }
                
            int i;
            for( i = 0; i < nodeCount; i++ )
            {
                int128      hash = int128random();
                in_addr_t   addr;
                in_port_t   port;
                int type;
                getint128n( hash, &bp );
                addr = getipn( &bp );
                port = getushortle( &bp );
                type = *bp++;
                kc_dhtAddNode( dht, addr, port, hash );
            }
            
            break;
            
        default:
            kc_logPrint( KADC_LOG_NORMAL, "readCallback: unknown message type %d", msg->payload[1] );
            break;
    }
}

int
writeCallback( const kc_dht * dht, dht_msg_type type, kc_udpMsg * msg )
{
    switch ( type )
    {
        case DHT_RPC_PING:
        {
            msg->payload = writeOvernetPing( dht, &msg->payloadSize );
            
            return 0;
        } 
            break;
            
        default:
            break;
    }
    
    return 0;
}
