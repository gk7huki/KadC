/****************************************************************\

Copyright 2004,2006 Enzo Michelangeli, Arto Jalkanen

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
/*#define OPTIMIZE_BY_RECURSING_ONLY_ON_PEERS_BETTER_THAN_CURRENT_BEST 1*/
#include <pthread.h>
#define DEBUG 1
/* #define VERBOSE_DEBUG 1 */
#ifdef NDEBUG
#undef DEBUG
#undef VERBOSE_DEBUG
#endif


#include <string.h>

#include <millisleep.h>

#include <stdio.h>
#include <ctype.h>
#include <Debug_pthreads.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include <queue.h>
#include <net.h>
#include <int128.h>
#include <MD4.h>
#include <rbt.h>
#include <KadCalloc.h>
#include <opcodes.h>
#include <KadCthread.h>
#include <KadCrouting.h>
#include <inifiles.h>
#include <tcpsrv.h>
#include <KadClog.h>
#include <bufio.h>
#include <KadCmeta.h>
#include <KadCparser.h>
#include <KadCapi.h>

#include <overnet.h>
#include <overnetexports.h>
#include <pthreadutils.h>

int isOvernetREQ(unsigned char opcode) {
	switch(opcode) {
	case OVERNET_CONNECT:
	case OVERNET_PUBLICIZE:
	case OVERNET_SEARCH:
	case OVERNET_SEARCH_INFO:
	case OVERNET_PUBLISH:
	case OVERNET_FIREWALL_CONNECTION:
	case OVERNET_IP_QUERY:
	case OVERNET_IDENTIFY:
	case OVERNET_IDENTIFY_ACK:
		return 1;
	default:
		return 0;
	}
}

/*
   In Overnet, the byte order is generally
   Intel host (i.e., little-endian), not network...
   The byte order of IP addresses and MD4 hashes, however,
   is big-endian...

   ppb is a pointer to the pointer in the buffer,
   which gets incremented as the buffer bytes are
   consumed or produced.
 */


static peernode *getpeernode(peernode *p, unsigned char **ppb) {
	getint128n(p->hash, ppb);
	p->ip = getipn(ppb);
	p->port = getushortle(ppb);
	/* p->tport = getushortle(ppb); no tport in Overnet */
	p->type = *(*ppb)++;
	return p;
}

static peernode *getcontactnode(peernode *p, unsigned char **ppb) {
	getint128n(p->hash, ppb);
	p->ip = getipn(ppb);
	p->port = getushortle(ppb);
	/* p->tport = getushortle(ppb);  no tport in Overnet */
	return p;
}

/* puts in the buffer pointed by *ppb our IP and port.
   If pKE->leafmode is true, always puts 0; otherwise,
   if external IP is available, put that one, else
   put the internal one.
   This function was moved here because eMuleKAD would
   require the IP address to be put into the buffer as
   little endian (at least in the UDP datagram:
   FIXME: check about that in the TCP hello...
   Note: whether the IP address is routable etc. is *NOT*
   checked here: if desired, that must be done by the caller.
   Setting pKE->leafmode to true allows to appear as
   NATted (and therefore reduce the bandwidth) even
   if we are not. */
static unsigned char *overnetputourIPport(unsigned char **ppb, KadEngine *pKE) {
	if(pKE->leafmode) {
		putipn(ppb, 0);		/* deliberately advertise invalid IP/port */
		putushortle(ppb, 0);
	} else {
		if(pKE->extip != 0)				/* if known... */
			putipn(ppb, pKE->extip);	/* advertise external IP */
		else
			putipn(ppb, pKE->localnode.ip);	/* else internal IP */

		putushortle(ppb, pKE->localnode.port); /* always internal port */
	}
	return *ppb;
}

/* "Client" Overnet-specific calls for 25-byte packets. Packets have the format:
    OP_EDONKEYHEADER
    opcode (parameter: can only be Connect or Publicize!)
    23-byte peernode (localnode)
 */
static SessionObject *sendOvernetPeerReq(int opcode, KadEngine *pKE, unsigned long int bootip, int bootport){
	unsigned char kadbuf[25];
	SessionObject *status;
	unsigned char *pb = kadbuf;

	*pb++ = OP_EDONKEYHEADER;
	*pb++ = opcode;
	putint128n(&pb, pKE->localnode.hash);
	overnetputourIPport(&pb, pKE);
	*pb++ = 0; /* pKE->localnode.type; */

	status = P2PnewSessionsend(pKE, kadbuf, sizeof(kadbuf), bootip, bootport);
	return status;
}

/* Send a boot (Connect) request UDP packet to a remote host
   returns 0 if OK or 1 if errors occurred
   then caller should inspect either errno or WSAGetLastError */

SessionObject *sendOvernetBootReq(KadEngine *pKE, unsigned long int bootip, int bootport){
	return sendOvernetPeerReq(OVERNET_CONNECT, pKE, bootip, bootport);
}

/* Send a hello request UDP packet to a remote host
   returns 0 if OK or 1 if errors occurred
   then caller should inspect either errno or WSAGetLastError */

SessionObject *sendOvernetHelloReq(KadEngine *pKE, unsigned long int bootip, int bootport){
	return sendOvernetPeerReq(OVERNET_PUBLICIZE, pKE, bootip, bootport);
}

/* FIXME: to be reworked */
SessionObject *sendOvernetFwReq(KadEngine *pKE, int mytcpport, unsigned long int recipientip, int recipientport) {
	unsigned char kadbuf[4];
	SessionObject *status;
	unsigned char *pb = kadbuf;

	*pb++ = OP_EDONKEYHEADER;
	*pb++ = OVERNET_FIREWALL_CONNECTION;
	putushortle(&pb, (unsigned short int)mytcpport);
	status = P2PnewSessionsend(pKE, kadbuf, sizeof(kadbuf), recipientip, recipientport);
	return status;
}

/* Send an IP check request to a remote host
   declaring our own TCP port */

SessionObject *sendOvernetIpReq(KadEngine *pKE, int mytcpport, unsigned long int recipientip, int recipientport) {
	unsigned char kadbuf[4];
	SessionObject *status;
	unsigned char *pb = kadbuf;

	*pb++ = OP_EDONKEYHEADER;
	*pb++ = OVERNET_IP_QUERY;
	putushortle(&pb, (unsigned short int)mytcpport);
	status = P2PnewSessionsend(pKE, kadbuf, sizeof(kadbuf), recipientip, recipientport);
	return status;
}

SessionObject *sendOvernetSearch(KadEngine *pKE, unsigned long int recipientip, int recipientport, unsigned char parameter, int128 hash) {
	unsigned char kadbuf[19];
	SessionObject *status;
	unsigned char *pb = kadbuf;

	*pb++ = OP_EDONKEYHEADER;
	*pb++ = OVERNET_SEARCH;
	*pb++ = parameter;
	putint128n(&pb, hash);
	status = P2PnewSessionsend(pKE, kadbuf, sizeof(kadbuf), recipientip, recipientport);
	return status;
}

SessionObject *sendOvernetSearchInfo(KadEngine *pKE, unsigned long int recipientip, int recipientport, int128 hash, unsigned char *psfilter, int sfilterlen, unsigned short int min, unsigned short int max) {
	unsigned char *kadbuf;
	SessionObject *status;
	unsigned char *pb;
	int kadbuflen;

	kadbuflen = 23 + sfilterlen;
	kadbuf = (unsigned char *)malloc(kadbuflen);
	pb = kadbuf;
	*pb++ = OP_EDONKEYHEADER;
	*pb++ = OVERNET_SEARCH_INFO;
	putint128n(&pb, hash);
	if(psfilter == NULL || sfilterlen == 0) {
		*pb++ = 0;
	} else {
		*pb++ = 1;
		memcpy(pb, psfilter, sfilterlen);
		pb += sfilterlen;
	}
	putushortle(&pb, min);
	putushortle(&pb, max);
	assert(kadbuflen == pb - kadbuf);
	status = P2PnewSessionsend(pKE, kadbuf, kadbuflen, recipientip, recipientport);
	free(kadbuf);
	return status;
}

SessionObject *sendOvernetPublish(KadEngine *pKE, kobject *pko, unsigned long int recipientip, int recipientport) {
	SessionObject *status;
	unsigned char *pb = malloc(2 + pko->size);

	if(pb == NULL)
		return NULL;
	*pb++ = OP_EDONKEYHEADER;
	*pb++ = OVERNET_PUBLISH;
	memcpy(pb, pko->buf, pko->size);
	pb -= 2;
	status = P2PnewSessionsend(pKE, pb, 2 + pko->size, recipientip, recipientport);
	free(pb);
	return status;
}


/*  boot mechanism based on breadth-first order using a FIFO queue
    can be multithreaded at will bi increasing the size of the array th[] */
#define arraysize(a) sizeof(a)/sizeof(a[0])
static void *overnet_kboot_th(void *p);

static const pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;

static void contact_status_update(KadEngine *pKE, peernode *ppn, int isalive) {
	if(isalive) {
		ppn->type = 0;
	} else {
		ppn->type++;
		if(ppn->type >= NONRESPONSE_THRESHOLD) {
			rbt_StatusEnum rbt_status;
			pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK CONTACTS \\\\\\ */
			rbt_status = rbt_eraseKey(pKE->contacts, ppn->hash);
			pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK CONTACTS ///// */
			if(rbt_status == RBT_STATUS_OK) { /* it could also not be, if the entry was removed why the rbt was unlocked */
	#ifdef VERBOSE_DEBUG
				KadC_log("in BG thread: peer %s:%u evicted from contacts table as its type (%d) exceeded the limit\n",htoa(ppn->ip), ppn->port, ppn->type);
	#endif
				free(ppn);
			} else {
	#ifdef DEBUG
				KadC_log("in BG thread: failed eviction of peer %s:%u, type %d: rbt_status = %d\n",htoa(ppn->ip), ppn->port, ppn->type, rbt_status);
	#endif
			}
		}
	}
}

#ifndef OLD_KBOOT
typedef struct _thparblock {
	KadEngine *pKE;
	queue *fifo_check_ip;
	queue *fifo_check_peer;
	time_t *pstoptime;
	int volatile addednodes;
	int volatile checkingnodes;
	int volatile exiting;
} thparblock;

static int overnet_kboot(KadEngine *pKE, time_t *pstoptime, int nthreads) {
	int i;
	pthread_t th[10];
	peernode *ppn = NULL;
	peernode *ppnr;
	SessionObject *psession;
	packet *pkt;
	thparblock pblk;	/* auto is OK because we wait on pthread_join before returning */
	unsigned char randhash[16];

	if(nthreads > arraysize(th))
		nthreads = arraysize(th);
	pblk.pKE = pKE;
	pblk.fifo_check_ip   = new_queue(128);
	pblk.fifo_check_peer = new_queue(128);
	pblk.pstoptime = pstoptime;
	pblk.addednodes = 0;	/* nodes added NOT to k-buckets, but to the auxiliary contacts rbt useful for boot */
	pblk.checkingnodes = 0;
	pblk.exiting = 0;
	
	/* spawn threads to process queue */
#ifdef VERBOSE_DEBUG
		KadC_log("Going to spawn %d copies of overnet_kboot_th", nthreads);
#endif
	for(i=0; i < nthreads; i++) {
		pthread_create(&th[i], NULL, &overnet_kboot_th, &pblk);
	}

	/* look for a contact that answers to OVERNET_PUBLICIZE requests
	   loop at least 4 times to prime the boot process sufficiently, and
	   keep looping if the external IP address can't be obtained */
	for(i=0; *pstoptime > time(NULL) && !pKE->shutdown; i++) {
		void *iter;
		int j = 0;
		if(pblk.addednodes >= 4 && pKE->extip != 0)
			break;

		pthread_mutex_lock(&pKE->cmutex);		/* \\\\\\ LOCK CONTACTS \\\\\\ */
		/* Seed threads with peers to check from contacts */
		for (j = pblk.checkingnodes; j < nthreads; j++) {			
		#ifdef VERBOSE_DEBUG
			KadC_log("Checking peer from contacts\n");
			KadC_log("thread %d randhash before setrandom ", pthread_self());
			KadC_int128log(randhash);
			KadC_log("\n"); /*, rand_seed is %d, original %d\n", rand_seed, rand_seed_orig); */
		#endif
			int128setrandom(randhash);
	#ifdef VERBOSE_DEBUG
			KadC_log("thread %d Pinging peer with hash  ", pthread_self());
			KadC_int128log(randhash);
			KadC_log("\n"); /* , rand_seed is %d, original %d, randhash %p\n", rand_seed, rand_seed_orig, randhash); */
	#endif
			rbt_find(pKE->contacts, randhash, &iter);
	
			if(iter != NULL) {
				peernode *ppndup;
				ppn = rbt_value(iter);

				ppndup = malloc(sizeof(peernode));
				memcpy(ppndup, ppn, sizeof(peernode));
				/* prime the queue with bootstrap peer */
				if(pblk.fifo_check_peer->enq(pblk.fifo_check_peer, ppndup) != 0) {
#ifdef VERBOSE_DEBUG
					KadC_log("Failed to enque the node\n");
#endif
					free(ppndup);	 /* if there is no space in FIFO queue, throw away */
				} else {
					pblk.checkingnodes++;
#ifdef VERBOSE_DEBUG
					KadC_log("Enqued the node, checkingnodes now %d\n", pblk.checkingnodes);
#endif
				}
			} else {
#ifdef VERBOSE_DEBUG
				KadC_log("No peer found for the hash???\n");
#endif				
			}
		}
		pthread_mutex_unlock(&pKE->cmutex);		/* ///// UNLOCK CONTACTS ///// */
				
		/* Wait for peer nodes that will be used for IP request */
#ifdef VERBOSE_DEBUG
		KadC_log("Waiting for peer nodes\n", nthreads);
#endif		
		ppn = pblk.fifo_check_ip->deqtw(pblk.fifo_check_ip, 1000);
		if (!ppn) {
	#ifdef VERBOSE_DEBUG
			KadC_log("None found\n");
	#endif		
			continue;
		}
		
		/* do an ip check, only at the beginning of the boot process */
		if(pKE->fwstatuschecked == 0) {
			tcpsrvpar tcpsrvparblock = {0};
			pthread_t tcpsrv;
			unsigned long int extip = pKE->extip;
			/* IP check, and at the same time find a live contact to prime the boot queue */
			/* spawn a thread to wait for an incoming connection to local tcp port */
			int ipchecked = 0;
			tcpsrvparblock.pKE = pKE;
			tcpsrvparblock.buf = NULL;
			tcpsrvparblock.quit = 0;
			pthread_create(&tcpsrv, NULL, &tcpsrv_th, &tcpsrvparblock);	/* launch TCP listener */

#ifdef VERBOSE_DEBUG
			KadC_log("Ping OK with %s:%d but extip still unchecked: trying sendOvernetIpReq()\n",
				htoa(ppn->ip), ppn->port);
#endif
			psession = sendOvernetIpReq(pKE, pKE->localnode.tport, ppn->ip, ppn->port);
			if(psession != NULL) { /* Session allocation might fail */
				pkt = getpktt(psession, 5000);				/* expect OVERNET_IP_QUERY_ANSWER */
				if(pkt == NULL) { 	/* Timeout */
#ifdef VERBOSE_DEBUG
					KadC_log("TIMEOUT while expecting OVERNET_IP_QUERY_ANSWER\n");
#endif
					tcpsrvparblock.quit = 1;
				} else {
					unsigned char *p = pkt->buf+2;
					if(pkt->len == 6) {
						extip = getipn(&p);
#ifdef VERBOSE_DEBUG
						KadC_log("Our IP: %s\n", htoa(extip));
#endif
						ipchecked = 1;
					} else {
#ifdef VERBOSE_DEBUG
						KadC_log("Hmm, pkt->len was %d instead of 6:\n", pkt->len);
						{
							int i;
							for(i=0; i < pkt->len; i++) {
								if((i % 16) == 0)
									KadC_log("\n");
								KadC_log("%02x ", pkt->buf[i]);
							}
							KadC_log("\n--------------------------------\n");
						}
#endif
					}
					free(pkt->buf);
					free(pkt);
				}
				/* that's enough, ignore OVERNET_IP_QUERY_END if any */
				destroySessionObject(psession);
			} else {
#ifdef VERBOSE_DEBUG
				KadC_log("Can't create session for OVERNET_IP_QUERY to peer %s:%u\n", htoa(ppn->ip), ppn->port);
				/* KadC_log("%s:%d Session allocation failed to send OVERNET_IP_QUERY\n", __FILE__, __LINE__); */
#endif
			}
			pthread_join(tcpsrv, NULL);
#ifdef VERBOSE_DEBUG
			KadC_log("joined tcpsrv_th thread %lx\n", tcpsrv);
#endif
			if(ipchecked) {
				pKE->fwstatuschecked = 1;
				/* has the external IP it changed from before? */
				if(extip != pKE->extip) {
					/* yes, update the firewalled status as well.
					   sometimes firewall check fails, and we don't want to
					   recheck the firewalled status too often. */
					pKE->notfw = tcpsrvparblock.notfw;
					pKE->extip = extip;
				}
			}
		}
		/* Free the peer node as it was allocated for us by the 
		 * kboot_th thread */
		if (ppn) free(ppn);			
	}

#ifdef VERBOSE_DEBUG
		KadC_log("waiting for overnet_boot_th threads\n");
#endif

	pblk.exiting = 1;

	for(i=0; i < nthreads; i++) {
		pthread_join(th[i], NULL);		/* reap terminated threads */
#ifdef VERBOSE_DEBUG
		KadC_log("joined overnet_boot_th thread %lx\n", (unsigned long int)&th[i]);
#endif
	}

	/* Free outstanding peernode's in the queues */
	for(i=0; (ppnr = pblk.fifo_check_ip->deqtw(pblk.fifo_check_ip, 200)) != NULL; i++) {
		free(ppnr);	/* flush FIFO queue */
	}
	for(i=0; (ppnr = pblk.fifo_check_peer->deqtw(pblk.fifo_check_peer, 200)) != NULL; i++) {
		free(ppnr);	/* flush FIFO queue */
	}
#ifdef VERBOSE_DEBUG
	KadC_log("Flushing kboot queue found %d peernodes\n", pblk.addednodes);
#endif
	pblk.fifo_check_ip->destroy(pblk.fifo_check_ip);
	pblk.fifo_check_peer->destroy(pblk.fifo_check_peer);

	return pblk.addednodes;
}

