/*
 *  newqueue.c
 *  KadC
 *
 *  Created by Etienne on 16/01/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

/* Queues */
typedef struct _qnode {
	struct _qnode *next;
	void *data;
} kc_qnode;

/* The mutex/cond pair may be shared among queues; one thread may
 then wait for data to be available in any of them */

struct _mutex_cond_pair {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int refcount;
};

/* TODO: decrement size so that we keep track of how many items there is in queue */
struct _kc_queue {
	kc_qnode *head;
	kc_qnode *tail;
	struct _mutex_cond_pair *mcp;
	int size;
	int n;
};

/* allocates queue and initializes all fields incl. methods */
kc_queue *kc_queueInit( int size )
{
	kc_queue *q;
	const static pthread_mutex_t pti = PTHREAD_MUTEX_INITIALIZER;
	const static pthread_cond_t  pci = PTHREAD_COND_INITIALIZER;
	q = malloc( sizeof(kc_queue) );
	if( q != NULL ) {
		q->head = NULL;
		q->tail = NULL;
		q->size = size;
		q->n = 0;
		q->mcp = malloc( sizeof(struct _mutex_cond_pair) );
		if( q->mcp == NULL ) {
			free( q );
			return NULL;
		}
        
		q->mcp->mutex = pti;
		q->mcp->cond = pci;
		q->mcp->refcount = 1;
		/*q->resetq = &resetq;
		q->enq = &enq;
		q->deqw = &deqw;
		q->deqtw = &deqtw;
		q->associate = &associateq;
		q->select = &selectq;
		q->destroy = &destroyq;*/
	}
	return q;
}

void kc_queueFree( kc_queue *q )
{
	kc_queueEmpty( q );
	if( --q->mcp->refcount == 0) {
		pthread_mutex_destroy( &q->mcp->mutex );
		pthread_cond_destroy( &q->mcp->cond );
		free( q->mcp );
	}
	free( q );
}

