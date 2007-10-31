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

#ifndef KADC_ROUTING_H
#define KADC_ROUTING_H

#include <sys/types.h>

#include <net.h>
#include <int128.h>

typedef struct _kc_dht kc_dht;
typedef struct _kc_dhtNode kc_dhtNode;

typedef enum {
    DHT_RPC_PING = 0
//    DHT_RPC_
//    DHT_RPC_,
} dht_msg_type;

/**
 * Callback called when the dht needs to write a packet
 * This callback is called as soon as the DHT needs to send data to another node.
 * You'll get a pointer to the kc_dht, a dht_msg_type specifiying the type of message
 * you should write, and a pointer to the message that will be sent.
 *
 * @param dht The DHT willing to communicate
 * @param type The type of message to write
 * @param msg A malloc()ed pointer to a kc_udpMsg. You'll need to malloc() msg->payload,
 * and set msg->payloadSize accordingly... The pointer will be freed after use.
 * @return 
 */
typedef int (*kc_dhtWriteCallback)( const kc_dht * dht, dht_msg_type type, kc_udpMsg * msg );

typedef int (*kc_dhtReadCallback)( const kc_dht * dht, const kc_udpMsg * msg, kc_udpMsg * answer );


/**
 * Creates and init a new kc_dhtInit
 * This function handles the successful creation of a kc_dht structure.
 * @param addr Our local IP address, in host byte-order
 * @param port Our local port, in host byte-order
 * @param callback A callback function called when there's a need to parse/write a message to the network
 */
kc_dht*
kc_dhtInit( in_addr_t addr, in_port_t port, int bucketMaxSize, kc_dhtReadCallback readCallback, kc_dhtWriteCallback writeCallback );

/**
 * Frees an existing kc_dhtInt structure
 * This function is responsible for the correct deallocation of a kc_dht structure
 * @param dht The kc_dht to free
 */
void
kc_dhtFree( kc_dht * dht );

/**
 * Schedule a node for addition in the DHT
 * This function takes an IP address/port, and will subsequently issue a PING to 
 * the corresponding node to obtain its nodeId, before adding it to the DHT.
 * @param dht The kc_dht in which to add the node
 * @param addr The node's IP address, in host byte-order
 * @param port The node's port number, in host byte-order
 */
void
kc_dhtCreateNode( const kc_dht * dht, in_addr_t addr, in_port_t port);

/**
 * Adds a node to the DHT
 * This method is here for protocol-implementors to use
 TODO Finish
 */
void
kc_dhtAddNode( const kc_dht * dht, in_addr_t addr, in_port_t port, int128 hash );

/**
 * Outputs the kc_dht structure to stdout
 * This is for debugging purposes. Each entry in the DHT is output...
 * @param dht The kc_dht to print
 */
void
kc_dhtPrintTree( const kc_dht * dht );

/**
 * Returns the number of nodes currently known in a DHT
 * 
 * @param dht The kc_dht whose nodes to count
 * @return The number of currently known good nodes
 */
int
kc_dhtNodeCount( const kc_dht *dht );

/**
 * Remove all nodes from a kc_dht
 * This function clears the list of known nodes and free them
 * @param dht The kc_dht to clear
 * @return The number of nodes removed
 */
int
kc_dhtRemoveNodes( kc_dht *dht );

const kc_dhtNode**
kc_dhtGetNode( const kc_dht * dht, int * nodeCount );

in_addr_t
kc_dhtGetOurIp( const kc_dht * dht );

in_port_t
kc_dhtGetOurPort( const kc_dht * dht );

int128
kc_dhtGetOurHash( const kc_dht * dht );


in_addr_t
kc_dhtNodeGetIp( const kc_dhtNode * node );

in_port_t
kc_dhtNodeGetPort( const kc_dhtNode * node );

int128
kc_dhtNodeGetHash( const kc_dhtNode * node );

/*void setup_kba(KadEngine *pKE, int kbsize);
void destroy_kba(KadEngine *pKE);
void dump_kba(KadEngine *pKE);
void dump_kspace(KadEngine *pKE);*/

/* Seek a knode in kspace and kbuckets.
   If not in kbuckets and kspace,
   			if(isalive) try to add it.

   If it's already there:
   			If isalive reset type to 0;
   			If ! isalive , increment type;
   			If type >= 5, remove the corresponding knode from both tables and free it

   In any case, ppn is left alone (not free'd).
   Returns:

   	-1 our own node or non-routable address: nothing done

	if isalive:
	 0 added or left there
     1 could no be added because bucket full

   	if ! isalive:
   	 0 it was, and was left there, or was not there already
   	 1 it was there, but it was removed due to type >= 5

 */
//int UpdateNodeStatus(peernode *ppn, KadEngine *pKE, int isalive);


#endif /* KADC_ROUTING_H */