void *overnet_kboot_th(void *p) {
	thparblock *ppblk = p;
	peernode *ppn = NULL;
	KadEngine *pKE = ppblk->pKE;
	int ping;
	int i = 0;
	int free_ppn = 1;
	
	for(;; i++) {
	#ifdef VERBOSE_DEBUG
		/* Keep this away unless testing, it's not thread safe */
		/* KadC_log("Checking nodes amount: %d\n", ppblk->checkingnodes); */
	#endif
		if (ppn != NULL) {
			/* Decrease last checking nodes amount from last round */
			pthread_mutex_lock(&pKE->cmutex);		/* \\\\\\ LOCK CONTACTS \\\\\\ */
			ppblk->checkingnodes--;
			pthread_mutex_unlock(&pKE->cmutex);		/* ///// UNLOCK CONTACTS ///// */
		}

		if (ppblk->exiting) { /* time to go? */
			break;
		}
/* not needed if above ppblk->exiting is used
		if(time(NULL) >= *ppblk->pstoptime) {	time to go? 
			break;
		}
*/
		if(ppblk->pKE->shutdown != 0) {	/* time to go? */
			break;
		}
		/* The fifo is filled by the main thread and also when a peer found
		 * during boot process returns more peers.
		 */
		ppn = ppblk->fifo_check_peer->deqtw(ppblk->fifo_check_peer, 2000);
		if (!ppn) continue;

		/* ping with OVERNET_PUBLICIZE and if OK send a OVERNET_IP_QUERY */
#ifdef VERBOSE_DEBUG
		KadC_log("Pinging %s:%d ", htoa(ppn->ip), ppn->port);
		KadC_log("(pKE->extip = %s)\n", htoa(pKE->extip));
#endif
		ping = Overnet_ping(pKE, ppn->ip, ppn->port, 2000);
		/* -1 means usually some other thread is already 
		 * checking this target, just pick another
		 * target in that case
		 */
		if (ping == -1) continue;
		else if (ping) {
			peernode *ppndup;
			packet *pkt;
			SessionObject *psession;

			contact_status_update(pKE, ppn, 1);
 			/* do an ip check, only at the beginning of the boot process */
			if(pKE->fwstatuschecked == 0) {
				/* Now use a clone of this contact, which we know is live, to 
				 * possibly obtain our IP and firewall status */
				ppndup = malloc(sizeof(peernode));
				memcpy(ppndup, ppn, sizeof(peernode));
				/* prime the queue with bootstrap peer */
				if(ppblk->fifo_check_ip->enq(ppblk->fifo_check_ip, ppndup) != 0) {
					free(ppndup);	 /* if there is no space in FIFO queue, throw away */
				} 
			}
			
#ifdef VERBOSE_DEBUG
			KadC_log("-- Seed %s:%d received from boot FIFO\n", htoa(ppn->ip), ppn->port);
#endif
			ppn->type = 0;	/* FIXME: why?? */
			psession = sendOvernetBootReq(ppblk->pKE, ppn->ip, ppn->port);
			if(psession != NULL) { /* session allocation sometimes fails. FIXME: find out why */
				pkt = getpktt(psession, 3000);	/* we want speed here: 3 s timeout */
				destroySessionObject(psession);
				if(pkt != NULL) { 	/* if it answered */
					unsigned char *buf = pkt->buf;
					int nrecvd = pkt->len;
					if( nrecvd >= 4 && buf[1] == OVERNET_CONNECT_REPLY) {
						unsigned char *pb = &buf[2];
						int npeers = getushortle(&pb);
						if(nrecvd == 4 + npeers * 23) { /* packet must contain npeers 23-byte peers */
							int i;
							rbt_StatusEnum rbt_status;
							int updstatus;

							for(i=0; i < npeers; i++) {
								peernode *ppnr;
								void *iter;

								ppnr = (peernode *)malloc(sizeof(peernode));
								assert(ppnr != 0);	/* or else, no memory */
								getpeernode(ppnr, &pb);


								pthread_mutex_lock(&ppblk->pKE->cmutex);			/* \\\\\\ LOCK CONTACTS \\\\\\ */
								rbt_status = rbt_find(ppblk->pKE->contacts, ppnr->hash, &iter);
								pthread_mutex_unlock(&ppblk->pKE->cmutex);			/* ///// UNLOCK CONTACTS ///// */
								if(rbt_status == RBT_STATUS_OK ||	/* if already in contacts... */
								   ppblk->fifo_check_peer->enq(ppblk->fifo_check_peer, ppnr) != 0) /*...or FIFO FULL... */
									free(ppnr); /* ...then, throw ppnr away */
								else if (rbt_status != RBT_STATUS_OK) {
									pthread_mutex_lock(&pKE->cmutex);		/* \\\\\\ LOCK CONTACTS \\\\\\ */
									ppblk->checkingnodes++;
									pthread_mutex_unlock(&pKE->cmutex);		/* ///// UNLOCK CONTACTS ///// */									
								}
							}
							/* This is the single point where the kboot process
							   populates routing-related kbuckets/kspace */
							updstatus = UpdateNodeStatus(ppn, ppblk->pKE, 1);

							if(updstatus == 0) {
#ifdef VERBOSE_DEBUG
								KadC_log("new peer %s:%d inserted in kbucket %d\n", htoa(ppn->ip), ppn->port, int128xorlog(ppn->hash, ppblk->pKE->localnode.hash));
#endif
							} else if(updstatus == -1) {
#ifdef VERBOSE_DEBUG
								KadC_log("new peer %s:%d had nonroutable address, or referred to ourselves\n", htoa(ppn->ip), ppn->port);
#endif
							}

							/* regardless, see if there is space in aux contacts rbt */
							pthread_mutex_lock(&ppblk->pKE->cmutex);			/* \\\\\\ LOCK CONTACTS \\\\\\ */
							if(rbt_size(ppblk->pKE->contacts) < ppblk->pKE->maxcontacts) {
								rbt_status = rbt_insert(ppblk->pKE->contacts, ppn->hash, ppn, 0);
								if(rbt_status == RBT_STATUS_OK) {
#ifdef VERBOSE_DEBUG
									KadC_log("new peer %s:%d inserted in contacts table\n", htoa(ppn->ip), ppn->port);
#endif
									ppblk->addednodes++;
									ppn = NULL; /* so its free()ing will be postponed */
								}
							}
							pthread_mutex_unlock(&ppblk->pKE->cmutex);			/* ///// UNLOCK CONTACTS ///// */
						}
					}
					destroypkt(pkt);
				}
			} else {
			#ifdef VERBOSE_DEBUG
				KadC_log("Can't create session for OVERNET_CONNECT to peer %s:%u\n", htoa(ppn->ip), ppn->port);
			#endif
			}
			if(free_ppn && ppn != NULL)
				free(ppn);	/* the seed ones were from the contacts rbt, not malloc'ed here */
		} else {
			contact_status_update(pKE, ppn, 0);
		}		
	}	/* end for(;;) */
	return NULL;
}

#else /* OLD_KBOOT */
typedef struct _thparblock {
	KadEngine *pKE;
	queue *fifo;
	time_t *pstoptime;
	int addednodes;
} thparblock;


static int overnet_kboot(KadEngine *pKE, time_t *pstoptime, int nthreads) {
	int i;
	pthread_t th[10];
	peernode *ppn = NULL;
	peernode *ppnr;
	SessionObject *psession;
	packet *pkt;
	thparblock pblk;	/* auto is OK because we wait on pthread_join before returning */
	void *iter;
	unsigned char randhash[16];

#ifdef VERBOSE_DEBUG
		KadC_log("Starting kboot\n", nthreads);
#endif

	if(nthreads > arraysize(th))
		nthreads = arraysize(th);
	pblk.pKE = pKE;
	pblk.fifo = new_queue(128);	/* candidate for kbuckets */
	pblk.pstoptime = pstoptime;
	pblk.addednodes = 0;	/* nodes added NOT to k-buckets, but to the auxiliary contacts rbt useful for boot */

	/* spawn threads to process queue */
#ifdef VERBOSE_DEBUG
		KadC_log("Going to spawn %d copies of overnet_kboot_th\n", nthreads);
#endif
	for(i=0; i < nthreads; i++) {
		pthread_create(&th[i], NULL, &overnet_kboot_th, &pblk);
	}

	/* look for a contact that answers to OVERNET_PUBLICIZE requests
	   loop at least 4 times to prime the boot process sufficiently, and
	   keep looping if the external IP address can't be obtained */
	for(i=0; *pstoptime > time(NULL) && !pKE->shutdown; i++) {
		int128setrandom(randhash);
#ifdef VERBOSE_DEBUG
		KadC_log("Pinging peer with hash  ");
		KadC_int128log(randhash);
		KadC_log("\n");
#endif
		pthread_mutex_lock(&pKE->cmutex);		/* \\\\\\ LOCK CONTACTS \\\\\\ */
		rbt_find(pKE->contacts, randhash, &iter);

		if(iter == NULL) {
		#ifdef VERBOSE_DEBUG
				KadC_log("No peer founds found from contacts for the hash???");
		#endif			
			pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK CONTACTS ///// */
			continue;
		}
		ppn = rbt_value(iter);
		pthread_mutex_unlock(&pKE->cmutex);		/* ///// UNLOCK CONTACTS ///// */

		/* ping with OVERNET_PUBLICIZE and if OK send a OVERNET_IP_QUERY */

		if(i >= 4 && pKE->extip != 0) {
		#ifdef VERBOSE_DEBUG
				KadC_log("Breaking out");
		#endif			
			
			break;
		}
#ifdef VERBOSE_DEBUG
		KadC_log("Pinging %s:%d ", htoa(ppn->ip), ppn->port);
		KadC_log("(pKE->extip = %s, i = %d)\n", htoa(pKE->extip), i);
#endif
		if(Overnet_ping(pKE, ppn->ip, ppn->port, 2000) == 1) {
			peernode *ppndup;
			contact_status_update(pKE, ppn, 1);
			/* do an ip check, only at the beginning of the boot process */
			if(pKE->fwstatuschecked == 0) {
				tcpsrvpar tcpsrvparblock = {0};
				pthread_t tcpsrv;
				unsigned long int extip = pKE->extip;
				/* IP check, and at the same time find a live contact to prime the boot queue */
				/* spawn a thread to wait for an incoming connection to local tcp port */
				int ipchecked = 0;
				tcpsrvparblock.pKE = pKE;
				tcpsrvparblock.buf = NULL;
				tcpsrvparblock.quit = 0;
				pthread_create(&tcpsrv, NULL, &tcpsrv_th, &tcpsrvparblock);	/* launch TCP listener */

#ifdef VERBOSE_DEBUG
				KadC_log("Ping OK with %s:%d but extip still unchecked: trying sendOvernetIpReq()\n",
					htoa(ppn->ip), ppn->port);
#endif
				psession = sendOvernetIpReq(pKE, pKE->localnode.tport, ppn->ip, ppn->port);
				if(psession != NULL) { /* Session allocation might fail */
					pkt = getpktt(psession, 5000);				/* expect OVERNET_IP_QUERY_ANSWER */
					if(pkt == NULL) { 	/* Timeout */
#ifdef VERBOSE_DEBUG
						KadC_log("TIMEOUT while expecting OVERNET_IP_QUERY_ANSWER\n");
#endif
						tcpsrvparblock.quit = 1;
					} else {
						unsigned char *p = pkt->buf+2;
						if(pkt->len == 6) {
							extip = getipn(&p);
#ifdef VERBOSE_DEBUG
							KadC_log("Our IP: %s\n", htoa(extip));
#endif
							ipchecked = 1;
						} else {
#ifdef VERBOSE_DEBUG
							KadC_log("Hmm, pkt->len was %d instead of 6:\n", pkt->len);
							{
								int i;
								for(i=0; i < pkt->len; i++) {
									if((i % 16) == 0)
										KadC_log("\n");
									KadC_log("%02x ", pkt->buf[i]);
								}
								KadC_log("\n--------------------------------\n");
							}
#endif
						}
						free(pkt->buf);
						free(pkt);
					}
					/* that's enough, ignore OVERNET_IP_QUERY_END if any */
					destroySessionObject(psession);
				} else {
#ifdef VERBOSE_DEBUG
					KadC_log("Can't create session for OVERNET_IP_QUERY to peer %s:%u\n", htoa(ppn->ip), ppn->port);
					/* KadC_log("%s:%d Session allocation failed to send OVERNET_IP_QUERY\n", __FILE__, __LINE__); */
#endif
				}
				pthread_join(tcpsrv, NULL);
#ifdef VERBOSE_DEBUG
				KadC_log("joined tcpsrv_th thread %lx\n", tcpsrv);
#endif
				if(ipchecked) {
					pKE->fwstatuschecked = 1;
					/* has the external IP it changed from before? */
					if(extip != pKE->extip) {
						/* yes, update the firewalled status as well.
						   sometimes firewall check fails, and we don't want to
						   recheck the firewalled status too often. */
						pKE->notfw = tcpsrvparblock.notfw;
						pKE->extip = extip;
					}
				}
			}
			/* Now use a clone of this contact, which we know is live, to seed the bootstrap */
			ppndup = malloc(sizeof(peernode));
			memcpy(ppndup, ppn, sizeof(peernode));
			/* prime the queue with bootstrap peer */
			if(pblk.fifo->enq(pblk.fifo, ppndup) != 0) {
				free(ppndup);	 /* if there is no space in FIFO queue, throw away */
			}
		} else {
			contact_status_update(pKE, ppn, 0);
		}
	}

	for(i=0; i < nthreads; i++) {
		pthread_join(th[i], NULL);		/* reap terminated threads */
#ifdef VERBOSE_DEBUG
		KadC_log("joined overnet_boot_th thread %lx\n", (unsigned long int)&th[i]);
#endif
	}

	for(i=0; (ppnr = pblk.fifo->deqtw(pblk.fifo, 200)) != NULL; i++) {
		free(ppnr);	/* flush FIFO queue */
	}
#ifdef VERBOSE_DEBUG
	KadC_log("Flushing kboot queue found %d peernodes\n", pblk.addednodes);
#endif
	pblk.fifo->destroy(pblk.fifo);

#ifdef VERBOSE_DEBUG
		KadC_log("Stopping kboot");
#endif			

	return pblk.addednodes;
}

static void *overnet_kboot_th(void *p) {
	thparblock *ppblk = p;
	peernode *ppn;

	for(;;) {
		if(time(NULL) >= *ppblk->pstoptime) {	/* time to go? */
			break;
		}
		if(ppblk->pKE->shutdown != 0) {	/* time to go? */
			break;
		}
		ppn = ppblk->fifo->deqtw(ppblk->fifo, 5000);
		if(ppn != NULL) {	/* if not timeout on queue */
			packet *pkt;
			SessionObject *psession;
#ifdef VERBOSE_DEBUG
			KadC_log("-- Seed %s:%d received from boot FIFO\n", htoa(ppn->ip), ppn->port);
#endif
			ppn->type = 0;	/* FIXME: why?? */
			psession = sendOvernetBootReq(ppblk->pKE, ppn->ip, ppn->port);
			if(psession != NULL) { /* session allocation sometimes fails. FIXME: find out why */
				pkt = getpktt(psession, 3000);	/* we want speed here: 3 s timeout */
				destroySessionObject(psession);
				if(pkt != NULL) { 	/* if it answered */
					unsigned char *buf = pkt->buf;
					int nrecvd = pkt->len;
					if( nrecvd >= 4 && buf[1] == OVERNET_CONNECT_REPLY) {
						unsigned char *pb = &buf[2];
						int npeers = getushortle(&pb);
						if(nrecvd == 4 + npeers * 23) { /* packet must contain npeers 23-byte peers */
							int i;
							rbt_StatusEnum rbt_status;
							int updstatus;

							for(i=0; i < npeers; i++) {
								peernode *ppnr;
								void *iter;

								ppnr = (peernode *)malloc(sizeof(peernode));
								assert(ppnr != 0);	/* or else, no memory */
								getpeernode(ppnr, &pb);


								pthread_mutex_lock(&ppblk->pKE->cmutex);			/* \\\\\\ LOCK CONTACTS \\\\\\ */
								rbt_status = rbt_find(ppblk->pKE->contacts, ppnr->hash, &iter);
								pthread_mutex_unlock(&ppblk->pKE->cmutex);			/* ///// UNLOCK CONTACTS ///// */
								if(rbt_status == RBT_STATUS_OK ||	/* if already in contacts... */
								   ppblk->fifo->enq(ppblk->fifo, ppnr) != 0) /*...or FIFO FULL... */
									free(ppnr); /* ...then, throw ppnr away */
							}
							/* This is the single point where the kboot process
							   populates routing-related kbuckets/kspace */
							updstatus = UpdateNodeStatus(ppn, ppblk->pKE, 1);

							if(updstatus == 0) {
#ifdef VERBOSE_DEBUG
								KadC_log("new peer %s:%d inserted in kbucket %d\n", htoa(ppn->ip), ppn->port, int128xorlog(ppn->hash, ppblk->pKE->localnode.hash));
#endif
							} else if(updstatus == -1) {
#ifdef VERBOSE_DEBUG
								KadC_log("new peer %s:%d had nonroutable address, or referred to ourselves\n", htoa(ppn->ip), ppn->port);
#endif
							}

							/* regardless, see if there is space in aux contacts rbt */
							pthread_mutex_lock(&ppblk->pKE->cmutex);			/* \\\\\\ LOCK CONTACTS \\\\\\ */
							if(rbt_size(ppblk->pKE->contacts) < ppblk->pKE->maxcontacts) {
								rbt_status = rbt_insert(ppblk->pKE->contacts, ppn->hash, ppn, 0);
								if(rbt_status == RBT_STATUS_OK) {
#ifdef VERBOSE_DEBUG
									KadC_log("new peer %s:%d inserted in contacts table\n", htoa(ppn->ip), ppn->port);
#endif
									ppblk->addednodes++;
									ppn = NULL; /* so its free()ing will be postponed */
								}
							}
							pthread_mutex_unlock(&ppblk->pKE->cmutex);			/* ///// UNLOCK CONTACTS ///// */
						}
					}
					destroypkt(pkt);
				}
			} else {
#ifdef VERBOSE_DEBUG
				KadC_log("Can't create session for OVERNET_CONNECT to peer %s:%u\n", htoa(ppn->ip), ppn->port);
#endif
			}
			if(ppn != NULL)
				free(ppn);	/* the seed ones were from the contacts rbt, not malloc'ed here */
		} else {
#ifdef VERBOSE_DEBUG
			KadC_log("overnet_kboot_th timed out while waiting for seeds...\n");
#endif
		}
	}	/* end for(;;) */
	return NULL;
}

