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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <Debug_pthreads.h>
#include <assert.h>
#include <time.h>

#ifdef __WIN32__
#include <winsock.h>
#define socklen_t int
#else /* __WIN32__ */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define min(a,b) (((a)<(b))?(a):(b))
#include <errno.h>
#endif /* __WIN32__ */

#include <queue.h>
#include <int128.h>
#include <KadClog.h>
#include <rbt.h>

#include <net.h>

typedef struct _ip_port {
	unsigned long int ip;
	unsigned short int port;
	time_t expiry;
} ip_port;

/* compare ip_port structures by ip address, then port
 */
static int ip_port_eq(ip_port *ippt1, ip_port *ippt2) {
	return (ippt1->ip == ippt2->ip && ippt1->port == ippt2->port);
}

static int ip_port_lt(ip_port *ippt1, ip_port *ippt2) {
	if(ippt1->ip != ippt2->ip)
		return (ippt1->ip < ippt2->ip);
	else
		return (ippt1->port < ippt2->port);	/* if ip is the same, compare ports */
}

int node_is_blacklisted(UDPIO *pul, unsigned long int ip, unsigned short int port) {
	ip_port ippt, *val;
	int is_bl, missingsecs;
	void *iter;

	ippt.ip = ip;
	ippt.port = port;
	pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK \\\\\\ */
	is_bl = (rbt_find(pul->blacklist, &ippt, &iter) == RBT_STATUS_OK);
	if(is_bl) {
		val = rbt_value(iter);
		missingsecs = val->expiry - time(NULL);
		if(missingsecs <= 0) {	/* expired? */
			is_bl = 0;	/* then it ain't blacklisted */
			rbt_erase(pul->blacklist, iter);	/* remove record */
			free(val);						/* destroy it as well */
		} else {
			is_bl = missingsecs;
		}
	}
	pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK ///// */
	return is_bl;
}

int node_blacklist(UDPIO *pul, unsigned long int ip, unsigned short int port, int howmanysecs) {
	ip_port *pippt, *val;
	int was_bl, missingsecs;
	void *iter;

	pippt = malloc(sizeof(ip_port));
	assert(pippt != NULL);
	pippt->ip = ip;
	pippt->port = port;
	pippt->expiry = time(NULL)+howmanysecs;
	pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK \\\\\\ */
	was_bl = (rbt_find(pul->blacklist, pippt, &iter) == RBT_STATUS_OK);
	if(was_bl) {
		val = rbt_value(iter);
		missingsecs = val->expiry - time(NULL);
		if(missingsecs <= 0) 	/* expired? */
			was_bl = 0;	/* then it wasn't really blacklisted anymore... */
		else
			was_bl = missingsecs;
		val->expiry = pippt->expiry;	/* anyway just update expiry */
		free(pippt);			/* we don't need the new one anymore */
	} else {	/* no entry in table, yet? */
		rbt_insert(pul->blacklist, pippt, pippt, 0);	/* add to blacklist */
	}
	pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK ///// */
	return was_bl;
}

int node_unblacklist(UDPIO *pul, unsigned long int ip, unsigned short int port) {
	ip_port ippt, *val;
	int was_bl, missingsecs;
	void *iter;

	ippt.ip = ip;
	ippt.port = port;
	pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK \\\\\\ */
	was_bl = (rbt_find(pul->blacklist, &ippt, &iter) == RBT_STATUS_OK);
	if(was_bl) {
		val = rbt_value(iter);
		missingsecs = val->expiry - time(NULL);
		if(missingsecs <= 0) 	/* expired? */
			was_bl = 0;	/* then it wasn't really blacklisted anymore... */
		else
			was_bl = missingsecs;
		rbt_erase(pul->blacklist, iter);	/* anyway, remove record */
		free(val);
	}
	pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK ///// */
	return was_bl;
}

