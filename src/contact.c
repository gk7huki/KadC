/*
 *  contact.c
 *  KadC
 *
 *  Created by Etienne on 14/02/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include "contact.h"

struct _kc_contact
{
    void      * addr;
    int         type;   /* AF_INET or AF_INET6 */
    in_port_t   port;
};

kc_contact *
kc_contactInit( void * addr, size_t len, in_port_t port )
{
    kc_contact * self;
    self = malloc( sizeof(kc_contact*) );
    if( !self )
        return NULL;
    
    switch( len )
    {
        case sizeof(struct in_addr):
            self->type = AF_INET;
            self->addr = malloc( sizeof(struct in_addr*) );
            memcpy( self->addr, addr, sizeof(struct in_addr) );
            break;
            
        case sizeof( struct in6_addr ):
            self->type = AF_INET6;
            self->addr = malloc( sizeof(struct in6_addr*) );
            memcpy( &self->addr, addr, sizeof(struct in6_addr) );
            break;
            
        default:
            kc_logAlert( "Invalid length for addr parameter, len: %d", sizeof(*addr) );
            free( self );
            return NULL;
    }
    self->port = port;
    
    return self;
}

kc_contact *
kc_contactDup( const kc_contact * contact )
{
    assert( contact != NULL );
    
    int length;
    int type = kc_contactGetType( contact );
    switch( type )
    {
        case AF_INET: {
            length = sizeof(struct in_addr);
        }
            break;
        case AF_INET6: {
            length = sizeof(struct in6_addr);
        }
            break;
        default:
            return NULL;
            break;
    }
    
    return kc_contactInit( kc_contactGetAddr( contact ), length, kc_contactGetPort( contact ) );
}

void
kc_contactFree( kc_contact * contact )
{
    free( contact->addr );
    free( contact );
}

int kc_contactCmp( const void * a, const void * b)
{
    const kc_contact * ca = a;
    const kc_contact * cb = b;
    
    if( ca->type != cb->type )
        return ( ca->type < cb->type ? 1 : -1 );
    
    int res;
    if( ca->type )
        res = memcmp( ca->addr, cb->addr, sizeof(struct in_addr) );
    else
        res = memcmp( ca->addr, cb->addr, sizeof(struct in6_addr) );
    
    if( res != 0 )
        return res;
    
    if( ca->port != cb->port )
        return ( ca->port < cb->port ? -1 : 1 );
    
    return 0;
}

const void *
kc_contactGetAddr( const kc_contact * contact )
{
    assert( contact != NULL );
    return contact->addr;
}

in_port_t
kc_contactGetPort( const kc_contact * contact )
{
    assert( contact != NULL );
    return contact->port;
}

int
kc_contactGetType( const kc_contact * contact )
{
    assert( contact != NULL );
    return contact->type;
}

void
kc_contactSetAddr( kc_contact * contact, void * addr, size_t length )
{
    assert( contact != NULL );
    switch( length )
    {
        case sizeof(struct in_addr):
            memcpy( contact->addr, addr, sizeof(struct in_addr) );
            break;
        case sizeof(struct in6_addr):
            memcpy( contact->addr, addr, sizeof(struct in6_addr) );
            break;
        default:
            kc_logAlert( "Unknown address length %d", length );
            break;
    }
}

void
kc_contactSetPort( kc_contact * contact, in_port_t port )
{
    assert( contact != NULL );
    contact->port = port;
}

char *
kc_contactPrint( const kc_contact * contact )
{
    assert( contact != NULL );
    
    int addrLen;
    switch( contact->type )
    {
        case AF_INET:
            /* sizeof("255.255.255.255\0") */
            addrLen = 16;
            break;
        case AF_INET6:
            /* sizeof("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff\0") */
            addrLen = 40;
            break;
        default:
            kc_logDebug( "Non-IP protocol address, ignoring..." );
            return "(Unknown)";
    }
    
    static char * addrStr;
    if( addrStr == NULL )
        addrStr = calloc( addrLen, sizeof(char) );
    assert( addrStr != NULL );
            
    inet_ntop( contact->type, contact->addr, addrStr, addrLen );
    
    static char * contactStr = NULL;
    if( contactStr == NULL )
        contactStr = malloc( sizeof(char*) );
    assert( contactStr != NULL );
    
    sprintf( contactStr, "%s:%d", addrStr, contact->port );
    free( addrStr );
    return contactStr;
}
