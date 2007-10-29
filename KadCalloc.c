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

#define USE_PTHREADS 1
#define TRACKALLOCATIONS 1

#ifdef USE_PTHREADS
#include <pthread.h>
#include <Debug_pthreads.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <int128.h>
#include <KadClog.h>
#define __KADC_DO_NOT_REDEFINE_ALLOC_MACROS__
#include <KadCalloc.h>

static int malloc_cnt, free_cnt;

#ifdef TRACKALLOCATIONS
#define MAXALLOCS (65536)
typedef struct _allocrecord {
	void *p;
	char *sfile;
	int lineno;
} allocrecord;

static allocrecord inuse[MAXALLOCS];
static int nused = 0;
static int nused_max = 0; /* high water mark */
#ifdef USE_PTHREADS
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif


/* call: find(p, nused-1)
   returns either the index of the data, or the index of the location
   where the data should be inserted; if table is empty, returns -1
 */

static int find(void *p, int last) {
	int high, i, low;
	for(low=(-1), high=last; high-low > 1; ) {
		i = (high+low) / 2;
		if(p <= inuse[i].p)
	  		high = i;
		else
			low  = i;
	}
 	return(high);
}


static int p_insert(void *p, char *sfile, int lineno) {
	int i, retval;
	if(nused >= MAXALLOCS) {	/* table full */
		retval = -1;
		goto exit;
	}

	if(nused == 0) {		/* table empty, easy: insert here */
		inuse[0].p = p;
		inuse[0].sfile = sfile;
		inuse[0].lineno = lineno;
		nused++;
		retval = 0;
		goto exit;
	}

	i = find(p, nused-1);	/* insert at i or after it */

	if(inuse[i].p == p) {
		retval = 1;			/* already there, can't insert */
		goto exit;
	}

	if(inuse[i].p < p) {
		i++;	/* ensure that we have to shift down from i included */
	}

	/* shift down from insertion point to end */
	nused++;
	if(nused > i+1)
		memmove(&inuse[i+1] , &inuse[i], (nused-i-1)*sizeof(inuse[0]));
	inuse[i].p = p;				/* insert here */
	inuse[i].sfile = sfile;
	inuse[i].lineno = lineno;
	if(nused > nused_max)
		nused_max = nused;		/* update high water mark */
	retval = 0;		/* inserted */
exit:
	return retval;
}

static int p_remove(void *p) {
	int i, retval;
	i = find(p, nused);
	if(inuse[i].p != p) {
		retval = 1;			/* not here */
		goto exit;
	}
	/* shift up from inuse[nused] to inuse[i] */
	if(nused > i+1)
		memmove(&inuse[i], &inuse[i+1], (nused-i-1) * sizeof(inuse[0]));
	nused--;
	inuse[nused].p = NULL;	/* erase last */
	retval = 0;		/* inserted */
exit:
	return retval;
}
#endif

void KadC_list_outstanding_mallocs(int maxentries) {
	KadC_log("Total malloc's: %d, total free's: %d \n",
			malloc_cnt, free_cnt);
#ifdef TRACKALLOCATIONS
	{
		int i;
		KadC_log("Outstanding blocks now: %d all-time high: %d\n",
				nused, nused_max);
		for(i=0; i < nused && i < maxentries; i++) {
			KadC_log("0x%8lx: alloc'd in %s, line %d\n",
				(unsigned long int)inuse[i].p, inuse[i].sfile, inuse[i].lineno);
		}
		if(i < nused)
			KadC_log("(%d additional outstanding blocks are not listed)\n", nused - i);
	}
#endif
}

void *KadC_realloc(void *p, size_t size, char *sf, int ln) {
#ifdef TRACKALLOCATIONS
	int status;
#ifdef USE_PTHREADS
	pthread_mutex_lock(&mutex);		/* \\\\\\ LOCK \\\\\\ */
#endif
#endif
	if(p == NULL && size != 0) {	/* is this a malloc()? */
		malloc_cnt++;
		p = malloc(size);
		if(p == NULL) {
			KadC_log("Warning: call to malloc(%d) returned NULL in %s, line %d\n",
						size, sf, ln);
		}
#ifdef TRACKALLOCATIONS
		status = p_insert(p, sf, ln);
		if(status != 0){	/* catches table full and malloc() bugs */
			KadC_log("Fatal: p_insert(%lx, %s, %d) returned NULL in %s, line %d\n",
					(unsigned long int)p, sf, ln, __FILE__, __LINE__);
		}
#endif
	} else if(p != NULL && size == 0) {	/* is this a free()? */
#ifdef TRACKALLOCATIONS
		status = p_remove(p);
		if(status != 0) {	/* catches double-frees */
			KadC_log("can't free(%lx): double free? Called in %s, line %d\n",
					(unsigned long int)p, sf, ln);
			assert(status == 0);
		}
#endif
		free_cnt++;
		free(p);
	} else if(p == NULL && size == 0) {	/* uhm, malloc(0) */
		KadC_log("Warning: call to malloc(0) or realloc(NULL,0) in %s, line %d\n", sf, ln);
	} else { /* a true realloc() to resize an existing block */
#ifdef TRACKALLOCATIONS
		status = p_remove(p);
		if(status != 0) {	/* catches double-frees */
			KadC_log("can't free(%lx): double free? Called in %s, line %d\n",
					(unsigned long int)p, sf, ln);
			assert(status == 0);
		}
#endif
		p = realloc(p, size);
#ifdef TRACKALLOCATIONS
		status = p_insert(p, sf, ln);
		assert(status == 0);	/* catches table full and malloc() bugs */
#endif
	}
#ifdef TRACKALLOCATIONS
	assert(nused == malloc_cnt - free_cnt);
#ifdef USE_PTHREADS
	pthread_mutex_unlock(&mutex);	/* ///// UNLOCK ///// */
#endif
#endif
	return p;
}

void *KadC_malloc(size_t size, char *sf, int ln) {
	return KadC_realloc(NULL, size, sf, ln);
}

void KadC_free(void *p, char *sf, int ln) {
	KadC_realloc(p, 0, sf, ln);
}

void *KadC_calloc(size_t nelem, size_t elsize, char *sf, int ln) {
	size_t size = nelem*elsize;
	void *p = KadC_realloc(NULL, size, sf, ln);
	if(p != NULL)
		memset(p, 0, size);
	return p;
}

char *KadC_strdup(char *s, char *sf, int ln) {
	char *s1;
	int len;
	if(s == NULL)
		return NULL;
	len = strlen(s)+1;
	s1 = KadC_realloc(NULL, len, sf, ln);
	if(s1 != NULL)
		memcpy(s1, s, len);
	return s1;
}

