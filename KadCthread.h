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


typedef enum {
	EMULE,
	OVERNET,
	REVCONNECT,
	OTHER
} KadFlavour;

typedef struct _peernode {
	unsigned char hash[16];		/* MUST be at the beginning (some code depends on it) */
	unsigned long int ip;		/* in host byte order */
	unsigned short int port;	/* UDP port used for I/O */
	unsigned short int tport;	/* TCP port in eMule KAD etc. */
	unsigned char type;			/* number of successive failures to reply */
} peernode;

typedef struct _KadEngine {		/* one per flavour */
	KadFlavour KF;				/* Kademlia Flavour */
	void *SessionsTable;		/* rbt containing all the active sessions */
	pthread_mutex_t	mutex;		/* a mutex controlling access to the KE */
	int ServerThreadsPoolsize;	/* number of server threads that can be created at any time */
	queue *DeadServerSessionsFifo;/* pointer to FIFO holding dead server threads */
	UDPIO *pul;					/* pointer to UDPIO in charge for I/O */
	peernode localnode;			/* data of our node for this KE */
	unsigned long int extip;	/* IP address as seen from outside NAT */
	unsigned notfw;				/* 0 = we are NAT/firewalled, 1 = we aren't */
	void *kb[128];				/* array of kbuckets for contacts */
	void *kspace;				/* an rbt holding the same peers in the kb but indexed by hash */
	pthread_t BGth;				/* thread used for background stuff. */
	unsigned char shutdown;		/* if found != 0, any thread referencing this KE should cleanup and terminate */
	void *contacts;				/* rbt of boot contacts */
	int maxcontacts;			/* max number of items in contacts */
	pthread_mutex_t	cmutex;		/* a mutex controlling access to the contacts rbt */
	unsigned char fwstatuschecked;	/* 1 if we've already performed at least one fw check  */
	unsigned char leafmode;		/* if 1, we want to appear NATted even if we are not */
} KadEngine;

/* A Session is identified by a SessionID object */
typedef struct _SessionID {
	KadFlavour KF;				/* Kademlia Flavour */
	unsigned long int IP;		/* peer's IP address */
	unsigned short int port;	/* peer's UDP port */
	char isServerSession;		/* set to 1 if the thread is started by requests from a peer */
} SessionID;

/* Live sessions are associated to SessionObject objects */
typedef struct _SessionObject {
	SessionID ID;				/* embedded: that's NOT a pointer */
	queue *fifo;				/* pointer to FIFO holding input packets */
	pthread_mutex_t	mutex;		/* a mutex controlling access to this S.O. */
	pthread_t	thread;			/* Server Sessions run as this thread */
	KadEngine *pKE;				/* backpointer to Kad Engine */
} SessionObject;

typedef struct _packet {
	int len;
	unsigned char *buf;
} packet;

/* creates a new Session Object based on the given parameters
   return NULL if such session existed already */
SessionObject *newSessionObject(KadEngine *pKE, unsigned long int IP, unsigned short int port, int isServer);

/* retrieves an existing session object for <KF, IP, port, isServerSession>,
   or return NULL if there aren't any */
SessionObject *retrieveSessionObject(KadEngine *pKE, unsigned long int IP, unsigned short int port, int isServerSession);

/* retrieves the session object for <KF, IP, port, isServerSession> if alive, or creates a new one */
SessionObject *getSessionObject(KadEngine *pKE, unsigned long int IP, unsigned short int port, int isServerSession);

int destroySessionObject(SessionObject *psession);

KadEngine *startKadEngine(UDPIO *pul, KadFlavour KF);
int stopKadEngine(KadEngine *pKE);

int reapDeadServerThreads(KadEngine *pKE);
SessionObject *P2Psend(KadEngine *pKE, unsigned char *kpacket, int kpacketlen, unsigned long int remoteip, unsigned short int remoteport);
SessionObject *P2PnewSessionsend(KadEngine *pKE, unsigned char *kadbuf, int kadbuflen, unsigned long int remoteip, unsigned short int remoteport);

int postbuffer2fifo(SessionObject *psession, unsigned char *buf, int buflen);

packet *newpacket(unsigned char *buf, int buflen);
void destroypkt(packet *pkt);
int sendbuf(SessionObject *psession, unsigned char *buf, int buflen);
int sendpkt(SessionObject *psession, packet *pkt);
packet *getpkt(SessionObject *psession);

/* like getpkt, but the second parameter may define timeout in ms */
packet *getpktt(SessionObject *psession, int timeoutmilli);
packet *waitforpkt(SessionObject *psession, unsigned char opcode1, unsigned char opcode2);

void SessionsTable_dump(KadEngine *pKE);
