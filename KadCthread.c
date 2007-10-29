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

/* #define DEBUG 1 */
#include <pthread.h>
#include <stdio.h>
#include <Debug_pthreads.h>
#include <pthreadutils.h>
#include <assert.h>
#include <zlib.h>
#include <string.h>
#include <time.h>

#include <queue.h>
#include <net.h>
#include <rbt.h>
#include <int128.h>
#include <KadCalloc.h>
#include <opcodes.h>
#include <KadCthread.h>
#include <eMuleKAD.h>
#include <KadClog.h>
#include <overnet.h>
#include <bufio.h>
#include <millisleep.h>


#define MAXPACKETS (16) /* Session object's FIFO size */
#define MAXSERVERTHREADS (16) /* Max number of independent Server Threads */
#define FIFOTIMEOUT (15000)	/* 15 seconds timeout on queue input */

static const pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;

static void KAD_UDPlistener(UDPIO *pul);
static int deallocSessionObject(SessionObject *psession);

static int sid_compLT(void *a, void *b) {
	SessionID *pa = a;
	SessionID *pb = b;
	if(pa->isServerSession != pb->isServerSession)
		return pa->isServerSession < pb->isServerSession;
	else if(pa->KF != pb->KF)
		return pa->KF < pb->KF;
	else if (pa->IP != pb->IP)
		return pa->IP < pb->IP;
	else
		return pa->port < pb->port;
}

static int sid_compEQ(void *a, void *b) {
	SessionID *pa = a;
	SessionID *pb = b;
	return pa->KF == pb->KF && pa->IP == pb->IP
		&& pa->port == pb->port && pa->isServerSession == pb->isServerSession;
}

/* Starts the Kademlia engine for all the implemented flavours */

KadEngine *startKadEngine(UDPIO *pul, KadFlavour KF){
	KadEngine *pKE;
 	int status;

	pKE = (KadEngine *)malloc(sizeof(KadEngine));
	if(pKE != NULL) {
		memset(pKE, 0, sizeof(KadEngine));
		pKE->KF = KF;

		pthreadutils_mutex_init_recursive(&pKE->mutex);

		pKE->SessionsTable = rbt_new(&sid_compLT, &sid_compEQ);
		if(pKE->SessionsTable != NULL) {
			rbt_StatusEnum rbt_status;

			pKE->ServerThreadsPoolsize = MAXSERVERTHREADS;
			pKE->DeadServerSessionsFifo = new_queue(MAXSERVERTHREADS+1);
			if(pKE->DeadServerSessionsFifo != NULL) {
				pKE->pul = pul;
				pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK pKE \\\\\\ */
				if(KF == EMULE) {
					pul->arg[OP_KADEMLIAHEADER] = (void *)pKE;
					pul->callback[OP_KADEMLIAHEADER] = KAD_UDPlistener;	/* hook up I/O */
					pul->arg[OP_KADEMLIAPACKEDPROT] = (void *)pKE;
					pul->callback[OP_KADEMLIAPACKEDPROT] = KAD_UDPlistener;	/* hook up I/O */
			 	} else if(KF == OVERNET) {
					pul->arg[OP_EDONKEYHEADER] = (void *)pKE;
					pul->callback[OP_EDONKEYHEADER] = KAD_UDPlistener;	/* hook up I/O */
				} if(KF == REVCONNECT) {
					pul->arg[OP_REVCONNHEADER] = (void *)pKE;
					pul->callback[OP_REVCONNHEADER] = KAD_UDPlistener;	/* hook up I/O */
					pul->arg[OP_REVCONNPACKEDPROT] = (void *)pKE;
					pul->callback[OP_REVCONNPACKEDPROT] = KAD_UDPlistener;	/* hook up I/O */
				}
				pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK pKE ///// */
				pKE->cmutex = mutex_initializer;
				return pKE;
			}
			rbt_status = rbt_destroy(pKE->SessionsTable);
			assert(rbt_status == RBT_STATUS_OK);
			status = pthread_mutex_destroy(&pKE->mutex);
			assert(status == 0);
		}
		free(pKE);
	}
	return NULL;
}