int node_blacklist_load(UDPIO *pul, FILE *inifile) {
	int cnt = 0;
	int ntok;
	char *p, line[256];
	char dqip[64];
	unsigned int port;
	int expiry, ttl;
	for(;;) {
		p = fgets(line, sizeof(line)-1, inifile);
		if(p == NULL)
			break;
		if(strchr(p, '[') != NULL)	/* if we just read new section start... */
			break;
		ntok = sscanf(line, "%63s%u%d", dqip, &port, &expiry);
		if(ntok != 3) {
#ifdef DEBUG
			KadC_log("Ignoring malformed line: %s\n", line);
#endif
			continue;
		}
		if(dqip[0] == '#')
			continue;	/* ignore comments */
		ttl = expiry - time(NULL);
		if(ttl <= 0) {	/* if already expired... */
#ifdef DEBUG
			KadC_log("Ignoring expired blacklisting for %s:%u\n", dqip, port);
#endif
			continue;				/* ...then ignore this entry */
		}
#ifdef DEBUG
		KadC_log("Blacklisted %s:%u will be snubbed for %d s\n", dqip, port, ttl);
#endif
		/* also domain names are OK here, besides dotted quads */
		node_blacklist(pul, domain2hip(dqip), port, ttl);
		cnt++;
	}
	return cnt;
}

int node_blacklist_dump(UDPIO *pul, FILE *wfile) {
	int cnt = 0;
	void *iter;
	pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK \\\\\\ */
	for(iter = rbt_begin(pul->blacklist);iter != NULL; iter=rbt_next(pul->blacklist, iter)) {
		ip_port *val = rbt_value(iter);
		assert(val != NULL);
		KadC_flog(wfile, "%s %u %d (t+%d)\n", htoa(val->ip), val->port, val->expiry, val->expiry - time(NULL));
		cnt++;
	}
	pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK ///// */
	return cnt;
}

int node_blacklist_purge(UDPIO *pul, int unconditional) {
	int cnt = 0;
	pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK \\\\\\ */
	for(;;) {
		ip_port *val;
		void *iter = rbt_begin(pul->blacklist);
		if(iter == NULL)
			break;
		val = rbt_value(iter);
		if(unconditional || (val->expiry - time(NULL) <= 0)) {
			rbt_erase(pul->blacklist, iter);
			free(val);
			cnt++;
		}
	}
	pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK ///// */
	return cnt;
}

/* Thread performing blocking recv() and calling callback when
   a datagram is received */

