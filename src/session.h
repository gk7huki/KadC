/*
 *  session.h
 *  KadC
 *
 *  Created by Etienne on 16/05/08.
 *  Copyright 2008 Etienne Samson. All rights reserved.
 *
 */

#ifndef __KADC_SESSION_H__
#define __KADC_SESSION_H__

typedef int (*kc_sessionCallback)( const kc_dht * dht, const kc_message * msg );

typedef struct _kc_session kc_session;

kc_session *
kc_sessionInit( kc_dht * dht, kc_contact * connectContact, kc_messageType type, int incoming, kc_sessionCallback callback );

void
kc_sessionFree( kc_session * session );

int
kc_sessionCmp( const void *a, const void *b );

int
kc_sessionStart( kc_session * session );

int
kc_sessionSend( kc_session * session, kc_message * message );

int
kc_sessionRecieved( kc_session * session, kc_message * message );

const kc_contact *
kc_sessionGetContact( const kc_session * session );

kc_messageType
kc_sessionGetType( const kc_session * session );

char *
kc_sessionPrint( const kc_session * session );

#endif /* __KADC_SESSION_H__ */