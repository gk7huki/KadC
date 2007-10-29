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
#include <zlib.h>
#include <string.h>

#include <Debug_pthreads.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <queue.h>
#include <net.h>
#include <int128.h>
#include <rbt.h>
#include <KadCalloc.h>
#include <opcodes.h>
#include <KadCthread.h>
#include <KadCrouting.h>
#include <inifiles.h>
#include <KadClog.h>

#include <eMuleKAD.h>


int iseMuleREQ(unsigned char opcode) {
	switch(opcode) {
	case KADEMLIA_BOOTSTRAP_REQ:
	case KADEMLIA_HELLO_REQ:
	case KADEMLIA_FIREWALLED_REQ:
	case KADEMLIA_REQ:
	case KADEMLIA_SEARCH_REQ:
	case KADEMLIA_PUBLISH_REQ:
	case KADEMLIA_BUDDY_REQ:
		return 1;
	default:
		return 0;
	}
}

/* functions to get/put short/long unsigned integers
   and peernode structures from/to Kademlia packets.
   Against any common sense, the byte order is
   Intel host (little endian), not network...
   The byte order of the hash is even nuttier: 32107654...
   ppb is a pointer to the pointer in the buffer,
   which gets incremented as the buffer bytes are
   consumed.
 */
static unsigned short int getushort(unsigned char **ppb) {
	unsigned short u;
	u = *(*ppb)++;
	u += (*(*ppb)++)<<8 ;
	return u;
}
static unsigned long int getulong(unsigned char **ppb) {
	unsigned long u;
	u = *(*ppb)++;
	u += (*(*ppb)++)<<8;
	u += (*(*ppb)++)<<16;
	u += (*(*ppb)++)<<24;
	return u;
}

static int128 getint128(int128 hash, unsigned char **ppb) {
	int i, j;
	for(i=0; i<16; i+=4)
		for(j=3; j>=0; j--)
			hash[i+j] = *(*ppb)++;
	return hash;
}

static unsigned char *putushort(unsigned char **ppb, unsigned short int u) {
	*(*ppb)++ = (unsigned char)u; u = u>>8;
	*(*ppb)++ = (unsigned char)u;
	return *ppb;
}

static unsigned char *putulong(unsigned char **ppb, unsigned long int u) {
	*(*ppb)++ = (unsigned char)u; u = u>>8;
	*(*ppb)++ = (unsigned char)u; u = u>>8;
	*(*ppb)++ = (unsigned char)u; u = u>>8;
	*(*ppb)++ = (unsigned char)u;
	return *ppb;
}

static unsigned char *putint128(unsigned char **ppb, int128 hash) {
	int i, j;
	for(i=0; i<16; i+=4)
		for(j=3; j>=0; j--)
			*(*ppb)++ = hash[i+j];
	return *ppb;
}

static peernode *getpeernode(peernode *p, unsigned char **ppb) {
	getint128(p->hash, ppb);
	p->ip = getulong(ppb);
	p->port = getushort(ppb);
	p->tport = getushort(ppb);
	p->type = *(*ppb)++;
	return p;
}

static unsigned char *putpeernode(unsigned char **ppb, peernode *p) {
	putint128(ppb, p->hash);
	putulong(ppb, p->ip);
	putushort(ppb, p->port);
	putushort(ppb, p->tport);
	*(*ppb)++ = p->type;
	return *ppb;
}

/* "Client" eMule KAD-specific calls */

static SessionObject *sendEmKadPeerReq(int opcode, KadEngine *pKE, unsigned long int bootip, int bootport){
	unsigned char kadbuf[27];
	SessionObject *status;
	unsigned char *pb = kadbuf;

	*pb++ = OP_KADEMLIAHEADER;
	*pb++ = opcode;
	putpeernode(&pb, &pKE->localnode);

	status = P2PnewSessionsend(pKE, kadbuf, sizeof(kadbuf), bootip, bootport);
	return status;
}

/* Send a boot request UDP packet to a remote host
   returns 0 if OK or 1 if errors occurred
   then caller should inspect either errno or WSAGetLastError */

SessionObject *sendEmKadBootReq(KadEngine *pKE, unsigned long int bootip, int bootport){
	return sendEmKadPeerReq(KADEMLIA_BOOTSTRAP_REQ, pKE, bootip, bootport);
}

