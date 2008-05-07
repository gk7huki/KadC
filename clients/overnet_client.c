/*
 *  main.c
 *  KadC
 *
 *  Created by Etienne on 24/10/07.
 *  Copyright 2007 __MyCompanyName__. All rights reserved.
 *
 */

#include <kadc/kadc.h>
#include "overnet.h"
#include "overnet_opcodes.h"

int
main( int argc, const char* argv[] )
{
    kc_dht * dht;
    FILE* iniFile;
    kc_contact **contacts;
    int nodeCount;
    kc_contact *contact;
    kc_hash *hash;
    
#if 0
    // Debug code to test my overnet structs size
#define qm(x) "sizeof %s = %d", #x, sizeof(x)
    kc_logNormal( qm(struct ov_node) );
    kc_logNormal( qm(struct ov_header) );
    kc_logNormal( qm(struct ov_connect) );
    kc_logNormal( qm(struct ov_connect_reply) );
    kc_logNormal( "sizeof ov_connect_reply = %d", sizeof(struct ov_connect_reply) + sizeof(struct ov_node) * 23);
#endif
    
    if( argc < 2 )
    {
        kc_logError( "You need to provide a configuration file !" );
        return EXIT_FAILURE;
    }
    
    kc_logNormal( "Opening file %s for reading", argv[1] );
    iniFile = fopen( argv[1], "r" );
    if( iniFile == NULL )
    {
        kc_logError( "Failed opening configuration file %s for reading: %s", argv[1], strerror( errno ) );
        return EXIT_FAILURE;
    }
    
    kc_iniParseLocalSection( iniFile, &contact, &hash );
    contacts = kc_iniParseNodeSection( iniFile, "[overnet_peers]", &nodeCount ); 
    fclose( iniFile );
    
    kc_logNormal( "%s", kc_contactPrint( contact ) );
    
    dht = kc_dhtInit( hash, &ov_parameters );
    if ( dht == NULL )
        return EXIT_FAILURE;
    
    kc_logNormal( "%s", kc_contactPrint( contact ) );
    
    if( hash != NULL )
        kc_hashFree( hash );
    
    kc_dhtAddIdentity( dht, contact );
    
    kc_dhtPrintState( dht );
    
    int i;
    for( i = 0; i < nodeCount; i++ )
    {
        kc_dhtCreateNode( dht, contacts[i] );
        kc_contactFree( contacts[i] );
    }
    
    int k;
    int j = 0;
    while ( j != 1 )
    {
        kc_logNormal( "%d nodes available in DHT", kc_dhtNodeCount( dht ) );
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
    }
    
    kc_dhtFree( dht );
    
    return EXIT_SUCCESS;
}