kc_queue *kc_queueEmpty( kc_queue *q )
{
	kc_qnode *cur, *next;
    
	assert( q != NULL );
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

int
kc_queueEnqueue( kc_queue *q, void *data )
{
	kc_qnode *new;
    
	pthread_mutex_lock(&q->mcp->mutex);
	if( data != NULL )
    {
		assert( q != NULL );
		if( q->n >= q->size )
        {
			pthread_mutex_unlock( &q->mcp->mutex );
			return 1; /* queue full */
		}
		new = malloc( sizeof(kc_qnode) );
		if( new == NULL )
        {
			pthread_mutex_unlock( &q->mcp->mutex );
			return -1; /* no memory */
		}
		q->n++;
		new->data = data;
		new->next = NULL;
        
		if( q->tail == NULL )
        {
			assert( q->head == NULL );
			q->head = new;
		} else {
			assert( q->tail->next == NULL );
			q->tail->next = new;
		}
		q->tail = new;
	}
	pthread_mutex_unlock( &q->mcp->mutex );
	pthread_cond_signal( &q->mcp->cond );
	return 0;
}


void *kc_queueDequeue( kc_queue *q )
{
	kc_qnode *cur;
	void *data = NULL;
    
	assert( q != NULL );
	pthread_mutex_lock( &q->mcp->mutex );
    
	if( q->head == NULL )	/* if no data at this moment */
		pthread_cond_wait( &q->mcp->cond, &q->mcp->mutex );
    
	if( q->head != NULL )
    {
        /* if pthread_cond_wait() was NOT interrupted, but instead there are non-NULL data */
		q->n--;
		cur = q->head;
		q->head = cur->next;
		if( q->head == NULL )
        {
			assert( q->n == 0 );
			q->tail = NULL;
		}
        else
        {
			assert( q->n != 0 );
		}
		data = cur->data;
		free( cur );
	}
	pthread_mutex_unlock( &q->mcp->mutex );
	return data;	/* which will be NULL if pthread_cond_wait() was interrupted or a NULL was enq'd */
}

void *kc_queueDequeueTimeout( kc_queue *q, unsigned long int timeout)
{
	kc_qnode *cur;
	void *data = NULL;
	int status = 0;
    
	assert( q != NULL );
	pthread_mutex_lock( &q->mcp->mutex );
    
	if( q->head == NULL ) /* i.e., while queue is empty */
		status = pthread_cond_incrtimedwait( &q->mcp->cond, &q->mcp->mutex, timeout );
    
	/* Here status can only be either 0 or ETIMEDOUT */
	if( status == 0 && q->head != NULL )
    {
		q->n--;
		cur = q->head;
		q->head = cur->next;
		if( q->head == NULL )
        {
			assert( q->n == 0 );
			q->tail = NULL;
		}
        else
        {
			assert( q->n != 0 );
		}
		data = cur->data;
		free( cur );
	}
	pthread_mutex_unlock( &q->mcp->mutex );
	return data;
}

int kc_queueCount( kc_queue *q )
{
    kc_qnode *cur;
    
    assert( q != NULL );
    
    pthread_mutex_lock( &q->mcp->mutex );
    
    for( cur = q->head; cur != NULL; cur = cur->next )
    {
        //TODO: 
    }
    pthread_mutex_unlock( &q->mcp->mutex );
    return 0;
}

/* let q point to q1's mutex/cond pair; the pair originally
 pointed by q has the reference count decreased and, if
 that reaches zero, is destroyed.
 NOTE: BOTH q1 and q must have no threads holding their
 mutexes or waiting on their conds when this method is called,
 or else the results will be undefined!
 Returns: 0 = OK, 1 = the queues were associated already */

int kc_queueAssociate( kc_queue *q, kc_queue *q1)
{
	struct _mutex_cond_pair *oldqmcp = q->mcp;
    
	if( q->mcp == q1->mcp )
		return 1;	/* they are already associated! do nothing */
	q->mcp = q1->mcp;
	q->mcp->refcount++;
	if( --oldqmcp->refcount == 0) {
		pthread_mutex_destroy( &oldqmcp->mutex );
		pthread_cond_destroy( &oldqmcp->cond );
		free( oldqmcp );
	}
	return 0;
}

long int kc_queueSelect( kc_queue *qarray[], unsigned long int timeout )
{
	long int returned_value = 0;
	struct _mutex_cond_pair *mcp;
	int status = 0;
	int nqueues;
	int i;
    
    assert( qarray == NULL );
    assert( qarray[0] == NULL );
    
	mcp = qarray[0]->mcp;
	if(mcp == NULL)
		return -1;
    
	nqueues = mcp->refcount;
	if( nqueues < 0 || nqueues > 31 )
		return -1;
    
	/* sanity check: qarray[0]...qarray[refcount-1] must be non-NULL and
     their mcp must point to the same mcp as qarray[0] */
	for( i = 0; i < nqueues; i++ )
    {
		if( qarray[i] == NULL || qarray[i]->mcp != mcp )
			return -1;
	}
    
	pthread_mutex_lock( &mcp->mutex );	/* \\\\\\ LOCK SHARED MUTEX \\\\\\ */
    
	for( i = 0; i < nqueues; i++ )
    {
		if( qarray[i]->head != NULL ) /* i.e., if this queue has data */
			break;
	}
	if( i == nqueues )	/* i.e. if none of the queues have data */
		status = pthread_cond_incrtimedwait( &mcp->cond, &mcp->mutex, timeout );
    
	/* Here status can only be either 0 or ETIMEDOUT */
	for( i = 0; i < nqueues; i++ )
    {
		if( qarray[i]->head != NULL ) /* i.e., if this queue has data */
			returned_value |= (1 << i);
	}
    
	pthread_mutex_unlock( &mcp->mutex );	/* ///// UNLOCK SHARED MUTEX ///// */
    
	return returned_value;	/* zero if no queue has data */
}

