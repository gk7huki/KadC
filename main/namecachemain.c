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

#define arraysize(a) (sizeof(a)/sizeof(a[0]))
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

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

#include <Debug_pthreads.h>
#include <int128.h>
#include <rbt.h>
#include <queue.h>
#include <KadCalloc.h>
#include <KadClog.h>
#include <millisleep.h>
#include <net.h>	/* for htoa() */
#include <KadCapi.h>
#include <dns.h>
#include <droppriv.h>

#include <config.h>

#define DNS_PORT 53

typedef struct _DNSIO {
	int fd;
	unsigned long int ip;
	unsigned long int port;
	unsigned long int upstream[4];	/* upstream DNS servers array */
	int nupstream;					/* number of upstream DNS servers */
	pthread_mutex_t mutex;
	queue *fifo;
	int err;
	int shutdown_flag;		/* set by signal handler, protected by mutex */
#if !defined(__WIN32__) && !defined(__CYGWIN__)
	int uid;				/* uid to fall back to in order to drop root privileges after bind() under UNIX */
	int gid;				/* gid to fall back to in order to drop root privileges after bind() under UNIX */
#endif
} DNSIO;

typedef struct _processing_thread_params {
	KadCcontext *pkcc;
	DNSIO *pdnsio;
	char *tlpd[100];
	int ntlpd;
	char *my_pseudo[100];
	int nmy_pseudo;
	void *pending_q_rbt;	/* table of raw_qa structs for pending questions  by question */
	void *cache_q_rbt;		/* table indexing raw_qa (question/answer pairs) by question */
	int cache_maxentries;	/* typically 4096 */
	pthread_mutex_t mutex;
} processing_thread_params;

/* a structure holding DNS client's address/port, a pointer to a buffer
   containing the raw DNS message as it is in the UDP payload, and
   the number of bytes in that buffer.  For packets exchanged over TCP,
   fd contains the fd of the TCP connection, otherwise it's -1.
 */
typedef struct _DNSpacket {
	unsigned long int remoteip;
	unsigned short int remoteport;
	int fd;
	int bufsize;
	unsigned char *buf;
	int qsize;	/* length of questions area inside buf */
} DNSpacket;

/* a structure containing one "query" and one "response" DNSpackets,
   and an expiration timestamp */
typedef struct _raw_qa {
	DNSpacket *q;
	DNSpacket *a;
	time_t expiry;
	time_t last_accessed;
	int being_refreshed;
	/* the fields below are used only by detached queries (which call the P2P backend) */
	int isdetached;
	pthread_t thread;				/* the thread servicing the P2P query */
	pthread_mutex_t mutex;			/* protects critical areas in thread routine code */
	processing_thread_params *pptp;	/* useful to access e.g. the fifo in the DNSIO block */
	int done;	/* set by thread routine just before terminating: tells that pthread_join won't block for long */
} raw_qa;

/* header is 12 byte long, and the question(s) come next */
#define HS 12

/* compare DNSpacket structures by raw_question area (1st question only)
 */
/* if the sizes are the same, compare the content; else return false */
static int DNSpacket_q_eq(DNSpacket *pdnp1, DNSpacket *pdnp2) {
	if(pdnp1->qsize != pdnp2->qsize)
		return 0;
	else
		return (memcmp(pdnp1->buf + HS, pdnp2->buf + HS, pdnp1->qsize) == 0);
}
/* if the sizes are the same, compare the content; else compare question sizes */
static int DNSpacket_q_lt(DNSpacket *pdnp1, DNSpacket *pdnp2) {
	if(pdnp1->qsize != pdnp2->qsize)
		return pdnp1->qsize < pdnp2->qsize;
	else
		return (memcmp(pdnp1->buf + HS, pdnp2->buf + HS, pdnp1->qsize) < 0);
}

/* compare raw_qa structures by raw_question AND by ID (important for pending queries table!) */
/* if the sizes are the same, compare the content; else return false */
static int DNSpacket_qi_eq(DNSpacket *pdnp1, DNSpacket *pdnp2) {
	if(pdnp1->qsize != pdnp2->qsize)
		return 0;
	else
		return (memcmp(pdnp1->buf + HS, pdnp2->buf + HS, pdnp1->qsize) == 0 &&
				memcmp(pdnp1->buf, pdnp2->buf, 2) == 0);
}
/* if the sizes are the same, compare the content (incl. ID); else compare question sizes */
static int DNSpacket_qi_lt(DNSpacket *pdnp1, DNSpacket *pdnp2) {
	if(pdnp1->qsize != pdnp2->qsize)
		return pdnp1->qsize < pdnp2->qsize;
	else {
		int compare_id = memcmp(pdnp1->buf, pdnp2->buf, 2);
		if(compare_id != 0)
			return compare_id;
		else
			return (memcmp(pdnp1->buf + HS, pdnp2->buf + HS, pdnp1->qsize) < 0);
	}
}

static int qa_rbt_destroy(void *rbt, int destroy_data_too) {
	void *iter;
	raw_qa *prqa;
	rbt_StatusEnum rbt_status;
	for(;;) {
		iter = rbt_begin(rbt);
		if(iter == NULL)
			break;
		prqa = rbt_value(iter);
		rbt_erase(rbt, iter);
		if(destroy_data_too && (prqa != NULL)) {
			if(prqa->q != NULL) {
				if(prqa->q->buf != NULL)
					free(prqa->q->buf);
				free(prqa->q);
			}
			if(prqa->a != NULL) {
				if(prqa->a->buf != NULL)
					free(prqa->a->buf);
				free(prqa->a);
			free(prqa);
			}
		}
	}
	rbt_status = rbt_destroy(rbt);
	if(rbt_status == RBT_STATUS_OK)
		return 0;
	else
		return 1;
}

/* conversion routines between DNSpacket (which contains a packed buffer
   together with peer information which is not retained) and the
   parsed dns_msg defined in dns.h
 */
static dns_msg *DNSpacket2msg(DNSpacket *pdnp) {
	return dns_parse((char *)pdnp->buf, pdnp->bufsize);
}

static DNSpacket *DNSmsg2packet(dns_msg *pdm, unsigned long int ip, unsigned short int port, int fd) {
	DNSpacket *pdnp;
	int status;

	pdnp = malloc(sizeof(DNSpacket));
	assert(pdnp != NULL);
	pdnp->bufsize = 4096;	/* preliminary, vastly oversized */
	pdnp->buf = malloc(pdnp->bufsize);
	assert(pdnp->buf != NULL);
	status = dns_pack((char *)pdnp->buf, pdnp->bufsize, pdm);
	assert(status > 0);
/* we should also check if status > 512, and, if so, limit to 512 and set
   the truncation bit:  */
#if 1
	if(status > 512) {
#ifdef DEBUG
		KadC_log("Truncation occurred: %d bytes reduced to 512\n", status);
#endif
		status = 512;
		pdnp->buf[2] |= (1 << 1);
	}
#endif
	pdnp->bufsize = status;
	pdnp->buf = realloc(pdnp->buf, pdnp->bufsize);	/* trim down to what it's necessary */
	pdnp->remoteip = ip;
	pdnp->remoteport = port;
	pdnp->fd = fd;
	return pdnp;
}

static void DNSpacket_destroy(DNSpacket *pdnp) {
	if(pdnp != NULL) {
		if(pdnp->buf != NULL)
			free(pdnp->buf);
		free(pdnp);
	}
}

/* for detached P2P queries, it also reaps the thread used in the search */
static void raw_qa_destroy(raw_qa *pqa) {
	if(pqa != NULL) {
		if(pqa->isdetached) {
			pthread_join(pqa->thread, NULL);
		}
		pthread_mutex_destroy(&pqa->mutex);
		if(pqa->q != NULL)
			DNSpacket_destroy(pqa->q);
		if(pqa->a != NULL)
			DNSpacket_destroy(pqa->a);
		free(pqa);
	}
}

