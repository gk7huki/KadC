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

#include <pthread.h>
#include <Debug_pthreads.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <queue.h>
#include <stdio.h>
#include <pthreadutils.h>

#include <KadCalloc.h>

/* returns 0 if successful, -1 if no memory, 1 if queue full.
   enq'ing NULL simulates a timeout on the deqw() (and deqw()),
   as it performs a pthread_cond_signal() without putting
   data in the queue. */

static int enq(queue *q, void *data) {
	qnode *new;

	pthread_mutex_lock(&q->mcp->mutex);
	if(data != NULL) {
		assert(q != NULL);
		if(q->n >= q->size) {
			pthread_mutex_unlock(&q->mcp->mutex);
			return 1; /* queue full */
		}
		new = malloc(sizeof(qnode));
		if(new == NULL) {
			pthread_mutex_unlock(&q->mcp->mutex);
			return -1; /* no memory */
		}
		q->n++;
		new->data = data;
		new->next = NULL;

		if(q->tail == NULL) {
			assert(q->head == NULL);
			q->head = new;
		} else {
			assert(q->tail->next == NULL);
			q->tail->next = new;
		}
		q->tail = new;
	}
	pthread_mutex_unlock(&q->mcp->mutex);
	pthread_cond_signal(&q->mcp->cond);
	return 0;
}

/* blocks the thread until some data is retrieved from the queue */
static void *deqw(queue *q) {
	qnode *cur;
	void *data = NULL;

	assert(q != NULL);
	pthread_mutex_lock(&q->mcp->mutex);

	if(q->head == NULL)	/* if no data at this moment */
		pthread_cond_wait(&q->mcp->cond, &q->mcp->mutex);

	if(q->head != NULL) {	/* if pthread_cond_wait() was NOT interrupted, but instead there are non-NULL data */
		q->n--;
		cur = q->head;
		q->head = cur->next;
		if(q->head == NULL) {
			assert(q->n == 0);
			q->tail = NULL;
		} else {
			assert(q->n != 0);
		}
		data = cur->data;
		free(cur);
	}
	pthread_mutex_unlock(&q->mcp->mutex);
	return data;	/* which will be NULL if pthread_cond_wait() was interrupted or a NULL was enq'd */
}

/* blocks the thread until some data is retrieved from the queue, or
   a millis_timeout expires. Signals do NOT cause early termination. */
static void *deqtw(queue *q, unsigned long int millis_timeout) {
	qnode *cur;
	void *data = NULL;
	int status = 0;

	assert(q != NULL);
	pthread_mutex_lock(&q->mcp->mutex);

	if(q->head == NULL) /* i.e., while queue is empty */
		status = pthread_cond_incrtimedwait(&q->mcp->cond, &q->mcp->mutex, millis_timeout);

	/* Here status can only be either 0 or ETIMEDOUT */
	if(status == 0 && q->head != NULL) {
		q->n--;
		cur = q->head;
		q->head = cur->next;
		if(q->head == NULL) {
			assert(q->n == 0);
			q->tail = NULL;
		} else {
			assert(q->n != 0);
		}
		data = cur->data;
		free(cur);
	}
	pthread_mutex_unlock(&q->mcp->mutex);
	return data;
}

/* reset queue
   frees nodes, but NOT any dynamic data pointed by the nodes!! */
static queue *resetq(queue *q) {
	qnode *cur, *next;

	assert(q != NULL);
	pthread_mutex_lock(&q->mcp->mutex);
	for(cur=q->head; cur != NULL; cur = next) {
		next = cur->next;
		free(cur);
	}
	q->head = NULL;
	q->tail = NULL;
	q->n = 0;
	pthread_mutex_unlock(&q->mcp->mutex);
	return q;
}

static void destroyq(queue *q) {
	q->resetq(q);
	if(--(q->mcp->refcount) == 0) {
		pthread_mutex_destroy(&q->mcp->mutex);
		pthread_cond_destroy(&q->mcp->cond);
		free(q->mcp);
	}
	free(q);
}

