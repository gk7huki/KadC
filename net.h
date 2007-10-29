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


typedef struct _UDPIO {
	unsigned char *buf;		/* caller must set this */
	int bufsize;				/* caller must set this */
	unsigned long int localip;	/* caller must set this, in host byte order */
	int localport;	/* caller must set this, in host byte order */
	void (*callback[256])(struct _UDPIO *pul); /* caller must set this */
	void *arg[256];					/* caller must set this */
	unsigned long int totalbc;	/* bytecount */
	unsigned long int totalbw;
	unsigned long int totalmaxbw;
	int fd;	/* callback will find this set (by startUDPIO() ) */
	unsigned long int remoteip;	/* callback will find this set, in host byte order */
	int remoteport;	/* callback will find this set, in host byte order */
	int nrecv;		/* callback will find this set */
	/* private fields below */
	pthread_t _udp_recv;		/* UDP listener thread */
	pthread_t _udp_proc;		/* UDP processing thread */
	queue *udp_recv_fifo;	/* _udp_recv enqueues, _udp_proc dequeues */
	pthread_mutex_t mutex;	/* to prevent concurrent access from different threads */
	void *blacklist;	/* rbt of blacklisted nodes (contains <IP.UDPport> pairs) */
} UDPIO;

typedef struct _udpmsg {
	unsigned char *buf;
	int nrecv;
	unsigned long int remoteip;	/* callback will find this set, in host byte order */
	int remoteport;	/* callback will find this set, in host byte order */
} udpmsg;

/* creates a UDP listener calling callback(self) through a
   separate thread every time a UDP packet is received.
   The callback is guaranteed not to interrupt itself: it must
   return before its thread issue another recvfrom() and calls
   it again. To allow multiple listeners to work concurrently,
   all the context is kept in the UDPIO structure. */
int startUDPIO(UDPIO *pul);

/* closes the socket, abrting any pending or subsequent recvfrom()
   with an error code () which, when detected, will terminate the
   listening thread. After calling stopUDPIO, the program may
   safely free() the buffer pointed by buf (if malloc'ed) and then
   deallocate the memory of the UDPIO itself. */
int stopUDPIO(UDPIO *pul);

/* the fd may be (and usually is) the one opened by startUDPIO()
   in which case destip and destport may be copied from remoteip
   and remoteport, in order to answer incoming packets (e.g.,
   when UDPsend() is called by the listener's callback)
   returns: number of sent bytes, or <0 if error */
int UDPsend(UDPIO *pul, unsigned char *buf, int buflen, unsigned long int destip, int destport);

/* nErrorID is the code returned in WIN32 by WSAGetLastError() */
char *WSAGetLastErrorMessage(int nErrorID);
char *WSAGetLastErrorMessageOccurred(void);

unsigned long int domain2hip(const char *domain);
/* return the dot-quad ASCIIZ string corresponding to the
   IP address in HOST byte order */
char *htoa(unsigned long int ip);

/* returns true if the ip address (in host byte order) is non-routable */
int isnonroutable(unsigned long int ip);

/* returns true if the ip address (in host byte order) is assigned to a local interface */
int is_a_local_address(unsigned long int ip);

int node_is_blacklisted(UDPIO *pul, unsigned long int ip, unsigned short int port);

int node_blacklist(UDPIO *pul, unsigned long int ip, unsigned short int port, int howmanysecs);

int node_unblacklist(UDPIO *pul, unsigned long int ip, unsigned short int port);

/* write in ASCII to wfile the list of all entries in pul->blaclklisted */
int node_blacklist_dump(UDPIO *pul, FILE *wfile);

/* read from inifile the list of all entries to be loaded in pul->blaclklisted */
int node_blacklist_load(UDPIO *pul, FILE *inifile);

/* remove expired entried from pul->blacklisted rbt, or ALL entries if
   unconditional == 1. Return number of removed entries */
int node_blacklist_purge(UDPIO *pul, int unconditional);



#ifdef __WIN32__
int wsockstart(void);
void wsockcleanup(void);
#endif