static int trim_qa_rbt(void *rbt, int maxentries) {
	int removed_entries = 0;
	rbt_StatusEnum rbt_status;
	while(rbt_size(rbt) > maxentries) {
		/* cache full: let's expire the oldest record */
		void *iter = NULL;
		raw_qa *soonest_to_expire_qa = NULL;
		for(iter=rbt_begin(rbt); iter != NULL; iter = rbt_next(rbt, iter)) {
			raw_qa *pqa = rbt_value(iter);
			if(soonest_to_expire_qa == NULL || pqa->expiry < soonest_to_expire_qa->expiry)
				soonest_to_expire_qa = pqa;
		}
		if(soonest_to_expire_qa != NULL) {
			rbt_status = rbt_eraseKey(rbt, soonest_to_expire_qa->q);
			assert(rbt_status == RBT_STATUS_OK);
#ifdef DEBUG
			KadC_log("Expunging the soonest_to_expire cached record; %d remain in cache\n", rbt_size(rbt));
#endif
			raw_qa_destroy(soonest_to_expire_qa);
			removed_entries++;
		} else {
			break;	/* should only be executed if maxentries <= 0... */
		}
	}
	return removed_entries;
}

static int purge_expired(void *qarbt) {
	void *iter;
	int removed_entries = 0;
	rbt_StatusEnum rbt_status;
	int rbt_is_cache = 0;
	do {
		for(iter = rbt_begin(qarbt); iter != NULL; iter = rbt_next(qarbt, iter)) {
			raw_qa *pqa = rbt_value(iter);
			/* only for cache rbt (where pqa->a != NULL) implement "predictive caching":
			   if the entry is going to expire soon (e.g., 1 minute), and it has been accessed recently,
			   (e.g., less than 5 minutes ago) then perform a refresh so that
			   the next access will find the data already in cache. This is implemented
			   posting to the DNSIO fifo queue a dummy DNSpacket obtained by cloning
			   the ->q member in the cached qa. The clone's remoteip will be set to 0, so to
			   suppress the DNS reply otherwise issued by the processing thread. */

			if(pqa->a != NULL && 						/* i.e., it's a cached qa, not a pending one... */
			   pqa->expiry < time(NULL) + 60 &&			/* ...expiring within 60 seconds */
			   pqa->last_accessed > time(NULL) - 300 &&	/* ...and accessed less that 300 seconds ago... */
			   pqa->being_refreshed == 0) {				/* ...and we haven't yet taken care of it... */
				DNSIO *pdnsio = pqa->pptp->pdnsio;
				/* allocate a DNSpacket */
				DNSpacket *dp = malloc(sizeof(DNSpacket));
				assert(dp != NULL);

				pqa->being_refreshed = 1;
				*dp = *(pqa->q);	/* shallow clone of cached question packet */
				dp->remoteip = 0;	/* identify it as a locally-generated psudo-packet */
				dp->remoteport = 0;	/* same */
				dp->fd = -1;		/* means "packet not arrived over TCP" */

				/* allocate a copy of buffer referenced by DNSpacket */
				dp->buf = malloc(dp->bufsize);
				assert(dp->buf != NULL);
				/* copy there the question's buf: this makes the cloning deep */
				memcpy(dp->buf, pqa->q->buf, dp->bufsize);

#ifdef VERBOSE_DEBUG /* DEBUG ONLY */
				{
					int i;
					KadC_log("Predictive caching: enqueuing pseudo-packet from %s:%d %d bytes:",
							htoa(dp->remoteip), dp->remoteport, dp->bufsize);
					for(i=0; i < dp->bufsize /* && i < 48 */; i++) {
						if((i % 16) == 0)
							KadC_log("\n");
						KadC_log("%02x ", dp->buf[i]);
					}
				}
				KadC_log("\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
#endif

				/* enqueue the cloned DNSpacket */
				if(pdnsio->fifo->enq(pdnsio->fifo, dp) != 0) {
					/* if FIFO full, drop the packet? */
					free(dp->buf);
					free(dp);
				}

			}

			if(pqa->expiry < time(NULL)) {
				if(pqa->a != NULL)
				    rbt_is_cache = 1;	/* only qa's in cache have a non-NULL a */
				if(pqa->isdetached == 0 || pqa->done != 0) {	/* if detached and not done, skip it */
					rbt_status = rbt_erase(qarbt, iter);
					removed_entries++;
					assert(rbt_status == RBT_STATUS_OK);
					raw_qa_destroy(pqa);
					break;	/* restart scan after erase because iter becomes invalid */
				}
			}
		}
	} while(iter != NULL);	/* until end of table is reached without deletions during a scan */
#ifdef DEBUG
	if(removed_entries > 0)
		KadC_log("Expunging from %s %d expired record; %d remain in table\n",
			(rbt_is_cache ? "cache" : "pending requests"), removed_entries, rbt_size(qarbt));
#endif
	return removed_entries;
}

/* static global variables and prototypes */

static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;

static void *processing_thread(void *p);
static void *publishing_thread(void *p);

static int udp_dns_init(DNSIO *pdnsio);
void *dns_udp_recv_thread(void *p);
static void ConsoleLoop(processing_thread_params *pptp);
static int DNSquery(DNSIO *pd, DNSpacket *pdnp, unsigned long int ip);
static int DNSreply(DNSIO *pd, DNSpacket *pdnpreply);

void usage(char *prog) {
#if !defined(__WIN32__) && !defined(__CYGWIN__)
	KadC_log("usage: %s [-k inifile.ini] [-i ip_to_bind_to] [-p port_to_bind_to] [-s DNSserver]* -c cache_maxentries [-d my_pseudodomain[=dotted.quad.ip.addr]]* [-t toplevelpseudodomain]* [-u UID] [-g GID]\n", prog);
#else
	KadC_log("usage: %s [-k inifile.ini] [-i ip_to_bind_to] [-p port_to_bind_to] [-s DNSserver]* -c cache_maxentries [-d my_pseudodomain[=dotted.quad.ip.addr]]* [-t toplevelpseudodomain]*\n", prog);
#endif
}


static DNSIO *glob_pdnsio;	/* used by sighandler */

#ifdef OLD_STYLE_SIGNAL

void sighandler(int sig) {
	DNSIO *pdnsio = glob_pdnsio;
	pthread_mutex_lock(&pdnsio->mutex);
#ifdef DEBUG
	KadC_log("Signal %d caught, shutdown_flag set to 1\n", sig);
#endif
	pdnsio->shutdown_flag = 1;
	pthread_mutex_unlock(&pdnsio->mutex);
	signal(sig, SIG_IGN);
}

#else

/* This thread waits for TERM and INT signals, and when
   they are found, sets the shutdown_flag */
void* sig_handler_th(void* arg) {
	DNSIO *pdnsio = glob_pdnsio;
	sigset_t signal_set;
	int sig;

	/* we loop forever (and the main thread does not join() on this thread)
	   because under GNU Pth signal blocking doesn't seem to work; a
	   signal issued when no sigwait() is waiting for it always terminates
	   the process... So we leave this thread to sigwait() till the process'
	   exit.
	 */
	for(;;) {
		/* wait for these signal (they are all already blocked) */
		sigemptyset(&signal_set);
		sigaddset(&signal_set, SIGTERM);
		sigaddset(&signal_set, SIGINT);
		sigwait(&signal_set, &sig);

		/* here we've caught a signal! */
		pthread_mutex_lock(&pdnsio->mutex);
#ifdef DEBUG
		KadC_log("Signal %d caught, shutdown_flag set to 1\n", sig);
#endif
		pdnsio->shutdown_flag = 1;
		pthread_mutex_unlock(&pdnsio->mutex);
	}

	return NULL;
}

#endif


int main(int ac, char *av[]) {
	char *kadcinifile = NULL;
	int leafmode = 1;	/* default: leaf mode */
	KadC_status kcs;
	processing_thread_params ptp = {0};	/* don't forget to init to all 0's... */
	pthread_t udprecv;
	pthread_t proc;
	pthread_t publish;
	char c;
	DNSIO dnsio = {0};
	KadCcontext kcc = {0};
	int status;
	unsigned long int ip;
#ifndef __WIN32__
	char *user = NULL;
	char *group = NULL;
#endif
#ifndef OLD_STYLE_SIGNAL
	sigset_t signal_set;
	pthread_t sig_thread;
#endif

	KadC_log("namecache - KadC library version: %d.%d.%d\n",
		KadC_version.major, KadC_version.minor, KadC_version.patchlevel);

	dnsio.mutex = mutex_initializer;
	glob_pdnsio = &dnsio;

	ptp.cache_maxentries = -1;
	dnsio.port = DNS_PORT;	/* default */

#ifndef __WIN32__	/* if UNIX */
	while( (c = getopt( ac, av, "k:c:t:s:d:li:p:u:g:h")) != EOF ) {
#else
	while( (c = getopt( ac, av, "k:c:t:s:d:li:p:h")) != EOF ) {
#endif
		switch(c) {
		case 'k':
			kadcinifile = optarg;
			break;
		case 'c':
			ptp.cache_maxentries = atoi(optarg);
			break;
		case 't':
			if(ptp.ntlpd >= arraysize(ptp.tlpd)) {
				KadC_log("Can't handle more than %d \"-t\" options\n", arraysize(ptp.tlpd));
				exit(1);
			}
			ptp.tlpd[ptp.ntlpd++] = optarg;
			break;
		case 's':
			if(dnsio.nupstream >= arraysize(dnsio.upstream)) {
				KadC_log("Can't specify more than %d upstream servers with \"-s\" options\n", arraysize(dnsio.upstream));
				exit(1);
			}
			ip = inet_addr(optarg);
			if(ip == -1) {
				KadC_log("Invalid IP address %s in \"-s\" option\n", optarg);
				exit(1);
			}
			dnsio.upstream[dnsio.nupstream++] = ntohl(ip);
			break;

		case 'd':
			if(ptp.nmy_pseudo >= arraysize(ptp.my_pseudo)) {
				KadC_log("Can't handle more than %d \"-d\" options\n", arraysize(ptp.my_pseudo));
				exit(1);
			}
			ptp.my_pseudo[ptp.nmy_pseudo++] = optarg;
			break;
/*
		case 'l':
			leafmode = 1;
			break;
 */
		case 'i':				/* IP address to bind to */
			ip = inet_addr(optarg);
			if(ip == -1) {
				KadC_log("Invalid IP address %s in \"-b\" option\n", optarg);
				exit(1);
			}
			dnsio.ip = ntohl(ip);
			break;
		case 'p':				/* UDP port to bind to */
			dnsio.port = atoi(optarg);
			break;
#ifndef __WIN32__
		case 'u':
			user = optarg;
			break;
		case 'g':
			group = optarg;
			break;
#endif
		case 'h':
		default:
			usage(av[0]);
			return 1;
		}
	}

	/* a few consistency checks between options: */

#if !defined(__WIN32__) && !defined(__CYGWIN__)
	if(user != NULL) {
		dnsio.uid = user2uid(user);
		if(dnsio.uid < 0) {
			KadC_log("Could not retrieve the UID of the user %s\n", user);
			return 1;
		}
		dnsio.gid = user2gid(user);
		if(dnsio.gid < 0) {
			KadC_log("Could not retrieve the GID of the user %s\n", user);
			return 1;
		}
	} else {	/* "-u" option not present: no good if we are root! */
		if(we_have_root_privilege()) {	/* ###### FIXME: detect privilege here */
			KadC_log("For security reasons, this program must drop root privilege after startup.\n");
			KadC_log("Please use the \"-u\" option to define a non-privileged user (e.g., \"-u nobody\")\n");
			return 1;
		}
	}
	if(group != NULL) {
		dnsio.gid = group2gid(group);
		if(dnsio.gid < 0) {
			KadC_log("Could not retrieve the GID of the group %s\n", group);
			return 1;
		}
	}
#endif

	if(kadcinifile == NULL && ptp.ntlpd > 0) {
		KadC_log("Can't resolve queries via P2P for the %d pseudo-domains specified with \"-t\" options if a KadC ini file is not specified\n", ptp.ntlpd);
		usage(av[0]);
		return 1;
	}

	if(kadcinifile == NULL && ptp.nmy_pseudo != 0) {
		KadC_log("Can't publish via P2P with \"-d\" record associating names to IP addresses if a KadC ini file is not specified\n");
		usage(av[0]);
		return 1;
	}

	if(ptp.ntlpd == 0 && dnsio.nupstream == 0) {
		KadC_log("Can't resolve queries either via DNS or P2P: at least one of -s or -t must be specified.\n");
		usage(av[0]);
		return 1;
	}

	if(kadcinifile != NULL && ptp.ntlpd == 0 && ptp.nmy_pseudo == 0) {
		KadC_log("Warning: if we neither resolve (-t) nor publish (-p) via P2P, what's the point of specifying \"-k %s\" and running KadC?\n", kadcinifile);
	}

	if(ptp.cache_maxentries == -1)
		ptp.cache_maxentries = 4096;	/* default */



	/* if we are under WIN32, this will call WSAStartup() and put in
	   place an atexit hook to WSACleanup() */
	KadC_init_network();

	/* open UDP socket and prepare UDP I/O */
	dnsio.fifo = new_queue(8); /* not more than 8 outstanding requests */
	assert(dnsio.fifo != NULL);
	if(udp_dns_init(&dnsio) < 1) {
		KadC_log("dns_udp_recv_thread reports socket error %d\n", dnsio.err);
		dnsio.fifo->destroy(dnsio.fifo);
		goto exit;
	}

/* Drop root privileges, if under some flavour of *NIX */

#if !defined(__WIN32__) && !defined(__CYGWIN__)
	if((status = droppriv(dnsio.uid, dnsio.gid)) != 0) {
		KadC_log("FATAL: Couldn't drop root privilege: ");
		if(status == -1)
			KadC_log("suppl. group IDs reset to non-root GID failed\n");
		else if(status == -2)
			KadC_log("couldn't change to non-privileged GID %d\n", dnsio.gid);
		else if(status == -3)
			KadC_log("couldn't change to non-root UID %d\n", dnsio.uid);
		else if(status == -4)
			KadC_log("once non-root, setuid(0) managed to restore root UID!\n");
		exit(1);
	}
#endif

/* Install signal handlers */

#ifdef OLD_STYLE_SIGNAL

	signal(SIGTERM, sighandler);	/* for the TERM signal (15, default for kill command)...  */
	signal(SIGINT, sighandler); 	/* .. the INT signal... (Ctrl-C  hit on keyboard) */

#else

	/* block signals we'll wait for */
	sigemptyset(&signal_set);
	sigaddset(&signal_set, SIGTERM);
	sigaddset(&signal_set, SIGINT);
#ifdef __WIN32__
	sigaddset(&signal_set, SIGBREAK);
#endif
	pthread_sigmask(SIG_BLOCK, &signal_set,	NULL);

	/* create the signal handling thread */
	pthread_create(&sig_thread, NULL, sig_handler_th, NULL);

#endif

/* Start KadC engine, if the -k option was present */

	if(kadcinifile != NULL) {
		kcc = KadC_start(kadcinifile, leafmode, 0);
		if(kcc.s != KADC_OK) {
			KadC_log("KadC_start(%s, %d) returned error %d:\n",
				kadcinifile, leafmode, kcc.s);
			KadC_log("%s %s", kcc.errmsg1, kcc.errmsg2);
			return 2;
		}
		KadC_log("Our hash ID: %s ");
		KadC_int128flog(stdout, KadC_getourhashID(&kcc));
		KadC_log("\nour UDP port: %u, our TCP port: %u\n",
		KadC_getourUDPport(&kcc),
		KadC_getourTCPport(&kcc));
	}

	/* spawn the UDP init & listener loop thread */
	pthread_create(&udprecv, NULL, dns_udp_recv_thread, &dnsio);

	/* go to process requests posted by UDP listener to pdnsio->fifo,
	   spawning threads as needed, and send UDP replies with
	   sendto() to pdnsio->fd */
	ptp.pkcc = &kcc;
	ptp.pdnsio = &dnsio;
	/* create the pending questions table - ID is significant! */
	ptp.pending_q_rbt = rbt_new((rbtcomp *)&DNSpacket_qi_lt, (rbtcomp *)&DNSpacket_qi_eq);
	assert(ptp.pending_q_rbt != NULL);
	/* create the raw_qa cache. ID is NOT significant */
	ptp.cache_q_rbt = rbt_new((rbtcomp *)&DNSpacket_q_lt, (rbtcomp *)&DNSpacket_q_eq);
	assert(ptp.cache_q_rbt != NULL);
	ptp.mutex = mutex_initializer;

	/* start the processing thread, which reads incoming DNS data from FIFO queue */
	pthread_create(&proc, NULL, processing_thread, &ptp);

	/* start the publishing thread, which every two hours republishes records for our domain names */
	pthread_create(&publish, NULL, publishing_thread, &ptp);

	ConsoleLoop(&ptp);

	KadC_log("Shutting down, please wait...\n");

/* sig_thread never returns (see comments in processing_thread() ).
#ifndef OLD_STYLE_SIGNAL
	pthread_join(sig_thread, NULL);
#endif
 */
	pthread_join(publish, NULL);
	pthread_join(proc, NULL);
	pthread_mutex_destroy(&ptp.mutex);

	pthread_join(udprecv, NULL);
	pthread_mutex_destroy(&dnsio.mutex);

	status = qa_rbt_destroy(ptp.cache_q_rbt, 1);	/* also destroy data */
	assert(status == 0);
	status = qa_rbt_destroy(ptp.pending_q_rbt, 1);	/* also destroy data */
	assert(status == 0);

	dnsio.fifo->destroy(dnsio.fifo);	/* destroy queue used to communicate with udprecv */

	if(kadcinifile != NULL) {
		kcs = KadC_stop(&kcc);
		if(kcs != KADC_OK) {
			KadC_log("KadC_stop(&kcc) returned error %d:\n", kcc.s);
			KadC_log("%s %s", kcc.errmsg1, kcc.errmsg2);
			return 3;
		}
	}
exit:
	KadC_list_outstanding_mallocs(10);

	return 0;
}

/* converts "4.3.2.1.in-addr.arpa" into the IP address 1.2.3.4 */

unsigned long int reverse_inet_addr(char *s) {
	unsigned int b1, b2, b3, b4;
	char *p;
	int n;

	if(s == NULL)
		return -1;
	n = sscanf(s, "%u.%u.%u.%u.in-addr.arpa%s", &b4, &b3, &b2, &b1, p);
	if(n<4)
		return -1;
	if(n == 5 && strcmp(p, ".") != 0)
		return -1;
	return(b1<<24) + (b2<<16) + (b3<<8) + b4;
}

/* we are authoritative:
   - for PTR queries resolving to an address on some interface;
     the proper answer is "cachehost.cachedomain", replied immedately
   - for A queries relative to our pseudo-domain
     the proper answer is our external IP, replied immediately
   - for A queries under some tlpd
     the proper answer is a KadC search, replied later
 */

static void ConsoleLoop(processing_thread_params *pptp) {
	for(;;){
		int sf;
		pthread_mutex_lock(&pptp->pdnsio->mutex);	/* \\\\\\ LOCK \\\\\\ */
		sf = pptp->pdnsio->shutdown_flag;
		pthread_mutex_unlock(&pptp->pdnsio->mutex);	/* ///// UNLOCK ///// */
		if(sf)
			break;
#if 0	/* suppress console input when not in debug mode */
		KadCcontext *pkcc = pptp->pkcc;
		char line[80];
		int linesize = sizeof(line)-1;
		char *s;
		int fwstatus = KadC_getfwstatus(pkcc);
		KadC_log("%4d/%4d%s ",
			KadC_getnknodes(pkcc), KadC_getncontacts(pkcc),
			(fwstatus>0 ? (fwstatus>1 ? ">" : "]") : "?"));
		s = KadC_getsn(line, linesize);
		if(s == NULL)
			break;
#else
		/* KadC_log("."); */
		millisleep(1000);
#endif
	}
}

/* create an entry in the pending queries rbt, unless already there
   This is the single point where raw_qa objects are created. They
   are then converted in cached qa's in the processing thread by
   adding the reply, when it arrives. */
static int create_pending_entry(processing_thread_params *pptp, DNSpacket *pdnp, void *threadcode(void *)) {
	void *iter;
	rbt_StatusEnum rbt_status = rbt_find(pptp->pending_q_rbt, pdnp, &iter);
	if(rbt_status == RBT_STATUS_KEY_NOT_FOUND) {
		/* this is the only place where a raw_qa is created */
		raw_qa *pending_qa = calloc(1, sizeof(raw_qa));
		assert(pending_qa != NULL);
		pending_qa->q = pdnp;
		pending_qa->a = NULL;	/* answer is void, of course */
		pending_qa->last_accessed = time(NULL); /* informational, actually unused in pending_q_rbt */
		pending_qa->expiry = pending_qa->last_accessed + 300;
		pending_qa->being_refreshed = 0;	/* unused in pending_q_rbt */
		pending_qa->mutex = mutex_initializer;
		pending_qa->pptp = pptp;
		rbt_status = rbt_insert(pptp->pending_q_rbt, pending_qa->q, pending_qa, 0);
		assert(rbt_status == RBT_STATUS_OK);	/* can't be already there now... */
#ifdef VERBOSE_DEBUG
			KadC_log("create_pending_entry() created a pending_query entry \n");
#endif
		if(threadcode != NULL) {
#ifdef DEBUG
			KadC_log("create_pending_entry() also started a p2pquery thread\n");
#endif
			pending_qa->isdetached = 1;
			pthread_create(&pending_qa->thread, NULL, threadcode, pending_qa);
		}
		return 0;
	} else if(rbt_status == RBT_STATUS_OK) {
#ifdef DEBUG
		KadC_log("create_pending_entry() found an existing pending_query entry for that question, so did nothing\n");
#endif
		/* already there, do nothing */
		return 1;
	} else {
		assert(0);	/* if other cases, big trouble */
		return -1;
	}
}

/* If name is equal (apart from capitalization and
   trailing dots) to one of array's elements (which are supposed
   to have have no trailing dots), return 1 + index in the array */
static int is_in(char *array[], int arraysize, char *name) {
	int i, l;

	if(name == NULL)
		return 0;

	for(l = strlen(name); l > 0 ; --l) {
		if(name[l-1] != '.')
			break;
	}
	/* l is now the length of name trimmed of any trailing dots */

	if(l == 0)
		return 0;	/* zero-length names are not supposed to be good */

	for(i=0; i<arraysize; i++) {
		char *s = strdup(array[i]);
		char *p;

		assert(s != NULL);
		if((p = strchr(s, '=')) != NULL) {
			*p = 0;	/* truncate string's copy at first '=' (if any) */
		}
		if(strlen(s) == l && strncasecmp(name, s, l) == 0) {
			free(s);
			return 1+i;
		}
		free(s);
	}
	return 0;
}


/* returns true (1) if name ends with a sequence of labels equal
   (apart from capitalization and trailing dots) to one of array's
   elements (which are guaranteed to have no trailing dots) */
static int end_is_in(char *array[], int arraysize, char *name) {
	int i, l;

	if(name == NULL)
		return 0;

	for(l = strlen(name); l > 0 ; --l) {
		if(name[l-1] != '.')
			break;
	}
	/* l is now the length of name trimmed of any trailing dots */

	if(l == 0)
		return 0;	/* zero-length names are not supposed to be good */

	for(i=0; i<arraysize; i++) {
		int la = strlen(array[i]);
		if(la > l)
			continue;	/* array element must be not longer than name */
		if(strncasecmp(name+l-la, array[i], la) == 0 &&
		  (l == la || name[l-la-1] == '.')) {
			return 1;
			break;
		}
	}
	return 0;
}

static void replyA(DNSIO *pdnsio, dns_msg *pdm, DNSpacket *pdnp, unsigned long int ip) {
	DNSpacket *pdnpreply;
	char *ipbuf = calloc(1, sizeof(unsigned long int));
	assert(ipbuf != NULL);

	pdm->rcode = RCODE_NO_ERROR;
	pdm->ra = 1;	/* recursion available */
	pdm->qr = 1;	/* it's a reply */
	pdm->aa = 1;	/* we are authority */
	pdm->nanswer_rr = 1;
	assert(pdm->answer_rr == NULL);	/* pdm came from a query... */
	pdm->answer_rr = calloc(1, sizeof(dns_rr *));	/* allocate space for one parsed answer field */
	assert(pdm->answer_rr != NULL);
	pdm->answer_rr[0] = calloc(1, sizeof(dns_rr));
	assert(pdm->answer_rr[0] != NULL);
	pdm->answer_rr[0]->rdata = ipbuf;
	ipbuf[0] = (ip>>24) & 0xff;
	ipbuf[1] = (ip>>16) & 0xff;
	ipbuf[2] = (ip>> 8) & 0xff;
	ipbuf[3] = (ip    ) & 0xff;
	pdm->answer_rr[0]->rdatalen = sizeof(unsigned long int);
	strcpy(pdm->answer_rr[0]->name, pdm->questions[0]->name);
	pdm->answer_rr[0]->type = A;
	pdm->answer_rr[0]->class = pdm->questions[0]->class;
	pdm->answer_rr[0]->ttl = 600;	/* Why 10 minutes? And why not? */

	pdnpreply = DNSmsg2packet(pdm, pdnp->remoteip, pdnp->remoteport, pdnp->fd);
	DNSreply(pdnsio, pdnpreply);
	DNSpacket_destroy(pdnpreply);
}

static void replyPTR(DNSIO *pdnsio, dns_msg *pdm, DNSpacket *pdnp, char *name) {
	DNSpacket *pdnpreply;
	packedname pn;
	pdm->rcode = RCODE_NO_ERROR;
	pdm->ra = 1;	/* recursion available */
	pdm->qr = 1;	/* it's a reply */
	pdm->aa = 1;	/* we are authority */
	pdm->nanswer_rr = 1;
	assert(pdm->answer_rr == NULL);	/* pdm came from a query... */
	pdm->answer_rr = calloc(1, sizeof(dns_rr *));	/* allocate space for one parsed answer field */
	assert(pdm->answer_rr != NULL);
	pdm->answer_rr[0] = calloc(1, sizeof(dns_rr));
	assert(pdm->answer_rr[0] != NULL);
	pn = dns_domain_pack(name);
	assert(pn.buf != NULL);
	pdm->answer_rr[0]->rdata = pn.buf;
	pdm->answer_rr[0]->rdatalen = pn.buflen;
	strcpy(pdm->answer_rr[0]->name, pdm->questions[0]->name);
	pdm->answer_rr[0]->type = PTR;
	pdm->answer_rr[0]->class = pdm->questions[0]->class;
	pdm->answer_rr[0]->ttl = 600;	/* Why 10 minutes? And why not? */

	pdnpreply = DNSmsg2packet(pdm, pdnp->remoteip, pdnp->remoteport, pdnp->fd);
	DNSreply(pdnsio, pdnpreply);
	DNSpacket_destroy(pdnpreply);
}


static void replyERR(DNSIO *pdnsio, dns_msg *pdm, DNSpacket *pdnp, dns_rcode_t rcode) {
	/* sigh, we can't answer. */
	DNSpacket *pdnpreply;
	pdm->rcode = rcode;
	pdm->ra = 1;	/* recursion available */
	pdm->qr = 1;	/* it's a reply */
	pdnpreply = DNSmsg2packet(pdm, pdnp->remoteip, pdnp->remoteport, pdnp->fd);
	DNSreply(pdnsio, pdnpreply);
	DNSpacket_destroy(pdnpreply);
}


static void *p2pquery(void *p) {
	raw_qa *pqa = p;
	processing_thread_params *pptp = pqa->pptp;
	DNSIO *pdnsio = pptp->pdnsio;
	DNSpacket *pdnp = pqa->q;
	dns_msg *pdm = DNSpacket2msg(pdnp);
	dns_question *question = pdm->questions[0];
	DNSpacket *pdnpreply;

#ifdef DEBUG
	KadC_log("p2pquery thread routine processing query for %s\n", question->name);
#endif
	/* it has already been checked that nquestions == 1 and
	   question is sane and has class == I */
	if(question->type == A) {
		/* now start the time-consuming P2P search */
		void *resdictrbt;
		char index[512];
		char filter[1024];
		void *iter;

		sprintf(index, "KadC::namecache::%s", question->name);
		sprintf(filter, "KadCapp=namecache&rrtype=A");

		/* launch a search with 10 threads, max 5 hits and 7 s duration */
		resdictrbt = KadC_find(pptp->pkcc, index, filter, 20, 5, 7);

		pdm->rcode = RCODE_NAME_ERROR;	/* default: record NOT found */

		if(rbt_size(resdictrbt) == 0) {
#ifdef DEBUG
			KadC_log("p2pquery found no records, and will reply RCODE_NAME_ERROR\n");
#endif
		} else {
			unsigned long int ip;
			KadCdictionary *pkd;
			char *ipbuf = calloc(1, sizeof(unsigned long int));

#ifdef DEBUG
			KadC_log("p2pquery found %d records, and will reply RCODE_NO_ERROR\n", rbt_size(resdictrbt));
#endif
			assert(ipbuf != NULL);
			/* list each k-object */
			for(iter = rbt_begin(resdictrbt); iter != NULL; iter = rbt_next(resdictrbt, iter)) {
				KadCtag_iter kt_iter;
				pkd = rbt_value(iter);

				KadC_log("Found: \n");
				KadCdictionary_dump(pkd);
				KadC_log("\n");

				if(KadCtag_find(pkd, "kadcapp", &kt_iter) == KADCTAG_STRING &&
				   		strcasecmp(kt_iter.tagvalue, "namecache") == 0 &&
				   KadCtag_find(pkd, "rrtype", &kt_iter) == KADCTAG_STRING &&
				   		strcasecmp(kt_iter.tagvalue, "a") == 0 &&
				   KadCtag_find(pkd, "ip", &kt_iter) == KADCTAG_STRING) {
					ip = inet_addr(kt_iter.tagvalue);
					ip = ntohl(ip);
					KadC_log("Retrieved ip = %s\n", htoa(ip));
					break;
				}
			}

			/* build answer_rr */
			assert(pdm->answer_rr == NULL);	/* double checking won't hurt */
			pdm->answer_rr = calloc(1, sizeof(dns_rr *));	/* allocate space for one parsed answer field */
			assert(pdm->answer_rr != NULL);
			pdm->answer_rr[0] = calloc(1, sizeof(dns_rr));
			assert(pdm->answer_rr[0] != NULL);
			pdm->answer_rr[0]->rdata = ipbuf;
			ipbuf[0] = (ip>>24) & 0xff;
			ipbuf[1] = (ip>>16) & 0xff;
			ipbuf[2] = (ip>> 8) & 0xff;
			ipbuf[3] = (ip    ) & 0xff;
			pdm->answer_rr[0]->rdatalen = sizeof(unsigned long int);
			strcpy(pdm->answer_rr[0]->name, pdm->questions[0]->name);
			pdm->answer_rr[0]->type = A;
			pdm->answer_rr[0]->class = pdm->questions[0]->class;
			pdm->answer_rr[0]->ttl = 600;	/* Why 10 minutes? And why not? */
			pdm->nanswer_rr = 1;

			pdm->rcode = RCODE_NO_ERROR;	/* record found */
		}


		for(iter = rbt_begin(resdictrbt); iter != NULL; iter = rbt_begin(resdictrbt)) {
			KadCdictionary *pkd = rbt_value(iter);
			rbt_erase(resdictrbt, iter);
			KadCdictionary_destroy(pkd);
		}
		rbt_destroy(resdictrbt);

	} else {
		pdm->rcode = RCODE_NOT_IMPLEMENTED;	/* TEMPORARY!! */
	}
	pdm->aa = 1;	/* we are authoritative on this */
	pdm->ra = 1;	/* recursion available */
	pdm->qr = 1;	/* it's a reply */
	pdnpreply = DNSmsg2packet(pdm, pdnp->remoteip, pdnp->remoteport, pdnp->fd);

	dns_msg_destroy(pdm);	/* parsed dns_msg no longer necessary */

	/* post the reply to the same queue used by the DNS network listener */
	if(pdnsio->fifo->enq(pdnsio->fifo, pdnpreply) != 0) {
		/* if FIFO full, drop the packet? */
		free(pdnpreply->buf);
		free(pdnpreply);
	}

	pqa->done = 1;
	return NULL;
}

static void *processing_thread(void *p){
	processing_thread_params *pptp = p;
	for(;;) {
		int sf;
		DNSpacket *pdnp;

		pthread_mutex_lock(&pptp->pdnsio->mutex);	/* \\\\\\ LOCK \\\\\\ */
		sf = pptp->pdnsio->shutdown_flag;
		pthread_mutex_unlock(&pptp->pdnsio->mutex);	/* ///// UNLOCK ///// */
		if(sf)
			break;
		/* wait for requests on pptp->d.fifo */
		pdnp = pptp->pdnsio->fifo->deqtw(pptp->pdnsio->fifo, 1000);
		if(pdnp != NULL) {
			dns_msg *pdm;
			/* process request, possibly starting a separate thread
			   if the tld is in tlpd[], use KadC search rather than upstream
			   DNS servers */
#ifdef VERBOSE_DEBUG /* DEBUG ONLY */
			{
				int i;
				KadC_log("processing_thread - received from %s:%d %d bytes:",
						htoa(pdnp->remoteip), pdnp->remoteport, pdnp->bufsize);
				for(i=0; i < pdnp->bufsize /* && i < 48 */; i++) {
					if((i % 16) == 0)
						KadC_log("\n");
					KadC_log("%02x ", pdnp->buf[i]);
				}
			}
			KadC_log("\n--------------------------------\n");
#endif

			/* parse the DNSpacket into a dns_msg to perform tests more easily */
			pdm = DNSpacket2msg(pdnp);

			if(pdm != NULL) {
				/* adjust the qsize field in pdnp (used by rbt comparison functions) */
				pdnp->qsize = pdm->questions[0]->raw_length;
				/* is this a query or a response? */
				if(pdm->qr == 0) {
					/* it's a query */
					if(pdm->nquestions != 1 || 	/* sorry, unable to handle more than one question */
					   pdm->nanswer_rr != 0) {	/* hey, queries are supposed not to contain answers... */
						DNSpacket *pdnpreply;
#ifdef DEBUG
						KadC_log("This query had %d questions\n", pdm->nquestions);
#endif
						pdm->rcode = RCODE_NOT_IMPLEMENTED;
						pdm->ra = 1;	/* recursion available */
						pdm->qr = 1;	/* it's a reply */
						pdnpreply = DNSmsg2packet(pdm, pdnp->remoteip, pdnp->remoteport, pdnp->fd);
						DNSreply(pptp->pdnsio, pdnpreply);
						DNSpacket_destroy(pdnpreply);
					} else {
						/* single question, good. */
						void *iter = NULL;
						rbt_StatusEnum rbt_status;
						unsigned long int ip;
						dns_type_t qtype;
						char *qname;
						dns_class_t qclass;
						int dnindex;

						assert(pdm->questions != NULL);
						assert(pdm->questions[0] != NULL);	/* if there is one question it must be the first... */

						qtype = pdm->questions[0]->type;
						qname = pdm->questions[0]->name;
						qclass = pdm->questions[0]->class;

						rbt_status = RBT_STATUS_KEY_NOT_FOUND;	/* default */

						/* check if a q/a for it is in cache */
						pthread_mutex_lock(&pptp->mutex);	/* \\\\\\ LOCK \\\\\\ */
						if(pdnp->remoteip != 0)	/* if not locally generated for predictive caching refresh */
							rbt_status = rbt_find(pptp->cache_q_rbt, pdnp, &iter);
						if(rbt_status == RBT_STATUS_OK) {
							DNSpacket dnpreply;
							raw_qa *cached_qa = rbt_value(iter);
							assert(cached_qa != NULL);
							cached_qa->last_accessed = time(NULL);	/* update "last read" time */
							/* create on the stack a clone of reply fetched from cache */
							dnpreply = *(cached_qa->a);
							dnpreply.buf = malloc(dnpreply.bufsize);
							assert(dnpreply.buf != NULL);
							memcpy(dnpreply.buf, cached_qa->a->buf, dnpreply.bufsize);
							/* modify the ID to match the query's */
							dnpreply.buf[0] = ((pdm->id)>>8) & 0xff;
							dnpreply.buf[1] = ((pdm->id)   ) & 0xff;
							/* also match destination IP and port to the requestor's */
							dnpreply.remoteip = pdnp->remoteip;
							dnpreply.remoteport = pdnp->remoteport;
							dnpreply.fd = pdnp->fd;
							/* send the reply to the requestor */
							DNSreply(pptp->pdnsio, &dnpreply);
							/* free the buffer (the DNSpacket is auto... */
							free(dnpreply.buf);
						} else if(qclass == I &&
								(qtype == PTR || qtype == 255) &&
								is_a_local_address(ip = reverse_inet_addr(qname))) {
							/* perhaps it's a query for things on which we
							   are authoritative: PTR for local IP address,
							   A and PTR for localhost (duh) or KadC-related
							   domains (subdomains of arguments of "-t")
							   IMMEDIATE REPLY, no pending_qa is created */
							if(ip == 0x7f000001) {	/* 127.0.0.1 */
								replyPTR(pptp->pdnsio, pdm, pdnp, "localhost");	/* which dumb resolver could ever ask that over the network? */
							} else { /* must be an IP on a local interface */
								replyPTR(pptp->pdnsio, pdm, pdnp, "cachehost.cachedomain");	/* assumed to be an alias of our machine... */
							}
						} else if(qclass == I && (dnindex=is_in(pptp->my_pseudo, pptp->nmy_pseudo, qname)) > 0) {
							char *arg = strchr(pptp->my_pseudo[dnindex-1], '=');
							if(arg == NULL)
								ip = KadC_getextIP(pptp->pkcc);
							else
								ip = ntohl(inet_addr(arg+1));
							if(ip != 0)	{ /* if we know from KadC our external IP address */
								/* handle here queries for domain names equal to our pseudomains
								   IMMEDIATE REPLY, no pending_qa is created */
#ifdef DEBUG
								KadC_log("A query for the pseudodomain %s resolves to %s\n", qname, htoa(ip));
#endif
								replyA(pptp->pdnsio, pdm, pdnp, ip);	/* assumed to be an alias of our machine... */
							} else {
								replyERR(pptp->pdnsio, pdm, pdnp, RCODE_NAME_ERROR);	/* we are authoritative, but the IP is unknown */
							}
						} else if(qclass == I && end_is_in(pptp->tlpd, pptp->ntlpd, qname)) {
								/* handle here A queries for domain names that are subdomains of one of our tpld's */
								/* post a search for the keyword "namecache::some.domain" */
#ifdef DEBUG
								KadC_log("A query for %s, a subdomain of one of the KadC TLPD's\n", qname);
#endif
							if(create_pending_entry(pptp, pdnp, &p2pquery) == 0) {	/* if there wasn't already one (also with same id), and it's been created... */
								pdnp = NULL;	/* so it won't be freed below (now the packet is referenced by the pending_q rbt...) */
							} else {
								/* do nothing, maybe it's an unnecessary retry for timeout that is already being taken care of */
							}
						} else if(pptp->pdnsio->nupstream > 0) {
							/* nope: if no pending queries, query all upstream servers and create a "pending" entry */
							if(create_pending_entry(pptp, pdnp, NULL) == 0) {	/* if there wasn't already one (also with same id), and it's been created... */
								int i;
								for(i=0; i < pptp->pdnsio->nupstream; i++) {
									DNSquery(pptp->pdnsio, pdnp, pptp->pdnsio->upstream[i]);
								}
								pdnp = NULL;	/* so it won't be freed below (now the packet is referenced by the pending_q rbt...) */
							} else {
								/* do nothing, maybe it's an unnecessary retry for timeout that is already being taken care of */
							}
						} else {
							replyERR(pptp->pdnsio, pdm, pdnp, RCODE_SERVER_FAILURE);
						}
						pthread_mutex_unlock(&pptp->mutex);	/* ///// UNLOCK ///// */
					}
				} else {
					/* it's a response */
					void *iter = NULL;
					raw_qa *pending_qa = NULL;
					rbt_StatusEnum rbt_status;

					/* see if there is a pending query matching it */
					pthread_mutex_lock(&pptp->mutex);	/* \\\\\\ LOCK \\\\\\ */
#ifdef VERBOSE_DEBUG
					KadC_log("pending_q_rbt contains %d entries\n", rbt_size(pptp->pending_q_rbt));
#endif
					rbt_status = rbt_find(pptp->pending_q_rbt, pdnp, &iter);
					if(rbt_status != RBT_STATUS_OK) {
						/* if not, discard the response (pdnp will be destroyed below) */
					} else {
						/* if yes retrieve the pending query... */
						pending_qa = rbt_value(iter);
						/* ...and remove it from the pending rbt */
						rbt_status = rbt_erase(pptp->pending_q_rbt, iter);
						assert(rbt_status == RBT_STATUS_OK);
						/* copy retrieved ID, IP, port and fd into pdnp */
						pdnp->buf[0] = pending_qa->q->buf[0];
						pdnp->buf[1] = pending_qa->q->buf[1];
						pdnp->remoteip = pending_qa->q->remoteip;
						pdnp->remoteport = pending_qa->q->remoteport;
						pdnp->fd = pending_qa->q->fd;
						/* forward pdnp as response to the original requestor */
						if(pdnp->remoteip != 0)	/* if question DNSpacket was not a pseudo, used for predictive caching */
							DNSreply(pptp->pdnsio, pdnp);
						/* add the reply to the retrieved qa (whose a was NULL) */
						pending_qa->a = pdnp;
						pdnp = NULL;	/* prevent its deallocation, as it's referenced by pending_qa */
						/* if positive result and there is at least one answer, try to place the result in the cache */
						if(pdm->rcode == RCODE_NO_ERROR && pdm->answer_rr != NULL && pdm->answer_rr[0] != NULL) {
							time_t now = time(NULL);
							time_t answttl = pdm->answer_rr[0]->ttl;	/* use TTL of first answer (if present) */
							time_t texp = now + answttl;
#ifdef DEBUG
							KadC_log("Caching answ for qt=%d on %s; TTL %d s, exp %s", pdm->questions[0]->type, pdm->questions[0]->name, answttl, ctime(&texp));
#endif
							/* see if we are replacing an existing entry */
							rbt_status = rbt_find(pptp->cache_q_rbt, pending_qa->q, &iter);
							pending_qa->expiry = texp;
							pending_qa->last_accessed = now;
							if(rbt_status == RBT_STATUS_OK) {
								raw_qa *pqa = rbt_value(iter);
								rbt_status = rbt_erase(pptp->cache_q_rbt, iter);
								assert(rbt_status == RBT_STATUS_OK);
								raw_qa_destroy(pqa);
#ifdef DEBUG
								KadC_log("...after replacing existing record for predictive caching refresh\n");
#endif
								/* the replacement will appear as NOT recently accessed. This
								   prevents replacement loops */
								pending_qa->last_accessed = 0;
							}
							rbt_status = rbt_insert(pptp->cache_q_rbt, pending_qa->q, pending_qa, 0);
							if(rbt_status == RBT_STATUS_OK) {
								pending_qa = NULL;	/* preventing its deallocation */
								/* keep size below limit by expunging oldest entries */
								trim_qa_rbt(pptp->cache_q_rbt, pptp->cache_maxentries);
							} else if(rbt_status != RBT_STATUS_DUPLICATE_KEY) {
								assert(0);	/* trouble if it's something else... */
							}
						}
						/* if not placed in cache, destroy the retrieved qa */
						raw_qa_destroy(pending_qa);
					}
					pthread_mutex_unlock(&pptp->mutex);	/* ///// UNLOCK ///// */
				}
				dns_msg_destroy(pdm);
			}
			DNSpacket_destroy(pdnp);
		} /* end processing of DNS packet retrieved from FIFO */

		/* scan pending_q_rbt and cache_q_rbt to expire old records */

		pthread_mutex_lock(&pptp->mutex);	/* \\\\\\ LOCK \\\\\\ */
		purge_expired(pptp->cache_q_rbt);
		pthread_mutex_unlock(&pptp->mutex);	/* ///// UNLOCK ///// */

		pthread_mutex_lock(&pptp->mutex);	/* \\\\\\ LOCK \\\\\\ */
		purge_expired(pptp->pending_q_rbt);
		pthread_mutex_unlock(&pptp->mutex);	/* ///// UNLOCK ///// */

		/* KadC_log("."); */
	}
	return NULL;
}

int millisleep_exit(DNSIO *pdnsio, int millis) {
	int sf;
	pthread_mutex_lock(&pdnsio->mutex);		/* \\\\\\ LOCK \\\\\\ */
	sf = pdnsio->shutdown_flag;
	pthread_mutex_unlock(&pdnsio->mutex);	/* ///// UNLOCK ///// */
	if(sf)
		return 1;
	millisleep(500);
	return 0;
}

static void *publishing_thread(void *p){
	processing_thread_params *pptp = p;
	int i;

	for(;;) {
		unsigned long int extip;
		extip = KadC_getextIP(pptp->pkcc);
		if(extip == 0 || pptp->nmy_pseudo <= 0) {
#ifdef VERBOSE_DEBUG
			KadC_log("Waiting... our external IP address is %s, nmy_pseudo = %d.\n",  htoa(extip), pptp->nmy_pseudo);
#endif
			if(millisleep_exit(pptp->pdnsio, 500))
				goto exit;
		} else {		/* only if we are connected */
#ifdef DEBUG
			KadC_log("Connected! Our external IP address is %s .\n", htoa(extip));
			KadC_log("Going to publish records for the %d \"-d\" params\n", pptp->nmy_pseudo);
#endif
			/* and even then, wait for 1 min to ensure the kbuckets are reasonably full * /
			for(i=0; i < 60*2; i++) {
				if(millisleep_exit(pptp->pdnsio, 500))
					goto exit;
			} */

			for(i=0; i < pptp->nmy_pseudo; i++) {
				char index[1024];
				char value[64];
				char metalist[1024];
				char *mpd = strdup(pptp->my_pseudo[i]);
				char *arg;
				int i, n_published;

				assert(mpd != NULL);
				if((arg=strchr(mpd, '=')) != NULL) {
					*arg++ = 0;
					sprintf(metalist,"KadCapp=namecache;rrtype=A;ip=%s", arg);
				} else {
					sprintf(metalist,"KadCapp=namecache;rrtype=A;ip=%s", htoa(extip));
				}
				sprintf(index, "KadC::namecache::%s", mpd);
				free(mpd);
				sprintf(value, "#1234");	/* any idea for a meaningful use? */
				/* p KadC::namecache::www.xxx.yyy #12 KadCapp=namecache;rrtype=A;ip=1.2.3.4 */
				for(i=0, n_published=0; i<5 && n_published < 15; i++) {
					int n;
					/* repeat up to 5 times, or until a total of at least 15 nodes have
					   received the publishing. (Yes, the same nodes could receive
					   it multiple times: nothing's perfect...) Usually one round suffices. */
					n = KadC_republish(pptp->pkcc, index, value, metalist, 20, 30); /* 20 threads, max 30 s */
#ifdef DEBUG
					KadC_log("Record published to %d peernodes\n", n);
#endif
					n_published += n;
				}
#ifdef DEBUG
				KadC_log("Totally, record published to %d peernodes\n", n_published );
#endif
			}

			{
				int sleeptime = 2*3600-60;	/* times 2 because each step of slep is 500 ms */
				time_t wakeuptime = time(NULL)+sleeptime;
#ifdef DEBUG
			KadC_log("Done. Going to sleep for %d seconds, till %s", sleeptime, ctime(&wakeuptime));
#endif
				/* sleep for 2 hours before republishing */
				for(i=0; i < sleeptime * 2; i++) {
					if(millisleep_exit(pptp->pdnsio, 500))
						goto exit;
				}
			}
		}
	}
exit:
#ifdef DEBUG
	KadC_log("Thread publishing_thread() terminated.\n");
#endif
	return NULL;
}

/* opens socket setting pdnsio->fd; returns pdnsio->fd or error status */
static int udp_dns_init(DNSIO *pdnsio) {
	int fd, on = 1;
	struct sockaddr_in local;
	int status;

	memset(&(local), 0, sizeof(struct sockaddr_in));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(pdnsio->ip);
	local.sin_port = htons(pdnsio->port);

	status = socket(AF_INET, SOCK_DGRAM, 0);
	if(status < 0)
		goto err;

	fd = status;

	status = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on));
	if(status < 0)
		goto err;

	status = bind(fd, (struct sockaddr *)&local, (socklen_t)sizeof(struct sockaddr_in));
	if(status < 0)
		goto err;

	pdnsio->err = 0;
	pdnsio->fd = fd;
	return fd;

err:
#ifdef __WIN32__
	pdnsio->err = WSAGetLastError();
#else
	pdnsio->err = errno;
#endif

	return status;
}

