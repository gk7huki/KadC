/*
 *  newqueue.h
 *  KadC
 *
 *  Created by Etienne on 16/01/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef _KADC_NEWQUEUE_H
#define _KADC_NEWQUEUE_H

typedef struct _kc_queue kc_queue;

/* ----- methods ----- */

/**
 * Allocates and initializes a new queue
 *
 * This function handle the succesful creation of a queue
 * @param size The maximum size of the queue
 * @return An initialized kc_queue structure
 */
kc_queue *kc_queueInit( int size );

/**
 * Destroys an existing queue
 * 
 * This function resets q and deallocates all its items.
 * @param q The queue to destroy.
 */
void kc_queueFree( kc_queue *q );


/**
 * Empties a queue
 * This function frees nodes, but not dynamic data pointed by the nodes
 * @param q The queue to empty
 * @return The empty queue
 */
kc_queue *kc_queueEmpty( kc_queue *q );

/**
 * Add an item to the queue
 * 
 * This function enqueues an item in the queue.
 * Enqueuing NULL will trigger dequeuing on the listening threads
 * because it performs a pthread_cond_signal() without putting
 * data in the queue.
 * 
 * @param q The queue to enqueue in.
 * @param data A pointer to the item to queue
 * @return Returns 0 if successful, -1 if no memory, 1 if queue full.
 */
int kc_queueEnqueue( kc_queue *q, void *data );

/**
 * Dequeue an item from a queue.
 * 
 * This function dequeues an item from the queue.
 * It will block the executing thread until there is data avaliable
 * @param The queue to dequeue from.
 * @return A pointer to the dequeued item. You are responsible for freeing it.
 */
void *kc_queueDequeue( kc_queue *q );

/**
 * Dequeue an item from a queue with a timeout.
 * 
 * This function dequeues an item from the queue.
 * It will block the executing thread until some data is retrieved from the queue,
 * or the timeout (in ms) expires. Signals do NOT cause early termination.
 * @param q The queue to dequeue from.
 * @param timeout A timeout in ms.
 * @return A pointer to the dequeued item. You are responsible for freeing it.
 */
void *kc_queueDequeueTimeout( kc_queue *q, unsigned long int timeout );

/* TODO: Doxy this ! */
int kc_queueCount( kc_queue *q );

/**
 * Test if a queue has data available.
 *
 * Use this macro to test a positive status returned by kc_queueSelect
 * for a given queue index in the queue array.
 * If HAS_DATA(queueSelectStatus, i) is true, a call to 
 * kc_queueDequeue[Timeout](qarray[i][, timeout]) will return immediately.
 *
 * @param queueSelectStatus The return bitmap from kc_queueSelect.
 * @param i The index of the queue to check for data availability.
 * @return 0 if there's data available, 0 otherwise.
 */
#define kc_queueHasData( queueSelectStatus, i ) ( ( queueSelectStatus & (1 << i) ) != 0 )

/**
 * Associate 2 queues
 *
 * let q point to q1's mutex/cond pair; the pair originally
 * pointed by q has the reference count decreased and, if
 * that reaches zero, is destroyed.
 * NOTE: BOTH q1 and q must have no threads holding their
 * mutexes or waiting on their conds when this method is called,
 * or else the results will be undefined!
 * @param q The queue to associate
 * @param q1 The that will be used
 * @return 0 if successful, 1 if the queues were already associated
 */
int kc_queueAssociate( kc_queue *q, kc_queue *q1 );

/**
 * Wait for data availability
 *
 * This function will block at most timeout milliseconds for data to be
 * available for dequeuing from one of more _associated_ queues
 * in qarray.
 * In case of a positive return value, use if (returned_value & (1 << i)) != 0
 * to read the indexes of the queues that have data available and won't block.
 *
 * @param q An array of kc_queues (max 31-element large). All of those will need to be associated.
 * @param timeout The timeout value in milliseconds.
 * @return If > 0,  a bitmap of the ready queues, otherwise 0 on timeout, -1 on error (qarray == NULL, unassociated queues, etc.).
 */
long int kc_queueSelect( kc_queue *qarray[], unsigned long int timeout );

#endif