/* let q point to q1's mutex/cond pair; the pair originally
   pointed by q has the reference count decreased and, if
   that reaches zero, is destroyed.
   NOTE: BOTH q1 and q must have no threads holding their
   mutexes or waiting on their conds when this method is called,
   or else the results will be undefined!
   Returns: 0 = OK, 1 = the queues were associated already */

static int associateq(queue *q, queue *q1) {
	mutex_cond_pair *oldqmcp = q->mcp;

	if(q->mcp == q1->mcp)
		return 1;	/* they are already associated! do nothing */
	q->mcp = q1->mcp;
	q->mcp->refcount++;
	if(--(oldqmcp->refcount) == 0) {
		pthread_mutex_destroy(&oldqmcp->mutex);
		pthread_cond_destroy(&oldqmcp->cond);
		free(oldqmcp);
	}
	return 0;
}

/* waits at most millis_timeout milliseconds for data to be
   available for dequeuing from one of more _associated_ queues
   in the array *qarray[] (max 31-element large).
   Returns:
	0	for timeout
	-1	error (qarray = NULL, some queues were not associated etc.)
	>0  a bitmap of the ready queues: if (returned_value & (1 << i)) != 0,
		then qarray[i] has data and qarray[i]->deqtw(qarray[i], millistimeout)
		won't block. */
static long int selectq(queue *qarray[], unsigned long int millis_timeout) {
	long int returned_value = 0;
	mutex_cond_pair *mcp;
	int status = 0;
	int nqueues;
	int i;

	if(qarray[0] == NULL)
		return -1;

	mcp = qarray[0]->mcp;
	if(mcp == NULL)
		return -1;

	nqueues = mcp->refcount;
	if(nqueues < 0 || nqueues > 31)
		return -1;

	/* sanity check: qarray[0]...qarray[refcount-1] must be non-NULL and
	   their mcp must point to the same mcp as qarray[0] */
	for(i=0; i < nqueues; i++) {
		if(qarray[i] == NULL || qarray[i]->mcp != mcp)
			return -1;
	}

	pthread_mutex_lock(&mcp->mutex);	/* \\\\\\ LOCK SHARED MUTEX \\\\\\ */

	for(i=0; i < nqueues; i++) {
		if(qarray[i]->head != NULL) /* i.e., if this queue has data */
			break;
	}
	if(i == nqueues)	/* i.e. if none of the queues have data */
		status = pthread_cond_incrtimedwait(&mcp->cond, &mcp->mutex, millis_timeout);

	/* Here status can only be either 0 or ETIMEDOUT */
	for(i = 0; i < nqueues; i++) {
		if(qarray[i]->head != NULL) /* i.e., if this queue has data */
			returned_value |= (1 << i);
	}

	pthread_mutex_unlock(&mcp->mutex);	/* ///// UNLOCK SHARED MUTEX ///// */

	return returned_value;	/* zero if no queue has data */
}

/* allocates queue and initializes all fields incl. methods */
queue *new_queue(int size) {
	queue *q;
	const static pthread_mutex_t pti = PTHREAD_MUTEX_INITIALIZER;
	const static pthread_cond_t  pci = PTHREAD_COND_INITIALIZER;
	q = malloc(sizeof(queue));
	if(q != NULL) {
		q->head = NULL;
		q->tail = NULL;
		q->size = size;
		q->n = 0;
		q->mcp = calloc(1, sizeof(mutex_cond_pair));
		if(q->mcp == NULL) {
			free(q);
			return NULL;
		}
		assert(q->mcp != NULL);
		q->mcp->mutex = pti;
		q->mcp->cond = pci;
		q->mcp->refcount = 1;
		q->resetq = &resetq;
		q->enq = &enq;
		q->deqw = &deqw;
		q->deqtw = &deqtw;
		q->associate = &associateq;
		q->select = &selectq;
		q->destroy = &destroyq;
	}
	return q;
}

