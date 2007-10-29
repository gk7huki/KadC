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
    
    kc_dhtPrintTree( dht );
    
    int j = 0;
    while ( j != 1 )
        sleep( 10 );
    
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
    kc_logPrint( KADC_LOG_NORMAL, "readCallback: got a message from %s:%d\n", inet_ntoa( ad ), ntohs( msg->remotePort ) );
    kc_logPrint( KADC_LOG_NORMAL, "readCallback: message len %d: %s\n", msg->payloadSize, msg->payload );
}

int
writeCallback( const kc_dht * dht, dht_msg_type type, kc_udpMsg * msg )
{
    switch ( type )
    {
        case DHT_RPC_PING:
        {
            /*
            int128  otherHash;
            char    buf[33];
            
            otherHash = int128random();
            struct in_addr ad;
            ad.s_addr = ntohl( msg->remoteIp );
            
            kc_logPrint( KADC_LOG_NORMAL, "%s:%d, generated %s\n", inet_ntoa( ad ), msg->remotePort, int128sprintf( buf, otherHash ) );
            
            kc_dhtAddNode( dht, msg->remoteIp, msg->remotePort, otherHash );*/

            msg->payload = writeOvernetPing( dht, &msg->payloadSize );
            
            return 0;
        } 
            break;
            
        default:
            break;
    }
    
    return 0;
}