int stopKadEngine(KadEngine *pKE) {
	UDPIO *pul = pKE->pul;
	void *iter;
	rbt_StatusEnum rbt_status;

	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK pKE \\\\\\ */
	if(pKE->KF == EMULE) {
		pul->callback[OP_KADEMLIAHEADER] = NULL;	/* unhook up I/O */
		pul->callback[OP_KADEMLIAPACKEDPROT] = NULL;	/* unhook up I/O */
	} else if(pKE->KF == OVERNET) {
		pul->callback[OP_EDONKEYHEADER] = NULL;	/* unhook up I/O */
	} if(pKE->KF == REVCONNECT) {
		pul->callback[OP_REVCONNHEADER] = NULL;	/* unhook up I/O */
		pul->callback[OP_REVCONNPACKEDPROT] = NULL;	/* unhook up I/O */
	}

	if(rbt_size(pKE->SessionsTable) != 0) {

		KadC_log("**** Sessions Table not empty ****\n");
		SessionsTable_dump(pKE);
		KadC_log("Now attempting to flush the stuck sessions\n");

		/* empty SessionsTable */
		for(iter = rbt_begin(pKE->SessionsTable); iter != NULL; iter = rbt_next(pKE->SessionsTable, iter)) {
			SessionObject *psession;

			psession = rbt_value(iter);
			KadC_log("Posting a NULL to session 0x%08x's FIFO\n");
			postbuffer2fifo(psession, NULL, 0);	/* force a timeout killing session */
		}
	}
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK pKE ///// */
	millisleep(1000);	/* let trheads process the timeout and die */
	reapDeadServerThreads(pKE);
	pKE->DeadServerSessionsFifo->destroy(pKE->DeadServerSessionsFifo);
	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK pKE \\\\\\ */
	rbt_status = rbt_destroy(pKE->SessionsTable);
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK pKE ///// */
	if(rbt_status != RBT_STATUS_OK)
		KadC_log("Groan, still pending session. Oh well.\n");

	pthread_mutex_destroy(&pKE->mutex);
	pthread_mutex_destroy(&pKE->cmutex);

	free(pKE);
	return 0;
}

/* Create a malloc'd packet from a buffer and its length */
packet *newpacket(unsigned char *buf, int buflen) {
	packet *pkt = malloc(sizeof(packet));
	if(pkt != NULL) {
		pkt->buf = malloc(buflen);
		if(pkt->buf == NULL) {
			free(pkt);
		} else {
			pkt->len = buflen;
			memmove(pkt->buf, buf, buflen);
		}
	}
	return pkt;
}

/* Receive a packet from the Session Object's input FIFO
   the packet must then be deallocated by the caller calling
   destroypkt(pkt). */
packet *getpkt(SessionObject *psession) {
	packet *pkt;
	if(psession == NULL)
		return NULL;
	pkt = (packet *)psession->fifo->deqtw(psession->fifo, FIFOTIMEOUT);
	return pkt;
}

/* like getpktt, but the second parameter may define timeout in ms */
packet *getpktt(SessionObject *psession, int fifotimeoutmilli) {
	packet *pkt;
	if(psession == NULL)
		return NULL;
	pkt = (packet *)psession->fifo->deqtw(psession->fifo, fifotimeoutmilli);
	return pkt;
}


/* Wait for a packet from the Session Object's input FIFO
   with an opcode (second byte in the buffer) equal to either
   opcode 1 or opcode 2.
   Throw away intermediate packets, and return only
   when that type of packet has arrived, or for timeout.
   The returned packet must then be deallocated by the caller calling
   destroypkt(pkt). */