/* Send a hello request UDP packet to a remote host
   returns 0 if OK or 1 if errors occurred
   then caller should inspect either errno or WSAGetLastError */

SessionObject *sendEmKadHelloReq(KadEngine *pKE, unsigned long int bootip, int bootport){
	return sendEmKadPeerReq(KADEMLIA_HELLO_REQ, pKE, bootip, bootport);
}

/* Send a Firewall check request to a remote host
   declaring our own TCP port */

SessionObject *sendEmKadFwReq(KadEngine *pKE, int mytcpport, unsigned long int recipientip, int recipientport) {
	unsigned char kadbuf[4];
	SessionObject *status;
	unsigned char *pb = kadbuf;

	*pb++ = OP_KADEMLIAHEADER;
	*pb++ = KADEMLIA_FIREWALLED_REQ;
	putushort(&pb, (unsigned short int)mytcpport);
	status = P2PnewSessionsend(pKE, kadbuf, sizeof(kadbuf), recipientip, recipientport);
	return status;
}


/* Parse and dump to console an eMuleKAD packet */

int eMuleKadDumppkt(packet *pkt) {
	unsigned char *buf = pkt->buf;
	unsigned char *pb;
	int nrecvd = pkt->len;
	unsigned char opcode = buf[1];
	peernode *ppn;
	int i, npeers;
	unsigned long int myexternalip;
	int unknownOpcode = 0;

	switch(opcode) {
	case KADEMLIA_BOOTSTRAP_RES:
		if(nrecvd < 4)
			break;	/* at least must have 4 bytes */
		pb = &buf[2];
		npeers = getushort(&pb);
		if(nrecvd != 4 + npeers * 25) /* packet must contain npeers 25-byte peers */
			break;
		for(i=0; i < npeers; i++) {
			ppn = (peernode *)malloc(sizeof(peernode));
			getpeernode(ppn, &pb);
#ifdef DEBUG
			KadC_log("eMule KAD KADEMLIA_BOOTSTRAP_RES: peer %d of %d\n", i, npeers);
			KadC_log("hash: "); KadC_int128flog(stdout, (int128)(ppn->hash));
			KadC_log(" IP: %s", htoa(ppn->ip));
			KadC_log(" port: %d", ppn->port);
			KadC_log(" tport: %d", ppn->tport);
			KadC_log(" type: %d", ppn->type);
			KadC_log("\n");
#endif /* DEBUG */
			free(ppn); /* // remove this when ppn is put in bucket...*/

			/* now put peer in k_bucket (TBD) */

			/* The following  is not used for routing, but to estimate
			   the number of users. */
		}
		break;
	case KADEMLIA_HELLO_RES:
		if(nrecvd != 27)
			break;	/* fixed length response: only one peer */
		pb = &buf[2];
		ppn = (peernode *)malloc(sizeof(peernode));
		getpeernode(ppn, &pb);
#ifdef DEBUG
		KadC_log("eMule KAD KADEMLIA_HELLO_RES\n");
		KadC_log("hash: "); KadC_int128flog(stdout, (int128)(ppn->hash));
		KadC_log(" IP: %s", htoa(ppn->ip));
		KadC_log(" port: %d", ppn->port);
		KadC_log(" tport: %d", ppn->tport);
		KadC_log(" type: %d", ppn->type);
		KadC_log("\n");
#endif /* DEBUG */
		free(ppn); /* // remove this when ppn is put in bucket...*/

			/* now do something  (TBD) */
		break;
	case KADEMLIA_FIREWALLED_RES:
		pb = &buf[2];
		if(nrecvd != 6)
			break;	/* fixed length response: only one IP address */
		myexternalip = getulong(&pb);
#ifdef DEBUG
		KadC_log("eMule KAD KADEMLIA_FIREWALLED_RES\n");
		KadC_log(" My external IP: %s", htoa(myexternalip));
		KadC_log("\n");
#endif /* DEBUG */

			/* now do something  (TBD) */
		break;
	case KADEMLIA_FIREWALLED_ACK:
		if(nrecvd != 2)
			break;	/* fixed length response: no params returned */
#ifdef DEBUG
		KadC_log("eMule KAD KADEMLIA_FIREWALLED_ACK\n");
#endif /* DEBUG */

			/* now do something  (TBD) */
		break;
	case KADEMLIA_BOOTSTRAP_REQ:
		if(nrecvd != 27)
			break;	/* fixed length response: only one peer */
		pb = &buf[2];
		ppn = (peernode *)malloc(sizeof(peernode));
		getpeernode(ppn, &pb);
#ifdef DEBUG
		KadC_log("eMule KAD KADEMLIA_BOOTSTRAP_REQ\n");
		KadC_log("hash: "); KadC_int128flog(stdout, (int128)(ppn->hash));
		KadC_log(" IP: %s", htoa(ppn->ip));
		KadC_log(" port: %d", ppn->port);
		KadC_log(" tport: %d", ppn->tport);
		KadC_log(" type: %d", ppn->type);
		KadC_log("\n");
#endif /* DEBUG */
		free(ppn); /* // remove this when/if ppn is put in bucket...*/

			/* now do something  (TBD) */
		break;
	default:
#ifdef DEBUG /* DEBUG ONLY */
		KadC_log("eMuleKadDumppkt() can't recognize these %d bytes:\n", nrecvd);
		for(i=0; i < nrecvd; i++) {
			if((i % 16) == 0)
				KadC_log("\n");
			KadC_log("%02x ", buf[i]);
		}
		KadC_log("\n================================\n");
#endif /* DEBUG */

		unknownOpcode = 1;
	}
	return unknownOpcode;
}