#endif /* OLD_KBOOT */


/* Returns -1 if session could not be allocated (usually means someone is probing
 * the target already), 1 if target is alive, 0 if not */
int Overnet_ping(KadEngine *pKE, unsigned long int ip, unsigned short int port, int timeout) {
	int isalive = 0;
	SessionObject *psession = sendOvernetHelloReq(pKE, ip, port);
	if(psession != NULL) { /* Session allocation sometimes fails. FIXME: find out why */
		packet *pkth = getpktt(psession, timeout);

		destroySessionObject(psession);

		if(pkth != NULL && pkth->len == 2 && pkth->buf[1] == OVERNET_PUBLICIZE_ACK)
			isalive = 1;
		if(pkth != NULL) {
			destroypkt(pkth);	/* if a packet had arrived, destroy it */
		}
	} else {
#ifdef VERBOSE_DEBUG
		KadC_log("Can't create session for OVERNET_PUBLICIZE to peer %s:%u\n", htoa(ip), port);
#endif
		isalive = -1;
	}
	return isalive;
}

/* starts a recursive node lookup sending OVERNET_SEARCH packets to
   the most promising nodes, optionally followed by a OVERNET_SEARCH_INFO
   operation (with or without an s-filter).
   If the OVERNET_SEARCH_INFO is requested, each peer returned in
   OVERNET_SEARCH_NEXT will be queried with it (not more than once),
   and OVERNET_SEARCH packets will have parameter OVERNET_FIND_SEARCH (0x02);
   else, the peers closest to target_hash will be returned, and
   OVERNET_SEARCH packets will have parameter OVERNET_FIND_ONLY (0x04)
   (typically, this will be done when performing a node lookup before
   publishing).

   The process works like this:

   one starts with a small set of peers (say, 20), sorted by closeness
   to the target into an input_rbt. The set is passed to a team of threads
   each of which extracts a peer (removing it from the input_rbt), sends
   a OVERNET_SEARCH, and if the OVERNET_SEARCH_NEXT arrives adds the
   peers back to the input_rbt, the peer just queried to a results_rbt
   and terminates; then it iterates until the stoptime is reached,
   or the input_rbt was empty, or max_hits have accumulated in the
   results_rbt.
   The main tread join()s on all those threads.
   If the hash is our peer's hash, and the parameter dosearch is 0,
   the parameter of the OVERNET_SEARCH packet is set to 0x14, and the
   procedure returns NULL. This type of lookup is common to populate
   the k-buckets with low index; Overnet does it mostly upon startup.

   If, on the other hand, the target hash is NOT coincident with our
   peer's hash, the parameter of the OVERNET_SEARCH packet is set to
   0x04, and the procedure returns the results rbt. This type of lookup
   is done to choose nodes where to publish information.
   NOTE: the peernodes returned in the results rbt have their hash
   XOR'r with the target hash. This allows to extract them in order
   of decreasing closeness to the target, but the caller must
   remember to XOR their hash again using:
   int128xor(ppn->hash, ppn->hash, targethash). That must be done
   AFTER removing each peernode from the rbt (with rbt_erase), or
   else the rbt structure will be messed up (the hashes are the indices...)

   Finally, if the parameter dosearch is 1, each thread, instead of
   adding the peer to the results rbt, checks if the peer recommended
   itself in the list of peers contained in the OVERNET_SEARCH_NEXT
   reply packet. If not, it discards it; if yes, it sends to the peer
   a OVERNET_SEARCH_INFO and waits for all the OVERNET_SEARCH_RESULTS
   up to the OVERNET_SEARCH_END (or a timeout). Then it passes each
   k-object through sfilter (if any) and, if it passes, adds the
   k-object to the results rbt. In other words, a caller setting
   dosearch = 1 must expect as results k-objects rather than peers.
   NOTE: in this case, the parameter for the OVERNET_SEARCH
   is 0x02 rather than 0x04.

   Summarizing:
                         OVERNET_SEARCH  returned       purpose
                         parameter       value
   dosearch == 0
      target hash == self 0x14           NULL           k-buckets refresh
      target hash != self 0x04           peernodes rbt  find nodes for publishing
   dosearch != 0
      target hash == any  0x02           kstore         search

 */

static void *Overnet_find_th(void *p);

#ifndef OLD_SEARCH_ONLY

typedef struct _findblk {
	int dosearch;
	int128 targethash;
	unsigned char *psfilter;
	int sfilterlen;
	KadEngine *pKE;
	void *find_input_rbt;
	pthread_mutex_t fimutex;
	void *find_results_rbt;
	pthread_mutex_t frmutex;
	int find_results_avail;
	time_t *pstoptime;
	kstore *ks;
	unsigned short int resn1;
	unsigned short int resn2;
	pthread_mutex_t fsmutex;
	unsigned char kstore_full;
	int nworking_on_fi;
	KadCfind_params *pfpar;
} findblk;

/* Send to ppn OVERNET_SEARCH_INFO
   Get all replies  OVERNET_SEARCH_RESULT until OVERNET_SEARCH_END or timeout
   Put returned peers wrapped as kobjects into kstore pfb->ks
   decrememnt pfb->find_results_avail */

static void OvernetSearchInfoSession(findblk *pfb, peernode *ppn) {
	SessionObject *psession;
	peernode pnunx;

	if (pfb->kstore_full) {
		/* If full already, no use to send search infos */
		return;
	}
	
#if 0	/* DISABLED FOR NOW */
	if(ppn->type > NONRESPONSE_THRESHOLD) {
#ifdef VERBOSE_DEBUG
		KadC_log("not sending OVERNET_SEARCH_INFO to sleepy peer %s:%u type: %d\n", htoa(ppn->ip), ppn->port, ppn->type);
#endif
		return;
	}
#endif

	pnunx = *ppn;
	int128xor(pnunx.hash, pnunx.hash, pfb->targethash); /* save for future use */

	psession = sendOvernetSearchInfo(pfb->pKE, ppn->ip, ppn->port, pfb->targethash, pfb->psfilter, pfb->sfilterlen, 0, pfb->pfpar->max_hits);

	if(psession == NULL) { /* Session allocation sometimes fails. FIXME: find out why */
#ifdef VERBOSE_DEBUG
			KadC_log("Client session cannot be created to send an OVERNET_SEARCH_INFO\n");
#endif
	} else {
		int endarrived = 0;
	/* get OVERNET_SEARCH_RESULT list and OVERNET_SEARCH_END from peer  */
		while(!endarrived &&
				time(NULL) < *(pfb->pstoptime) &&
				!pfb->kstore_full) {
			packet *pkt = getpktt(psession, 2000);	/* 2 s timeout */
			if(pkt == NULL) {
#ifdef VERBOSE_DEBUG
				KadC_log("timeout while waiting for OVERNET_SEARCH_RESULT or OVERNET_SEARCH_END from %s:%u\n",
							htoa(ppn->ip), ppn->port);
#endif
				UpdateNodeStatus(&pnunx, pfb->pKE, 0);
				break;
			} else {
				unsigned char *buf = pkt->buf;
				int nrecvd = pkt->len;
				unsigned char *psf = pfb->psfilter;

				if(nrecvd >= 22) { /* the min is for OVERNET_SEARCH_END */
					/* UpdateNodeStatus(&pnunx, pfb->pKE, 1); / * USELESS: we just did it when we got OVERNET_SEARCH_NEXT  */
					if(buf[1] == OVERNET_SEARCH_RESULT) {
						kobject *pko;
						int kstatus;
						int allow_hit_callback = 1;
						
#ifdef VERBOSE_DEBUG
						KadC_log("Results from %s:%u\n", htoa(pnunx.ip), pnunx.port);
#endif
						pko = kobject_new(&buf[2], nrecvd-2);

						/* pass anyway through filter (some clients out there are defective) */

						if(psf != NULL && s_filter(pko, &psf, psf+pfb->sfilterlen) != 1) {
#ifdef VERBOSE_DEBUG
							psf = pfb->psfilter;
							KadC_log("kobject ");
							kobject_dump(pko, ";");
							KadC_log(" from %s:%u does not pass the s-filter ", htoa(pnunx.ip), pnunx.port);
							s_filter_dump(&psf, psf+pfb->sfilterlen);
							KadC_log("\n");
#elif DEBUG
							KadC_log("X");	/* post-filtering blocked record escaped from faulty server filter on peer's side */
#endif
							kobject_destroy(pko);
						} else {
							if (!pfb->pfpar->hit_callback ||
							     pfb->pfpar->hit_callback_mode == KADC_COLLECT_HITS)
							{
								/* Accumulate K-Objects in K-store pfb->ks
								   We already know that the first hash is the same for all,
								   so we index by second hash. This enforces the principle of
								   cosidering as duplicates k-objects with BOTH hashes equal. */
								pthread_mutex_lock(&pfb->fsmutex);			/* \\\\\\ LOCK FS \\\\\\ */
								kstatus = kstore_insert(pfb->ks, pko, 1, 0); /* index w/ second hash, reject duplicates */
								if(pfb->ks->avail <= 0)
									pfb->kstore_full = 1;
								pthread_mutex_unlock(&pfb->fsmutex);		/* ///// UNLOCK FS ///// */
								if(kstatus != 0) {
									kobject_destroy(pko); /* because insert failed */
									allow_hit_callback = 0;
	#ifdef VERBOSE_DEBUG
									KadC_log("kstore_insert returned status %d for results returned by peer %s:%u\n", kstatus,  htoa(pnunx.ip), pnunx.port);
	#elif DEBUG
									if(kstatus == 1) {
										KadC_log(".");	/* store full, stop! (geddit?) */
									} else {
										KadC_log("D");	/* rbt error, most likely duplicate key */
									}
#endif
								} else {
	#ifdef VERBOSE_DEBUG
									KadC_log("Results from peer %s:%u inserted in k-store\n", htoa(pnunx.ip), pnunx.port);
	#elif DEBUG
									KadC_log("!");	/* inserted! */
	#endif
								}
							}
							
							if (pfb->pfpar->hit_callback && allow_hit_callback) {
								int res;
								res = (*pfb->pfpar->hit_callback)(pko, pfb->pfpar->hit_callback_context);
								if (res == 1) {
									/* The callback asked to terminate returning of hits */
									pfb->kstore_full = 1;
								}
								if (pfb->pfpar->hit_callback_mode != KADC_COLLECT_HITS) {
									kobject_destroy(pko);
								}
							}
						}
					} else if (buf[1] == OVERNET_SEARCH_END) {
#ifdef VERBOSE_DEBUG
						unsigned char *pb = buf+18;
						KadC_log("OVERNET_SEARCH_END received from peer %s:%u (%u, %u)\n",
									htoa(pnunx.ip), pnunx.port, getushortle(&pb), getushortle(&pb));
#endif
						/* #### FIXME: do something (what??) with the two ushorts */
						endarrived = 1;
					} else {
#ifdef VERBOSE_DEBUG
						KadC_log("Unexpected packet with opcode 0x%02x from peer %s:%u \n",
								buf[1], htoa(pnunx.ip), pnunx.port);
#endif
					}
				} else {
#ifdef VERBOSE_DEBUG
					KadC_log("peer %s:%u sent a reply too short (%d bytes)\n",
							htoa(pnunx.ip), pnunx.port, nrecvd);
#endif
				}
				destroypkt(pkt);
			}

		}
		destroySessionObject(psession);
	}

}

void *Overnet_find(KadEngine *pKE, int128 targethash, int dosearch, unsigned char *psfilter, int sfilterlen, time_t *pstoptime, int maxhits, int nthreads) {
	KadCfind_params fpar;
	KadCfind_init(&fpar);
	fpar.max_hits = maxhits;
	fpar.threads  = nthreads;
	return Overnet_find2(pKE, targethash, dosearch, psfilter, sfilterlen, pstoptime, &fpar);
}

void *Overnet_find2(KadEngine *pKE, int128 targethash, int dosearch, unsigned char *psfilter, int sfilterlen, time_t *pstoptime, KadCfind_params *pfpar) {
	int i;
	int addednodes;
	void *iter;
	rbt_StatusEnum rbt_status;
	findblk fb;	/* block shared between main thread and children threads */
	pthread_t th[20];
	int logd_target_local = int128xorlog(pKE->localnode.hash, targethash);
	if(logd_target_local < 0)
		logd_target_local = 0;	/* looking up our own node... */

	/* regardless of what nthreads says, never use more than arraysize(th) threads for this */
	if(pfpar->threads > arraysize(th))
		pfpar->threads = arraysize(th);

	fb.dosearch = dosearch;
	fb.targethash = targethash;
	fb.psfilter = psfilter;
	fb.sfilterlen = sfilterlen;
	fb.pKE = pKE;
	fb.find_input_rbt = rbt_new((rbtcomp *)int128lt, (rbtcomp *)int128eq);
	fb.fimutex = mutex_initializer;
	fb.find_results_rbt = rbt_new((rbtcomp *)int128lt, (rbtcomp *)int128eq);
	fb.frmutex = mutex_initializer;
	fb.fsmutex = mutex_initializer;
	if(dosearch)
		fb.find_results_avail = pfpar->max_hits * pfpar->threads;
	else
		fb.find_results_avail = pfpar->max_hits;
	if(fb.find_results_avail < 10)
		fb.find_results_avail = 10;	/* at least lookup to gather 10 hits */
#ifdef VERBOSE_DEBUG
	KadC_log("Will lookup not more than %d nodes\n", fb.find_results_avail);
#endif
	fb.pstoptime = pstoptime;
#ifdef VERBOSE_DEBUG
	KadC_log("Creating a kstore with room for %d objects\n", pfpar->max_hits);
#endif
	fb.ks = kstore_new(pfpar->max_hits);
	fb.kstore_full = 0;
	fb.nworking_on_fi = pfpar->threads;
	fb.pfpar = pfpar;
	
	/* prime the find_input_rbt with contact nodes starting from the closest
	   to the targer, and getting as many peernodes as possible
	   from adjacent nodes above and below until 20 are obtained,
	   or there aren't any more left */

	/* We prime find_input_rbt using the closest nodes in contacts table */
	{
		void *iterf, *iterb;

		pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK CONTACTS \\\\\\ */
		rbt_find(pKE->kspace, targethash, &iterf);	/* forward iterator */
		if(iterf == NULL)
			iterf = rbt_end(pKE->kspace);	/* if targethash is past the end, use last */
		iterb = rbt_previous(pKE->kspace, iterf);		/* backward iterator */
		for(addednodes=0; addednodes < 20; ) {
			if(iterf == NULL && iterb == NULL)
				break;
			if(iterf != NULL) {
				peernode *ppn = rbt_value(iterf);
				peernode *newppn = malloc(sizeof(peernode));
				*newppn = *ppn;
				int128xor(newppn->hash, newppn->hash, targethash);	/* XORing to sort on dist from targethash... */
				rbt_insert(fb.find_input_rbt, newppn->hash, newppn, 0);/* add to find_input_rbt */
#ifdef VERBOSE_DEBUG
				KadC_log(">Priming with ");
				KadC_int128flog(stdout, ppn->hash);
				KadC_log(" %s:%d logd = %d\n", htoa(newppn->ip), newppn->port, int128log(newppn->hash));
#endif
				addednodes++;
				iterf = rbt_next(pKE->kspace, iterf);
			}
			if(iterb != NULL) {
				peernode *ppn = rbt_value(iterb);
				peernode *newppn = malloc(sizeof(peernode));
				*newppn = *ppn;
				int128xor(newppn->hash, newppn->hash, targethash);	/* XORing to sort on dist from targethash... */
				rbt_insert(fb.find_input_rbt, newppn->hash, newppn, 0);/* add to find_input_rbt */
#ifdef VERBOSE_DEBUG
				KadC_log("<Priming with ");
				KadC_int128flog(stdout, ppn->hash);
				KadC_log(" %s:%d logd = %d\n", htoa(newppn->ip), newppn->port, int128log(newppn->hash));
#endif
				addednodes++;
				iterb = rbt_previous(pKE->kspace, iterb);
			}
		}
		pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK CONTACTS ///// */
	}

#ifdef VERBOSE_DEBUG
	KadC_log("launching %d-thread Overnet_find_th\n", pfpar->threads);
#endif
	for(i=0; i < pfpar->threads; i++) {
		pthread_create(&th[i], NULL, &Overnet_find_th, &fb);
	}

	/* join() the spawned threads */
	for(i=0; i < pfpar->threads; i++) {
		pthread_join(th[i], NULL);		/* reap terminated threads */
#ifdef VERBOSE_DEBUG
		KadC_log("joined Overnet_find_th thread %lx\n", (unsigned long int)&th[i]);
#endif
	}
#ifdef VERBOSE_DEBUG
	KadC_log("All threads joined, could not get %d hits out of %d\n",
		fb.find_results_avail, pfpar->max_hits);
#endif
	/* fb.find_input_rbt must now be destroyed together with its content */

#ifdef VERBOSE_DEBUG
	KadC_log("- Going to destroy input set and its %d peers\n", rbt_size(fb.find_input_rbt));
#endif

	for(i = rbt_size(fb.find_input_rbt);; i--) {
		peernode *ppn;
		iter=rbt_begin(fb.find_input_rbt);
		if(iter == NULL)
			break;
		if(i <= 0) {
#ifdef DEBUG
			KadC_log("File %s, line %d: Uh-oh, iter != NULL with %d elements in the rbt?!?",
					__FILE__, __LINE__, i);
#endif
			break;
		}
		ppn = rbt_value(iter);
		rbt_erase(fb.find_input_rbt, iter);
		free(ppn);
	}
	rbt_status = rbt_destroy(fb.find_input_rbt);
	assert(rbt_status == RBT_STATUS_OK); /* should be empty now... */

	if(! dosearch) {
#ifdef VERBOSE_DEBUG
		KadC_log("- Going to destroy empty search set\n");
#endif
		kstore_destroy(fb.ks, 0);
	} else {
		/* Now this part is done directly in above threads */
#if 0
		
		/* again in parallel, remove entries from results and establish
		   OVERNET_SEARCH_INFO sessions with them */
#ifdef DEBUG
		KadC_log("got %d results from node lookup\n", rbt_size(fb.find_results_rbt));
		KadC_log("now launching %d-thread OvernetSearchInfoSession_th\n", pfpar->threads);
#endif
		for(i=0; i < pfpar->threads; i++) {
			pthread_create(&th[i], NULL, &OvernetSearchInfoSession_th, &fb);
		}
		/* join() the spawned threads */
		for(i=0; i < pfpar->threads; i++) {
			pthread_join(th[i], NULL);		/* reap terminated threads */
#ifdef VERBOSE_DEBUG
			KadC_log("joined OvernetSearchInfoSession_th thread %lx\n", (unsigned long int)&th[i]);
#endif
		}
#ifdef VERBOSE_DEBUG
		KadC_log("All OvernetSearchInfoSession_th threads joined\n");
#endif
#endif /* 0 */
	}

	if(dosearch || int128eq(targethash, pKE->localnode.hash)) {
		/* destroy results rbt (if necessary after extracting all
		   the remaining content), as it's not needed */
		int j;
#ifdef VERBOSE_DEBUG
		KadC_log("- Going to destroy results set and its %d peers\n", rbt_size(fb.find_results_rbt));
#endif
		for(i = rbt_size(fb.find_results_rbt), j = 0;; i--, j++) {
			peernode *ppn;
			iter=rbt_begin(fb.find_results_rbt);
			if(iter == NULL)
				break;
			if(i <= 0) {
#ifdef DEBUG
				KadC_log("File %s, line %d: Uh-oh, iter != NULL with %d elements in the rbt?!?",
						__FILE__, __LINE__, i);
#endif
				break;
			}
			ppn = rbt_value(iter);
			rbt_erase(fb.find_results_rbt, iter);

			free(ppn);
		}
		rbt_status = rbt_destroy(fb.find_results_rbt);
		assert(rbt_status == RBT_STATUS_OK); /* should be empty now... */
	}

	pthread_mutex_destroy(&fb.fimutex);
	pthread_mutex_destroy(&fb.frmutex);
	pthread_mutex_destroy(&fb.fsmutex);

	if(dosearch) {
#ifdef DEBUG
		KadC_log("- Returning kstore with %d k-objects as result\n", rbt_size(fb.ks->rbt));
#endif

		return (void *)fb.ks;	/* kstore of k-objects */
	} else if(int128eq(targethash, pKE->localnode.hash)) {
#ifdef VERBOSE_DEBUG
		KadC_log("- Returning NULL\n");
#endif
		return NULL;	/* self lookup, no need to return anything */
	} else {
#ifdef DEBUG
		KadC_log("Returning %d peers as result\n", rbt_size(fb.find_results_rbt));
#endif
		return fb.find_results_rbt; /* rbt of peers, with hashes XOR'd with targethash */
	}
}