void *dns_udp_recv_thread(void *p) {
	DNSIO *pdnsio = p;
	unsigned char buf[1024];
	int bufsize = sizeof(buf);

	for(;;) {
		DNSpacket *dp;
		int nrecv;
		struct sockaddr_in remote;
		socklen_t sa_len = sizeof(struct sockaddr_in);
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
		fd_set rset;
		struct timeval timeout;
		int sf;

		pthread_mutex_lock(&pdnsio->mutex);	/* \\\\\\ LOCK \\\\\\ */
		sf = pdnsio->shutdown_flag;
		pthread_mutex_unlock(&pdnsio->mutex);	/* ///// UNLOCK ///// */
		if(sf)
			break;

		for(;;) {
			pthread_mutex_lock(&pdnsio->mutex);	/* \\\\\\ LOCK \\\\\\ */
			sf = pdnsio->shutdown_flag;
			pthread_mutex_unlock(&pdnsio->mutex);	/* ///// UNLOCK ///// */
			if(sf)
				goto exit;
			FD_ZERO(&rset);
			FD_SET(pdnsio->fd, &rset);
			timeout.tv_sec = 0;
			timeout.tv_usec = 500000;	/* 500 ms timeout */
			status = select(pdnsio->fd+1, &rset, NULL, NULL, &timeout);
			if(status > 0){
				break;	/* data available, go read it */
			}
		};	/* wait outside recvfrom */

		pthread_mutex_lock(&pdnsio->mutex);	/* \\\\\\ LOCK UDPIO \\\\\\ */
		nrecv = recvfrom(
						pdnsio->fd,
						(char *)buf,
						bufsize,
						0,
						(struct sockaddr *)&remote,
						&sa_len);
		pthread_mutex_unlock(&pdnsio->mutex);	/* ///// UNLOCK UDPIO ///// */
		if(nrecv > bufsize)
			nrecv = -1;	/* in UNIX as in WIN32 ignore oversize datagrams */
		if(nrecv <= 0) {/* ...catch oversize datagrams */
			continue; /* in case of other errors, just skip this datagram */
		}

		/* allocate a DNSpacket */
		dp = malloc(sizeof(DNSpacket));
		assert(dp != NULL);
		dp->remoteip = ntohl(remote.sin_addr.s_addr);
		dp->remoteport = ntohs(remote.sin_port);
		dp->fd = -1;	/* means "packet arrived over UDP" */

		/* allocate a copy of buffer referenced by DNSpacket */
		dp->buf = malloc(nrecv);
		assert(dp->buf != NULL);
		memcpy(dp->buf, buf, nrecv);
		dp->bufsize = nrecv;

#ifdef VERBOSE_DEBUG /* DEBUG ONLY */
		{
			int i;
			KadC_log("dns_udp_recv_thread - received from %s:%d %d bytes:",
					htoa(dp->remoteip), dp->remoteport, dp->bufsize);
			for(i=0; i < dp->bufsize /* && i < 48 */; i++) {
				if((i % 16) == 0)
					KadC_log("\n");
				KadC_log("%02x ", dp->buf[i]);
			}
		}
		KadC_log("\n================================\n");
#endif

		/* enqueue the DNSpacket */
		if(pdnsio->fifo->enq(pdnsio->fifo, dp) != 0) {
			/* if FIFO full, drop the packet? */
			free(dp->buf);
			free(dp);
		}

	}
exit:

#ifdef __WIN32__
	pdnsio->err = closesocket(pdnsio->fd);
#else
	pdnsio->err = close(pdnsio->fd);
#endif

	return NULL;
}

