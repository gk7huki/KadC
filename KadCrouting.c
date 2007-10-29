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

#define DEBUG 1

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <time.h>
#include <Debug_pthreads.h>

#include <pthreadutils.h>

#include <KadCalloc.h>
#include <int128.h>
#include <queue.h>
#include <rbt.h>
#include <net.h>
#include <inifiles.h>
#include <KadCthread.h>
#include <opcodes.h>
#include <KadClog.h>

#include <KadCrouting.h>

typedef struct _knode {
	peernode pn;		/* not pointer */
	time_t	lastseen;	/* last-see time */
	unsigned char bucketnum;	/* log in base 2 of distance */
} knode;

typedef struct _kbucket {
	void *rbt;				/* table */
	pthread_mutex_t mutex;	/* protect access */
	unsigned char avail;				/* available element - init'ed to repl factor */
} kbucket;

/* Management of the kbuckets/kspace table */

static int knode_compLT(void *a, void *b) {
	knode *pa = a;
	knode *pb = b;
	return (pa->lastseen < pb->lastseen);
}

static int knode_compEQ(void *a, void *b) {
	knode *pa = a;
	knode *pb = b;
	return (pa->lastseen == pb->lastseen);
}

static knode *new_knode(peernode *ppn, int128 ourhash) {
	knode *pkn = (knode *)malloc(sizeof(knode));
	if(pkn != NULL) {
		int logdist;
		pkn->pn = *ppn;		/* copy dereferenced data */
		pkn->lastseen = 0;
		logdist = int128xorlog(pkn->pn.hash, ourhash);
		if(logdist < 0) {
			free(pkn);
			pkn = NULL;
		} else {
			assert(logdist <= 127);
			pkn->bucketnum = logdist;
		}
	}
	return pkn;
}

static void destroy_knode(knode *pkn) {
	free(pkn);
}


static kbucket *new_kbucket(size) {
	kbucket *pkb = (kbucket *)malloc(sizeof(kbucket));
	if(pkb != NULL) {

/* if static initialization of recursive mutexes is available, use it;
   otherwise, hope that dynamic initialization is available... */

   		pthreadutils_mutex_init_recursive(&pkb->mutex);

		pkb->rbt = rbt_new(knode_compLT, knode_compEQ);
		pkb->avail = size;
	}
	return pkb;
}

static void destroy_kbucket(kbucket *pkb) {
	void *iter;
	rbt_StatusEnum rbt_status;
	pthread_mutex_lock(&pkb->mutex);

	/* empty pkb->rbt */
	for(;;) {
		knode *pkn;
		iter = rbt_begin(pkb->rbt);
		if(iter == NULL)
			break;
		pkn = rbt_value(iter);
		rbt_erase(pkb->rbt, iter); /* ?? */
		destroy_knode(pkn);	/* deallocate knode */
	}
	rbt_status = rbt_destroy(pkb->rbt);
	assert(rbt_status == RBT_STATUS_OK);	/* deallocate pkb->rbt */

	pthread_mutex_unlock(&pkb->mutex);
	pthread_mutex_destroy(&pkb->mutex);
	free(pkb);
	return;
}

void setup_kba(KadEngine *pKE, int kbsize) {
	int i;
	for(i=0; i<128; i++) {
		pKE->kb[i] = (void *)new_kbucket(kbsize);
	}
	pKE->kspace = rbt_new((rbtcomp *)int128lt, (rbtcomp *)int128eq);
}

void destroy_kba(KadEngine *pKE) {
	int i;
	void *iter;
	rbt_StatusEnum rbt_status;

	pthread_mutex_lock(&pKE->mutex);

	/* destroy the kspace table */
	/* empty pKE->kspace; do not deallocate knodes, they will be later */
	for(;;) {
		if((iter = rbt_begin(pKE->kspace)) == NULL)
			break;
		rbt_erase(pKE->kspace, iter);
	}
	rbt_status = rbt_destroy(pKE->kspace);
	assert(rbt_status == RBT_STATUS_OK);

	for(i=0; i<128; i++) {
		destroy_kbucket((kbucket *)pKE->kb[i]);
	}
	pthread_mutex_unlock(&pKE->mutex);
}