/* Each of the threads running the following code tries to get a peernode from
   pfb->find_input_rbt; if it finds one NOT also in pfb->find_results_rbt, it
   puts it provisionally there; then it
   sends to it a OVERNET_SEARCH and waits up to 1 s for a OVERNET_SEARCH_NEXT
   reply; if it doesn't get it, it removes the peernode from pfb->find_results_rbt
   and tries another one; if it does, it scans the peernodes suggested in the
   OVERNET_SEARCH_NEXT and, if they qualify, puts them into pfb->find_input_rbt
   (optionally, only if they are closer to the target than the peernode that
   recommends them). NOTE: this code does NOT remove peernodes from pfb->find_input_rbt,
   it only copies the responsive ones to pfb->find_results_rbt, and adds
   the recommended nodes to pfb->find_input_rbt . */

static void *Overnet_find_th(void *p) {
	findblk *pfb = p;
	peernode *ppn;
	rbt_StatusEnum rbt_status;
	int morehitsneeded = 1;
	/* while(not time_over and not shutdown)  */
	while(time(NULL) < *(pfb->pstoptime) - 2  && /* reserve 2 s for OVERNET_SEARCH_RESULT */
			!pfb->pKE->shutdown &&
			morehitsneeded && !pfb->kstore_full) {
		void *iter;
		unsigned char parameter;
		SessionObject *psession;
		packet *pkt;
		unsigned char *buf;
		int nrecvd;
		unsigned char *pb;
		int npeers;
		int i;
		int recommends_itself;
		peernode pnunx;
		int is_not_overnet_search_next;

		pthread_mutex_lock(&pfb->fimutex);			/* \\\\\\ LOCK FI \\\\\\ */
#ifdef VERBOSE_DEBUG
		KadC_log("pfb->nworking_on_fi: %d backlog: %d\n", pfb->nworking_on_fi, rbt_size(pfb->find_input_rbt));
#endif
		iter = rbt_begin(pfb->find_input_rbt);
		if(iter == NULL) {		/* no more input nodes... */
#ifdef OVERNET_FIND_TH_TERMINATES_WHEN_INPUT_RBT_EMPTY
			pthread_mutex_unlock(&pfb->fimutex);	/* ///// UNLOCK FI ///// */
			break;	/*  return  */
#else	/* stay alive a little, just in case more input was produced by other threads */
			pfb->nworking_on_fi--;
			pthread_mutex_unlock(&pfb->fimutex);	/* ///// UNLOCK FI ///// */
			millisleep(100);	/* idle a little but stick around... */
			if(pfb->nworking_on_fi <= 0) {
				break;	/*  return  */
			}
			pthread_mutex_lock(&pfb->fimutex);		/* \\\\\ LOCK FI \\\\\ */
			pfb->nworking_on_fi++;
			pthread_mutex_unlock(&pfb->fimutex);	/* ///// UNLOCK FI ///// */
			continue;			/* see if there is work now */
#endif
		}
		ppn = rbt_value(iter);
		rbt_status = rbt_erase(pfb->find_input_rbt, iter);	/* remove node from input rbt */
		assert(rbt_status == RBT_STATUS_OK);	/* it was there, wasn't it? */
		pthread_mutex_unlock(&pfb->fimutex);		/* ///// UNLOCK FI ///// */

		if((ppn->ip == pfb->pKE->extip && ppn->port == pfb->pKE->localnode.port) ||
			isnonroutable(ppn->ip)) {
#ifdef VERBOSE_DEBUG
			KadC_log("ignoring peer %s:%u - our own or nonroutable address\n",
						htoa(ppn->ip), ppn->port);
#endif
			free(ppn);	/* ignore peers with IP/port equal to us or nonroutable */
			continue;	/* try next node */
		}

		/* ppn is now in find_results_rbt, so we can't unxor it.
		   let's use its local unxored copy pnunx */
		pnunx = *ppn; /* memcpy(&pnunx, ppn, sizeof(peernode)); */
		int128xor(pnunx.hash, pnunx.hash, pfb->targethash); /* save for future use */

		/* see if ppn is in results rbt; if yes, exit, if not, put it provisionally there */
		pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
		rbt_status = rbt_insert(pfb->find_results_rbt, ppn->hash, ppn, 0);
		pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */
		if(rbt_status != RBT_STATUS_OK) {
#ifdef VERBOSE_DEBUG
			KadC_log("rbt_insert() returned %d: ignoring peer %s:%u already queried successfully\n",
						rbt_status, htoa(ppn->ip), ppn->port);
#endif
			free(ppn);	/* ignore peers already queried successfully */
			continue;	/* try next node */
		}

		if(pfb->dosearch)
			parameter = 2;	/* search? */
		else if(int128eq(pfb->targethash, pfb->pKE->localnode.hash))
			parameter = 20;	/* node lookup for self */
		else
			parameter = 4;	/* node lookup to publish */

		psession = sendOvernetSearch(pfb->pKE, ppn->ip, ppn->port, parameter, pfb->targethash);
		/* get OVERNET_SEARCH_NEXT from peer; if it replied,  */

		if(psession == NULL) { /* Session allocation sometimes fails. FIXME: find out why */
#ifdef VERBOSE_DEBUG
			KadC_log("Can't create session for OVERNET_SEARCH to peer %s:%u\n", htoa(ppn->ip), ppn->port);
#endif
			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_status = rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			assert(rbt_status == RBT_STATUS_OK);
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

			free(ppn);	/* ignore unresponsive peers */
			continue;	/* try next node */
		}

		pkt = getpktt(psession, 2000);	/* 1 s timeout */

		destroySessionObject(psession);

		if(pkt == NULL) { 	/* if it did not answer */
#ifdef VERBOSE_DEBUG
			pthread_mutex_lock(&pfb->fimutex);		/* \\\\\ LOCK FI \\\\\ */
			KadC_log("timeout on OVERNET_SEARCH_NEXT from ");
			KadC_int128flog(stdout, pnunx.hash);
			KadC_log(" %s:%u.%d\n",
				htoa(ppn->ip), ppn->port, ppn->type);
			pthread_mutex_unlock(&pfb->fimutex);	/* ///// UNLOCK FI ///// */
#endif
			UpdateNodeStatus(&pnunx, pfb->pKE, 0);
			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_status = rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			assert(rbt_status == RBT_STATUS_OK);
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

			free(ppn);	/* ignore unresponsive peers */
			continue;	/* try next node */
		}
		UpdateNodeStatus(&pnunx, pfb->pKE, 1);
		buf = pkt->buf;
		nrecvd = pkt->len;

		is_not_overnet_search_next = 1;	/* by default */
		if( nrecvd >= (2+16+1) && buf[1] == OVERNET_SEARCH_NEXT) {

			if(memcmp(&buf[2], pfb->targethash, 16) == 0) {
				is_not_overnet_search_next = 0;
			} else if(memcmp(&buf[3], pfb->targethash, 15) == 0) {
#ifdef VERBOSE_DEBUG
				KadC_log("Trying to fix broken OVERNET_SEARCH_NEXT format sent by %s:%u.%d\n",
					htoa(ppn->ip), ppn->port, ppn->type);
#endif
				memmove(&buf[2], pfb->targethash, 16);
				is_not_overnet_search_next = 0;
			}
		}

		if(is_not_overnet_search_next) {

#ifdef DEBUG
			KadC_log("not a OVERNET_SEARCH_NEXT in response to an OVERNET_SEARCH!\n");
			KadC_log("nrecvd = %d, buf[1] = %02x hash: ", nrecvd, buf[1]);
			KadC_int128flog(stdout, &buf[2]);
			KadC_log("\nSent by: ");
			KadC_int128flog(stdout, pnunx.hash);
			KadC_log(" %s:%u.%d\n",
				htoa(ppn->ip), ppn->port, ppn->type);
			{
				int i;
				UDPIO *pul = pfb->pKE->pul;
				pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK UDPIO \\\\\\ */
				for(i=0; i < nrecvd && i < 48 ; i++) {
					if((i % 16) == 0)
						KadC_log("\n");
					KadC_log("%02x ", buf[i]);
				}
				KadC_log("\n================================\n");
				pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK UDPIO ///// */
			}

#endif
			destroypkt(pkt);

			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_status = rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			assert(rbt_status == RBT_STATUS_OK);
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

			free(ppn);	/* ignore peers sending back weird replies */
			continue;	/* try next node */
		}

		pb = &buf[2+16];	/* point to peer list count */
		npeers = *pb++;
		if(nrecvd != (2+16+1) + npeers * 23) { /* packet must contain npeers 23-byte peers */
#ifdef DEBUG
			KadC_log("malformed packet!\n");
#endif
			destroypkt(pkt);

			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_status = rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			assert(rbt_status == RBT_STATUS_OK);
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

			free(ppn);	/* ignore peers sending back weird replies */
			continue;	/* try next node */
		}
		/* OK, ppn has replied correctly, so we do have one more result */

		pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
		morehitsneeded = (--(pfb->find_results_avail) > 0);
		pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

		/* scan the returned peers */
		for(i=0, recommends_itself = 0; i<npeers; i++) {
			peernode *ppnr;
			ppnr = (peernode *)malloc(sizeof(peernode));
			assert(ppnr != 0);	/* or else, no memory! */
			getpeernode(ppnr, &pb);
#ifdef VERBOSE_DEBUG
			/* out of curiosity, see if this new candidate has lower distance from target than pnunx */
			KadC_log("ppnr %s ", htoa(ppnr->ip));
			KadC_log("proposed by %s has logdistance %d (ppn has %d)\n",
						htoa(pnunx.ip),
						int128xorlog(ppnr->hash, pfb->targethash),
						int128xorlog(pnunx.hash, pfb->targethash)
					);
#endif

			if(pnunx.ip == ppnr->ip && pnunx.port == ppnr->port) {	/* if peer references itself... */
				recommends_itself = 1;
#ifdef VERBOSE_DEBUG
				KadC_log("######### peer %s:%u recommends itself, with logdistance = %d\n",
							htoa(pnunx.ip), pnunx.port, int128xorlog(pnunx.hash, pfb->targethash) );
#endif
			}

			int128xor(ppnr->hash, ppnr->hash, pfb->targethash);	/* XORing... */
			/* put returned peer into find_input_rbt */
			pthread_mutex_lock(&pfb->fimutex);			/* \\\\\\ LOCK FI \\\\\\ */
#ifdef OPTIMIZE_BY_RECURSING_ONLY_ON_PEERS_BETTER_THAN_CURRENT_BEST
			iter = rbt_begin(pfb->find_input_rbt);
			rbt_status = RBT_STATUS_DUPLICATE_KEY;	/* by default, so that insert skipped == insert failed */
			if(iter == NULL || int128lt(ppnr->hash, ((peernode *)rbt_value(iter))->hash)) /* if rbt empty, or new better than rbt's best */
#endif
			/* Might be better to catch here already cases that are already in
			   results_input_rbt, as its bound to happen many different
			   peers will report same nodes. Also, inserting a node
			   that recommends itself is unnecessary.
			 */
				rbt_status = rbt_insert(pfb->find_input_rbt, ppnr->hash, ppnr, 0);
			pthread_mutex_unlock(&pfb->fimutex);		/* ///// UNLOCK FI ///// */
			if(rbt_status != RBT_STATUS_OK)
				free(ppnr);	/* if already there, throw away */
		} /* end for */

		destroypkt(pkt);
		if(pfb->dosearch /* && recommends_itself */) {
#ifdef VERBOSE_DEBUG
			KadC_log("Launching OvernetSearchInfoSession\n");
#endif			
			OvernetSearchInfoSession(pfb, ppn);
#if 0
			/* Remove it from find_results_rbt to ensure the same node
			   won't be searched twice 
			    - disabled, as if we'd remove it here then nodes
			      that have been checked already can not be detected.
			      Maybe add yet one more rbt for cases that have
			      been done OvernetSearchInfoSession? */
			 
#ifdef VERBOSE_DEBUG
			KadC_log("Removing from find_results_rbt\n");
#endif			
			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */
#endif /* 0 */
			
		}
		
	}
	return NULL;
}

#else /* OLD_SEARCH_ONLY */

static void *OvernetSearchInfoSession_th(void *p);

typedef struct _findblk {
	int dosearch;
	int128 targethash;
	unsigned char *psfilter;
	int sfilterlen;
	KadEngine *pKE;
	void *find_input_rbt;
	pthread_mutex_t fimutex;
	void *find_results_rbt;
	pthread_mutex_t frmutex;
	int find_results_avail;
	time_t *pstoptime;
	kstore *ks;
	unsigned short int resn1;
	unsigned short int resn2;
	pthread_mutex_t fsmutex;
	unsigned char kstore_full;
	int nworking_on_fi;
} findblk;

/* Send to ppn OVERNET_SEARCH_INFO
   Get all replies  OVERNET_SEARCH_RESULT until OVERNET_SEARCH_END or timeout
   Put returned peers wrapped as kobjects into kstore pfb->ks
   decrememnt pfb->find_results_avail */