packet *waitforpkt(SessionObject *psession, unsigned char opcode1, unsigned char opcode2) {
	packet *pkt;
	struct timespec ts, tsnow;
	long int millis_timeout = FIFOTIMEOUT;

	/* set ts to now() */
	ts_set(&ts);
	for(;;) {
		pkt = (packet *)psession->fifo->deqtw(psession->fifo, millis_timeout);
		if(pkt == NULL)
			break;			/* if timeout, return NULL */
		if(pkt->buf[1] == opcode1 || pkt->buf[1] == opcode2)
			break;			/* if got the expected packet, return it */
		destroypkt(pkt);	/* throw it away, it's not the expected one */
		ts_set(&tsnow);
		/* reduce timeout by time already elapsed */
		millis_timeout -= millisdiff(&tsnow, &ts);
		if(millis_timeout < 0)
			millis_timeout = 0;	/* just in case */
	}
	return pkt;
}


/* Deallocate a packet received from getpkt() or created by newpacket() */
void destroypkt(packet *pkt) {
	free(pkt->buf);
	free(pkt);
}

/* Send a buffer to a peer when a session is already established
   The buffer, if necessary, must be deallocated by the caller ()
 */
int sendbuf(SessionObject *psession, unsigned char *buf, int buflen) {
	int status = UDPsend(psession->pKE->pul, buf, buflen, psession->ID.IP, psession->ID.port);
	return (status < 0);
}

/* Send a packet to a peer when a session is already established
   The packet is automatically deallocated by sendpkt()
 */
int sendpkt(SessionObject *psession, packet *pkt) {
	int status = sendbuf(psession, pkt->buf, pkt->len);
	destroypkt(pkt);
	return (status);
}

/* This thread will only initially receive one of the "req"-type packets
   that, when received, can start a Server Session. Thereafter,
   it will receive all the packet relative to that session.
   The purpose of this code is to get a packet from the UDPlistener
   thread and pass it to the appropriate handler for each of the
   supported Kademlia flavours. */

void *ServerSessionThread(void *arg) {
	SessionObject *psession = (SessionObject *)arg;
	packet *pkt;

	pkt = getpkt(psession);
	if(pkt == NULL)
		return NULL;

#ifdef DEBUG /* DEBUG ONLY */
	{
		int i;
		KadC_log("ServerSessionThread: received from fifo %d bytes\n", pkt->len);
		KadC_log("the sender's address/port was ");
		KadC_log("%s:%d", htoa(psession->ID.IP), psession->ID.port);
		for(i=0; i < pkt->len /* && i < 48 */; i++) {
			if((i % 16) == 0)
				KadC_log("\n");
			KadC_log("%02x ", pkt->buf[i]);
		}
	}
	KadC_log("\n================================\n");
#endif

	if(psession->ID.KF == EMULE) {
		eMuleKADServerThread(psession, pkt);
	} else if(psession->ID.KF == OVERNET) {
		OvernetServerThread(psession, pkt);
	} else if(psession->ID.KF == REVCONNECT) {
		;	/* To be implemented */
	} else {
#ifdef DEBUG /* DEBUG ONLY */
		KadC_log("Unknown Kademlia Flavour %d\n", psession->ID.KF);
#endif
	}

	free(pkt->buf);
	free(pkt);

	/* ------- End Session ------- */

	destroySessionObject(psession);

	return NULL;
}

/* Destroy all session objects posted to the KadEngine's DeadServerSessionsFifo
   Client Session objects are destroyed by the calling thread, which does
   not have to worry about sawing the branch they are on (server threads
   allocate the pthread_t variable in the SessionObject itself!)
   Besides, this offers an oppurtunity to reap the dead server threads. */

int reapDeadServerThreads(KadEngine *pKE) {
	int i;
	SessionObject *dsts;	/* dead server thread session */
	queue *dstq = pKE->DeadServerSessionsFifo;

	pthread_mutex_lock(&pKE->mutex);
	for(i=0;;i++) {
		dsts = (SessionObject *)(dstq->deqtw(dstq, 0));
		if(dsts == NULL)
			break;
		deallocSessionObject(dsts); /* also reaps thread, being a server session */
	}
	pthread_mutex_unlock(&pKE->mutex);
	return i;
}