static void *udp_recv_thread(void *arg) {
	struct sockaddr_in remote;
	socklen_t sa_len = sizeof(struct sockaddr_in);
	UDPIO *pul = arg;

#ifdef VERBOSE_DEBUG
	KadC_log("\nIn udp_recv_thread:\n");
	KadC_log("pul->fd = %d, pul->bufsize = %d\n", pul->fd, pul->bufsize);
#endif

	while(1) {
		/* NOTE: if a datagram longer than t->size arrives,
		   the buffer is only filled in with the first t->size
		   characters; however:
		   - With Cygwin, the whole datagram size is returned
		     in nrecv, WITHOUT TRUNCATION
		   - With -mno-cygwin, nrecv returns -1
		 */
		/* the following select() works around a problem with OpenBSD
		   and possibly other BSD systems as well: close(fd) in one
		   thread hangs if another thread is waiting for input. So,
		   we make this thread wait in select(), BEFORE the recvfrom(). */
		int status;
		struct timeval timeout;
		int bl_seconds;
		udpmsg *pum;
		unsigned long int remoteip;
		unsigned short int remoteport;
		unsigned char *buf;
		int nrecv;

		fd_set rset;
		for(;;) {
			if(pul->fd == -1)
				goto exit_thread;
			FD_ZERO(&rset);
			FD_SET(pul->fd, &rset);
			timeout.tv_sec = 0;
			timeout.tv_usec = 500000;	/* 500 ms timeout */
			status = select(pul->fd+1, &rset, NULL, NULL, &timeout);
			if(status > 0){
				break;	/* data available, go read it */
			} else if(status < 0) {
#ifdef VERBOSE_DEBUG
# ifdef __WIN32__
				int nErrorID = WSAGetLastError();
				KadC_log("%d %s\n", nErrorID, WSAGetLastErrorMessage(nErrorID));
# else
				KadC_log("%d %s", errno, strerror(errno));
# endif
#endif

#ifdef __WIN32__
#else
				if(errno != EINTR)
					break;
#endif
			}
		};	/* wait outside recvfrom */

		buf = calloc(1, pul->bufsize);
		if(buf == NULL) {
			KadC_log("%s:%d: failed to allocate memory for buffer\n", __FILE__, __LINE__);
			goto exit_thread;
		}

		pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK UDPIO \\\\\\ */
		nrecv = recvfrom(
						pul->fd,
						(char *)buf,
						pul->bufsize - 1,
						0,
						(struct sockaddr *)&remote,
						&sa_len);
		pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK UDPIO ///// */
		if(nrecv > pul->bufsize - 1)
			nrecv = -1;	/* in UNIX as in WIN32 ignore oversize datagrams */
		if(nrecv <= 0) {/* ...catch oversize datagrams */
			goto next_iteration;
		}

		remoteip = ntohl(remote.sin_addr.s_addr);
		remoteport = ntohs(remote.sin_port);
		if((bl_seconds = node_is_blacklisted(pul, remoteip, remoteport)) != 0) {
#ifdef DEBUG
			KadC_log("*** Discarding datagram from node %s:%u blacklisted for %d more seconds\n",
						htoa(pul->remoteip), pul->remoteport, bl_seconds);
#endif
			goto next_iteration;
		}

		pum = calloc(1, sizeof(udpmsg));
		if(pum == NULL) {
			goto next_iteration;
		}

		pum->buf = malloc(nrecv);
		if(pum->buf == NULL) {
			free(pum);
			goto next_iteration;
		}

		pum->nrecv = nrecv;
		pum->remoteip = remoteip;
		pum->remoteport = remoteport;
		memcpy(pum->buf, buf, nrecv);

		/* enqueue the message for udp_proc_thread */
		status = pul->udp_recv_fifo->enq(pul->udp_recv_fifo, pum);

		if(status == 0) {
			/* if enq succeeded, just iterate (consumer thread will destroy message) */
			goto next_iteration;
		} else {
#ifdef DEBUG
			KadC_log("%s:%d ** UDP message enq failed: %d input datagram lost!\n", __FILE__, __LINE__, status);
#endif
			free(pum->buf);
			free(pum);	/* if enq failed, destroy message */
		}
next_iteration:
		free(buf);	/* in case of any errors, destroy old buffer */
	}
exit_thread:
	return NULL;
}

/* wait on queue for messages containing UDP data, length and
   source IP and port; then copy them to UDPIO and, if required,
   call callback */
static void *udp_proc_thread(void *arg) {
	UDPIO *pul = arg;
	udpmsg *pum;

	while(pul->fd != -1) {
		pum = pul->udp_recv_fifo->deqtw(pul->udp_recv_fifo, 500);
		if(pum == NULL) {
			continue;
		}

		pul->nrecv = pum->nrecv;
		pul->remoteip = pum->remoteip;
		pul->remoteport = pum->remoteport;
		pul->buf = pum->buf;
		free(pum);

		if(pul->callback[pul->buf[0]] != NULL)
			(*pul->callback[pul->buf[0]])(pul);
		free(pul->buf);
		pul->buf = NULL;
	}

	return NULL;
}


/* Creates a UDP socket bound to a specific interface and port
   (if the interface is 0, it binds to any).
   Sets the *pfd parameter used for recvfrom()/send() I/O
   Creates a separate thread to wait for inbound packets

   Associates to the socket an optional callback function
   called by the recvfrom()'ing thread when a datagram is
   received. A buffer containing the data is passed
   to that function; it won't be rewritten until
   that callback will return (as the callback routine
   belongs to the same thread issuing recvfrom() ).

   Returns 0, or errno/WSAGetLastError()

 */

