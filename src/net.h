/****************************************************************\

Copyright 2004 Enzo Michelangeli

This file is part of the KadC library.

KadC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

KadC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with KadC; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

In addition, closed-source licenses for this software may be granted
by the copyright owner on commercial basis, with conditions negotiated
case by case. Interested parties may contact Enzo Michelangeli at one
of the following e-mail addresses (replace "(at)" with "@"):

 em(at)em.no-ip.com
 em(at)i-t-vision.com

\****************************************************************/
#ifndef _KADC_NET_H
#define _KADC_NET_H

/** @file net.h
 * This file provide UDP communication capabilities.
 */

/**
 * A typedef for referring to a kc_udpIo object.
 */
typedef struct _kc_udpIo kc_udpIo;

int
kc_netOpen( int type, int domain );

int
kc_netBind( int fd, kc_contact * contact );

int
kc_netConnect( int fd, kc_contact * contact );

int
kc_netSetNonBlockingSocket( int socket );

void
kc_netClose( int socket );

/** 
 * The callback prototype used when a UDP message arrives.
 *
 * @see kc_udpIoInit.
 * @param ref Pointer to the ref passed at the kc_udpIO creation
 * @param io Pointer to the kc_udpIo object that called-back.
 * @param msg Pointer to the recieved message.
 */
typedef void (*kc_ioCallback)( void * ref, kc_udpIo * io, kc_message *msg );

/** 
 * Creates and initialize a kc_udpIo for UDP input/output handling.
 *
 * This function creates a kc_udpIo listener calling its callback on a
 * separate thread every time a UDP packet is received.
 * FIXME: Is this still right ?
 * The callback is guaranteed not to interrupt itself: it must
 * return before its thread issue another recvfrom() and calls
 * it again. To allow multiple listeners to work concurrently,
 * all the context is kept in the kc_udpIo structure.
 *
 * @see kc_ioCallback.
 * @param addr Our local IP, in network byte-order
 * @param port Our local port to bind to, in network byte-order
 * @param bufferSize The size of the incoming buffer
 * @param callback The callback that will be called when a packet is recieved
 * @param ref A pointer that will be passed as the first argument of the callback.
 * @return An initialized kc_udpIo
 */
kc_udpIo *
kc_udpIoInit( struct in_addr addr, struct in6_addr addr6, in_port_t port, int bufferSize, kc_ioCallback callback, void * ref );

/** 
 * Cleanup and free a kc_udpIo.
 *
 * This function stops the processing threads, then closes the corresponding socket.
 *
 * @param io A kc_udpIo to free
 */
void
kc_udpIoFree( kc_udpIo * io );

/** 
 * Send a message to another node.
 *
 * This function uses a kc_udpIo to send a kc_udpMsg to another node.
 * 
 * @param io The kc_udpIo to use to send the message
 * @param msg The kc_udpMsg to send
 * @return The number of bytes sent, or < 0 if an error occured
 */
int
kc_udpIoSendMsg( kc_udpIo * io, kc_message * msg );

/**
 * A inet_ntoa wrapper for direct in_addr_t conversion.
 * @see inet_ntoa()
 * @param addr_t A network byte-ordered IP address.
 */
char *
addr_ntoa( in_addr_t addr_t );

/**
 * A inet_ntoa wrapper for direct in_addr_t conversion.
 * @see inet_ntoa()
 * @param addr_t A host byte-ordered IP address.
 */
#define addr_htoa( addr ) addr_ntoa( htonl( addr ) )

/** 
 * A multithreading-safe gethostbyname
 *
 * @see gethostbyname
 */
in_addr_t gethostbyname_s(const char *domain);

/** 
 * Check if an address is in one of the reserved (a.k.a private) networks.
 *
 * This function returns true if the ip address is non-routable
 *
 * @param ip The IP address to check, in host byte order
 * @return 0 if the address is not a routable address, 1 otherwise
 */
int inet_isnotroutable(in_addr_t ip);

/** 
 * Check if an address is local to this machine.
 *
 * This function returns true if the passed-in IP address is
 * currently assigned to a local interface
 *
 * @param ip The IP address to check, in host byte order
 * @return 0 if the address is local, 1 otherwise 
 */
int inet_islocal(in_addr_t ip);

int node_is_blacklisted(kc_udpIo * io, in_addr_t ip, in_port_t port);

int node_blacklist(kc_udpIo * io, in_addr_t ip, in_port_t port, int howmanysecs);

int node_unblacklist(kc_udpIo * io, in_addr_t ip, unsigned short int port);

/* write in ASCII to wfile the list of all entries in pul->blaclklisted */
int node_blacklist_dump(kc_udpIo * io, FILE *wfile);

/* read from inifile the list of all entries to be loaded in pul->blaclklisted */
int node_blacklist_load(kc_udpIo * io, FILE *inifile);

/* remove expired entried from pul->blacklisted rbt, or ALL entries if
   unconditional == 1. Return number of removed entries */
int node_blacklist_purge(kc_udpIo * io, int unconditional);


#ifdef __WIN32__

/* nErrorID is the code returned in WIN32 by WSAGetLastError() */
char *WSAGetLastErrorMessage(int nErrorID);
char *WSAGetLastErrorMessageOccurred(void);

int wsockstart(void);
void wsockcleanup(void);
#endif

#endif /* _KADC_NET_H */