static void KAD_UDPlistener(UDPIO *pul) {

	SessionID sid;
	SessionObject *psession;
	unsigned char *buf = pul->buf;
	unsigned char decompr[4096];
	unsigned short int zstatus;
	unsigned long int unpackedsize;
	int status;
	KadEngine *pKE;
	int isREQ = 0;


	pKE = (KadEngine *)pul->arg[buf[0]];
	if(pKE == NULL)
		return;			/* unregistered flavour or encapsulation */

#ifdef DEBUG
	{
		int i;
		/* diag only */
		/* pul->buf[pul->nrecv] = 0; */
		KadC_log("| UDPListener: from fd %d received %d characters\n", pul->fd, pul->nrecv);
		KadC_log("| the sender's address/port was ");
		KadC_log("| %s:%d", htoa(pul->remoteip), pul->remoteport);
		/* KadC_log("buffer: <%s>\n", pul->buf); */
		for(i=0; i < pul->nrecv /* && i < 48 */; i++) {
			if((i % 16) == 0)
				KadC_log("\n| ");
			KadC_log("%02x ", pul->buf[i]);
		}
		KadC_log("\n `--------------------------------\n");
	}
#endif

	reapDeadServerThreads(pKE);	/* always a good thing to free resources */

	if(pul->nrecv < 2)	/* needs at least flavour flag and opcode */
		return;

	if(buf[0] == OP_KADEMLIAPACKEDPROT) {	/* 0xE5: compressed packet, decompress */
		unpackedsize = sizeof(decompr);
		zstatus = uncompress(decompr + 2, &unpackedsize, pul->buf + 2, pul->nrecv - 2);
		if(zstatus != Z_OK) {
#ifdef DEBUG
			KadC_log("uncompress failed returning %d; nrecv = %d\n",
				zstatus, pul->nrecv); /* uncompress failed, hmmm... */
#endif
			return;	/* drop packet */
		} else {
#ifdef DEBUG
			KadC_log("uncompress succeeded expanding from %d to %lu bytes\n",
				pul->nrecv - 2, unpackedsize);
#endif
		}
		pul->nrecv = unpackedsize+2;
		decompr[0] = OP_KADEMLIAHEADER;
		decompr[1] = buf[1];			/* copy opcode */
		buf = decompr;		/* proceeds with decompressed buffer */
 	}

	/* Now decide what to do based on flavour/encapsulation.
	   In Kademlia a session starts with one REQuest sent by the "client",
	   and proceeds with a number of RESponses or followups sent back
	   by the client. The opcode (buf[1]) decides whether or not the
	   packet is a REQuest, but this depends of the Kademlia Flavour
	   (which is determined by buf[0]). So we have separate is...REQ()
	   functions for each supported flavour.
	 */
	switch(pKE->KF) {
	case EMULE:
		isREQ = iseMuleREQ(buf[1]);
		break;
	case OVERNET:
		isREQ = isOvernetREQ(buf[1]);
		break;
	case REVCONNECT:
		return;			/* not implemented yet, ignore packet */
	case OTHER:
						/* RTP won't be seen here, being dispatched to
						   RTP_UDPlistener instad of KAD_UDPlistener() */
		return;			/* not implemented yet, ignore packet */
	}

	sid.KF = pKE->KF;
	sid.IP = pul->remoteip;
	sid.port = pul->remoteport;

	if(isREQ == 0) {		/* is this some type of Kademlia RESponse or followup? */
		SessionObject *psession;

		/* look for a client session with this <flavour, ip, port>
		   if exists, post buffer
		   but first lock KE so that session won't disappear after we looked */
		pthread_mutex_lock(&pKE->mutex);	/* \\\\\\\\\\\\\\\\\\\\\\\\\\\\ */
		psession = retrieveSessionObject(pKE, pul->remoteip, pul->remoteport, 0);
		if(psession != NULL) { /* if relative to a live existing session, use that one! */
		/* deposit packet in FIFO queue of the existing session found */
			status = postbuffer2fifo(psession, buf, pul->nrecv);
#ifdef VERBOSE_DEBUG
			KadC_log("Posted packet with opcode %02x to client session %lx\n",
					buf[1], (unsigned long int)psession);
			if(status != 0) {
				KadC_log("postbuffer2fifo() failed returning %d\n", status);
			}
#endif
		} else {
#ifdef DEBUG
			if(psession == NULL) {
				KadC_log("Strange: a RESponse arrived without a Client Session already open!\n");
			}
#endif
		}
		pthread_mutex_unlock(&pKE->mutex);	/* //////////////////////////// */
	} else {	/* if instead it's some type of Kademlia REQuest or END */
		/* There may be an already open server session with this <flavour, ip, port>
		   if this is the closing followup of a three-way session */
		pthread_mutex_lock(&pKE->mutex);	/* \\\\\\\\\\\\\\\\\\\\\\\\\\\\ */
		psession = retrieveSessionObject(pKE, pul->remoteip, pul->remoteport, 1);
		if(psession == NULL) {
			/* if not there, create a new session to handle the REQ */
			psession = newSessionObject(pKE, pul->remoteip, pul->remoteport, 1);
			pthread_mutex_unlock(&pKE->mutex);	/* //////////////////////////// */
			if(psession == NULL) {
	#ifdef DEBUG
				KadC_log("newSessionObject() failed in UDPlistener()\n");
	#endif
				return;	/* and drop the packet */
			} else {
				/* server session successfully created.  */
				/* Now need to start the thread */
				/* first see if there are dead threads to reap */

				status = pthread_create(&psession->thread, NULL, ServerSessionThread, psession);
				if(status != 0) {
		#ifdef DEBUG
					KadC_log("pthread_create() failed returning %d\n", status);
		#endif
					destroySessionObject(psession);
					return;	/* and drop the packet */
				}
			}
		} else {
			pthread_mutex_unlock(&pKE->mutex);	/* //////////////////////////// */
		}
		/* put packet into FIFO queue of the server session found or created */
		status = postbuffer2fifo(psession, buf, pul->nrecv);
#ifdef VERBOSE_DEBUG
			KadC_log("Posted packet with opcode %02x to server session %lx\n",
					buf[1], (unsigned long int)psession);
#endif
		if(status != 0) {
#ifdef DEBUG
			KadC_log("postbuffer2fifo() failed returning %d\n", status);
#endif
			return;		/* drop the packet */
		}
	} /* end if(isREQ) */
}