static void OvernetSearchInfoSession(findblk *pfb, peernode *ppn) {
	SessionObject *psession;
	peernode pnunx;

#if 0	/* DISABLED FOR NOW */
	if(ppn->type > NONRESPONSE_THRESHOLD) {
#ifdef VERBOSE_DEBUG
		KadC_log("not sending OVERNET_SEARCH_INFO to sleepy peer %s:%u type: %d\n", htoa(ppn->ip), ppn->port, ppn->type);
#endif
		return;
	}
#endif

	pnunx = *ppn;
	int128xor(pnunx.hash, pnunx.hash, pfb->targethash); /* save for future use */

	psession = sendOvernetSearchInfo(pfb->pKE, ppn->ip, ppn->port, pfb->targethash, pfb->psfilter, pfb->sfilterlen, 0, 100);

	if(psession == NULL) { /* Session allocation sometimes fails. FIXME: find out why */
#ifdef VERBOSE_DEBUG
			KadC_log("Client session cannot be created to send an OVERNET_SEARCH_INFO\n");
#endif
	} else {
		int endarrived = 0;
	/* get OVERNET_SEARCH_RESULT list and OVERNET_SEARCH_END from peer  */
		while(!endarrived &&
				time(NULL) < *(pfb->pstoptime) + 2 &&  /* allow extra 2 s after stoptime */
				!pfb->kstore_full) {
			packet *pkt = getpktt(psession, 2000);	/* 2 s timeout */
			if(pkt == NULL) {
#ifdef VERBOSE_DEBUG
				KadC_log("timeout while waiting for OVERNET_SEARCH_RESULT or OVERNET_SEARCH_END from %s:%u\n",
							htoa(ppn->ip), ppn->port);
#endif
				UpdateNodeStatus(&pnunx, pfb->pKE, 0);
				break;
			} else {
				unsigned char *buf = pkt->buf;
				int nrecvd = pkt->len;
				unsigned char *psf = pfb->psfilter;

				if(nrecvd >= 22) { /* the min is for OVERNET_SEARCH_END */
					/* UpdateNodeStatus(&pnunx, pfb->pKE, 1); / * USELESS: we just did it when we got OVERNET_SEARCH_NEXT  */
					if(buf[1] == OVERNET_SEARCH_RESULT) {
						kobject *pko;
						int kstatus;

#ifdef VERBOSE_DEBUG
						KadC_log("Results from %s:%u\n", htoa(pnunx.ip), pnunx.port);
#endif
						pko = kobject_new(&buf[2], nrecvd-2);

						/* pass anyway through filter (some clients out there are defective) */

						if(psf != NULL && s_filter(pko, &psf, psf+pfb->sfilterlen) != 1) {
#ifdef VERBOSE_DEBUG
							psf = pfb->psfilter;
							KadC_log("kobject ");
							kobject_dump(pko, ";");
							KadC_log(" from %s:%u does not pass the s-filter ", htoa(pnunx.ip), pnunx.port);
							s_filter_dump(&psf, psf+pfb->sfilterlen);
							KadC_log("\n");
#elif DEBUG
							KadC_log("X");	/* post-filtering blocked record escaped from faulty server filter on peer's side */
#endif
							kobject_destroy(pko);
						} else {

							/* Accumulate K-Objects in K-store pfb->ks
							   We already know that the first hash is the same for all,
							   so we index by second hash. This enforces the principle of
							   cosidering as duplicates k-objects with BOTH hashes equal. */
							pthread_mutex_lock(&pfb->fsmutex);			/* \\\\\\ LOCK FS \\\\\\ */
							kstatus = kstore_insert(pfb->ks, pko, 1, 0); /* index w/ second hash, reject duplicates */
							if(pfb->ks->avail <= 0)
								pfb->kstore_full = 1;
							pthread_mutex_unlock(&pfb->fsmutex);		/* ///// UNLOCK FS ///// */
							if(kstatus != 0) {
								kobject_destroy(pko); /* because insert failed */
#ifdef VERBOSE_DEBUG
								KadC_log("kstore_insert returned status %d for results returned by peer %s:%u\n", kstatus,  htoa(pnunx.ip), pnunx.port);
#elif DEBUG
								if(kstatus == 1) {
									KadC_log(".");	/* store full, stop! (geddit?) */
								} else {
									KadC_log("D");	/* rbt error, most likely duplicate key */
								}
#endif

							} else {
#ifdef VERBOSE_DEBUG
								KadC_log("Results from peer %s:%u inserted in k-store\n", htoa(pnunx.ip), pnunx.port);
#elif DEBUG
								KadC_log("!");	/* inserted! */
#endif
							}
						}
					} else if (buf[1] == OVERNET_SEARCH_END) {
#ifdef VERBOSE_DEBUG
						unsigned char *pb = buf+18;
						KadC_log("OVERNET_SEARCH_END received from peer %s:%u (%u, %u)\n",
									htoa(pnunx.ip), pnunx.port, getushortle(&pb), getushortle(&pb));
#endif
						/* #### FIXME: do something (what??) with the two ushorts */
						endarrived = 1;
					} else {
#ifdef VERBOSE_DEBUG
						KadC_log("Unexpected packet with opcode 0x%02x from peer %s:%u \n",
								buf[1], htoa(pnunx.ip), pnunx.port);
#endif
					}
				} else {
#ifdef VERBOSE_DEBUG
					KadC_log("peer %s:%u sent a reply too short (%d bytes)\n",
							htoa(pnunx.ip), pnunx.port, nrecvd);
#endif
				}
				destroypkt(pkt);
			}

		}
		destroySessionObject(psession);
	}

}


void *Overnet_find(KadEngine *pKE, int128 targethash, int dosearch, unsigned char *psfilter, int sfilterlen, time_t *pstoptime, int max_hits, int nthreads) {
	int i;
	int addednodes;
	void *iter;
	rbt_StatusEnum rbt_status;
	findblk fb;	/* block shared between main thread and children threads */
	pthread_t th[20];
	int logd_target_local = int128xorlog(pKE->localnode.hash, targethash);
	if(logd_target_local < 0)
		logd_target_local = 0;	/* looking up our own node... */

	/* regardless of what nthreads says, never use more than arraysize(th) threads for this */
	if(nthreads > arraysize(th))
		nthreads = arraysize(th);

	fb.dosearch = dosearch;
	fb.targethash = targethash;
	fb.psfilter = psfilter;
	fb.sfilterlen = sfilterlen;
	fb.pKE = pKE;
	fb.find_input_rbt = rbt_new((rbtcomp *)int128lt, (rbtcomp *)int128eq);
	fb.fimutex = mutex_initializer;
	fb.find_results_rbt = rbt_new((rbtcomp *)int128lt, (rbtcomp *)int128eq);
	fb.frmutex = mutex_initializer;
	fb.fsmutex = mutex_initializer;
	if(dosearch)
		fb.find_results_avail = max_hits * nthreads;
	else
		fb.find_results_avail = max_hits;
	if(fb.find_results_avail < 10)
		fb.find_results_avail = 10;	/* at least lookup to gather 10 hits */
#ifdef VERBOSE_DEBUG
	KadC_log("Will lookup not more than %d nodes\n", fb.find_results_avail);
#endif
	fb.pstoptime = pstoptime;
#ifdef VERBOSE_DEBUG
	KadC_log("Creating a kstore with room for %d objects\n", max_hits);
#endif
	fb.ks = kstore_new(max_hits);
	fb.kstore_full = 0;
	fb.nworking_on_fi = nthreads;

	/* prime the find_input_rbt with contact nodes starting from the closest
	   to the targer, and getting as many peernodes as possible
	   from adjacent nodes above and below until 20 are obtained,
	   or there aren't any more left */

	/* We prime find_input_rbt using the closest nodes in contacts table */
	{
		void *iterf, *iterb;

		pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK CONTACTS \\\\\\ */
		rbt_find(pKE->kspace, targethash, &iterf);	/* forward iterator */
		if(iterf == NULL)
			iterf = rbt_end(pKE->kspace);	/* if targethash is past the end, use last */
		iterb = rbt_previous(pKE->kspace, iterf);		/* backward iterator */
		for(addednodes=0; addednodes < 20; ) {
			if(iterf == NULL && iterb == NULL)
				break;
			if(iterf != NULL) {
				peernode *ppn = rbt_value(iterf);
				peernode *newppn = malloc(sizeof(peernode));
				*newppn = *ppn;
				int128xor(newppn->hash, newppn->hash, targethash);	/* XORing to sort on dist from targethash... */
				rbt_insert(fb.find_input_rbt, newppn->hash, newppn, 0);/* add to find_input_rbt */
#ifdef VERBOSE_DEBUG
				KadC_log(">Priming with ");
				KadC_int128flog(stdout, ppn->hash);
				KadC_log(" %s:%d logd = %d\n", htoa(newppn->ip), newppn->port, int128log(newppn->hash));
#endif
				addednodes++;
				iterf = rbt_next(pKE->kspace, iterf);
			}
			if(iterb != NULL) {
				peernode *ppn = rbt_value(iterb);
				peernode *newppn = malloc(sizeof(peernode));
				*newppn = *ppn;
				int128xor(newppn->hash, newppn->hash, targethash);	/* XORing to sort on dist from targethash... */
				rbt_insert(fb.find_input_rbt, newppn->hash, newppn, 0);/* add to find_input_rbt */
#ifdef VERBOSE_DEBUG
				KadC_log("<Priming with ");
				KadC_int128flog(stdout, ppn->hash);
				KadC_log(" %s:%d logd = %d\n", htoa(newppn->ip), newppn->port, int128log(newppn->hash));
#endif
				addednodes++;
				iterb = rbt_previous(pKE->kspace, iterb);
			}
		}
		pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK CONTACTS ///// */
	}

#ifdef VERBOSE_DEBUG
	KadC_log("launching %d-thread Overnet_find_th\n", nthreads);
#endif
	for(i=0; i < nthreads; i++) {
		pthread_create(&th[i], NULL, &Overnet_find_th, &fb);
	}

	/* join() the spawned threads */
	for(i=0; i < nthreads; i++) {
		pthread_join(th[i], NULL);		/* reap terminated threads */
#ifdef VERBOSE_DEBUG
		KadC_log("joined Overnet_find_th thread %lx\n", (unsigned long int)&th[i]);
#endif
	}
#ifdef VERBOSE_DEBUG
	KadC_log("All threads joined, could not get %d hits out of %d\n",
		fb.find_results_avail, max_hits);
#endif
	/* fb.find_input_rbt must now be destroyed together with its content */

#ifdef VERBOSE_DEBUG
	KadC_log("- Going to destroy input set and its %d peers\n", rbt_size(fb.find_input_rbt));
#endif

	for(i = rbt_size(fb.find_input_rbt);; i--) {
		peernode *ppn;
		iter=rbt_begin(fb.find_input_rbt);
		if(iter == NULL)
			break;
		if(i <= 0) {
#ifdef DEBUG
			KadC_log("File %s, line %d: Uh-oh, iter != NULL with %d elements in the rbt?!?",
					__FILE__, __LINE__, i);
#endif
			break;
		}
		ppn = rbt_value(iter);
		rbt_erase(fb.find_input_rbt, iter);
		free(ppn);
	}
	rbt_status = rbt_destroy(fb.find_input_rbt);
	assert(rbt_status == RBT_STATUS_OK); /* should be empty now... */

	if(! dosearch) {
#ifdef VERBOSE_DEBUG
		KadC_log("- Going to destroy empty search set\n");
#endif
		kstore_destroy(fb.ks, 0);
	} else {
		/* again in parallel, remove entries from results and establish
		   OVERNET_SEARCH_INFO sessions with them */
#ifdef DEBUG
		KadC_log("got %d results from node lookup\n", rbt_size(fb.find_results_rbt));
		KadC_log("now launching %d-thread OvernetSearchInfoSession_th\n", nthreads);
#endif
		for(i=0; i < nthreads; i++) {
			pthread_create(&th[i], NULL, &OvernetSearchInfoSession_th, &fb);
		}
		/* join() the spawned threads */
		for(i=0; i < nthreads; i++) {
			pthread_join(th[i], NULL);		/* reap terminated threads */
#ifdef VERBOSE_DEBUG
			KadC_log("joined OvernetSearchInfoSession_th thread %lx\n", (unsigned long int)&th[i]);
#endif
		}
#ifdef VERBOSE_DEBUG
		KadC_log("All OvernetSearchInfoSession_th threads joined\n");
#endif
	}

	if(dosearch || int128eq(targethash, pKE->localnode.hash)) {
		/* destroy results rbt (if necessary after extracting all
		   the remaining content), as it's not needed */
		int j;
#ifdef VERBOSE_DEBUG
		KadC_log("- Going to destroy results set and its %d peers\n", rbt_size(fb.find_results_rbt));
#endif
		for(i = rbt_size(fb.find_results_rbt), j = 0;; i--, j++) {
			peernode *ppn;
			iter=rbt_begin(fb.find_results_rbt);
			if(iter == NULL)
				break;
			if(i <= 0) {
#ifdef DEBUG
				KadC_log("File %s, line %d: Uh-oh, iter != NULL with %d elements in the rbt?!?",
						__FILE__, __LINE__, i);
#endif
				break;
			}
			ppn = rbt_value(iter);
			rbt_erase(fb.find_results_rbt, iter);

			free(ppn);
		}
		rbt_status = rbt_destroy(fb.find_results_rbt);
		assert(rbt_status == RBT_STATUS_OK); /* should be empty now... */
	}

	pthread_mutex_destroy(&fb.fimutex);
	pthread_mutex_destroy(&fb.frmutex);
	pthread_mutex_destroy(&fb.fsmutex);

	if(dosearch) {
#ifdef DEBUG
		KadC_log("- Returning kstore with %d k-objects as result\n", rbt_size(fb.ks->rbt));
#endif

		return (void *)fb.ks;	/* kstore of k-objects */
	} else if(int128eq(targethash, pKE->localnode.hash)) {
#ifdef VERBOSE_DEBUG
		KadC_log("- Returning NULL\n");
#endif
		return NULL;	/* self lookup, no need to return anything */
	} else {
#ifdef DEBUG
		KadC_log("Returning %d peers as result\n", rbt_size(fb.find_results_rbt));
#endif
		return fb.find_results_rbt; /* rbt of peers, with hashes XOR'd with targethash */
	}
}

/* Each of the threads running the following code tries to get a peernode from
   pfb->find_input_rbt; if it finds one NOT also in pfb->find_results_rbt, it
   puts it provisionally there; then it
   sends to it a OVERNET_SEARCH and waits up to 1 s for a OVERNET_SEARCH_NEXT
   reply; if it doesn't get it, it removes the peernode from pfb->find_results_rbt
   and tries another one; if it does, it scans the peernodes suggested in the
   OVERNET_SEARCH_NEXT and, if they qualify, puts them into pfb->find_input_rbt
   (optionally, only if they are closer to the target than the peernode that
   recommends them). NOTE: this code does NOT remove peernodes from pfb->find_input_rbt,
   it only copies the responsive ones to pfb->find_results_rbt, and adds
   the recommended nodes to pfb->find_input_rbt . */

