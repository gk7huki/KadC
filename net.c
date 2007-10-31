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

#include "queue.h"
#include "int128.h"
#include "logging.h"
#include "rbt.h"

#include "net.h"

struct _kc_udpIo {
	in_addr_t           localip;	/* our local IP */
	in_port_t           localport;	/* our local UDP port number */
	kc_ioCallback       callback;   /* function that will be called when data is available */
    void              * ref;        /* A user-defined value that will be passed back to the callback */
	
//    unsigned char     * buf;		/* will hold recieved udp data */
	int                 bufsize;	/* size of the above buffer */
    
	int                 fd;         /* the file descriptor used for communications */
    
	/* private fields below */
	pthread_t           _udp_recv;		/* UDP listener thread */
	pthread_t           _udp_proc;		/* UDP processing thread */
	queue             * udp_recv_fifo;	/* _udp_recv enqueues, _udp_proc dequeues */
	pthread_mutex_t     mutex;          /* to prevent concurrent access from different threads */
	RbtHandle         * blacklist;      /* rbt of blacklisted nodes (contains <IP.UDPport> pairs) */

#if 0 /* UNUSED ? */
	unsigned long int   totalbc;	/* bytecount */
	unsigned long int   totalbw;
	unsigned long int   totalmaxbw;
#endif    
};

typedef struct _ip_port {
	in_addr_t ip;
	in_port_t port;
	time_t expiry;
} ip_port;

static int
kc_udpIoLock( kc_udpIo * io );

static int
kc_udpIoUnlock( kc_udpIo * io );

/* compare ip_port structures by ip address, then port
 */
#if 0
static int ip_port_eq(ip_port *ippt1, ip_port *ippt2) {
	return (ippt1->ip == ippt2->ip && ippt1->port == ippt2->port);
}

static int ip_port_lt(ip_port *ippt1, ip_port *ippt2) {
	if(ippt1->ip != ippt2->ip)
		return (ippt1->ip < ippt2->ip);
	else
		return (ippt1->port < ippt2->port);	/* if ip is the same, compare ports */
}
#endif

/* TODO: Check this is equivalent to the above */
static int ip_port_cmp(const void *ippt1, const void *ippt2) {
    const ip_port *p1 = ippt1;
    const ip_port *p2 = ippt2;
    
    if ( p1->ip != p2->ip )
        return (p1->ip < p2->ip ? -1 : 1 );

	if(p1->port != p2->port)
		return (p1->port < p2->port ? -1 : 1 );
    
    return 0;
}

int node_is_blacklisted(kc_udpIo * pul, in_addr_t ip, in_port_t port) {
	ip_port ippt, *val;
	int is_bl, missingsecs;
	void *iter;

	ippt.ip = ip;
	ippt.port = port;
	kc_udpIoLock( pul );	/* \\\\\\ LOCK \\\\\\ */
	iter = rbtFind(pul->blacklist, &ippt);
	if(iter != NULL) {
		rbtKeyValue(pul->blacklist, iter, NULL, (void**)&val);
		missingsecs = val->expiry - time(NULL);
		if(missingsecs <= 0) {	/* expired? */
			is_bl = 0;	/* then it ain't blacklisted */
			rbtErase(pul->blacklist, iter);	/* remove record */
			free(val);						/* destroy it as well */
		} else {
			is_bl = missingsecs;
		}
	}
	kc_udpIoUnlock( pul );	/* ///// UNLOCK ///// */
	return is_bl;
}