void dump_kba(KadEngine *pKE) {	/* dump kbucket array */
	int i;
	int estusers;
	kbucket **pkb = (kbucket **)pKE->kb;

#ifdef VERBOSE_DEBUG
	time_t now=time(NULL);
#endif

	KadC_log("-- pKE->kb[127..0] (in brackets: age in seconds):\n");
	for(i=127; i >= 0; i--) {
		int nodes_in_this_bucket = 0;
		void *iter = rbt_begin(pkb[i]->rbt);
		if(iter == NULL)
			continue;	/* skip empty buckets */
		KadC_log(">>%3d: ", i);
		pthread_mutex_lock(&pkb[i]->mutex); 	/* \\\\\\ LOCK \\\\\\ */
		for(; iter != NULL; iter = rbt_next(pkb[i]->rbt, iter)) {
			knode *pkn;
			pkn = rbt_value(iter);
			nodes_in_this_bucket++;
#ifdef VERBOSE_DEBUG
			KadC_log("%s:%d.%d(%ld) ", htoa(pkn->pn.ip), pkn->pn.port, pkn->pn.type, now - pkn->lastseen);
#endif
		}
		if(pkb[i]->avail > 2) {
			estusers = (1 << (128 - i)) * nodes_in_this_bucket;
			KadC_log("bucket %2d holds %2d nodes => estimated online users: %9d\n",
					i, nodes_in_this_bucket, estusers);
		} else {
			KadC_log("bucket %2d holds %2d nodes\n",
					i, nodes_in_this_bucket);
		}
		pthread_mutex_unlock(&pkb[i]->mutex);	/* ///// UNLOCK ///// */
	}

}

void dump_kspace(KadEngine *pKE) {	/* dump kspace rbt */
	void *iter;
	time_t now=time(NULL);

	KadC_log("-- pKE->kspace (in brackets: age in seconds):\n");
	pthread_mutex_lock(&pKE->mutex); 	/* \\\\\\ LOCK \\\\\\ */
	for(iter = rbt_begin(pKE->kspace); iter != NULL; iter = rbt_next(pKE->kspace, iter)) {
		knode *pkn = rbt_value(iter);
		int128 pknkey = rbt_key(iter);
		assert(pknkey == pkn->pn.hash);
		KadC_int128flog(stdout, pkn->pn.hash);
		KadC_log(" %s:%d (%ld) ", htoa(pkn->pn.ip), pkn->pn.port, now - pkn->lastseen);
		KadC_log("\n");
	}
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK ///// */
}

/* Try and add peer to kbuckets+kspace (caller should check it's not there)
   return 0 if OK, 1 if kbucket full but peer valid, -1 otherwise */