static void *Overnet_find_th(void *p) {
	findblk *pfb = p;
	peernode *ppn;
	rbt_StatusEnum rbt_status;
	int morehitsneeded = 1;
	/* while(not time_over and not shutdown)  */
	while(time(NULL) < *(pfb->pstoptime) - 2  && /* reserve 2 s for OVERNET_SEARCH_RESULT */
			!pfb->pKE->shutdown &&
			morehitsneeded && !pfb->kstore_full) {
		void *iter;
		unsigned char parameter;
		SessionObject *psession;
		packet *pkt;
		unsigned char *buf;
		int nrecvd;
		unsigned char *pb;
		int npeers;
		int i;
		int recommends_itself;
		peernode pnunx;
		int is_not_overnet_search_next;

		pthread_mutex_lock(&pfb->fimutex);			/* \\\\\\ LOCK FI \\\\\\ */
#ifdef VERBOSE_DEBUG
		KadC_log("pfb->nworking_on_fi: %d backlog: %d\n", pfb->nworking_on_fi, rbt_size(pfb->find_input_rbt));
#endif
		iter = rbt_begin(pfb->find_input_rbt);
		if(iter == NULL) {		/* no more input nodes... */
#ifdef OVERNET_FIND_TH_TERMINATES_WHEN_INPUT_RBT_EMPTY
			pthread_mutex_unlock(&pfb->fimutex);	/* ///// UNLOCK FI ///// */
			break;	/*  return  */
#else	/* stay alive a little, just in case more input was produced by other threads */
			pfb->nworking_on_fi--;
			pthread_mutex_unlock(&pfb->fimutex);	/* ///// UNLOCK FI ///// */
			millisleep(100);	/* idle a little but stick around... */
			if(pfb->nworking_on_fi <= 0) {
				break;	/*  return  */
			}
			pthread_mutex_lock(&pfb->fimutex);		/* \\\\\ LOCK FI \\\\\ */
			pfb->nworking_on_fi++;
			pthread_mutex_unlock(&pfb->fimutex);	/* ///// UNLOCK FI ///// */
			continue;			/* see if there is work now */
#endif
		}
		ppn = rbt_value(iter);
		rbt_status = rbt_erase(pfb->find_input_rbt, iter);	/* remove node from input rbt */
		assert(rbt_status == RBT_STATUS_OK);	/* it was there, wasn't it? */
		pthread_mutex_unlock(&pfb->fimutex);		/* ///// UNLOCK FI ///// */

		if((ppn->ip == pfb->pKE->extip && ppn->port == pfb->pKE->localnode.port) ||
			isnonroutable(ppn->ip)) {
#ifdef VERBOSE_DEBUG
			KadC_log("ignoring peer %s:%u - our own or nonroutable address\n",
						htoa(ppn->ip), ppn->port);
#endif
			free(ppn);	/* ignore peers with IP/port equal to us or nonroutable */
			continue;	/* try next node */
		}

		pnunx = *ppn; /* memcpy(&pnunx, ppn, sizeof(peernode)); */
		int128xor(pnunx.hash, pnunx.hash, pfb->targethash); /* save for future use */

		/* see if ppn is in results rbt; if yes, exit, if not, put it provisionally there */
		pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
		rbt_status = rbt_insert(pfb->find_results_rbt, ppn->hash, ppn, 0);
		pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */
		if(rbt_status != RBT_STATUS_OK) {
#ifdef VERBOSE_DEBUG
			KadC_log("rbt_insert() returned %d: ignoring peer %s:%u already queried successfully\n",
						rbt_status, htoa(ppn->ip), ppn->port);
#endif
			free(ppn);	/* ignore peers already queried successfully */
			continue;	/* try next node */
		}

		if(pfb->dosearch)
			parameter = 2;	/* search? */
		else if(int128eq(pfb->targethash, pfb->pKE->localnode.hash))
			parameter = 20;	/* node lookup for self */
		else
			parameter = 4;	/* node lookup to publish */

		psession = sendOvernetSearch(pfb->pKE, ppn->ip, ppn->port, parameter, pfb->targethash);
		/* get OVERNET_SEARCH_NEXT from peer; if it replied,  */

		if(psession == NULL) { /* Session allocation sometimes fails. FIXME: find out why */
#ifdef VERBOSE_DEBUG
			KadC_log("Can't create session for OVERNET_SEARCH to peer %s:%u\n", htoa(ppn->ip), ppn->port);
#endif
			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_status = rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			assert(rbt_status == RBT_STATUS_OK);
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

			free(ppn);	/* ignore unresponsive peers */
			continue;	/* try next node */
		}

		pkt = getpktt(psession, 2000);	/* 1 s timeout */

		destroySessionObject(psession);

		if(pkt == NULL) { 	/* if it did not answer */
#ifdef VERBOSE_DEBUG
			pthread_mutex_lock(&pfb->fimutex);		/* \\\\\ LOCK FI \\\\\ */
			KadC_log("timeout on OVERNET_SEARCH_NEXT from ");
			KadC_int128flog(stdout, pnunx.hash);
			KadC_log(" %s:%u.%d\n",
				htoa(ppn->ip), ppn->port, ppn->type);
			pthread_mutex_unlock(&pfb->fimutex);	/* ///// UNLOCK FI ///// */
#endif
			UpdateNodeStatus(&pnunx, pfb->pKE, 0);
			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_status = rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			assert(rbt_status == RBT_STATUS_OK);
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

			free(ppn);	/* ignore unresponsive peers */
			continue;	/* try next node */
		}
		UpdateNodeStatus(&pnunx, pfb->pKE, 1);
		buf = pkt->buf;
		nrecvd = pkt->len;

		is_not_overnet_search_next = 1;	/* by default */
		if( nrecvd >= (2+16+1) && buf[1] == OVERNET_SEARCH_NEXT) {

			if(memcmp(&buf[2], pfb->targethash, 16) == 0) {
				is_not_overnet_search_next = 0;
			} else if(memcmp(&buf[3], pfb->targethash, 15) == 0) {
#ifdef VERBOSE_DEBUG
				KadC_log("Trying to fix broken OVERNET_SEARCH_NEXT format sent by %s:%u.%d\n",
					htoa(ppn->ip), ppn->port, ppn->type);
#endif
				memmove(&buf[2], pfb->targethash, 16);
				is_not_overnet_search_next = 0;
			}
		}

		if(is_not_overnet_search_next) {

#ifdef DEBUG
			KadC_log("not a OVERNET_SEARCH_NEXT in response to an OVERNET_SEARCH!\n");
			KadC_log("nrecvd = %d, buf[1] = %02x hash: ", nrecvd, buf[1]);
			KadC_int128flog(stdout, &buf[2]);
			KadC_log("\nSent by: ");
			KadC_int128flog(stdout, pnunx.hash);
			KadC_log(" %s:%u.%d\n",
				htoa(ppn->ip), ppn->port, ppn->type);
			{
				int i;
				UDPIO *pul = pfb->pKE->pul;
				pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK UDPIO \\\\\\ */
				for(i=0; i < nrecvd && i < 48 ; i++) {
					if((i % 16) == 0)
						KadC_log("\n");
					KadC_log("%02x ", buf[i]);
				}
				KadC_log("\n================================\n");
				pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK UDPIO ///// */
			}

#endif
			destroypkt(pkt);

			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_status = rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			assert(rbt_status == RBT_STATUS_OK);
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

			free(ppn);	/* ignore peers sending back weird replies */
			continue;	/* try next node */
		}

		pb = &buf[2+16];	/* point to peer list count */
		npeers = *pb++;
		if(nrecvd != (2+16+1) + npeers * 23) { /* packet must contain npeers 23-byte peers */
#ifdef DEBUG
			KadC_log("malformed packet!\n");
#endif
			destroypkt(pkt);

			pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
			rbt_status = rbt_eraseKey(pfb->find_results_rbt, ppn->hash); /* remove ppn from results */
			assert(rbt_status == RBT_STATUS_OK);
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

			free(ppn);	/* ignore peers sending back weird replies */
			continue;	/* try next node */
		}
		/* OK, ppn has replied correctly, so we do have one more result */

		pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
		morehitsneeded = (--(pfb->find_results_avail) > 0);
		pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */

		/* ppn is now in find_results_rbt, so we can't unxor it.
		   let's use its local unxored copy pnunx */

		/* scan the returned peers */
		for(i=0, recommends_itself = 0; i<npeers; i++) {
			peernode *ppnr;
			ppnr = (peernode *)malloc(sizeof(peernode));
			assert(ppnr != 0);	/* or else, no memory! */
			getpeernode(ppnr, &pb);
#ifdef VERBOSE_DEBUG
			/* out of curiosity, see if this new candidate has lower distance from target than pnunx */
			KadC_log("ppnr %s ", htoa(ppnr->ip));
			KadC_log("proposed by %s has logdistance %d (ppn has %d)\n",
						htoa(pnunx.ip),
						int128xorlog(ppnr->hash, pfb->targethash),
						int128xorlog(pnunx.hash, pfb->targethash)
					);
#endif

			if(pnunx.ip == ppnr->ip && pnunx.port == ppnr->port) {	/* if peer references itself... */
				recommends_itself = 1;
#ifdef VERBOSE_DEBUG
				KadC_log("######### peer %s:%u recommends itself, with logdistance = %d\n",
							htoa(pnunx.ip), pnunx.port, int128xorlog(pnunx.hash, pfb->targethash) );
#endif
			}

			int128xor(ppnr->hash, ppnr->hash, pfb->targethash);	/* XORing... */
			/* put returned peer into find_input_rbt */
			pthread_mutex_lock(&pfb->fimutex);			/* \\\\\\ LOCK FI \\\\\\ */
#ifdef OPTIMIZE_BY_RECURSING_ONLY_ON_PEERS_BETTER_THAN_CURRENT_BEST
			iter = rbt_begin(pfb->find_input_rbt);
			rbt_status = RBT_STATUS_DUPLICATE_KEY;	/* by default, so that insert skipped == insert failed */
			if(iter == NULL || int128lt(ppnr->hash, ((peernode *)rbt_value(iter))->hash)) /* if rbt empty, or new better than rbt's best */
#endif
				rbt_status = rbt_insert(pfb->find_input_rbt, ppnr->hash, ppnr, 0);
			pthread_mutex_unlock(&pfb->fimutex);		/* ///// UNLOCK FI ///// */
			if(rbt_status != RBT_STATUS_OK)
				free(ppnr);	/* if already there, throw away */
		} /* end for */

		destroypkt(pkt);
	}
	return NULL;
}

static void *OvernetSearchInfoSession_th(void *p) {
	findblk *pfb = p;
	peernode *ppn;
	void *iter;

	while(!pfb->kstore_full) {
		pthread_mutex_lock(&pfb->frmutex);			/* \\\\\\ LOCK FR \\\\\\ */
		iter=rbt_begin(pfb->find_results_rbt);
		if(iter == NULL) {
			pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */
			break;
		}
		ppn = rbt_value(iter);
		rbt_erase(pfb->find_results_rbt, iter);
		pthread_mutex_unlock(&pfb->frmutex);		/* ///// UNLOCK FR ///// */
		OvernetSearchInfoSession(pfb, ppn);

		free(ppn);
	}
	return NULL;
}

#endif /* OLD_SEARCH_ONLY */

typedef struct _republish_params {
	void *rbt;	/* containing peernodes for republishing; will be emptied in the process */
	pthread_mutex_t mutex;	/* to synchronize access to rbt */
	int n_unpublished;		/* initially, max number of peernodes to be targeted */
	int npub_ack;		/* incremented after successful publishing to a peernode */
	kobject *pko;	/* the k-object to republish */
	KadEngine *pKE;
	int min_logdist;	/* for statistics only */
	int sum_logdist;	/* for statistics only */
	int sumsq_logdist;	/* for statistics only */
} republish_params;

static void *overnet_republish_th(void *p) {
	republish_params *prp = p;
	int128 khash = prp->pko->buf;

	for(;;) {
		peernode *ppn;
		void *iter;
		SessionObject *psession;
		int successful = 0;
		rbt_StatusEnum rbt_status;
		int logdist;

		pthread_mutex_lock(&prp->mutex);		/* \\\\\\ LOCK \\\\\\ */
		iter = rbt_begin(prp->rbt);
		if(iter == NULL || prp->n_unpublished <= 0 || prp->pKE->shutdown) {
			pthread_mutex_unlock(&prp->mutex);	/* ///// UNLOCK ///// */
			break;		/* if rbt empty, terminate thread */
		}
		prp->n_unpublished--;					/* decrement unpubl. counter */
		ppn = rbt_value(iter);
		rbt_status = rbt_erase(prp->rbt, iter);	/* detach ppn from rbtres */
		assert(rbt_status == RBT_STATUS_OK);
		assert(ppn != NULL);
		/* try to republish prp->pko to ppn */
		int128xor(ppn->hash, ppn->hash, khash);	/* unXOR (now safe, after detaching it from rbtres) */
		logdist = int128xorlog(ppn->hash, khash);
#ifdef DEBUG
		KadC_int128flog(stdout, ppn->hash); fflush(stdout);
		KadC_log(" Logd = %d %s %u\n", logdist, htoa(ppn->ip), ppn->port);
#endif
		pthread_mutex_unlock(&prp->mutex);		/* ///// UNLOCK ///// */

		/* now perform a OVERNET_PUBLISH / OVERNET_PUBLISH_ACK session */
		psession = sendOvernetPublish(prp->pKE, prp->pko, ppn->ip, ppn->port);
		if(psession == NULL) { /* Session allocation sometimes fails. FIXME: find out why */
#ifdef VERBOSE_DEBUG
			KadC_log("Can't create session for OVERNET_PUBLISH to peer %s:%u\n", htoa(ppn->ip), ppn->port);
#endif
		} else {
			packet *pkt = getpktt(psession, 3000);	/* 3 s timeout */
			if(pkt == NULL) {
#ifdef DEBUG
				KadC_log("timeout while waiting for OVERNET_PUBLISH_ACK from %s:%u\n",
							htoa(ppn->ip), ppn->port);
#endif
				UpdateNodeStatus(ppn, prp->pKE, 0);
			} else {
				if(pkt->len < 18 || pkt->buf[1] != OVERNET_PUBLISH_ACK) { /* the min is for OVERNET_SEARCH_END */
#ifdef DEBUG
					KadC_log("Malformed or wrong reply received from peer %s:%u\n", htoa(ppn->ip), ppn->port);
#endif
				} else if(int128eq(khash, &pkt->buf[2]) == 0){
#ifdef DEBUG
					KadC_log("OVERNET_PUBLISH_ACK with mismatched hash received from peer %s:%u\n", htoa(ppn->ip), ppn->port);
#endif
				} else {
#ifdef VERBOSE_DEBUG
					KadC_log("OVERNET_PUBLISH_ACK (%d-byte) from peer %s:%u\n", pkt->len, htoa(ppn->ip), ppn->port);
#endif
					/* ONLY if session is completed successfully */
					successful = 1;
				}
				destroypkt(pkt);
			}
			destroySessionObject(psession);
		}

		pthread_mutex_lock(&prp->mutex);	/* \\\\\\ LOCK \\\\\\ */
		if(successful) {
			prp->npub_ack++;
			if(logdist < prp->min_logdist)
				prp->min_logdist = logdist;
			prp->sum_logdist += logdist;
			prp->sumsq_logdist += (logdist * logdist);
		}
		else
			prp->n_unpublished++;	/* if unsuccessful, increment again prp->n_unpublished */
		pthread_mutex_unlock(&prp->mutex);	/* ///// UNLOCK ///// */

		free(ppn);	/* regardless, free ppn */
	}
	return NULL;
}

/* integer square root function by Jim Ulery stolen from:
   http://www.azillionmonkeys.com/qed/sqroot.html */
static unsigned mborg_isqrt3(unsigned long val) {
	unsigned long temp, g=0, b = 0x8000, bshft = 15;
	do {
		if (val >= (temp = (((g<<1)+b)<<bshft--))) {
			g += b;
			val -= temp;
		}
	} while (b >>= 1);
	return g;
}

int overnet_republishnow(KadEngine *pKE, kobject *pko, time_t *pstoptime, int nthreads) {
	rbt_StatusEnum rbt_status;
	void *resrbt;
	void *iter;
	int i;
	time_t starttime = time(NULL);
	pthread_t th[20];	/* never use more than 20 threads */
	republish_params rp;
	int128 khash = pko->buf;

	if(nthreads < 1)
		nthreads = 1;
	if(nthreads > arraysize(th))
		nthreads = arraysize(th);

#ifdef DEBUG
	KadC_log("Going to republish ");
	kobject_dump(pko, ";");
	KadC_log("\n");
#endif

	/* lookup the first hash in pko and get the results in the rbt */
	resrbt = Overnet_find(pKE, khash, 0, NULL, 0, pstoptime, 200, nthreads);

#ifdef DEBUG
	KadC_log("Lookup for key hash returned %d hits in %d seconds\n",
			rbt_size(resrbt), time(NULL)-starttime);
#endif

	rp.rbt = resrbt;
	rp.mutex = mutex_initializer;
	rp.n_unpublished = 20;	/* publish to the best 20 nodes */
	rp.pko = pko;
	rp.pKE = pKE;
	rp.npub_ack = 0;	/* incremented when a peernode ack's a store op */
	rp.min_logdist = 128;
	rp.sum_logdist = 0;
	rp.sumsq_logdist = 0;

	for(i=0; i<nthreads; i++) {
		pthread_create(&th[i], NULL, &overnet_republish_th, &rp);
	}

	for(i=0; i<nthreads; i++) {
		pthread_join(th[i], NULL);
#ifdef VERBOSE_DEBUG
		KadC_log("Joined overnet_republish_th() thread %lx\n", &th[i]);
#endif
	}

	pthread_mutex_destroy(&rp.mutex);

	/* empty resrbt from leftovers */
	for(;;) {
		peernode *ppn;
		iter=rbt_begin(resrbt);
		if(iter == NULL)
			break;
		ppn = rbt_value(iter);
		assert(ppn != NULL);
		rbt_status = rbt_erase(resrbt, iter);	/* detach ppn from rbtres */
		assert(rbt_status == RBT_STATUS_OK);
		free(ppn);
	}

	rbt_status = rbt_destroy(resrbt);		/* should be empty by now */
	assert(rbt_status == RBT_STATUS_OK);	/* better check, though */

#ifdef DEBUG
	{
		int avg_logdist = (rp.npub_ack < 1 ? 0 : rp.sum_logdist / rp.npub_ack);
		int var_logdist = (rp.npub_ack < 2 ? 0 :
				 (rp.sumsq_logdist - rp.sum_logdist*rp.sum_logdist/rp.npub_ack)*100/(rp.npub_ack - 1) );
		int stddev_logdist = (int)mborg_isqrt3((unsigned long)var_logdist);
		KadC_log("Hits: %d Logdists: min: %d avg: %d stddev: %d.%d\n",
					rp.npub_ack, rp.min_logdist, avg_logdist, stddev_logdist/10, stddev_logdist%10);
	}
#endif

	return rp.npub_ack;	/* return number of packets sent and acknowledged */
}



/* Parse and dump to console an Overnet packet
   return 0 if OK, 1 if couldn't parse the packet */

int OvernetDumppkt(packet *pkt) {
	unsigned char *buf = pkt->buf;
	unsigned char *pb;
	int nrecvd = pkt->len;
	unsigned char opcode = buf[1];
	peernode *ppn;
	int i=0, npeers=0;
	unsigned long int myexternalip;
	int unknownOpcode = 0;
	unsigned char recvdhash[16];

	pb = &buf[2];

	switch(opcode) {

	case OVERNET_CONNECT_REPLY:
		if(nrecvd < 4)
			break;	/* at least must have 4 bytes */
		npeers = getushortle(&pb);
		if(nrecvd != 4 + npeers * 23) /* packet must contain npeers 23-byte peers */
			break;
		for(i=0; i < npeers; i++) {
			ppn = (peernode *)malloc(sizeof(peernode));
			getpeernode(ppn, &pb);
			KadC_log("Overnet OVERNET_CONNECT_REPLY: peer %d of %d\n", i, npeers);
			KadC_log("hash: "); KadC_int128flog(stdout, (int128)(ppn->hash));
			KadC_log(" IP: %s", htoa(ppn->ip));
			KadC_log(" port: %d", ppn->port);
			KadC_log(" type: %d", ppn->type);
			KadC_log("\n");
			free(ppn);
		}
		break;

	case OVERNET_PUBLICIZE_ACK:
		if(nrecvd != 2)
			break;	/* fixed length response: null */
		KadC_log("Overnet OVERNET_PUBLICIZE_ACK\n");
		break;

	case OVERNET_IP_QUERY_ANSWER:
		if(nrecvd != 6)
			break;	/* fixed length response: only one IP */
		myexternalip = getipn(&pb);
		KadC_log("Overnet OVERNET_IP_QUERY_ANSWER\n");
		KadC_log(" My external IP: %s", htoa(myexternalip));
		KadC_log("\n");
		break;

	case OVERNET_IP_QUERY_END:
		if(nrecvd != 2)
			break;
		KadC_log("Overnet OVERNET_IP_QUERY_END\n");
		break;	/* fixed length response: null */

	case OVERNET_IDENTIFY:
		if(nrecvd != 2)
			break;
		KadC_log("Overnet OVERNET_IDENTIFY\n");
		break;	/* fixed length response: null */

	case OVERNET_IDENTIFY_REPLY:
		if(nrecvd != 24)
			break;
		ppn = (peernode *)malloc(sizeof(peernode));
		getcontactnode(ppn, &pb);
		KadC_log("Overnet OVERNET_IDENTIFY_REPLY: peer %d of %d\n", i, npeers);
		KadC_log("hash: "); KadC_int128flog(stdout, (int128)(ppn->hash));
		KadC_log(" IP: %s", htoa(ppn->ip));
		KadC_log(" port: %d", ppn->port);
		KadC_log("\n");
		free(ppn);
		KadC_log("Overnet OVERNET_IDENTIFY_ACK\n");
		break;	/* fixed length response: null */

	case OVERNET_IDENTIFY_ACK:
		if(nrecvd != 4)
			break;
		KadC_log("Overnet OVERNET_IDENTIFY_ACK\n");
		break;	/* fixed length response: null */

	case OVERNET_FIREWALL_CONNECTION_ACK:
		pb = &buf[2];
		if(nrecvd != 18)
			break;	/* fixed length response: only one hash */
		getint128n(recvdhash, &pb);
		KadC_log("Overnet OVERNET_FIREWALL_ACK\n");
		KadC_log(" Hash: "); KadC_int128flog(stdout, recvdhash);
		KadC_log("\n");
		break;

	case OVERNET_FIREWALL_CONNECTION_NACK:
		pb = &buf[2];
		if(nrecvd != 18)
			break;	/* fixed length response: only one hash */
		getint128n(recvdhash, &pb);
		KadC_log("Overnet OVERNET_FIREWALL_NACK\n");
		KadC_log(" Hash: "); KadC_int128flog(stdout, recvdhash);
		KadC_log("\n");
		break;

	case OVERNET_CONNECT:
		if(nrecvd != 25)
			break;	/* fixed length response: only one peer */
		pb = &buf[2];
		ppn = (peernode *)malloc(sizeof(peernode));
		getpeernode(ppn, &pb);
		KadC_log("Overnet OVERNET_CONNECT\n");
		KadC_log("hash: "); KadC_int128flog(stdout, (int128)(ppn->hash));
		KadC_log(" IP: %s", htoa(ppn->ip));
		KadC_log(" port: %d", ppn->port);
		KadC_log(" type: %d", ppn->type);
		KadC_log("\n");
		free(ppn);
		break;
	default:
		KadC_log("OvernetDumppkt() can't recognize these %d bytes:\n", nrecvd);
		for(i=0; i < nrecvd; i++) {
			if((i % 16) == 0)
				KadC_log("\n");
			KadC_log("%02x ", buf[i]);
		}
		KadC_log("\n================================\n");
		unknownOpcode = 1;
	}
	return unknownOpcode;
}

