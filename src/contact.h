/*
 *  contact.h
 *  KadC
 *
 *  Created by Etienne on 14/02/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef __KADC_CONTACT_H__
#define __KADC_CONTACT_H__

typedef struct _kc_contact kc_contact;

kc_contact *
kc_contactInit( void * addr, size_t length, in_port_t port );

kc_contact *
kc_contactInitFromChar( char * address, char * port );

void
kc_contactFree( kc_contact * contact );

kc_contact *
kc_contactDup( const kc_contact * contact );

int
kc_contactCmp( const void * a, const void * b);

const void *
kc_contactGetAddr( const kc_contact * contact );

in_port_t
kc_contactGetPort( const kc_contact * contact );

int
kc_contactGetType( const kc_contact * contact );

void
kc_contactSetAddr( kc_contact * contact, void * addr, size_t length );

void
kc_contactSetPort( kc_contact * contact, in_port_t addr );

char *
kc_contactPrint( const kc_contact * contact );
#endif