static int Tryadd2buckets(peernode *ppn, KadEngine *pKE) {
	knode *pkn;		/* pointer to K-node */
	knode *pkno;	/* pointer to old K-node */
	kbucket *pkb;	/* pointer to k-bucket */
	int previousavail;
	rbt_StatusEnum rbt_status;
	int retval = 0;

	/* We have an alive peer here. See if we can put it in its K-bucket. */

	pkn = new_knode(ppn, pKE->localnode.hash);
	if(pkn == NULL) {
#ifdef DEBUG
		KadC_log("new_knode failed!\n");
#endif
		return -1;
	}
	pkno = NULL;
	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK KE \\\\\\ */
	pkb = pKE->kb[pkn->bucketnum];

	pthread_mutex_lock(&pkb->mutex); /* \\\\\\ LOCK k-bucket \\\\\\ */

	previousavail = pkb->avail;

	if(pkb->avail == 0) {	/* if k-bucket full... */
		/* see if oldest peer replies: if not, evict it */
		void *iter = rbt_begin(pkb->rbt);	/* get oldest */
		assert(iter != NULL);				/* if full can't be empty!! */
		pkno = rbt_value(iter);				/* save data by pointing it with pkno */
		rbt_status = rbt_erase(pkb->rbt, iter);	/* remove provisionally from k-bucket... */
		assert(rbt_status == RBT_STATUS_OK);
		rbt_status = rbt_eraseKey(pKE->kspace, pkno->pn.hash);	/* ...and from k-space as well */
		assert(rbt_status == RBT_STATUS_OK);
		pkb->avail++;

	}

	/* now pkn points to the new Knode, pkno to the oldest (or NULL if it wasn't full) */

	if(pkno != NULL) {
		/* Petar says to preserve old nodes when possible, but we just kick'em out
		To be nice would require being able to ping the old node here,
		but that's awkward so we don't do that now. Maybe later... */
#ifdef VERBOSE_DEBUG
		KadC_log("evicting old peer %s:%d (type %d), last seen %d s ago\n",
			htoa(pkno->pn.ip), pkno->pn.port, pkno->pn.type, time(NULL) - pkno->lastseen);
#endif
		free(pkno); /* throw away unresponsive old knode */
		pkno = NULL;
	}

	if(pkno != NULL) {	/* it means old knode is not to be killed, yet */
		rbt_status = rbt_insert(pkb->rbt, pkno, pkno, 1);	/* put it back */
		assert(rbt_status == RBT_STATUS_OK);
		rbt_status = rbt_insert(pKE->kspace, pkno->pn.hash, pkno, 0);
		assert(rbt_status == RBT_STATUS_OK);	/* we just removed it, can't be there! */
		pkb->avail--;
#ifdef VERBOSE_DEBUG
		KadC_log("kept old peer %s:%d\n", htoa(pkno->pn.ip), pkno->pn.port);
#endif
		free(pkn);	/* and throw away new node (maybe we should keep it aside for booting purposes...)*/
		retval = 1;
	} else {
		void *iter1;
		knode *pkn1 = NULL;

		pkn->lastseen = time(NULL);	/* set pkn's age as "seen now" */
		pkn->pn.type = 0;			/* hey, it just replied after all */

		/* if a knode with that same hash already there, remove it */
		if(rbt_find(pKE->kspace, pkn->pn.hash, &iter1) == RBT_STATUS_OK) {
			void *iter2 = NULL;
			pkn1 = rbt_value(iter1);
			/* now look for it scanning its K-bucket */
			for(iter2=rbt_begin(pkb->rbt); iter2 != NULL; iter2 = rbt_next(pkb->rbt, iter2)) {
				if(rbt_value(iter2) == pkn1) {
					rbt_erase(pkb->rbt, iter2); /* remove from k-bucket */
					break;
				}
			}
			assert(iter2 != NULL);	/* must have found it in kbucket! */
			rbt_erase(pKE->kspace, iter1); /* remove also from k-space */
#ifdef VERBOSE_DEBUG
		KadC_log("removed peer %s:%d with same hash as new\n", htoa(pkn1->pn.ip), pkn1->pn.port);
#endif
			free(pkn1);	/* destroy it as well */
			pkb->avail++;
		}
		/* now add new node */
		rbt_status = rbt_insert(pKE->kspace, pkn->pn.hash, pkn, 0);
		assert(rbt_status == RBT_STATUS_OK);	/* we just removed it, can't be there! */
		rbt_status = rbt_insert(pkb->rbt, pkn, pkn, 1);
		assert(rbt_status == RBT_STATUS_OK);
		pkb->avail--;
#ifdef VERBOSE_DEBUG
		KadC_log("added new peer %s:%d\n", htoa(pkn->pn.ip), pkn->pn.port);
#endif
		retval = 0;
	}

	pthread_mutex_unlock(&pkb->mutex); /* ////// UNLOCK k-bucket ////// */
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
	return retval;
}

/* Seek a peernode in kspace and kbuckets.
   If not in kbuckets and kspace,
   			if(isalive) try to add it.

   If it's already there:
   			If isalive reset type to 0;
   			If ! isalive , increment type;
   			If type >= NONRESPONSE_THRESHOLD, remove the corresponding knode from both tables and free it

   In any case, ppn is left alone (not free'd).
   Returns:

   	-1 our own node or non-routable address: nothing done

	if isalive:
	 0 added or left there
     1 could no be added because bucket full

   	if ! isalive:
   	 0 it was, and was left there, or was not there already
   	 1 it was there, but it was removed due to type >= NONRESPONSE_THRESHOLD

 */