int startUDPIO(UDPIO *pul) {
	int fd, on = 1;
	struct sockaddr_in local;
	static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;

	pul->blacklist = rbt_new((rbtcomp *)&ip_port_lt, (rbtcomp *)&ip_port_eq);
	pul->remoteip = 0;
	pul->remoteport = 0;
	pul->mutex = mutex_initializer;
	pul->udp_recv_fifo = new_queue(10);	/* max outstanding UDP packets */
	memset(&(local), 0, sizeof(struct sockaddr_in));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(pul->localip);
	local.sin_port = htons(pul->localport);

	if((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		goto err_ret;

	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on)) < 0)
		goto err_ret;

	if(bind(fd, (struct sockaddr *)&local, (socklen_t)sizeof(struct sockaddr_in)) < 0)
		goto err_ret;

	pul->fd = fd;

	pthread_create(&pul->_udp_recv, NULL, udp_recv_thread, pul);
	pthread_create(&pul->_udp_proc, NULL, udp_proc_thread, pul);

	return 0;

	err_ret:

#ifdef __WIN32__
	return WSAGetLastError();
#else
	return errno;
#endif
}

int stopUDPIO(UDPIO *pul) {
	int status;
	udpmsg *pum;

#ifdef __WIN32__
	/* under Winsock 1.1 should I call WSACancelBlockingCall() here? prob. no */
	status = closesocket(pul->fd);
#else
	status = close(pul->fd);
#endif
	pul->fd = -1;	/* tell the udp_recv and udp_proc threads that it's time to go... */
	pthread_join(pul->_udp_proc, NULL);	/* ensure the termination of udp_proc_thread */
	pthread_join(pul->_udp_recv, NULL);	/* ensure the termination of udp_recv_thread */

	if(pul->buf != NULL)
		free(pul->buf);
	/* cleanup any leftover in the udp message queue */
	while((pum = pul->udp_recv_fifo->deqtw(pul->udp_recv_fifo, 500)) != NULL) {
		free(pum->buf);
		free(pum);
	}
	pul->udp_recv_fifo->destroy(pul->udp_recv_fifo);
	node_blacklist_purge(pul, 1);	/* unconditionally empty blacklist */
	rbt_destroy(pul->blacklist);	/* destroy rbt */

	return status;
}

/* Call: domain2hip("some.domain.com");
   returns IP address in host byte order, or 0 if the domain name
   can't be resolved.
   Made MT safe through mutex locking.
 */

unsigned long int domain2hip(const char *domain) {
	static pthread_mutex_t __mutex = PTHREAD_MUTEX_INITIALIZER;
	struct hostent *hp;
	unsigned long int hip;	/* ip in host network order */


	if(domain[0] >= '0' && domain[0] <= '9') {
		hip = ntohl(inet_addr(domain));
	} else {

		pthread_mutex_lock(&__mutex);	/* begin critical area */
		hp = gethostbyname(domain);
		if(hp == NULL) {
#ifdef DEBUG
			KadC_log("\nCan't resolve hostname %s\n", domain);
#endif
			hip = 0xffffffff;
		} else {
			hip = ntohl(((unsigned long int *)(*hp->h_addr_list))[0]);
		}
#ifndef __WIN32__
		endhostent();
#endif
		pthread_mutex_unlock(&__mutex);	/* end critical area */
	}


	return hip;

}

