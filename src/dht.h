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

/**
 * A DHT structure
 */
typedef struct _kc_dht kc_dht;

/**
 * A DHT node
 */
typedef struct _kc_dhtNode kc_dhtNode;

typedef struct _kc_dhtParameters kc_dhtParameters;

/** 
 * Creates and init a new kc_dhtInit.
 *
 * This function handles the successful creation of a kc_dht structure.
 *
 * @param hash Our local hash
 * @param parameters A struct containing various settings and callback functions.
 * @return A pointer to an initialized kc_dht
 */
kc_dht*
kc_dhtInit( kc_hash * hash, kc_dhtParameters * parameters );

/** 
 * Frees an existing kc_dht structure.
 *
 * This function is responsible for the correct deallocation of a kc_dht structure
 *
 * @param dht The kc_dht to free
 */
void
kc_dhtFree( kc_dht * dht );

int
kc_dhtAddIdentity( kc_dht * dht, kc_contact * contact );

/**
 * Schedule a node for addition in the DHT.
 *
 * This function takes an IP address/port, and will subsequently issue a PING to 
 * the corresponding node to obtain its hash, before adding it to the DHT.
 *
 * @param dht The kc_dht in which to add the node
 * @param contact The node's contact info.
 */
void
kc_dhtCreateNode( kc_dht * dht, kc_contact * contact );

/**
 * Adds a node to the DHT.
 *
 * This method is here for protocol-implementors to use when a node is to be added to the DHT.
 * The contact and hash passed will be retained by the DHT. Do not free them.
 * 
 * @param dht The DHT in which to add this node.
 * @param contact The node's contact info.
 * @param hash The node's hash.
 * @return This function returns 0 on success, -1 on failure, and 1 if the node was already known.
 */
int
kc_dhtAddNode( kc_dht * dht, kc_contact * contact, kc_hash * hash );

/**
 * Store a key/value pair in the DHT.
 *
 * This function takes a key/value pair to be stored in the DHT.
 * If there's already a value for this key, it will be exchanged, and value will point to the old value.
 *
 * @param dht The DHT in which to store this key/value.
 * @param key The key to store in the DHT.
 * @param value The value associated with the above key.
 * @return This function returns 0 on success, 1 if there was an exchange, -1 on error.
 */
int
kc_dhtStoreKeyValue( kc_dht * dht, kc_hash * key, void * value );

/**
 * Retrieve a value for a key from the DHT.
 * 
 * This function search the known keys and returns the value associated with the specified key.
 *
 * @param dht The DHT to lookup.
 * @param key The key to lookup.
 * @return The value, or NULL if not found.
 */
void *
kc_dhtValueForKey( const kc_dht * dht, void * key );

/**
 * Outputs the DHT state to stdout.
 *
 * This is for debugging purposes. All running sessions are printed.
 * @param dht The DHT to print
 */
void
kc_dhtPrintState( const kc_dht * dht );

/**
 * Outputs the DHT keys to stdout.
 *
 * This is for debugging purposes. All currently known keys/values pairs are printed.
 * @param dht The DHT to print
 */
void
kc_dhtPrintKeys( const kc_dht * dht );

/**
 * Outputs the DHT structure to stdout.
 *
 * This is for debugging purposes. Each bucket in the DHT is printed, along with its known nodes.
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
 * If hash is NULL, the returned list will contain nodes from every bucket (actually in bucket order).
 * Otherwise, the returned list will contain nodes from the closest bucket to the hash.
 *
 * @param dht The kc_dht to clear
 * @param hash A hash for filtering results
 * @param nodeCount A pointer that will be set to the count of returned node
 * @return An array of kc_dhtNodes
 */
kc_dhtNode **
kc_dhtGetNodes( const kc_dht * dht, kc_hash * hash, int * nodeCount );

/**
 * Gets the IP address of the local node.
 *
 * @param dht The kc_dht to get the contact from
 * @return The local IP address
 */
kc_contact *
kc_dhtGetOurContact( const kc_dht * dht, int type );

/**
 * Gets the hash (identifier) of the local node.
 *
 * @param dht The kc_dht to get the local hash from
 * @return The local node's hash value
 */
kc_hash *
kc_dhtGetOurHash( const kc_dht * dht );

/**
 * Gets the hash (identifier) of a node.
 */
kc_hash *
kc_dhtNodeGetHash( const kc_dhtNode * node );

/**
 * Sets the hash of a node.
 */
void
kc_dhtNodeSetHash( kc_dhtNode * node, kc_hash * hash );

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