int node_blacklist(kc_udpIo *pul, in_addr_t ip, in_port_t port, int howmanysecs) {
	ip_port *pippt, *val;
	int was_bl, missingsecs;
	void *iter;

	pippt = malloc(sizeof(ip_port));
	assert(pippt != NULL);
	pippt->ip = ip;
	pippt->port = port;
	pippt->expiry = time(NULL)+howmanysecs;
	kc_udpIoLock( pul );	/* \\\\\\ LOCK \\\\\\ */
	iter = rbtFind(pul->blacklist, pippt);
	if(iter != NULL) {
		rbtKeyValue(pul->blacklist, iter, NULL, (void**)&val );
		missingsecs = val->expiry - time(NULL);
		if(missingsecs <= 0) 	/* expired? */
			was_bl = 0;	/* then it wasn't really blacklisted anymore... */
		else
			was_bl = missingsecs;
		val->expiry = pippt->expiry;	/* anyway just update expiry */
		free(pippt);			/* we don't need the new one anymore */
	} else {	/* no entry in table, yet? */
		rbtInsert(pul->blacklist, pippt, pippt);	/* add to blacklist */
	}
	kc_udpIoUnlock( pul );	/* ///// UNLOCK ///// */
	return was_bl;
}

int node_unblacklist(kc_udpIo *pul, in_addr_t ip, in_port_t port) {
	ip_port ippt, *val;
	int was_bl, missingsecs;
	void *iter;

	ippt.ip = ip;
	ippt.port = port;
	kc_udpIoLock( pul );	/* \\\\\\ LOCK \\\\\\ */
	iter = rbtFind(pul->blacklist, &ippt);
	if(iter != NULL) {
		rbtKeyValue(pul->blacklist, iter, NULL, (void**)&val );
		missingsecs = val->expiry - time(NULL);
		if(missingsecs <= 0) 	/* expired? */
			was_bl = 0;	/* then it wasn't really blacklisted anymore... */
		else
			was_bl = missingsecs;
		rbtErase(pul->blacklist, iter);	/* anyway, remove record */
		free(val);
	}
	kc_udpIoUnlock( pul );	/* ///// UNLOCK ///// */
	return was_bl;
}

int node_blacklist_load(kc_udpIo *pul, FILE *inifile) {
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
			kc_logPrint( KADC_LOG_DEBUG, "Ignoring malformed line: %s\n", line );
			continue;
		}
		if(dqip[0] == '#')
			continue;	/* ignore comments */
		ttl = expiry - time(NULL);
		if(ttl <= 0) {	/* if already expired... */
			kc_logPrint( KADC_LOG_DEBUG, "Ignoring expired blacklisting for %s:%u\n", dqip, port );
			continue;				/* ...then ignore this entry */
		}

		kc_logPrint( KADC_LOG_DEBUG, "Blacklisted %s:%u will be snubbed for %d s\n", dqip, port, ttl );

		/* also domain names are OK here, besides dotted quads */
		node_blacklist(pul, gethostbyname_s(dqip), port, ttl);
		cnt++;
	}
	return cnt;
}

int node_blacklist_dump(kc_udpIo *pul, FILE *wfile) {
	int cnt = 0;
	void *iter;
	kc_udpIoLock( pul );	/* \\\\\\ LOCK \\\\\\ */
	for(iter = rbtBegin(pul->blacklist);iter != NULL; iter=rbtNext(pul->blacklist, iter)) {
		ip_port *val;
        rbtKeyValue(pul->blacklist, iter, NULL, (void**)&val );
		assert(val != NULL);
        struct in_addr ad;
        ad.s_addr = val->ip;
/* FIXME */
// KadC_flog(wfile, "%s %u %d (t+%d)\n", inet_ntoa( ad ), val->port, val->expiry, val->expiry - time(NULL));
		cnt++;
	}
	kc_udpIoUnlock( pul );	/* ///// UNLOCK ///// */
	return cnt;
}

int
node_blacklist_purge( kc_udpIo *pul, int unconditional )
{
	int cnt = 0;
	kc_udpIoLock( pul );	/* \\\\\\ LOCK \\\\\\ */
	for(;;) {
		ip_port *val;
		void *iter = rbtBegin(pul->blacklist);
		if(iter == NULL)
			break;
		rbtKeyValue(pul->blacklist, iter, NULL, (void**)&val );
		if(unconditional || (val->expiry - time(NULL) <= 0)) {
			rbtErase(pul->blacklist, iter);
			free(val);
			cnt++;
		}
	}
	kc_udpIoUnlock( pul );	/* ///// UNLOCK ///// */
	return cnt;
}