static char *inet_ntoa_r(char *ip, struct in_addr sin_addr) {
	unsigned char *b = (unsigned char *)&sin_addr;
	sprintf(ip, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
	return ip;
}

/* blocking send() */
int UDPsend(UDPIO *pul, unsigned char *buf, int buflen, unsigned long int destip, int destport) {
	int status;
	struct sockaddr_in destsockaddr;
	int fd = pul->fd;

	memset(&destsockaddr, 0, sizeof(struct sockaddr_in));
	destsockaddr.sin_family = AF_INET;
	destsockaddr.sin_port   = htons((unsigned short int)destport);
	destsockaddr.sin_addr.s_addr = htonl(destip);

	/* this lock protects the fd from concurrent access
	   by separate threads either via sendto() recvfrom() */

	pthread_mutex_lock(&pul->mutex);	/* \\\\\\ LOCK UDPIO \\\\\\ */

	status = sendto(fd, (char *)buf, buflen,
		0, (struct sockaddr *)&destsockaddr,
		(socklen_t)sizeof(destsockaddr));

	pthread_mutex_unlock(&pul->mutex);	/* ///// UNLOCK UDPIO ///// */

	return status;
}
/* end blocking send() */

#if 0
char *htoa(unsigned long int ip) {
	struct in_addr ia;
	ia.s_addr = htonl(ip);
	return inet_ntoa(ia);
}
#else
char *htoa(unsigned long int ip) {
	static char s[32];	/* yeah, non-reentrant, and so is inet_ntoa()... */
	struct in_addr ia;
	ia.s_addr = htonl(ip);
	return inet_ntoa_r(s, ia);
}
#endif

/* returns true if the ip address is non-routable */
int isnonroutable(unsigned long int ip) {
#define q2b(a, b, c, d) (((a)<<24)+((b)<<16)+((c)<<8)+(d))
	return
			((ip & q2b(255,  0,0,0)) == q2b( 10,  0,0,0)) ||
			((ip & q2b(255,  0,0,0)) == q2b(127,  0,0,0)) ||
			((ip & q2b(255,240,0,0)) == q2b(172, 16,0,0)) ||
			((ip & q2b(255,255,0,0)) == q2b(192,168,0,0)) ||
			( ip                     == q2b(    0,0,0,0)) ||
			( ip                     == q2b(255,255,255,255));
}

/* returns true if the ip address (in host byte order) is assigned to a local interface */
int is_a_local_address(unsigned long int ip) {
	int sockfd;
	int status;
	struct sockaddr_in my_addr;

	if(ip == (unsigned long int)(-1))
		return 0;	/* here it means "inet_addr error", not "broadcast"! */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&(my_addr), 0, sizeof(struct sockaddr_in));
	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(ip);
	my_addr.sin_port = 0; 				/* any free port */
	status = bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr));
#ifndef __WIN32__
	close(sockfd);
#else
	closesocket(sockfd);
#endif
	return (status >= 0);
}