/* When a new eMuleKAD server session starts, the following function
   is invoked under a dedicated thread. The first packet is passed
   as parameter; the code may send other packets to the peer
   and wait for same-session replies with pkt = getpkt(psession);
   packets obtained from getpkt() must be explicitly deallocated with
   "free(pkt->buf); free(pkt);" .The first packet will
   be automatically deallocated by the caller. */

void eMuleKADServerThread(SessionObject *psession, packet *pkt) {
	unsigned char *buf = pkt->buf;
	int nrecvd = pkt->len;
	unsigned char *pb;
	peernode *ppn;

	switch(buf[1]) {
	case KADEMLIA_HELLO_REQ:
		if(nrecvd != 27)
			break;	/* fixed length response: only one peer */
		pb = &buf[2];
		ppn = (peernode *)malloc(sizeof(peernode));
		getpeernode(ppn, &pb);
#ifdef DEBUG
		KadC_log("eMule KAD KADEMLIA_BOOTSTRAP_REQ\n");
		KadC_log("hash: "); KadC_int128flog(stdout, (int128)(ppn->hash));
		KadC_log(" IP: %s", htoa(ppn->ip));
		KadC_log(" port: %d", ppn->port);
		KadC_log(" tport: %d", ppn->tport);
		KadC_log(" type: %d", ppn->type);
		KadC_log("\n");
		free(ppn); /* // remove this when ppn is put in bucket...*/
#endif /* DEBUG */

		{
			unsigned char outbuf[27];
			unsigned char *pb = outbuf;
			int status;

			*pb++ = OP_KADEMLIAHEADER;
			*pb++ = KADEMLIA_HELLO_RES;
			putpeernode(&pb, &psession->pKE->localnode);
			status = sendbuf(psession, outbuf, sizeof(outbuf));
#ifdef DEBUG
			if(status != 0)
				KadC_log("sendbuf() returned %d while sending an KADEMLIA_HELLO_RES\n", status);
#endif /* DEBUG */
		}

		{
			pkt = getpkt(psession);
			if(pkt != NULL)
				destroypkt(pkt);
		}

			/* now maybe do something ;-) (TBD) */
		break;

	case KADEMLIA_BOOTSTRAP_REQ:
		if(nrecvd != 27)
			break;	/* fixed length response: only one peer */
		pb = &buf[2];
		ppn = (peernode *)malloc(sizeof(peernode));
		getpeernode(ppn, &pb);
#ifdef DEBUG
		KadC_log("eMule KAD KADEMLIA_BOOTSTRAP_REQ\n");
		KadC_log("hash: "); KadC_int128flog(stdout, (int128)(ppn->hash));
		KadC_log(" IP: %s", htoa(ppn->ip));
		KadC_log(" port: %d", ppn->port);
		KadC_log(" tport: %d", ppn->tport);
		KadC_log(" type: %d", ppn->type);
		KadC_log("\n");
		free(ppn); /* // remove this when ppn is put in bucket...*/
#endif /* DEBUG */

		{
			pkt = getpkt(psession);
			if(pkt != NULL)
				destroypkt(pkt);
		}
		KadC_log("End sleep\n");

			/* now maybe do something ;-) (TBD) */
		break;
	}
}