/* Thread performing blocking recv() and calling callback when
   a datagram is received */
static void *
udp_recv_thread( void *arg )
{
	kc_udpIo          * pul = arg;
    unsigned char     * buf;

	kc_logPrint( KADC_LOG_VERBOSE, "udp_recv_thread: pul->fd = %d, pul->bufsize = %d\n", pul->fd, pul->bufsize );


	while( 1 )
    {
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
		int             status;
		struct timeval  timeout;
        
		fd_set rset;
        
        if( pul->fd == -1 )
            return NULL;
        
        FD_ZERO( &rset );
        FD_SET( pul->fd, &rset );
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;	/* 500 ms timeout */
        
        status = select( pul->fd + 1, &rset, NULL, NULL, &timeout );
        if( status <= 0 )
        {
            if ( status != 0 ) {
# ifdef __WIN32__
				kc_logPrint( KADC_LOG_VERBOSE, "%d %s\n", WSAGetLastError(), WSAGetLastErrorMessageOccurred());
# else
				kc_logPrint( KADC_LOG_VERBOSE, "%d %s\n", errno, strerror(errno));
# endif
            }
            // There was an error, or we timeout, try reading again...
            continue;
        }
        
#ifdef __WIN32__
#else
        if( errno == EINTR )
            continue;
#endif
        
		buf = calloc( pul->bufsize, sizeof( char*) );
		if( buf == NULL )
        {
			kc_logPrint(KADC_LOG_NORMAL, "%s:%d: failed to allocate memory for buffer\n", __FILE__, __LINE__);
			return NULL;
		}
        else
        {
            int             bl_seconds;
            kc_udpMsg     * pum;
            in_addr_t       remoteip;
            in_port_t       remoteport;
            struct sockaddr_in  remote;
            int             nrecv;
            socklen_t       sa_len = sizeof(struct sockaddr_in);
            
            kc_udpIoLock( pul );      /* \\\\\\ LOCK kc_udpIo \\\\\\ */
            
            nrecv = recvfrom( pul->fd, buf, pul->bufsize - 1, 0, (struct sockaddr *)&remote, &sa_len);
            
            
            kc_udpIoUnlock( pul );	/* ///// UNLOCK kc_udpIo ///// */
            
            if( nrecv > pul->bufsize - 1 )
                nrecv = -1;	/* in UNIX as in WIN32 ignore oversize datagrams */
            
            /* ...catch oversize datagrams */
            if( nrecv <= 0 )
            {
                free( buf );
                continue;
            }
            
            remoteip = remote.sin_addr.s_addr;
            remoteport = remote.sin_port;
#if 0
            if ( ( bl_seconds = node_is_blacklisted( pul, remoteip, remoteport )) != 0) {
#ifdef DEBUG
                struct in_addr ad;
                ad.s_addr = remoteip;
                kc_logPrint("udp_recv_thread: Discarded datagram from blacklisted node %s:%u (%d seconds left)\n",
                         inet_ntoa( ad ), remoteport, bl_seconds);
#endif
                free( buf );
                continue;
            }
#endif
            pum = calloc( 1, sizeof(kc_udpMsg) );
            if ( pum == NULL )
            {
                free( buf );
                continue;
            }
            
            pum->payload = malloc(nrecv);
            if ( pum->payload == NULL )
            {
                free( pum );
                free( buf );
                continue;
            }
            
            pum->payloadSize = nrecv;
            pum->remoteIp = remoteip;
            pum->remotePort = remoteport;
            memcpy( pum->payload, buf, nrecv );
            
            /* enqueue the message for udp_proc_thread */
            status = pul->udp_recv_fifo->enq( pul->udp_recv_fifo, pum );
            
            if( status != 0 )
            {
#ifdef DEBUG
                kc_logPrint(KADC_LOG_DEBUG, "%s:%d ** UDP message enq failed: %d input datagram lost!\n", __FILE__, __LINE__, status);
#endif
                free( pum->payload );
                free( pum );	/* if enq failed, destroy message */
                free( buf );
                continue;
            }
        }
        // We've finished recieving another packet, free the buffer we've used...
		free( buf );
	}
    free( buf );
    
	return NULL;
}