/* Post a packet to the input FIFO of a Session Object.
   If buffer == NULL it posts a NULL, simulating a timeout
   on getpkt().
   Used by UDPlistener to pass received data to a Session.
 */
int postbuffer2fifo(SessionObject *psession, unsigned char *buf, int buflen) {
	packet *pkt;
	int status;

	if(buf == NULL) {	/* special case */
		status = psession->fifo->enq(psession->fifo, NULL);	/* sim. timeout */
		return 0;
	}

	pkt = newpacket(buf, buflen);
	if(pkt == NULL) {
		return 1;
	} else {
		status = psession->fifo->enq(psession->fifo, pkt);	/* enqueue */
		if(status != 0) {		/* probably FIFO full */
			free(pkt->buf);
			free(pkt);
			return 2;
		}
	}
	return 0;
}


/* Create a new Session Object based on the given parameters
   return NULL if such session existed already */
SessionObject *newSessionObject(KadEngine *pKE, unsigned long int IP, unsigned short int port, int isServerSession) {
	SessionObject *psession = NULL;
	rbt_StatusEnum rbt_status;
	SessionID sid;
	void *iter;

	sid.KF = pKE->KF;
	sid.IP = IP;
	sid.port = port;
	sid.isServerSession = isServerSession;

	pthread_mutex_lock(&pKE->mutex);		/* \\\\\\ lock \\\\\\ */

	rbt_status = rbt_find(pKE->SessionsTable, &sid, &iter);

	if(rbt_status != RBT_STATUS_KEY_NOT_FOUND) {
#ifdef DEBUG
		KadC_log("rbt_find() failed returning %d in newSessionObject() while ensuring non-existence of session {%d>%s:%u(%c)}\n",
				rbt_status, sid.KF, htoa(sid.IP), sid.port, sid.isServerSession? 'S' : 'C');
#endif
		pthread_mutex_unlock(&pKE->mutex);		/* ///// unlock ///// */
		return NULL;
	}

	if(isServerSession == 0 || pKE->ServerThreadsPoolsize > 0) {
	 /* create server threads only if the pool is not exhausted */
		psession = (SessionObject *)malloc(sizeof(SessionObject));
		if(psession != NULL) {
			memset(psession, 0, sizeof(SessionObject));
			psession->pKE = pKE;
			psession->ID.KF = pKE->KF;
			psession->ID.IP = IP;
			psession->ID.port = port;		/* embed key in data record */
			psession->ID.isServerSession = isServerSession;

	   		pthreadutils_mutex_init_recursive(&psession->mutex);

			psession->fifo = new_queue(MAXPACKETS);
			if(psession->fifo == NULL) {
	#ifdef DEBUG
				KadC_log("new_queue() failed in newSessionObject()\n");
	#endif
				free(psession);
				psession = NULL;
			} else {
				rbt_status = rbt_insert(pKE->SessionsTable, &psession->ID, psession, 0);
				if(rbt_status != RBT_STATUS_OK) {
		#ifdef DEBUG
					KadC_log("rbt_insert() failed returning %d\n", rbt_status);
		#endif
					psession->fifo->destroy(psession->fifo);	/* deallocate newly created FIFO */
					pthread_mutex_destroy(&psession->mutex);	/* destroy mutex */
					free(psession);
					psession = NULL;
				} else {	/* Yeah! Session Object created! */
					if(isServerSession == 1)
						pKE->ServerThreadsPoolsize--;
				}
			}
		}
	}
	pthread_mutex_unlock(&pKE->mutex);		/* ///// unlock ///// */
	return psession;
}