int eMuleKADinifileread(FILE *inifile, peernode *pmynode,
			peernode *contact[], int ncontacts) {
	char line[132];
	parblock pb;
	int nnodes = 0;

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
			if(p == NULL) {
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
		pmynode->ip = domain2hip(pb[1]);
		pmynode->port = (unsigned short int)atoi(pb[2]);
		pmynode->tport = (unsigned short int)atoi(pb[3]);
		pmynode->type = (unsigned char)atoi(pb[4]);
	}
	/* Read contacts from INI file */
	if(findsection(inifile, "[eMuleKAD_peers]") != 0) {
#ifdef DEBUG
		KadC_log("can't find [eMuleKAD_peers] section in KadCmain.ini\n");
#endif
		return -3;
	} else {
		int i;
		for(i=nnodes=0; nnodes < ncontacts; i++) {
			int npars;
			char *p = trimfgets(line, sizeof(line), inifile);
			if(p == NULL) {
				if(nnodes > 0)
					break;
			}
			npars = parseline(line, pb);
			if(pb[0][0] == '[') {
				break;;		/* start of new section */
			}
			if(pb[0][0] == '#') {
				continue;	/* skip comments */
			}
			if(npars != 5) {
#ifdef DEBUG
				KadC_log("bad format for contact %d lines after [eMuleKAD_peers]: skipping...\n", i);
#endif
				continue;
			}
			contact[nnodes] = (peernode *)malloc(sizeof(peernode));
			string2int128((int128)contact[nnodes]->hash, pb[0]);
			contact[nnodes]->ip = domain2hip(pb[1]);
			contact[nnodes]->port = (unsigned short int)atoi(pb[2]);
			contact[nnodes]->tport = (unsigned short int)atoi(pb[3]);
			contact[nnodes]->type = (unsigned char)atoi(pb[4]);
			nnodes++;
		}
		if(nnodes == 0) {
#ifdef DEBUG
			KadC_log("can't find data under [eMuleKAD_peers] section of KadCmain.ini\n");
#endif
			return -4;		/* EOF */
		}
		return nnodes;
	}

}