#define SO_KADC 1000000
/* builds in buf a TCP Hello message for our node.
   returns number of bytes stored in buf, or -1 if buffer size exceeded. */
int makeHelloMsg(char *buf, char *bufend, KadEngine *pKE) {
	unsigned char *p = (unsigned char *)buf;
	int buflen;
	char name[100];

	*p++ = OP_EDONKEYHEADER;
	p += 4;	/* leave space for length */
	*p++ = 0x01;
	*p++ = 0x10;
	putint128n(&p, pKE->localnode.hash);
	putipn(&p, pKE->extip);
	putushortle(&p, pKE->localnode.tport);
	putulongle(&p, 3);	/* 3 metas follow */

	sprintf(name, "KadC_%d.%d.%d", KadC_version.major, KadC_version.minor, KadC_version.patchlevel);
	putmeta(&p, "NAME", name);	/* temporary */
	putmeta(&p, "VERSION", NULL);
	putulongle(&p, 100);
	putmeta(&p, "pr", NULL);
	putulongle(&p, 1);	/* pre-release flag */

	putulongle(&p, 0);	/* unknown */
	putushortle(&p, 0);	/* unknown */
	buflen = p - (unsigned char *)buf - 1;
	p = (unsigned char *)buf+1;
	putulongle(&p, buflen);

	return buflen;
}


/* When a new Overnet server session starts, the following function
   is invoked under a dedicated thread. The first packet is passed
   as parameter; the code may send other packets to the peer
   and wait for same-session replies with pkt = getpkt(psession);
   packets obtained from getpkt() must be explicitly deallocated with
   "free(pkt->buf); free(pkt);" .The first packet will
   be automatically deallocated by the caller. */

void OvernetServerThread(SessionObject *psession, packet *pkt) {
	unsigned char *buf = pkt->buf;
	int nrecvd = pkt->len;
	unsigned char *pb;
	peernode *ppn;

#ifdef VERBOSE_DEBUG
	KadC_log("******* OvernetServerThread %lx started *******\n",
		(unsigned long int)psession);
	OvernetDumppkt(pkt);	/* see what's going on */
#endif /* DEBUG */

	switch(buf[1]) {
	case OVERNET_PUBLICIZE:
		if(nrecvd != 25)
			break;	/* fixed length response: only one peer */
		pb = &buf[2];
		ppn = (peernode *)malloc(sizeof(peernode));
		getpeernode(ppn, &pb);
		/* we should do something now... */
		free(ppn); /* remove this when ppn is put in bucket...*/

		{	/* reply */
			unsigned char outbuf[2];
			unsigned char *pb = outbuf;
			int status;

#ifdef VERBOSE_DEBUG
			KadC_log("Going to reply a OVERNET_PUBLICIZE_ACK\n");
#endif
			*pb++ = OP_EDONKEYHEADER;
			*pb++ = OVERNET_PUBLICIZE_ACK;
			status = sendbuf(psession, outbuf, sizeof(outbuf));
#ifdef VERBOSE_DEBUG
			KadC_log("In server session %lx sent as reply a OVERNET_PUBLICIZE_ACK\n",
					(unsigned long int)psession);
#endif
#ifdef DEBUG
			if(status != 0)
				KadC_log("sendbuf() returned %d while sending an OVERNET_PUBLICIZE_ACK\n", status);
#endif

		}
		break;

	case OVERNET_CONNECT:
		if(nrecvd != 25)
			break;	/* fixed length response: only one peer */
		pb = &buf[2];
		ppn = (peernode *)malloc(sizeof(peernode));
		getpeernode(ppn, &pb);
		/* we should do something now... */
		free(ppn); /* remove this when ppn is put in bucket...*/

		{	/* reply */
		}
		break;

	case OVERNET_IDENTIFY:
		{	/* reply */
			unsigned char outbuf[24];
			unsigned char *pb = outbuf;
			int status;

			*pb++ = OP_EDONKEYHEADER;
			*pb++ = OVERNET_IDENTIFY_REPLY;
			putint128n(&pb, psession->pKE->localnode.hash);
			overnetputourIPport(&pb, psession->pKE);



			status = sendbuf(psession, outbuf, sizeof(outbuf));
#ifdef VERBOSE_DEBUG
			KadC_log("In server session %lx sent as reply a OVERNET_IDENTIFY_REPLY\n",
					(unsigned long int)psession);
			if(status != 0) {
				KadC_log("sendbuf() returned %d while sending an OVERNET_CONNECT_RES\n", status);
			}
#endif
			pkt = getpkt(psession);	/* wait for OVERNET_IDENTIFY_ACK */
			if(pkt != NULL) {
#ifdef VERBOSE_DEBUG
				OvernetDumppkt(pkt);
#endif
				destroypkt(pkt);
			} else {
#ifdef VERBOSE_DEBUG
				KadC_log("In server session %lx timeout while waiting for OVERNET_IDENTIFY_ACK\n",
						(unsigned long int)psession);
#endif
			}
		}
	}
#ifdef VERBOSE_DEBUG
	KadC_log("+++++++ OvernetServerThread %lx terminating +++++++\n",
		(unsigned long int)psession);
#endif /* DEBUG */
}



int overnetinifileread(FILE *inifile, peernode *pmynode, void *contacts, int maxcontacts) {
	char line[132];
	parblock pb;
	static unsigned char int128zero[16] = {0};

	/* Read local params from INI file */
	if(findsection(inifile, "[local]") != 0) {
#ifdef DEBUG
		KadC_log("can't find [local] section in KadCmain.ini\n");
#endif
		return -1;
	} else {
		for(;;) {
			int npars;
			char *p = trimfgets(line, sizeof(line), inifile);
			if(p == NULL) {	/* EOF? */
#ifdef DEBUG
				KadC_log("can't find data under [local] section of KadCmain.ini\n");
#endif
				return -2;		/* EOF */
			}
			npars = parseline(line, pb);
			if(npars < 1 || pb[0][0] == '#')
				continue;	/* skip comments and blank lines */
			if(pb[0][0] == '[') {
#ifdef DEBUG
				KadC_log("can't find data under [local] section of KadCmain.ini\n");
#endif
				return -2;		/* start of new section */
			}
			if(npars != 5) {
#ifdef DEBUG
				KadC_log("bad format for local node data: skipping...\n");
#endif
				continue;
			}
			break;
		}
		string2int128((int128)pmynode->hash, pb[0]);
		/* if hash is zero, generate a random one (*not* saved to INI file) */
		if(int128eq(pmynode->hash, int128zero))
			int128setrandom(pmynode->hash);
		pmynode->ip = domain2hip(pb[1]);
		pmynode->port = (unsigned short int)atoi(pb[2]);
		pmynode->tport = (unsigned short int)atoi(pb[3]);
		pmynode->type = (unsigned char)atoi(pb[4]);
	}
	/* Read contacts from INI file */
	if(findsection(inifile, "[overnet_peers]") != 0) {
#ifdef DEBUG
		KadC_log("can't find [overnet_peers] section in KadCmain.ini\n");
#endif
		return -3;
	} else {
		int i;
		int nnodes = 0;
		for(i = 0; ; i++) {
			int npars;
			peernode *ppn;
			rbt_StatusEnum rbt_status;
			char *p = trimfgets(line, sizeof(line), inifile);

			if(p == NULL) {	/* EOF? */
				break;
			}
			npars = parseline(line, pb);
			if(pb[0][0] == '[') {
				break;;		/* start of new section */
			}
			if(pb[0][0] == '#') {
				continue;	/* skip comments */
			}
			if(npars != 4) {
#ifdef DEBUG
				KadC_log("bad format for contact %d lines after [contacts]: skipping...\n", i);
#endif
				continue;
			}
			ppn = (peernode *)malloc(sizeof(peernode));
			string2int128((int128)ppn->hash, pb[0]);
			ppn->ip = domain2hip(pb[1]);
			ppn->port = (unsigned short int)atoi(pb[2]);
			ppn->tport = 0; /* (unsigned short int)atoi(pb[3]); */
			ppn->type = (unsigned char)atoi(pb[3]);
			/* no locking necessary here: nobody else is accessing this rbt */
			rbt_status = rbt_insert(contacts, ppn->hash, ppn, 0);
			if(rbt_status == RBT_STATUS_OK)
				nnodes++;
			else
				free(ppn);	/* if insertion failed, free ppn */
			assert(rbt_status == RBT_STATUS_OK || rbt_status == RBT_STATUS_DUPLICATE_KEY);
		}
		if(nnodes == 0) {
#ifdef DEBUG
			KadC_log("can't find data under [overnet_peers] section of KadCmain.ini\n");
#endif
			return -4;		/* EOF */
		}
#ifdef VERBOSE_DEBUG
		KadC_log("Read %d nodes from the [overnet_peers] section of KadCmain.ini\n", nnodes);
		assert(nnodes == rbt_size(contacts));
#endif
		return rbt_size(contacts);
	}

}

#if 0
/* So deprecated function */
/* Save contacts under the section "[overnet_peers]", then free
   them and destroy the rbt. NEVER USE CONTACTS RBT AFTER CALLING THIS!! */
int overnetinifileupdate(FILE *inifile, FILE *wfile, KadEngine *pKE) {
	/* update ini file with the most recent knodes */
	int writtenpeers = 0;
	rbt_StatusEnum rbt_status;
	void *iter;
	int status;

	/* copy beginning up to "[overnet_peers]" */
	status = startreplacesection(inifile, "[overnet_peers]", wfile);
	if(status == 0) {	/* if section not found */
#ifdef DEBUG
		KadC_log("[overnet_peers] section not found in old inifile\n");
		return -1;
#endif
	}

	/* Remove peers from contacts rbt and write them
	   to ini file; free them and finally destroy the rbt */
	pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK CONTACTS \\\\\\ */
	KadC_flog(wfile, "# %d contacts follow\n", rbt_size(pKE->contacts));
	for(;;) {
		peernode *ppn;

		iter = rbt_begin(pKE->contacts);
		if(iter == NULL)
			break;
		ppn = rbt_value(iter);
		if(ppn->type < NONRESPONSE_THRESHOLD) {
			KadC_int128flog(wfile, ppn->hash);
			KadC_flog(wfile, " %s %u %u\n", htoa(ppn->ip), ppn->port, ppn->type);
			writtenpeers++;
		} else {
			KadC_log("WARNING: contact with excessive non-responses (%d) skipped in INI file update\n", ppn->type);
		}
		rbt_erase(pKE->contacts, iter);
		free(ppn);
	}
	rbt_status = rbt_destroy(pKE->contacts);
	assert(rbt_status == RBT_STATUS_OK); /* otherwise rbt wasn't empty... */
	pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK CONTACTS ///// */
	pthread_mutex_destroy(&pKE->cmutex);	/* prevent leaks on some platforms */
	/* complete copy of the remaining sections (if any) */
	endreplacesection(inifile, wfile);


	return writtenpeers;
}
#endif

/* Just writes the inifile section without freeing anything. */
int overnetinifilesectionwrite(FILE *wfile, KadEngine *pKE) {
	/* update ini file with the most recent knodes */
	int writtenpeers = 0;
	void *iter;

	if (pKE == NULL) return -1;
	
	/* Iterate through contacts rbt writinging them to ini file */
	pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK CONTACTS \\\\\\ */
	KadC_flog(wfile, "# %d contacts follow\n", rbt_size(pKE->contacts));
	for(iter  = rbt_begin(pKE->contacts); 
	    iter != NULL; 
	    iter  = rbt_next(pKE->contacts, iter)) 
	{	
		peernode *ppn;
		ppn = rbt_value(iter);
		if(ppn->type < NONRESPONSE_THRESHOLD) {
			KadC_int128flog(wfile, ppn->hash);
			KadC_flog(wfile, " %s %u %u\n", htoa(ppn->ip), ppn->port, ppn->type);
			writtenpeers++;
		} else {
			KadC_log("WARNING: contact with excessive non-responses (%d) skipped in INI file update\n", ppn->type);
		}
	}
	pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK CONTACTS ///// */

	KadC_log("Wrote %d peers to inifile\n", writtenpeers);

	return writtenpeers;
}



/* Background thread in charge for initial bootstrap, periodic
   refreshes to contacts table etc. Spawned by main(). */

void *OvernetBGthread(void *p) {
	KadEngine *pKE = p;
	time_t stoptime;
	int i;
	int addednodes = 0;

	/* Endless loop, only exited when shutdown is requested */

	for(i=0; pKE->shutdown == 0; i++) {
		unsigned char randhash[16];
		void *iter;
		peernode *ppn;
		int ncontacts;
		int nknodes, nknodesold = 0;
		int nthreads;

		millisleep(1000);
		if(i < 0) {
			i = 0; /* just in case 2,147,483,648 second have elapsed ;-) */
		}

		/* every second pick a random peer and ping it. If it's dead,
	       increase its type and if it exceeds NONRESPONSE_THRESHOLD
	       kick it out. If the number of contacts in the table drops below
	       3/4 maxcontacts, launch a new 1-minute single-thread kboot process. */

		int128setrandom(randhash);
		pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK CONTACTS \\\\\\ */
		rbt_find(pKE->contacts, randhash, &iter);
		if(iter == NULL)	/* maybe randhash bigger than biggest hash in contacts? */
			iter = rbt_begin(pKE->contacts); /* then try the first */
		if(iter != NULL) {
			int isalive;
			ppn = rbt_value(iter);
			pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK CONTACTS ///// */

#ifdef VERBOSE_DEBUG
			KadC_log("Pinging peer %s:%u\n", htoa(ppn->ip), ppn->port);
#endif
			
			isalive = Overnet_ping(pKE, ppn->ip, ppn->port, 1000);
#ifdef VERBOSE_DEBUG
			KadC_log("Ping result %d\n", isalive);
#endif
			if (isalive == 1 || isalive == 0)
				contact_status_update(pKE, ppn, isalive);
		} else {
			pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK CONTACTS ///// */
#ifdef DEBUG
			KadC_log("Uh oh... BG thread has found the contacts table empty!!!\n");
#endif
		}

		/* See if it's the case of repopulating the contacts rbt */

		pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK CONTACTS \\\\\\ */
		ncontacts = rbt_size(pKE->contacts);
		pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK CONTACTS ///// */
		
#ifndef OLD_KBOOT
		/* Initially, a bootstrap with many threads is preferred
		 * because presumably there are many contacts in the list that
		 * might not be active any more. After the initial bootstrap,
		 * there is no need to find nodes that aggressively unless 
		 * nknodes is low or approaches the number of known contacts.
		 */
		nthreads = 0;
		nknodes = knodes_count(pKE);
		if (i == 0 || (nknodes < 20 && (i % 600) == 0)) {
			/* The initial case, prefer many threads
			 * This is also done periodically if nknodes is small
			 */
			nthreads = pKE->maxcontacts / 20;
			if (nthreads == 0) nthreads = 1;
		} else {
			/* Subsequent cases, no need to find more 
			 * contacts if there's many unchecked ones 
			 * already. If the ratio of nknodes to
			 * contacts rises to 3/4, start a new
			 * bootstrap thread with the number of
			 * threads determined by the difference
			 * between maxcontacts and contacts.
			 */			
			if (!ncontacts || (nknodes*4) / ncontacts >= 3) {
				nthreads = (pKE->maxcontacts - ncontacts) / 20;
			}
		}
#else
		nthreads = (pKE->maxcontacts - ncontacts) / 20; /* one thread per 20 missing contacts (anyway limited to 5 by called routine) */

#ifdef VERBOSE_DEBUG
			KadC_log("preparing kboot - maxcontacts:%d, ncontacts:%d, nthreads:%d\n", pKE->maxcontacts, ncontacts, nthreads);
#endif
		
		if(i == 0 && nthreads == 0)
			nthreads = 1;	/* force one at the beginning in order to get the external IP via tcpsrv */
#endif  /* OLD_KBOOT */

		if(nthreads > 0) {
#ifdef VERBOSE_DEBUG
			KadC_logt("BG thread starting %d-threads kboot...\n", nthreads);
#endif
		}

		if(nthreads > 0) {
			/* start a max 1 min. boot with 1 - 4 threads depending on the emptiness of contacts */
			stoptime = time(NULL)+60;
			pKE->fwstatuschecked = 0;	/* recheck IP, just in case it's changed */
			addednodes = overnet_kboot(pKE, &stoptime, nthreads);
#ifdef VERBOSE_DEBUG
			pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK KE \\\\\\ */
			KadC_logt("BG thread boot added %d nodes to contacts list, new total: %d\n",
					addednodes, rbt_size(pKE->contacts));
			pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK KE ///// */
#endif
		}

		/* Every few minutes, or if the k-buckets are too empty,
		   refresh the k-buckets rbt with a self-search */

		nknodesold = knodes_count(pKE);

		if(nknodesold <= 10 || (i % 120) == 0) { /* 2 min for test, change to 600 = 5 min */
		/* proceed only if kbuckets hold <= 10 nodes, or every 10 minutes */

#ifdef VERBOSE_DEBUG
			KadC_logt("BG thread starting a self-lookup\n");
#endif
			stoptime = time(NULL)+60;	/* max 1 minute */
			Overnet_find(pKE, pKE->localnode.hash, 0, NULL, 0, &stoptime, 200, 20);	/* max 20 threads */

			nknodes = knodes_count(pKE);
#ifdef VERBOSE_DEBUG
			KadC_logt("BG self-lookup added %d nodes to contacts list, new total: %d\n",
						nknodes-nknodesold, nknodes);
#endif
		}

	}

	return NULL;
}