/* retrieves an existing session object for <KF, IP, port, isServerSession>, or return NULL if there aren't any */
SessionObject *retrieveSessionObject(KadEngine *pKE, unsigned long int IP, unsigned short int port, int isServerSession) {
	SessionObject *psession = NULL;
	rbt_StatusEnum rbt_status;
	SessionID sid;
	void *iter;

	sid.KF = pKE->KF;
	sid.IP = IP;
	sid.port = port;
	sid.isServerSession = isServerSession;

	pthread_mutex_lock(&pKE->mutex);		/* \\\\\\ lock \\\\\\ */

	rbt_status = rbt_find(pKE->SessionsTable, &sid, &iter);

	if(rbt_status != RBT_STATUS_OK && rbt_status != RBT_STATUS_KEY_NOT_FOUND) {
#ifdef DEBUG
		KadC_log("rbt_find() failed returning %d in getSessionObject() while looking for session {%d>%s:%u(%c)}\n",
				rbt_status, sid.KF, htoa(sid.IP), sid.port, sid.isServerSession? 'S' : 'C');
#endif
	}

	if(rbt_status == RBT_STATUS_OK) {
		psession = (SessionObject *)rbt_value(iter); /* retrieve found session */
	}

	pthread_mutex_unlock(&pKE->mutex); 		/* ////// unlock ///// */

	return psession;
}