/* wait on queue for messages containing UDP data, length and
   source IP and port; then copy them to kc_udpIo and, if required,
   call callback */
static void *
udp_proc_thread( void *arg )
{
	kc_udpIo  * pul = arg;
	kc_udpMsg * pum;

	while( pul->fd != -1 )
    {
		pum = pul->udp_recv_fifo->deqtw(pul->udp_recv_fifo, 500);
		if( pum == NULL )
			continue;

		if( pul->callback != NULL )
			(*pul->callback)( pul->ref, pul, pum );
		free( pum->payload );
        free( pum );
	}

	return NULL;
}

static int
kc_udpIoLock( kc_udpIo * io )
{
    return pthread_mutex_lock( &io->mutex );
}

static int
kc_udpIoUnlock( kc_udpIo * io )
{
    return pthread_mutex_unlock( &io->mutex );
}

kc_udpIo *
kc_udpIoInit( in_addr_t addr, in_port_t port, int bufferSize, kc_ioCallback callback, void * ref )
{
	int fd, on = 1;
	struct sockaddr_in local;
	static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;

    kc_udpIo * pul = (kc_udpIo*)malloc( sizeof(kc_udpIo) );
    if ( pul == NULL )
    {
#ifdef VERBOSE_DEBUG
        kc_logPrint( KADC_LOG_VERBOSE, "kc_udpIoInit: failed malloc !\n" );
#endif
        return NULL;
    }
    
    pul->localip = addr;
    pul->localport = port;
    
    pul->bufsize = bufferSize;
//    pul->buf = calloc( pul->bufsize, sizeof(char) );
    
    pul->callback = callback;
    pul->ref = ref;
    
	pul->blacklist = rbtNew( ip_port_cmp );
    
	pul->mutex = mutex_initializer;
	pul->udp_recv_fifo = new_queue(10);	/* max outstanding UDP packets */
    
    pul->_udp_recv = NULL;
    pul->_udp_proc = NULL;

	if( ( fd = socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0)
    {
#ifdef __WIN32__
        kc_logPrint( KADC_LOG_VERBOSE, "kc_udpIoInit: failed opening socket (%d:%s) !\n", WSAGetLastError(), WSAGetLastErrorMessageOccurred() );
#else
        kc_logPrint( KADC_LOG_VERBOSE, "kc_udpIoInit: failed opening socket (%d:%s) !\n", errno, strerror(errno) );
#endif
        kc_udpIoFree( pul );
        return NULL;
    }

	if( setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on) ) < 0)
    {
#ifdef __WIN32__
        kc_logPrint( KADC_LOG_VERBOSE, "kc_udpIoInit: failed setting socket options (%d:%s) !\n", WSAGetLastError(), WSAGetLastErrorMessageOccurred() );
#else
        kc_logPrint( KADC_LOG_VERBOSE, "kc_udpIoInit: failed setting socket options (%d:%s) !\n", errno, strerror(errno) );
#endif
        kc_udpIoFree( pul );
        return NULL;
    }
    
	memset(&(local), 0, sizeof(struct sockaddr_in));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl( pul->localip );
    local.sin_port = htons( pul->localport );

	if( bind( fd, (struct sockaddr*) &local, sizeof(struct sockaddr) ) < 0)
    {
#ifdef __WIN32__
        kc_logPrint( KADC_LOG_VERBOSE, "kc_udpIoInit failed binding to port (%d:%s) !\n", WSAGetLastError(), WSAGetLastErrorMessageOccurred() );
#else
        kc_logPrint( KADC_LOG_VERBOSE, "kc_udpIoInit failed binding to port (%d:%s) !\n", errno, strerror(errno) );
#endif
        kc_udpIoFree( pul );
        return NULL;
    }

	pul->fd = fd;

	pthread_create( &pul->_udp_recv, NULL, udp_recv_thread, pul );
	pthread_create( &pul->_udp_proc, NULL, udp_proc_thread, pul );

	return pul;
}