int OvernetCommandLoop(KadEngine *pKE) {

#define PARSIZE 16
	while(1)
	{
		char line[4096];
		int linelen;
		char *p;
		char *par[PARSIZE];
		int ntok;
		int i;
		SessionObject *psession;
		packet *pkt;
		time_t stoptime = 0;
		rbt_StatusEnum rbt_status;
		int ncontacts, nknodes;


		nknodes = knodes_count(pKE);

		pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK contacts \\\\\\ */
		ncontacts = rbt_size(pKE->contacts);
		pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK contacts ///// */

		KadC_log("%4d/%4d%s ", nknodes, ncontacts, (pKE->fwstatuschecked?(pKE->notfw?">":"]"):"?"));

		p = KadC_getsn(line, sizeof(line) -1);
		if(p == NULL) /* EOF? */
			break;
		if((p = strrchr(line, '\n')) != NULL) *p = 0;
		if((p = strrchr(line, '\r')) != NULL) *p = 0;
		if(*line == 0)
			continue; /* ignore empty lines */

		linelen = strlen(line);

		for(i=0; i<PARSIZE; i++)
			par[i] = "";

		for(i=0, ntok=0; i<linelen && ntok < PARSIZE; i++) {
			int sepfound;
			sepfound = isspace(line[i]);
			if(i == 0 || (!sepfound && line[i-1] == '\0'))
				par[ntok++] = &line[i];
			if(sepfound)
				line[i] = 0;
		}

		if(ntok == 0)
			continue; /* ignore lines with no parameters */

		if(strcmp(par[0], "boot") == 0) {
			/* temporary: */
			psession = sendOvernetBootReq(pKE, domain2hip(par[1]), atoi(par[2]));
			if(psession != NULL) { /* Session allocation sometimes fails. FIXME: find out why */

				pkt = getpkt(psession);
				if(pkt == NULL) { 	/* Timeout */
					KadC_log("*TIMEOUT*\n");
				} else {
					if(OvernetDumppkt(pkt))
						KadC_log("Response unknown\n");
					free(pkt->buf);
					free(pkt);
				}
				destroySessionObject(psession);
			} else {
				KadC_log("Session allocation failed\n");
			}
		} else if(strcmp(par[0], "hello") == 0) {
			psession = sendOvernetHelloReq(pKE, domain2hip(par[1]), atoi(par[2]));
			if(psession != NULL) { /* Session allocation sometimes fails. FIXME: find out why */
				pkt = getpktt(psession, 3000);
				if(pkt == NULL) { 	/* Timeout */
					KadC_log("*TIMEOUT*\n");
				} else {
					if(OvernetDumppkt(pkt))
						KadC_log("Response unknown\n");
					free(pkt->buf);
					free(pkt);
				}
				destroySessionObject(psession);
			} else {
				KadC_log("Session allocation failed\n");
			}
		} else if(strcmp(par[0], "fwcheck") == 0) { /* IP request, in Overnet */
			/* Should make sure that we are listening on TCP port...: */
			unsigned long int mytcpport = pKE->localnode.tport;
			char hellobuffer[128];
			int hellobufsize = sizeof(hellobuffer);
			char ourhellobuffer[128];
			int ourhellobufsize = sizeof(ourhellobuffer);
			int ourhellolen;
			pthread_t tcpsrv;
			tcpsrvpar tcpsrvparblock = {0};
			unsigned long int peerIP = domain2hip(par[1]);
			unsigned short int peerUDP = atoi(par[2]);

			if(pKE->fwstatuschecked == 0) {
				KadC_log("Background IP/NAT/Firewall check in progress - retry later\n");
				continue;
			}

			if(Overnet_ping(pKE, peerIP, peerUDP, 3000) == 0) {
				KadC_log("Peer doesn't respond, try another one\n");
				continue;
			}

			/* prepare our greetings message */
			ourhellolen = makeHelloMsg(ourhellobuffer, ourhellobuffer+ourhellobufsize, pKE);

			tcpsrvparblock.pKE = pKE;
			tcpsrvparblock.buf = hellobuffer;
			tcpsrvparblock.bufsize = hellobufsize;
			tcpsrvparblock.outbuf = ourhellobuffer;
			tcpsrvparblock.outnbytes = ourhellolen;
			tcpsrvparblock.quit = 0;
			pthread_create(&tcpsrv, NULL, &tcpsrv_th, &tcpsrvparblock);	/* launch TCP listener */

			psession = sendOvernetIpReq(pKE, mytcpport, peerIP, peerUDP);

			if(psession != NULL) { /* Session allocation sometimes fails. FIXME: find out why */
				pkt = getpktt(psession, 5000);				/* expect OVERNET_IP_QUERY_ANSWER */
				if(pkt == NULL) { 	/* Timeout */
					KadC_log("*TIMEOUT*\n");
					tcpsrvparblock.quit = 1;
				} else {
					unsigned char *p;
					if(OvernetDumppkt(pkt))
						KadC_log("Response unknown\n");
					p = pkt->buf+2;
					if(pkt->len == 6)
						pKE->extip = getipn(&p);
					free(pkt->buf);
					free(pkt);

#if 0
					/* ignore OVERNET_IP_QUERY_END, which probably won't arrive */
					pkt = getpktt(psession, 3000);			/* expect OVERNET_IP_QUERY_END */
					if(pkt == NULL) { 	/* Timeout, probably because our TCP port is unreachable or our Hello reply is no good */
						/* KadC_log("*TIMEOUT*\n"); */
					} else {
						if(OvernetDumppkt(pkt))
							KadC_log("Response unknown\n");
						free(pkt->buf);
						free(pkt);
					}
#endif
				}
				destroySessionObject(psession);
			} else {
				KadC_log("%s:%d Session allocation failed\n", __FILE__, __LINE__);
			}
			pthread_join(tcpsrv, NULL);
			if(tcpsrvparblock.nbytes > 5) {
				printHelloMsg(hellobuffer, hellobuffer+tcpsrvparblock.nbytes, tcpsrvparblock.peerIP);
				KadC_log("Our reply:\n");
				printHelloMsg(ourhellobuffer, ourhellobuffer+ourhellolen, pKE->extip);
			}
		} else if(strcmp(par[0], "lookup") == 0) { /* node lookup for publishing or self hash */
			/* lookup #hash [nthreads [duration]]
			   or
			   lookup keyword [nthreads [duration]]
			 */
			unsigned char hashbuf[16];
			void *resrbt;
			int nhits;
			int nthreads = atoi(par[2]);
			int duration = atoi(par[3]);

			if(nthreads < 1)
				nthreads = 5;

			if(duration < 1)
				duration = 15;	/* default 15s of search */

			stoptime = time(NULL)+duration;

			if(ntok < 2 || strcmp(par[1], "#") == 0) {
				memmove(hashbuf, pKE->localnode.hash, 16);
			} else if(par[1][0] == '#')
				string2int128(hashbuf, par[1]+1);
			else
				MD4(hashbuf, (unsigned char *)par[1], strlen(par[1]));

			KadC_log("Looking up %shash ",
				int128eq(hashbuf, pKE->localnode.hash) ? "our own " : "");
			KadC_int128flog(stdout, hashbuf);
			KadC_log("...\n");

			resrbt = Overnet_find(pKE, hashbuf, 0, NULL, 0, &stoptime, 200, nthreads);

			if(int128eq(hashbuf, pKE->localnode.hash)) {
				assert(resrbt == NULL);
#ifdef VERBOSE_DEBUG
				KadC_log("Refresh by lookup of own hash completed in %ld seconds.\n",
						(long int)time(NULL) - stoptime + duration);
#endif
			} else {
				void *iter;
				peernode *ppn;
				nhits = rbt_size(resrbt);
				for(;;) {
					iter = rbt_end(resrbt);	/* list and demolish in reverse order */
					if(iter == NULL)
						break;
					ppn = rbt_value(iter);
					rbt_status = rbt_erase(resrbt, iter);
					assert(rbt_status == RBT_STATUS_OK); /* remove from rbt */
					int128xor(ppn->hash, ppn->hash, hashbuf);	/* unXOR */
					KadC_int128flog(stdout, ppn->hash);
					KadC_log(" Logd = %d %s %u\n", int128xorlog(ppn->hash, hashbuf), htoa(ppn->ip), ppn->port);
					free(ppn);
				}
				rbt_status = rbt_destroy(resrbt);
				assert(rbt_status == RBT_STATUS_OK);
				KadC_log("Total results: %d Elapsed time: %d seconds\n",
						nhits, time(NULL) - stoptime + duration);
			}
		} else if(strcmp(par[0], "search") == 0 || strcmp(par[0], "s") == 0) {
			/* search firstkw [filter [nthreads [duration]]] */
			unsigned char hashbuf[16];

			void *iter;
			kobject *pko;
			kstore *pks;
			int nhits;
			char *stringex = par[2];
			int nthreads = atoi(par[3]);
			int duration = atoi(par[4]);
			unsigned char *pnsf;
			unsigned char *psf;
			unsigned char *psfilter;
			int sfilterlen;
			KadC_parsedfilter pf;

			if(nthreads < 1)
				nthreads = 10;

			if(duration < 1)
				duration = 15;	/* default 15s of search */

			stoptime = time(NULL)+duration;

			if(par[1][0] == '#') {
				if(par[1][1] != 0)
					string2int128(hashbuf, par[1]+1);
				else
					memmove(hashbuf, pKE->localnode.hash, 16);
			}
			else
				MD4((unsigned char *)hashbuf, (unsigned char *)par[1], strlen(par[1]));

			KadC_log("Searching for hash ");
			KadC_int128flog(stdout, hashbuf);
			KadC_log("...\n");

			/* pnsf = make_nsfilter(stringex); */
			pf = KadC_parsefilter(stringex);
			if(pf.err) {
				KadC_log("Parsing failure: %s%s\n", pf.errmsg1, pf.errmsg2);
				continue;
			}
			pnsf = pf.nsf;

			if(pnsf == NULL) {
				psfilter = NULL;
				sfilterlen = 0;
			} else {
				sfilterlen = getushortle(&pnsf);	/* also add 2 to pnsf */
				psfilter = pnsf;
				pnsf -= 2;							/* restore pnsf, or free() will fail! */
			}

			psf = psfilter;
			if(psf != NULL) {
				KadC_log("Filtering with:\n");
				if(s_filter_dump(&psf, psf+sfilterlen) < 0)
					KadC_log("--Malformed filter!");
				KadC_log("\n");
			}

			pks = Overnet_find(pKE, hashbuf, 1, psfilter, sfilterlen, &stoptime, 100, nthreads);

			nhits = rbt_size(pks->rbt);

			if(pnsf != NULL)
				free(pnsf);

			/* list each k-object */
			for(iter = rbt_begin(pks->rbt); iter != NULL; iter = rbt_next(pks->rbt, iter)) {
				pko = rbt_value(iter);

				KadC_log("Found: \n");
				kobject_dump(pko, "; ");
				KadC_log("\n");
			}
			KadC_log("Search completed in %d seconds - %d hit%s returned\n",
				time(NULL)-stoptime+duration, nhits, (nhits == 1 ? "" : "s"));
			kstore_destroy(pks, 1);	/* also destroy contained k-objects */

		} else if(strcmp(par[0], "publish") == 0 || strcmp(par[0], "p") == 0) {
			/* publish {#[khash]|key} {#[vhash]|value} [meta-list [nthreads [nsecs]]] */
			unsigned char khashbuf[16], vhashbuf[16];

			kobject *pko;
			char *stringmeta = par[3];
			int nthreads = atoi(par[4]);
			int duration = atoi(par[5]);

			if(nthreads < 1)
				nthreads = 10;

			if(duration < 1)
				duration = 15;	/* default 15s of lookup */

			stoptime = time(NULL)+duration;

			if(par[1][0] == '#') {
				if(par[1][1] != 0)
					string2int128(khashbuf, par[1]+1);
				else
					memmove(khashbuf, pKE->localnode.hash, 16);
			}
			else
				MD4((unsigned char *)khashbuf, (unsigned char *)par[1], strlen(par[1]));

			if(par[2][0] == '#') {
				if(par[2][1] != 0)
					string2int128(vhashbuf, par[2]+1);
				else
					memmove(vhashbuf, pKE->localnode.hash, 16);
			}
			else
				MD4((unsigned char *)vhashbuf, (unsigned char *)par[2], strlen(par[2]));

			pko = make_kobject(khashbuf, vhashbuf, stringmeta);
			if(pko == NULL) {
				KadC_log("Syntax error. Try: p key #hash [tagname=tagvalue[;...]]\n");
				continue;
			}
/*
			KadC_log("Publishing k-object ");
			kobject_dump(pko, ";");
			KadC_log("\n");
*/
			/* this is an "instant publishing": pko is not stored for
			   periodic auto-republishing by BGthread (that is to be implemented) */
			overnet_republishnow(pKE, pko, &stoptime, nthreads);

			kobject_destroy(pko);

		} else if(strcmp(par[0], "gc") == 0) {
			/* temporary: */
			reapDeadServerThreads(pKE);
		} else if(strcmp(par[0], "q") == 0 || strcmp(par[0], "quit") == 0) {
			/* temporary: */
			break;
		} else if(strcmp(par[0], "dump") == 0 || strcmp(par[0], "d") == 0) { /* dump various structures */
			/* dump session table rbt */
			{
				int i;
				qnode *cur;

				KadC_log("------- Session Service Table -------\n");
				SessionsTable_dump(pKE);
				KadC_log("------- Dead Sessions Table -------\n");
				for(i=0, cur=pKE->DeadServerSessionsFifo->head; cur != NULL; i++, cur = cur->next) {
					int j;
					qnode *cur1;
					SessionObject *psession = (SessionObject *)cur->data;
					KadC_log("%d>%s:%d %s: entry %lx fifo holds %d: (",
							psession->ID.KF,
							htoa(psession->ID.IP),
							psession->ID.port,
							(psession->ID.isServerSession? "(s)":"   "),
							(unsigned long)psession,
							psession->fifo->n);
					pthread_mutex_lock(&psession->mutex);	/* \\\\\\ LOCK session \\\\\\ */
					for(j=0, cur1=psession->fifo->head; cur1 != NULL; j++, cur1 = cur1->next) {
						packet *pkt = (packet *)cur1->data;
						KadC_log("[%d]: %lx[%d]; ", j, (unsigned long int)pkt->buf, pkt->len);
					}
					pthread_mutex_unlock(&psession->mutex);	/* ///// UNLOCK session ///// */
					KadC_log(")\n");
				}
			}
			/* dump k-buckets table */
			dump_kba(pKE);
#ifdef VERBOSE_DEBUG
			/* dump kspace table (contacts table by hash) */
			dump_kspace(pKE);
#else
			KadC_log("K-buckets hold %d peernodes\n", knodes_count(pKE));
#endif
			pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK contacts \\\\\\ */
			KadC_log("Contacts table holds %d peernodes\n", rbt_size(pKE->contacts));
			pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK contacts ///// */
			/* dump local node characteristics */
			KadC_log("Our node: ");
			KadC_int128flog(stdout, pKE->localnode.hash);
			KadC_log(" %s:%u routable: %s\n",
				htoa(pKE->localnode.ip), pKE->localnode.port, isnonroutable(pKE->localnode.ip)? "NO" : "YES");
			KadC_log(" extip: %s tport: %u TCP-firewalled: %s leafmode: %s\n",
				htoa(pKE->extip), pKE->localnode.tport,
				pKE->notfw ? "NO" : "YES",
				pKE->leafmode? "YES" : "NO");
			/* dump other stuff, as required */
#ifdef VERBOSE_DEBUG
			KadC_list_outstanding_mallocs(3);
#endif
		} else if(strcmp(par[0], "z") == 0 || strcmp(par[0], "zeroize") == 0) {
			/* clear k-bucket table */
			erase_knodes(pKE);
		} else {
			KadC_log("Commands:\n");
			KadC_log(" dump\n");
			KadC_log(" hello   peerIP peerUDPport\n");
			KadC_log(" fwcheck peerIP peerUDPport\n");
			KadC_log(" s[earch]  {#[hash]|keyw} {nam1=val1[;nam2=val2...]|-} [nthreads [nsecs]]\n");
			KadC_log(" p[ublish] {#[khash]|key} {#[vhash]|value} [meta;[meta...] [nthreads [nsecs]]]\n");
			KadC_log(" q[uit]\n");
		}
	}
	KadC_log("Shutting down, please wait...\n");
	return 0;
}