/* retrieves the session object for <KF, IP, port> if alive, or creates a new one */
SessionObject *getSessionObject(KadEngine *pKE, unsigned long int IP, unsigned short int port, int isServerSession) {
	SessionObject *psession = NULL;

	pthread_mutex_lock(&pKE->mutex);		/* \\\\\\ lock \\\\\\ */
	psession = retrieveSessionObject(pKE, IP, port, isServerSession);
	if(psession == NULL) {
		psession = newSessionObject(pKE, IP, port, isServerSession);
	}
	pthread_mutex_unlock(&pKE->mutex);		/* ////// unlock ///// */

	return psession;
}

/* remove a Server SO from the pKE->SessionsTable rbt and post it to the
   pKE->DeadServerSessionsFifo; if a client session, do not post it
   but instead also deallocate right away the resources by calling
   deallocSessionObject(). Server Session Objects
   are deallocated by a call to deallocSessionObject() by separate
   threads through reapDeadServerThreads(), and only after the
   Server Session has called destroySessionObject() . */

int destroySessionObject(SessionObject *psession) {
	rbt_StatusEnum rbt_status;
	queue *pdssq;
	int status;

	pthread_mutex_lock(&psession->pKE->mutex);	/* \\\\\\\ lock \\\\\\\ */
	/* remove it from the Kademlia Engine's Session Table */
	rbt_status = rbt_eraseKey(psession->pKE->SessionsTable, &psession->ID);
	pthread_mutex_unlock(&psession->pKE->mutex); /* ////// unlock ///// */
	if(rbt_status != RBT_STATUS_OK) {
#ifdef DEBUG
		KadC_log("In destroySessionObject(), rbt_eraseKey() failed returning %d\n", rbt_status);
#endif
		return 1;	/* error, SO not found in SessionsTable */
	}
	if(psession->ID.isServerSession) {			/* only for server sessions */
	/* Post server session object to DeadServerSessionsFifo for removal by reapDeadServerThreads */
		pdssq = psession->pKE->DeadServerSessionsFifo;
		status = pdssq->enq(pdssq, psession);
		if(status != 0) {
#ifdef DEBUG /* DEBUG ONLY */
			KadC_log("in destroySessionObject(), enq of session data to DSSQ failed returning %d\n", status);
			return 2;
#endif
		}
	} else {			/* only for client sessions */
		return deallocSessionObject(psession);	/* */
	}
	return 0;
}


/* Deallocates a Session Object posted to the pKE->DeadServerSessionsFifo
	by a suicidal server thread with destroySessionObject() before terminating;
	or can be called by Client Thread willing to terminate the session.
	The session is presumed to be dead (killed by destroySessionObject())
	If a server session, it also reaps the relative thread
	Client sessions don't have their pthread_t object stored
	in the Session Object and can destroy their SO by themselves */

static int deallocSessionObject(SessionObject *psession) {
	packet *pkt;

#ifdef DEBUG
		KadC_log("deallocSessionObject(%lx)%s\n", (unsigned long int)psession,
		(psession->ID.isServerSession?"(server session)":""));
#endif
	if(psession->ID.isServerSession) {			/* only for server  sessions */
		pthread_join(psession->thread, NULL); /* wait for thread to terminate */
		pthread_mutex_lock(&psession->pKE->mutex);	/* \\\\\\\ lock \\\\\\\ */
		psession->pKE->ServerThreadsPoolsize++;
		pthread_mutex_unlock(&psession->pKE->mutex); /* ///// unlock pKE //// */
	}
	if(psession->fifo == NULL) {	/* it should NEVER happen */
#ifdef DEBUG
		KadC_log("deallocSessionObject() found a NULL fifo field!!\n");
#endif
	} else {
		pthread_mutex_lock(&psession->mutex);	/* \\\\\\\ lock \\\\\\\ */
		while(psession->fifo->n > 0) {	/* empty FIFO */
			pkt = psession->fifo->deqw(psession->fifo);
			free(pkt->buf);	/* dealloc any buffer */
			free(pkt);			/* dealloc packet itself */
		}
		psession->fifo->destroy(psession->fifo);	/* dealloc FIFO */
		pthread_mutex_unlock(&psession->mutex); /* ////// unlock ///// */
	}

	pthread_mutex_destroy(&psession->mutex);
	free(psession);

	return 0;

}

