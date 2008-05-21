/*
 *  contact.c
 *  KadC
 *
 *  Created by Etienne on 14/02/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include "contact.h"

/* Maybe I could use sockaddrs directly ? */
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
kc_contactInitFromSockAddr( struct sockaddr * addr, size_t addrLen )
{
    kc_contact * contact;
    switch( addrLen ) {
        case sizeof(struct sockaddr_in): {
            struct sockaddr_in * sock_in;
            struct in_addr addr4;
            sock_in = (struct sockaddr_in*)addr;
            addr4 = sock_in->sin_addr;
            contact = kc_contactInit( &addr4, sizeof(addr4), ntohs( sock_in->sin_port ) );
        }
            break;
            
        case sizeof(struct sockaddr_in6): {
            struct sockaddr_in6 * sock_in6;
            struct in6_addr addr6;
            sock_in6 = (struct sockaddr_in6*)addr;
            addr6 = sock_in6->sin6_addr;
            contact = kc_contactInit( &addr6, sizeof(addr6), ntohs( sock_in6->sin6_port ) );
        }
            break;
            
        default:
            break;
    }
    return contact;
}

kc_contact *
kc_contactInitFromChar( char * address, char * port )
{
    kc_contact * contact;
    
    struct in_addr addr;
    struct in6_addr addr6;
    int status = 0;
    /* Try parsing IPv4 address */
    status = inet_pton( AF_INET, address, &addr );
    if( status == 1 )
    {
        /* This is a valid IPv4 address, create a contact from it */
        contact = kc_contactInit( &addr, sizeof(struct in_addr), atoi( port ) );
        return contact;
    }
    if( status == 0 )
    {
        /* Failed with invalid address for proto type, retry parsing in IPv6 */
        status = inet_pton( AF_INET6, address, &addr6 );
        if( status == 1 )
        {
            /* This is a valid IPv6 address, create a contact from it */
            contact = kc_contactInit( &addr6, sizeof(struct in6_addr), atoi( port ) );
            return contact;
        }
    }
    
    /* We failed, report */
    if( status == -1 )
    {
        kc_logError( "System error while parsing contact address: %s", strerror( errno ) );
        return NULL;
    }
    
    return contact;
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

int
kc_contactGetDomain( const kc_contact * contact )
{
    assert( contact != NULL );
#warning TODO: In case someone wants to watch a DHT running over TCP...
    return SOCK_DGRAM;
    
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
    else
        addrStr = realloc( addrStr, addrLen * sizeof(char) );
    assert( addrStr != NULL );
            
    inet_ntop( contact->type, contact->addr, addrStr, addrLen );
    
    static char * contactStr = NULL;
    if( contactStr == NULL )
        contactStr = malloc( sizeof(char*) );
    assert( contactStr != NULL );
    
    sprintf( contactStr, "%s:%d", addrStr, contact->port );
    return contactStr;
}