#ifdef __WIN32__
char *WSAGetLastErrorMessage(int nErrorID)
{
	static struct ErrorEntry {
		int nID;
		char* pcMessage;
	} gaErrorList[] = {
		{0,                  "No error"},
		{WSAEINTR,           "WSAEINTR: Interrupted system call"},
		{WSAEBADF,           "WSAEBADF: Bad file number"},
		{WSAEACCES,          "WSAEACCES: Permission denied"},
		{WSAEFAULT,          "WSAEFAULT: Bad address"},
		{WSAEINVAL,          "WSAEINVAL: Invalid argument"},
		{WSAEMFILE,          "WSAEMFILE: Too many open sockets"},
		{WSAEWOULDBLOCK,     "WSAEWOULDBLOCK: Operation would block"},
		{WSAEINPROGRESS,     "WSAEINPROGRESS: Operation now in progress"},
		{WSAEALREADY,        "WSAEALREADY: Operation already in progress"},
		{WSAENOTSOCK,        "WSAENOTSOCK: Socket operation on non-socket"},
		{WSAEDESTADDRREQ,    "WSAEDESTADDRREQ: Destination address required"},
		{WSAEMSGSIZE,        "WSAEMSGSIZE: Message too long"},
		{WSAEPROTOTYPE,      "WSAEPROTOTYPE: Protocol wrong type for socket"},
		{WSAENOPROTOOPT,     "WSAENOPROTOOPT: Bad protocol option"},
		{WSAEPROTONOSUPPORT, "WSAEPROTONOSUPPORT: Protocol not supported"},
		{WSAESOCKTNOSUPPORT, "WSAESOCKTNOSUPPORT: Socket type not supported"},
		{WSAEOPNOTSUPP,      "WSAEOPNOTSUPP: Operation not supported on socket"},
		{WSAEPFNOSUPPORT,    "WSAEPFNOSUPPORT: Protocol family not supported"},
		{WSAEAFNOSUPPORT,    "WSAEAFNOSUPPORT: Address family not supported"},
		{WSAEADDRINUSE,      "WSAEADDRINUSE: Address already in use"},
		{WSAEADDRNOTAVAIL,   "WSAEADDRNOTAVAIL: Can't assign requested address"},
		{WSAENETDOWN,        "WSAENETDOWN: Network is down"},
		{WSAENETUNREACH,     "WSAENETUNREACH: Network is unreachable"},
		{WSAENETRESET,       "WSAENETRESET: Net connection reset"},
		{WSAECONNABORTED,    "WSAECONNABORTED: Software caused connection abort"},
		{WSAECONNRESET,      "WSAECONNRESET: Connection reset by peer"},
		{WSAENOBUFS,         "WSAENOBUFS: No buffer space available"},
		{WSAEISCONN,         "WSAEISCONN: Socket is already connected"},
		{WSAENOTCONN,        "WSAENOTCONN: Socket is not connected"},
		{WSAESHUTDOWN,       "WSAESHUTDOWN: Can't send after socket shutdown"},
		{WSAETOOMANYREFS,    "WSAETOOMANYREFS: Too many references, can't splice"},
		{WSAETIMEDOUT,       "WSAETIMEDOUT: Connection timed out"},
		{WSAECONNREFUSED,    "WSAECONNREFUSED: Connection refused"},
		{WSAELOOP,           "WSAELOOP: Too many levels of symbolic links"},
		{WSAENAMETOOLONG,    "WSAENAMETOOLONG: File name too long"},
		{WSAEHOSTDOWN,       "WSAEHOSTDOWN: Host is down"},
		{WSAEHOSTUNREACH,    "WSAEHOSTUNREACH: No route to host"},
		{WSAENOTEMPTY,       "WSAENOTEMPTY: Directory not empty"},
		{WSAEPROCLIM,        "WSAEPROCLIM: Too many processes"},
		{WSAEUSERS,          "WSAEUSERS: Too many users"},
		{WSAEDQUOT,          "WSAEDQUOT: Disc quota exceeded"},
		{WSAESTALE,          "WSAESTALE: Stale NFS file handle"},
		{WSAEREMOTE,         "WSAEREMOTE: Too many levels of remote in path"},
		{WSASYSNOTREADY,     "WSASYSNOTREADY: Network system is unavailable"},
		{WSAVERNOTSUPPORTED, "WSAVERNOTSUPPORTED: Winsock version out of range"},
		{WSANOTINITIALISED,  "WSANOTINITIALISED: WSAStartup not yet called"},
		{WSAEDISCON,         "WSAEDISCON: Graceful shutdown in progress"},
		{WSAHOST_NOT_FOUND,  "WSAHOST_NOT_FOUND: Host not found"},
		{WSANO_DATA,         "WSANO_DATA: No host data of that type was found"}
	};
	const int kNumMessages = sizeof(gaErrorList) / sizeof(struct ErrorEntry);
	static char defaultmsg[] = "Unknown Winsock error";

	int i;

	for(i=0; i< kNumMessages; i++) {
		if(nErrorID == gaErrorList[i].nID)
			return gaErrorList[i].pcMessage;
	}
	return defaultmsg;
}

char *WSAGetLastErrorMessageOccurred(void) {
	return WSAGetLastErrorMessage(WSAGetLastError());
}


void wsockcleanup(void) {
	int status;
	status = WSACleanup();
	if(status) {
		KadC_log("WSACleanup returned %d: error %d - aborting...", status, WSAGetLastError());
	}
}

int wsockstart(void) {
	void wsockcleanup(void);
	int status;
	WSADATA wsaData;
	status = WSAStartup(MAKEWORD(1, 1), &wsaData);
	if(status) {
		return status;
	}
	atexit(&wsockcleanup);	/* and call WSACleanup() at exit */
	return 0;
}
#endif