/* Get the existing thread for <KF, IP, port>, or creates a new one (of client type) if
   none is found, and then sends an UDP datagram through the UDPIO
   using the given packet. Does NOT deallocate the packet. */

SessionObject *P2Psend(KadEngine *pKE, unsigned char *kadbuf, int kadbuflen, unsigned long int remoteip, unsigned short int remoteport) {
	int status;
	SessionObject *psession;
	if(isnonroutable(remoteip) || remoteip == pKE->extip || remoteip == pKE->localnode.ip)
		return NULL;
	psession = (SessionObject *)getSessionObject(pKE, remoteip, remoteport, 0);
	if(psession == NULL)
		return NULL;
	status = UDPsend(pKE->pul, kadbuf, kadbuflen, remoteip, remoteport);
	if(status >= 0)
		return psession;
	else {
		destroySessionObject(psession);	/* !!!! destroy before returning NULL ! */
		return NULL;
	}
}

/* Like P2Psend, but forces the creation of a new session */

SessionObject *P2PnewSessionsend(KadEngine *pKE, unsigned char *kadbuf, int kadbuflen, unsigned long int remoteip, unsigned short int remoteport) {
	int status;
	SessionObject *psession;
	if(isnonroutable(remoteip) || remoteip == pKE->extip || remoteip == pKE->localnode.ip) {
#ifdef DEBUG
		KadC_log("bad IP in P2PnewSessionsend(...%02x, %s:%u) \n", kadbuf[1], htoa(remoteip), remoteport);
#endif
		return NULL;
	}
	psession = (SessionObject *)newSessionObject(pKE, remoteip, remoteport, 0);
	if(psession == NULL) {
#ifdef VERBOSE_DEBUG
		KadC_log("new session creation failed in P2PnewSessionsend(...%02x, %s:%u) \n", kadbuf[1], htoa(remoteip), remoteport);
#endif
		return NULL;
	}
	status = UDPsend(pKE->pul, kadbuf, kadbuflen, remoteip, remoteport);
	if(status >= 0)
		return psession;
	else {
#ifdef DEBUG
		KadC_log("UDPsend returned %d in P2PnewSessionsend\n", status);
#endif
		destroySessionObject(psession);	/* !!!! destroy before returning NULL ! */
		return NULL;
	}
}

void SessionsTable_dump(KadEngine *pKE) {
	void *rbt = pKE->SessionsTable;
	void *iter;

	pthread_mutex_lock(&pKE->mutex);	/* \\\\\\ LOCK pKE \\\\\\ */
	for(iter = rbt_begin(rbt); iter != NULL; iter = rbt_next(rbt, iter)) {
		int i;
		SessionID *key = (SessionID *)rbt_key(iter);
		SessionObject *psession = (SessionObject *)rbt_value(iter);
		qnode *cur;
		KadC_log("%d>%s:%d: entry %lx {%d>%s:%d %s} fifo holds %d: (",
				key->KF, htoa(key->IP), key->port, (unsigned long)psession,
				psession->ID.KF, htoa(psession->ID.IP),
				psession->ID.port,
				(psession->ID.isServerSession? "(s)":"(c)"),
				psession->fifo->n);
		pthread_mutex_lock(&psession->mutex);	/* \\\\\\ LOCK session \\\\\\ */
		for(i=0, cur=psession->fifo->head; cur != NULL; i++, cur = cur->next) {
			packet *pkt = (packet *)cur->data;
			KadC_log("[%d]: %lx[%d]; ", i, (unsigned long int)pkt->buf, pkt->len);
		}
		pthread_mutex_unlock(&psession->mutex);	/* ///// UNLOCK session ///// */
		KadC_log(")\n");
	}
	pthread_mutex_unlock(&pKE->mutex);	/* ///// UNLOCK pKE ///// */
}
