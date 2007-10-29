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
/* #define VERBOSE_DEBUG 1 */
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <Debug_pthreads.h>

#ifdef __WIN32__
#include <winsock.h>
#define socklen_t int
#else /* __WIN32__ */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define min(a,b) (((a)<(b))?(a):(b))
#include <errno.h>
#endif /* __WIN32__ */

#include <queue.h>
#include <net.h>
#include <KadCthread.h>
#include <int128.h>
#include <KadClog.h>

#include <tcpsrv.h>

/* does not update any field of pKE: all the returned values are
   in members of the structure pointed by ptcpsrvpar */

void *tcpsrv_th(void *p) {
	tcpsrvpar *ptcpsrvpar = p;
	KadEngine *pKE;

	int i, ssocket, csocket;
	int on = 1;
	int selectval;
	struct sockaddr_in csa, ssa;
	fd_set rset;
	struct timeval timeout;
	socklen_t cliLen = sizeof(csa);
	int gottcpconnection = 0;

	pKE = ptcpsrvpar->pKE;
	ptcpsrvpar->nbytes = 0;
	ssocket = socket(AF_INET, SOCK_STREAM, 0);
	if(ssocket >= 0 && setsockopt(ssocket, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on)) >= 0) {
		/* bind server port */
		ssa.sin_family = AF_INET;
		ssa.sin_addr.s_addr = htonl(pKE->localnode.ip);
		ssa.sin_port = htons(pKE->localnode.tport);

		if(bind(ssocket, (struct sockaddr *) &ssa, sizeof(ssa)) >= 0) {
			listen(ssocket,5);
			for(i=0; i < 10 * 2; i++) {	/* wait up to 10 seconds */
				if(pKE->shutdown || ptcpsrvpar->quit)
					break;
				FD_ZERO(&rset);
				timeout.tv_sec = 0;
				timeout.tv_usec = 500000;	/* 500 ms timeout */
				FD_SET(ssocket, &rset);
				selectval = select(ssocket+1, &rset, NULL, NULL, &timeout);
#ifdef DEBUG
				KadC_log("select() returned %d\n", selectval);
#endif
				if(selectval > 0) {
					csocket = accept(ssocket, (struct sockaddr *) &csa, &cliLen);
					if(csocket >= 0) {	/* connection accepted. don't bother to decode it. */
						char *buf = ptcpsrvpar->buf;
						int bufsize = ptcpsrvpar->bufsize;
						int nbytes;
						ptcpsrvpar->peerIP = ntohl(csa.sin_addr.s_addr);
#ifdef DEBUG
						KadC_log("in tcpsrv, connection accepted from %s \n", htoa(ptcpsrvpar->peerIP));
#endif
						if(buf != NULL) {
							do {
								nbytes = recv(csocket, buf, bufsize, 0);
							} while(nbytes < 0 && errno == EINTR);	/* wait outside recvfrom */
#ifdef DEBUG
							KadC_log("in tcpsrv, recv() from %s returned %d\n", htoa(ptcpsrvpar->peerIP), nbytes);
#endif
							ptcpsrvpar->nbytes = nbytes;
							if(ptcpsrvpar->outbuf != NULL) {
#ifdef MSG_NOSIGNAL
							send(csocket, ptcpsrvpar->outbuf, ptcpsrvpar->outnbytes, MSG_NOSIGNAL);
#else
							send(csocket, ptcpsrvpar->outbuf, ptcpsrvpar->outnbytes, 0);
#endif
							}
						}

#ifdef __WIN32__
						closesocket(csocket);
#else
						close(csocket);
#endif
						gottcpconnection = 1;
					}
					break;
				}
			}
#ifdef __WIN32__
			closesocket(ssocket);
#else
			close(ssocket);
#endif


			if(gottcpconnection) {
				ptcpsrvpar->notfw = 1;
#ifdef VERBOSE_DEBUG
				KadC_log("We are NOT TCP-firewalled!\n");
#endif
			} else {
				ptcpsrvpar->notfw = 0;
#ifdef VERBOSE_DEBUG
				KadC_log("Looks like we are TCP-firewalled...\n");
#endif
			}
		} else {
#if 1 /*def DEBUG */
			KadC_log("bind() to TCP port %u failed: in use?\n", pKE->localnode.tport);
#endif
		}
	} else {
#ifdef DEBUG
		KadC_log("TCP socket() or setsockopt() call failed!\n");
#endif
	}
	return NULL;
}