void
kc_udpIoFree(kc_udpIo *pul)
{
	int         tempFd;
    int         status;
	kc_udpMsg * pum;

    assert( pul != NULL );
    
    tempFd = pul->fd;
    /* tell the udp_recv and udp_proc threads that it's time to go... */
	pul->fd = -1;
    
    /* ... then we wait for them at the corner */
    if ( pul->_udp_proc != NULL )
        pthread_join( pul->_udp_proc, NULL);
    if ( pul->_udp_recv != NULL )
        pthread_join( pul->_udp_recv, NULL);
    
#ifdef __WIN32__
	/* under Winsock 1.1 should I call WSACancelBlockingCall() here? prob. no */
	status = closesocket(tempFd);
#else
	status = close(tempFd);
#endif
    if ( status != 0 )
    {
        kc_logPrint( KADC_LOG_VERBOSE, "kc_udpIoFree: error %d closing fd\n", status );
    }
    
//	if( pul->buf != NULL )
//		free( pul->buf );
    
	/* cleanup any leftover in the udp message queue */
	while((pum = pul->udp_recv_fifo->deqtw(pul->udp_recv_fifo, 500)) != NULL) {
		free(pum->payload);
		free(pum);
	}
    
	pul->udp_recv_fifo->destroy(pul->udp_recv_fifo);
	node_blacklist_purge(pul, 1);	/* unconditionally empty blacklist */
	rbtDelete(pul->blacklist);	/* destroy rbt */
    
    free( pul );
}

/* Call: gethostbyname_s("some.domain.com");
   returns IP address in host byte order, or 0 if the domain name
   can't be resolved.
   Made MT safe through mutex locking.
 */
in_addr_t
gethostbyname_s(const char *domain)
{
	static pthread_mutex_t __mutex = PTHREAD_MUTEX_INITIALIZER;
	struct hostent *hp;
	in_addr_t hip;	/* ip in host network order */

	if(domain[0] >= '0' && domain[0] <= '9') {
		hip = ntohl( inet_addr(domain) );
	} else {

		pthread_mutex_lock(&__mutex);	/* begin critical area */
		hp = gethostbyname( domain );
		if(hp == NULL) {

			kc_logPrint( KADC_LOG_DEBUG, "Can't resolve hostname %s\n", domain );

			hip = 0xffffffff;
		} else {
			hip = ntohl( ((unsigned long int *)(*hp->h_addr_list))[0] );
		}
#ifndef __WIN32__
		endhostent();
#endif
		pthread_mutex_unlock( &__mutex );	/* end critical area */
	}

	return hip;

}

/* blocking send() */
int
kc_udpIoSendMsg( kc_udpIo * io, kc_udpMsg * msg )
{
	int status;
	struct sockaddr_in destsockaddr;
	int fd = io->fd;

	memset( &destsockaddr, 0, sizeof(struct sockaddr_in) );
	destsockaddr.sin_family = AF_INET;
	destsockaddr.sin_port   = htons( msg->remotePort );
	destsockaddr.sin_addr.s_addr = htonl( msg->remoteIp );
    
	/* this lock protects the fd from concurrent access
	   by separate threads either via sendto() recvfrom() */

	kc_udpIoLock( io );

	status = sendto( fd, msg->payload, msg->payloadSize, 0,
                     (struct sockaddr *)&destsockaddr, sizeof(destsockaddr) );

	kc_udpIoUnlock( io );
    struct in_addr ad;
    ad.s_addr = htonl( msg->remoteIp );
    
//    kc_logPrint( KADC_LOG_VERBOSE, "Sent UDP message to %s:%d", inet_ntoa( ad ), msg->remotePort );
    
	return status;
}
/* end blocking send() */

/* returns true if the ip address is non-routable */
int isnonroutable(in_addr_t ip) {
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
int is_a_local_address(in_addr_t ip) {
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
		kc_logPrint("WSACleanup returned %d: error %d - aborting...\n", status, WSAGetLastError());
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

