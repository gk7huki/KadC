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

/** @file dht.h
 * This file provide an implementation of a Kademlia Distributed Hash Table (DHT).
 *
 * It is based on the the design specification available at
 * http://xlattice.sourceforge.net/components/protocol/kademlia/specs.html .
 *
 * Other interesting papers regarding DHT :
 * http://xlattice.sourceforge.net/components/protocol/kademlia/references.html .
 */

#ifndef KADC_ROUTING_H
#define KADC_ROUTING_H

#include <sys/types.h>

#include <net.h>
#include <int128.h>

/**
 * A DHT structure
 */
typedef struct _kc_dht kc_dht;

/**
 *A DHT node
 */
typedef struct _kc_dhtNode kc_dhtNode;

/**
 * A DHT message type.
 *
 * This is used in the DHT write callback.
 * @see kc_dhtWriteCallback
 */
typedef enum {
    DHT_RPC_PING = 0,
    DHT_RPC_STORE,
    DHT_RPC_FIND_NODE,
    DHT_RPC_FIND_VALUE
} kc_dhtMsgType;

/**
 * The callback protoype used when the DHT needs to write a packet.
 *
 * This callback is called as soon as the DHT needs to send data to another node.
 * You'll get a pointer to the kc_dht, a dht_msg_type specifiying the type of message
 * you should write, and a pointer to the message that will be sent.
 *
 * @param dht The DHT willing to communicate
 * @param type The type of message to write
 * @param msg A malloc()ed pointer to a kc_udpMsg. You'll need to malloc() msg->payload,
 * and set msg->payloadSize accordingly... The pointer will be freed after use.
 * @return You should return 0 if you want the msg message sent, or 1 if you don't.
 */
typedef int (*kc_dhtWriteCallback)( const kc_dht * dht, kc_dhtMsgType type, kc_udpMsg * msg );

/**
 * The callback protoype used when the DHT needs to read a packet.
 *
 * This callback is called as soon as the DHT needs to parset data from another node.
 * You'll get a pointer to the kc_dht, a pointer to the recieved kc_udpMsg, and a pointer to
 * the correct message to reply to the sender node.
 *
 * @param dht The DHT willing to communicate.
 * @param msg A kc_udpMsg containing the data recieved from the node.
 * @param answer A kc_udpMsg containing the data the DHT should sent to the node as a reply. You'll need to malloc() msg->payload, and set msg->payloadSize accordingly... The pointer will be freed after use.
 * @return You should return 0 if you want the answer message sent, or 1 if you don't.
 */
typedef int (*kc_dhtReadCallback)( const kc_dht * dht, const kc_udpMsg * msg, kc_udpMsg * answer );


/** 
 * Creates and init a new kc_dhtInit.
 *
 * This function handles the successful creation of a kc_dht structure.
 
 * @param addr Our local IP address, in host byte-order
 * @param port Our local port, in host byte-order
 * @param bucketMaxSize The size of the kBuckets inside of the DHT
 * @param readCallback A callback function called when the DHT need to parse a message from the network
 * @param writeCallback A callback function called when the DHT need to write a message to the network 
 * @return A pointer to an initialized kc_dht
 */
kc_dht*
kc_dhtInit( in_addr_t addr, in_port_t port, int bucketMaxSize, kc_dhtReadCallback readCallback, kc_dhtWriteCallback writeCallback );

/** 
 * Frees an existing kc_dhtInt structure.
 *
 * This function is responsible for the correct deallocation of a kc_dht structure
 *
 * @param dht The kc_dht to free
 */
void
kc_dhtFree( kc_dht * dht );

/**
 * Schedule a node for addition in the DHT.
 *
 * This function takes an IP address/port, and will subsequently issue a PING to 
 * the corresponding node to obtain its nodeId, before adding it to the DHT.
 *
 * @param dht The kc_dht in which to add the node
 * @param addr The node's IP address, in host byte-order
 * @param port The node's port number, in host byte-order
 */
void
kc_dhtCreateNode( const kc_dht * dht, in_addr_t addr, in_port_t port);

/**
 * Adds a node to the DHT.
 *
 * This method is here for protocol-implementors to use when a node is to be added to the DHT.
 * 
 * @param dht The DHT in which to add this node
 * @param addr The node's IP address, in host byte-order
 * @param port The node's port number, in host byte-order
 * @param hash The node's hash
 * @return This function returns 0 on success, -1 on failure, and 1 if the node was already known
 */
int
kc_dhtAddNode( const kc_dht * dht, in_addr_t addr, in_port_t port, int128 hash );


/**
 * Store a key/value pair in the DHT.
 *
 * This function takes a key/value pair, store it in the DHT
 *
 * @param dht The DHT in which to store this key/value.
 * @param key The key to store in the DHT.
 * @param value The value associated with the above key.
 * @return This function returns 0 on success, -1 otherwise.
 */
int
kc_dhtStoreKeyValue( const kc_dht * dht, void * key, void * value );

/**
 * Outputs the DHT structure to stdout.
 *
 * This is for debugging purposes. Each entry in the DHT is output...
 * @param dht The DHT to print
 */
void
kc_dhtPrintTree( const kc_dht * dht );

/**
 * Returns the number of nodes currently known in a DHT.
 * 
 * @param dht The kc_dht whose nodes to count
 * @return The number of currently known good nodes
 */
int
kc_dhtNodeCount( const kc_dht *dht );

/**
 * Remove all nodes from the DHT.
 *
 * This function clears the list of known nodes and free them
 * @param dht The kc_dht to clear
 * @return The number of nodes removed
 */
int
kc_dhtRemoveNodes( kc_dht *dht );

/** 
 * Returns a list of nodes
 *
 * The list will contain MIN( currentNodeCount, dhtBucketSize ).
 *
 * @param dht The kc_dht to clear
 * @param nodeCount A pointer that will be set to the count of returned node
 * @return An array of kc_dhtNodes
 */
const kc_dhtNode**
kc_dhtGetNode( const kc_dht * dht, int * nodeCount );

/**
 * Gets the IP address of the local node.
 *
 * @param dht The kc_dht to get the local IP from
 * @return The local IP address
 */
in_addr_t
kc_dhtGetOurIp( const kc_dht * dht );

/**
 * Gets the port number of the local node.
 *
 * @param dht The kc_dht to get the port number from
 * @return The local port number
 */
in_port_t
kc_dhtGetOurPort( const kc_dht * dht );

/**
 * Gets the hash (identifier) of the local node.
 *
 * @param dht The kc_dht to get the local hash from
 * @return The local node's hash value
 */
int128
kc_dhtGetOurHash( const kc_dht * dht );


/**
 * Gets the IP address of a node.
 *
 * @param node The kc_dhtNode to get the IP from
 * @return The IP address of the node
 */
in_addr_t
kc_dhtNodeGetIp( const kc_dhtNode * node );

/**
 * Gets the port number of a node.
 *
 * @param node The kc_dhtNode to get the port number from
 * @return The port number of the node
 */
in_port_t
kc_dhtNodeGetPort( const kc_dhtNode * node );

/**
 * Gets the hash (identifier) of a node.
 *
 * @param node The kc_dhtNode to get the hash from
 * @return The hash of the node
 */
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