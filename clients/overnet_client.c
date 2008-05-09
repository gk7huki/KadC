/*
 *  main.c
 *  KadC
 *
 *  Created by Etienne on 24/10/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */

#include <kadc/kadc.h>
#include <unistd.h>
#include "overnet.h"
#include "overnet_opcodes.h"

void usage( int exitCode )
{
    exit( exitCode );
}
int
main( int argc, const char* argv[] )
{
    kc_dht * dht;
    FILE* iniFile;
    kc_contact **contacts;
    int nodeCount;
    kc_contact *contact = NULL;
    kc_hash *hash = NULL;
    
    int opt;
    char * port = NULL;
    char * addr = NULL;
    while ( ( opt = getopt( argc, (char * const *)argv, "hf:a:p:") ) != -1 )
    {
        switch ( opt ) {
            case 'a':
                addr = calloc( strlen( optarg ), sizeof(char) );
                strcpy( addr, optarg );
                break;
            case 'f':
                kc_logNormal( "Opening file %s for reading", optarg );
                iniFile = fopen( optarg, "r" );
                if( iniFile == NULL )
                {
                    kc_logError( "Failed opening configuration file %s for reading: %s", argv[1], strerror( errno ) );
                    exit( EXIT_FAILURE );
                }
                
                kc_iniParseLocalSection( iniFile, &contact, &hash );
                contacts = kc_iniParseNodeSection( iniFile, "[overnet_peers]", &nodeCount ); 
                fclose( iniFile );
                
                break;                
            case 'h':
                usage(EXIT_SUCCESS);
                break;
            case 'p':
                port = calloc( strlen( optarg ), sizeof(char) );
                strcpy( port, optarg );
                break;     
            default:
                usage(EXIT_FAILURE);
                break;
        }
    }
    argc -= optind;
    argv += optind;
    if( port != NULL && addr != NULL )
    {
        if( contact != NULL )
            kc_contactFree( contact );
        contact = kc_contactInitFromChar( addr, port );
    }
    if( contact == NULL )
    {
        fprintf( stderr, "No local address set. Use -a & -p or -f\n" );
        usage( EXIT_FAILURE );
    }
    
    kc_logNormal( "%s", kc_contactPrint( contact ) );
    
#if 0
    // Debug code to test my overnet structs size
#define qm(x) "sizeof %s = %d", #x, sizeof(x)
    kc_logNormal( qm(struct ov_node) );
    kc_logNormal( qm(struct ov_header) );
    kc_logNormal( qm(struct ov_connect) );
    kc_logNormal( qm(struct ov_connect_reply) );
    kc_logNormal( "sizeof ov_connect_reply = %d", sizeof(struct ov_connect_reply) + sizeof(struct ov_node) * 23);
#endif
    
    dht = kc_dhtInit( hash, &ov_parameters );
    if ( dht == NULL )
        return EXIT_FAILURE;
    
    if( hash != NULL )
        kc_hashFree( hash );
        
    kc_dhtAddIdentity( dht, contact );
    
//    kc_dhtPrintState( dht );
    
/*    kc_contact * otherContact = kc_contactInit( &addr, sizeof(struct in_addr), 5678 );
    kc_dhtCreateNode( dht, otherContact );*/
    
/*    int i;
    for( i = 0; i < nodeCount; i++ )
    {
        kc_dhtCreateNode( dht, contacts[i] );
        kc_contactFree( contacts[i] );
    }*/
    
    
    int status;
    do {
        status = kc_iniParseCommand( stdin, dht );
    }
    while ( status == 0 );
        
/*    int k;
    int j = 0;
    while ( j != 1 )
    {
//        kc_logNormal( "%d nodes available in DHT", kc_dhtNodeCount( dht ) );
        sleep( 1 );
        if( k % 10 == 0 )
        {
            kc_dhtPrintTree( dht );
        }
        if( k % 100 == 0)
        {
            kc_dhtPrintState( dht );
            kc_dhtPrintKeys( dht );
        }
        
        k++;
    }*/
    
    kc_dhtFree( dht );
    
    return EXIT_SUCCESS;
}
