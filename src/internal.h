/*
 *  internal.h
 *  KadC
 *
 *  Created by Etienne on 13/02/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include <event.h>

/**
 * The callback protoype used when the DHT needs to know a message's type.
 *
 * This callback is called by the dht when a message arrives to know its type.
 * You'll get a pointer to the kc_dht, and a pointer to the recieved kc_dhtMsg.
 * You'll need to set the recieved message type field to the correct message type.
 * @see kc_dhtMsgType
 *
 * @param dht The DHT willing to communicate.
 * @param msg A kc_dhtMsg containing the data recieved from the node.
 * @return You should return the message type here.
 */
typedef int (*kc_dhtParseCallback)( const kc_dht * dht, const kc_message * msg );

/**
 * The callback protoype used when the DHT needs to read a message.
 *
 * This callback is called as soon as the DHT needs to parse data from another node.
 * You'll get a pointer to the kc_dht, a pointer to the recieved kc_dhtMsg, and a pointer to
 * the correct message to reply to the sender node.
 *
 * @param dht The DHT willing to communicate.
 * @param msg A kc_dhtMsg containing the data recieved from the node.
 * @return You should return 0 if you want the answer message sent, or 1 if you don't.
 */
typedef int (*kc_dhtReadCallback)( kc_dht * dht, const kc_message * msg );

/**
 * The callback protoype used when the DHT needs to write a packet.
 *
 * This callback is called as soon as the DHT needs to send data to another node.
 * You'll get a pointer to the kc_dht, a pointer to the message that needs an answer, and a pointer to NULL
 * that will be need to be malloc()ed, with message type & payload, IP and port number set to valid values.
 * Sometimes the msg will be NULL. When that happens, expect answer to be a pointer to a partially correct
 * message with type and destination set. You will just need to malloc() and set msg->payload and set
 * msg->payloadSize accordingly.
 * 
 * @param dht The DHT willing to communicate
 * @param msg A pointer to the kc_dhtMsg you should reply to. 
 * @param answer You should set this to NULL if there's no need to answer this. This will effectively end a session. Return a kc_dhtMsg with the correct info for the msg parameter.
 * @return You should return 0 on success, -1 otherwise.
 */
typedef int (*kc_dhtWriteCallback)( const kc_dht * dht, kc_message * msg, kc_message * answer );

typedef struct _kc_dhtCallbacks {
    kc_dhtParseCallback     parseCallback;
    kc_dhtReadCallback      readCallback;
    kc_dhtWriteCallback     writeCallback;
} kc_dhtCallbacks;

struct _kc_dhtParameters {
    int expirationDelay;
    int refreshDelay;
    int replicationDelay;
    int republishDelay;
    
    int lookupParallelism;
    
    int maxQueuedMessages;
    int maxSessionCount;
    int maxMessagesPerPulse;
    int sessionTimeout;
    
    int hashSize;
    int bucketSize;
    kc_dhtCallbacks callbacks;
};

int openAndBindSocket( kc_contact * contact );

typedef int (*kc_sessionCallback)( const kc_dht * dht, const kc_message * msg );

#pragma mark struct dhtSession
typedef struct dhtSession {
    kc_contact            * contact;
    
    kc_messageType          type;
    int                     incoming;
    
    int                     socket;
    
    kc_sessionCallback      callback;
    
    struct bufferevent    * bufferEvent;
    
/*    evbuffercb              readCb;
    evbuffercb              writeCb;
    everrorcb               errorCb;*/
    
} dhtSession;

#pragma mark struct kc_dhtNode
struct _kc_dhtNode {
    kc_contact    * contact;
    kc_hash       * hash;
    
	time_t          lastSeen;	/* Last time we heard of it */
    //    time_t          rtt;        /* Round-trip-time to it */
};

#pragma mark struct dhtBucket
typedef struct dhtBucket {
	RbtHandle         * nodes;              /* Red-Black tree of nodes */
    
    unsigned char       availableSlots;     /* Available slots in bucket */
    
    time_t              lastChanged;        /* Last time this bucket changed */
    pthread_mutex_t     mutex;
	
} dhtBucket;

#pragma mark struct dhtValue
typedef struct dhtValue {
    void              * value;
    
    int                 mine;
    time_t              published;
} dhtValue;

#pragma mark dhtIdentity
typedef struct dhtIdentity {
    kc_contact        * us;             /* Our contact (like IPv4, IPv6 node) */
    int                 fd;             /* The socket bound to the contact above */  
    struct bufferevent * inputEvent;    /* The bufferevent for the socket above */
} dhtIdentity;

#pragma mark struct kc_dht
struct _kc_dht {
    RbtHandle         * keys;           /* Our stored key/values pairs */    
    RbtHandle         * sessions;       /* Our running requests against the DHT */
    
    dhtBucket        ** buckets;        /* Array of BUCKET_COUNT buckets */
    
//    RbtHandle         * contacts;       /* All our known nodes */
    
    kc_dhtParameters  * parameters;     /* Our parameters */
        
    struct event_base * eventBase;      /* Our libevent base */
    struct event      * replicationTimer;
    
    dhtIdentity      ** identities;     /* Pointer to an array of identities (as in "IPv4/IPv6 identity") */
    kc_hash           * hash;           /* Our hash, because it is common between all our identities */
    
    time_t              lastReplication;/* Last time we replicated our keys/values */
    time_t              probeDelay;     /* Last time we sent our probes */
    kc_queue          * sndQueue;       /* A queue of probes we need to send */
    
    pthread_mutex_t     lock;
    pthread_t           eventThread;
};

dhtIdentity *
kc_dhtIdentityForContact( const kc_dht * dht,  kc_contact * contact );

kc_dhtNode *
dhtNodeInit( kc_contact * contact, const kc_hash * hash );

void
dhtNodeFree( kc_dhtNode *pkn );


kc_contact *
kc_dhtNodeGetContact( const kc_dhtNode * node );

void
kc_dhtNodeSetContact( kc_dhtNode * node, kc_contact * contact );

kc_hash *
kc_dhtNodeGetHash( const kc_dhtNode * node );

void
kc_dhtNodeSetHash( kc_dhtNode * node, kc_hash * hash );

int
dhtNodeCmpSeen( const void *a, const void *b );

int
dhtNodeCmpHash( const void *a, const void *b );

dhtBucket *
dhtBucketInit( int size );

void
dhtBucketFree( dhtBucket *pkb );

void
dhtBucketLock( dhtBucket *pkb );

void
dhtBucketUnlock( dhtBucket *pkb );

void
dhtPrintBucket( const dhtBucket * bucket );

dhtSession *
dhtSessionInit( const kc_dht * dht, kc_contact * bindContact, kc_contact * connectContact, kc_messageType type, int incoming, kc_sessionCallback callback, int sessionTimeout );

void
dhtSessionFree( dhtSession * session );

int
dhtSessionCmp( const void *a, const void *b );

dhtIdentity *
dhtIdentityInit( kc_dht * dht, kc_contact * contact );

void dhtIdentityFree( dhtIdentity * identity );
