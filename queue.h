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

/* Queues */
typedef struct _qnode {
	struct _qnode *next;
	void *data;
} qnode;

/* The mutex/cond pair may be shared among queues; one thread may
   then wait for data to be available in any of them */

typedef struct _mutex_cond_pair {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int refcount;
} mutex_cond_pair;

typedef struct _queue {
	qnode *head;
	qnode *tail;
	mutex_cond_pair *mcp;
	int size;
	int n;
	/* ----- methods ----- */
	/* reset queue
	   frees nodes, but NOT any dynamic data pointed by the nodes!! */
	struct _queue *(*resetq)(struct _queue *q);
	/* returns 0 if successful, -1 if no memory, 1 if queue full.
	   enq'ing NULL simulates a timeout on the deqw() (and deqw()),
	   as it performs a pthread_cond_signal() without putting
	   data in the queue. */
	int (*enq)(struct _queue *q, void *data);
	/* blocks the thread until some data is retrieved from the queue */
	void *(*deqw)(struct _queue *q);
	/* blocks the thread until some data is retrieved from the queue, or
	   a millis_timeout expires. Signals do NOT cause early termination. */
	void *(*deqtw)(struct _queue *q, unsigned long int millis_timeout);
	/* let q point to q1's mutex/cond pair; the pair originally
	   pointed by q has the reference count decreased and, if
	   that reaches zero, is destroyed.
	   NOTE: BOTH q1 and q must have no threads holding their
	   mutexes or waiting on their conds when this method is called,
	   or else the results will be undefined!
	   Returns: 0 = OK, 1 = the queues were associated already */
	int (*associate)(struct _queue *q, struct _queue *q1);
	/* waits at most millis_timeout milliseconds for data to be
	   available for dequeuing from one of more _associated_ queues
	   in the array *qarray[] (max 31-element large).
	   Returns:
	   	0	for timeout
	   	-1	error (qarray = NULL, some queues were not associated etc.)
	   	>0  a bitmap of the ready queues: if (returned_value & (1 << i)) != 0,
	   	    then qarray[i] has data and qarray[i]->deqtw(qarray[i], millistimeout)
	   	    won't block. */
	long int (*select)(struct _queue *q[], unsigned long int millis_timeout);
	/* resets q and then deallocates t */
	void (*destroy)(struct _queue *q);
} queue;

/* macro to test a positive status returned by:
   selectq_status = qarray[i]->select(qarray, millistimeout);
   if HAS_DATA(selectq_status, i) is true, a call to
   qarray[i]->deqtw(qarray[i], millis_timeout) will immediately
   return data available in qarray[i], without blocking */
#define HAS_DATA(selectq_status, i) (((selectq_status) & (1<<(i))) != 0)

/* allocates queue and initializes all fields incl. methods */
queue *new_queue(int size);