int UpdateNodeStatus(peernode *ppn, KadEngine *pKE, int isalive) {
	knode *pkn;		/* pointer to K-node in kspace */
	knode *pknb;	/* pointer to K-node in kbucket */
	kbucket *pkb;	/* pointer to k-bucket */
	void *iterks;
	void *iterkb;
	rbt_StatusEnum rbt_status;
	int removed = 0;
	int bucketnum;

	bucketnum = int128xorlog(ppn->hash, pKE->localnode.hash);
	if(bucketnum < 0 ||
		(ppn->ip == pKE->localnode.ip && ppn->port == pKE->localnode.port)) {
#ifdef VERBOSE_DEBUG
		KadC_log("not adding our own peer\n");
#endif
		return -1;
	}

	if(isnonroutable(ppn->ip) || ppn->ip == pKE->extip || ppn->ip == pKE->localnode.ip) {
#ifdef VERBOSE_DEBUG
		KadC_log("not adding peer with nonroutable or our int/ext IP address\n");
#endif
		return -1;
	}

	pkb =  pKE->kb[bucketnum];
	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK KE \\\\\\ */

	rbt_status = rbt_find(pKE->kspace, ppn->hash, &iterks);
	if(rbt_status != RBT_STATUS_OK) {	/* not found. Quite normal, for new nodes */
		pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
		/* In this case, if(isalive) we try to add the node */
		if(isalive)
			return Tryadd2buckets(ppn, pKE);
		else
			return 0;	/* not there and no need to add it */
	}

	assert(iterks != NULL);
	pkn = rbt_value(iterks);
	assert(pkn != NULL);	/* must be there! */

	if(isalive)
		pkn->pn.type = 0;
	else
		pkn->pn.type++;

	if(pkn->pn.type >= NONRESPONSE_THRESHOLD) {
		/* remove from kspace */
		rbt_status = rbt_erase(pKE->kspace, iterks);
		assert(rbt_status == RBT_STATUS_OK);

		/* now remove from k-bucket as well */
		pthread_mutex_lock(&pkb->mutex); /* \\\\\\ LOCK k-bucket \\\\\\ */
		for(pknb = NULL, iterkb=rbt_begin(pkb->rbt); iterkb != NULL; iterkb = rbt_next(pkb->rbt, iterkb)) {
			pknb = rbt_value(iterkb);
			if(memcmp(pknb->pn.hash, ppn->hash, 16) == 0)
				break;
		}
		assert(iterkb != NULL);	/* can't be in kspace and not in k-bucket */
		assert(pknb == pkn);	/* both kbucket and kspace entries point to the same object */
		rbt_erase(pkb->rbt, iterkb); /* remove from k-bucket */
		pkb->avail++;
		pthread_mutex_unlock(&pkb->mutex); /* ////// UNLOCK k-bucket ////// */


		/* free the knode */
		free(pkn);

		removed = 1;
#ifdef VERBOSE_DEBUG
		KadC_log("UpdateNodeStatus evicted unresponsive old peer %s:%d (type %d) last seen %d s ago\n",
			htoa(pkn->pn.ip), pkn->pn.port, pkn->pn.type,  time(NULL) - pkn->lastseen);
#endif
	}
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
	return removed;
}

int knodes_count(KadEngine *pKE) {
	int nknodes;
	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK KE \\\\\\ */
	nknodes = rbt_size(pKE->kspace);
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
	return nknodes;
}

int erase_knodes(KadEngine *pKE) {
	int i;
	void *iter;
	int nknodes = 0;

	pthread_mutex_lock(&pKE->mutex);

	/* empty pKE->kspace; do not deallocate knodes, they will be later */
	for(;;) {
		if((iter = rbt_begin(pKE->kspace)) == NULL)
			break;
		rbt_erase(pKE->kspace, iter);
	}

	for(i=0; i<128; i++) {
		kbucket *pkb = pKE->kb[i];

		pthread_mutex_lock(&pkb->mutex);
		/* empty pkb->rbt */
		for(;;) {
			knode *pkn;
			iter = rbt_begin(pkb->rbt);
			if(iter == NULL)
				break;
			pkn = rbt_value(iter);
			rbt_erase(pkb->rbt, iter); /* ?? */
			destroy_knode(pkn);	/* deallocate knode */
			pkb->avail++;
			nknodes++;
		}
		pthread_mutex_unlock(&pkb->mutex);
	}
	pthread_mutex_unlock(&pKE->mutex);
	return nknodes;
}