int eMuleKadCommandLoop(KadEngine *pKE) {

	while(1)
	{
		char line[512];
		char *p;
		char cmd[16], par1[80], par2[80], par3[80];
		int ntok;
		SessionObject *psession;
		packet *pkt;

		KadC_log("CMD>");
		p = fgets(line, sizeof(line) -1, stdin);
		if(p == NULL) /* EOF? */
			goto exit;
		if((p = strrchr(line, '\n')) != NULL) *p = 0;
		if((p = strrchr(line, '\r')) != NULL) *p = 0;
		if(*line == 0)
			continue; /* ignore empty lines */

		ntok = sscanf(line, "%15s%79s%79s%79s", cmd, par1, par2, par3);
		if(ntok == 0)
			continue; /* ignore empty lines */

		if(strcmp(cmd, "boot") == 0) {
			/* temporary: */
			psession = sendEmKadBootReq(pKE, domain2hip(par1), atoi(par2));
			pkt = getpkt(psession);
			if(pkt == NULL) { 	/* Timeout */
				KadC_log("*TIMEOUT*\n");
			} else {
				if(eMuleKadDumppkt(pkt))
					KadC_log("Response unknown\n");
				free(pkt->buf);
				free(pkt);
			}
			destroySessionObject(psession);
		} else if(strcmp(cmd, "hello") == 0) {
			psession = sendEmKadHelloReq(pKE, domain2hip(par1), atoi(par2));
			pkt = getpkt(psession);
			if(pkt == NULL) { 	/* Timeout */
				KadC_log("*TIMEOUT*\n");
			} else {
				if(eMuleKadDumppkt(pkt))
					KadC_log("Response unknown\n");
				free(pkt->buf);
				free(pkt);
			}
			destroySessionObject(psession);
		} else if(strcmp(cmd, "fwcheck") == 0) {
			/* temporary: */
			unsigned long int mytcpport = pKE->localnode.tport;
			psession = sendEmKadFwReq(pKE, mytcpport, domain2hip(par1), atoi(par2));

			pkt = getpkt(psession);
			if(pkt == NULL) { 	/* Timeout */
				KadC_log("*TIMEOUT*\n");
			} else {
				if(eMuleKadDumppkt(pkt))
					KadC_log("Response unknown\n");
				free(pkt->buf);
				free(pkt);

				pkt = getpkt(psession);
				if(pkt == NULL) { 	/* Timeout */
					KadC_log("*TIMEOUT*\n");
				} else {
					if(eMuleKadDumppkt(pkt))
						KadC_log("Response unknown\n");
					free(pkt->buf);
					free(pkt);
				}
			}
			destroySessionObject(psession);
		} else if(strcmp(cmd, "tmo") == 0) { /* force timeout on all sessions waiting for input */
			/* temporary: */
			{
				void *rbt = pKE->SessionsTable;
				void *iter;
				for(iter = rbt_begin(rbt); iter != NULL; iter = rbt_next(rbt, iter)) {
					SessionObject *psession = (SessionObject *)rbt_value(iter);
					KadC_log("Forcing timeout on entry %lx {%d>%s:%d %s} fifo: %lx\n",
							(unsigned long)psession,
							psession->ID.KF, htoa(psession->ID.IP),
							psession->ID.port,
							(psession->ID.isServerSession? "(s)":"   "),
							(unsigned long)psession->fifo
					);
					postbuffer2fifo(psession, NULL, 0); /* force timeout */
				}
			}
		} else if(strcmp(cmd, "gc") == 0) {
			/* temporary: */
			reapDeadServerThreads(pKE);
		} else if(strcmp(cmd, "dump") == 0) { /* dump various structures */
			/* temporary: */
			FILE *dumpfile = stdout;
			if(ntok > 1 && par1 != NULL && strlen(par1) > 0) {
				dumpfile = fopen(par1, "wt");
				if(dumpfile == NULL) {
					KadC_log("can't open for write the dump file %s\n", par1);
					continue;
				}
			}
			/* dump session table rbt */
			{
				int i;
				qnode *cur;

				void *rbt = pKE->SessionsTable;
				void *iter;
				KadC_log("------- Session Service Table -------\n");
				for(iter = rbt_begin(rbt); iter != NULL; iter = rbt_next(rbt, iter)) {
					int i;
					SessionID *key = (SessionID *)rbt_key(iter);
					SessionObject *psession = (SessionObject *)rbt_value(iter);
					qnode *cur;
					KadC_log("%d>%s:%d: entry %lx {%d>%s:%d %s} fifo holds %d: (",
							key->KF, htoa(key->IP), key->port, (unsigned long)psession,
							psession->ID.KF, htoa(psession->ID.IP),
							psession->ID.port,
							(psession->ID.isServerSession? "(s)":"   "),
							psession->fifo->n);
					pthread_mutex_lock(&psession->mutex);
					for(i=0, cur=psession->fifo->head; cur != NULL; i++, cur = cur->next) {
						packet *pkt = (packet *)cur->data;
						KadC_log("[%d]: %lx[%d]; ", i, (unsigned long int)pkt->buf, pkt->len);
					}
					pthread_mutex_unlock(&psession->mutex);
					KadC_log(")\n");
				}
				KadC_log("------- Dead Sessions Table -------\n");
				pthread_mutex_lock(&pKE->mutex);
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
					pthread_mutex_lock(&psession->mutex);
					for(j=0, cur1=psession->fifo->head; cur1 != NULL; j++, cur1 = cur1->next) {
						packet *pkt = (packet *)cur1->data;
						KadC_log("[%d]: %lx[%d]; ", j, (unsigned long int)pkt->buf, pkt->len);
					}
					pthread_mutex_unlock(&psession->mutex);
					KadC_log(")\n");
				}
				pthread_mutex_unlock(&pKE->mutex);
			}
			/* dump other stuff, as required */
			KadC_list_outstanding_mallocs(3);

		} else {
			KadC_log("Commands: boot host port | hello host port | fwcheck host port \n");
			/* UDPsend(&ul, (unsigned char *)line, strlen(line), ul.remoteip, ul.remoteport); */
		}


	}
exit:
	return 0;
}