/* blocking UDP send() */
int udp_send(DNSIO *pd, unsigned char *buf, int buflen, unsigned long int destip, int destport) {
	int status;
	struct sockaddr_in destsockaddr;
	int fd = pd->fd;

	memset(&destsockaddr, 0, sizeof(struct sockaddr_in));
	destsockaddr.sin_family = AF_INET;
	destsockaddr.sin_port   = htons((unsigned short int)destport);
	destsockaddr.sin_addr.s_addr = htonl(destip);

	/* this lock protects the fd from concurrent access
	   by separate threads either via sendto() recvfrom() */
	pthread_mutex_lock(&pd->mutex);	/* \\\\\\ LOCK UDPIO \\\\\\ */

	status = sendto(fd, (char *)buf, buflen,
		0, (struct sockaddr *)&destsockaddr,
		(socklen_t)sizeof(destsockaddr));

	pthread_mutex_unlock(&pd->mutex);	/* ///// UNLOCK UDPIO ///// */

	return status;
}
/* end blocking send() */

/* blocking TCP send() FIXME: not used at the moment, need to write listener */
int tcp_send(DNSIO *pd, unsigned char *buf, int buflen, int fd) {
	int status;
#ifdef MSG_NOSIGNAL
	status = send(fd, (char *)buf, buflen, MSG_NOSIGNAL);
#else
	status = send(fd, (char *)buf, buflen, 0);
#endif
#ifdef __WIN32__
	closesocket(fd);
#else
	close(fd);
#endif
	return status;
}

/* forward the packed DNS query in pdnp to the upstream server at the address ip */
static int DNSquery(DNSIO *pd, DNSpacket *pdnp, unsigned long int ip) {
	int status;
	/* for the time being, only UDP */
	status = udp_send(pd, pdnp->buf, pdnp->bufsize, ip, DNS_PORT);
	return status;
}

/* send back the reply packed in the DNSpacket pointed by pdnpreply */
static int DNSreply(DNSIO *pd, DNSpacket *pdnpreply) {
	int status;

	if(pdnpreply->fd < 0) {
		/* use UDP */
		if(pdnpreply->bufsize > 512) {
			pdnpreply->bufsize = 512;	/* truncate to 512 */
			pdnpreply->buf[2] |= 0x02;	/* set the TC (truncation) flag */
		}
		status = udp_send(pd, pdnpreply->buf, pdnpreply->bufsize, pdnpreply->remoteip, pdnpreply->remoteport);
	} else {
		/* use TCP on the same fd (FIXME: TBI) */
		status = tcp_send(pd, pdnpreply->buf, pdnpreply->bufsize, pdnpreply->fd);
	}
	return status;
}

