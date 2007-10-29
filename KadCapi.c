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

#define DEBUG
#include <pthread.h>

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <Debug_pthreads.h>

#include <queue.h>
#include <net.h>
#include <int128.h>
#include <MD4.h>
#include <bufio.h>
#include <KadCalloc.h>
#include <rbt.h>
#include <RTP.h>
#include <KadCthread.h>
#include <opcodes.h>
#include <eMuleKAD.h>
#include <overnet.h>
#include <inifiles.h>
#include <KadCrouting.h>
#include <KadClog.h>
#include <KadCmeta.h>
#include <KadCparser.h>
#include <KadClog.h>
#include <millisleep.h>

#include <KadCapi.h>
#include <overnetexports.h>

const KadC_version_t KadC_version = {0,1,0};

extern const specialxtable sxt[];

#define arraysize(a) (sizeof(a) / sizeof(a[0]))

static const pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;


KadCcontext KadC_start(char *inifilename, int leafmode, int init_network) {
	KadCcontext kcc = {0};
	unsigned long int ip;	/* in host byte order */
	int port;
	int status;
	peernode mynode;
	int ncontacts;
	KadEngine *pKE;
	UDPIO *pul;
	int maxcontacts = 2048;
	void *Ocontacts;
	FILE *inifile = NULL;
#if 0
	void *Econtacts;
	void *Rcontacts;
#endif

	srand(time(NULL));	/* we don't need crypto-grade randomicity here */

	kcc.inifilename = inifilename;
	inifile = fopen(inifilename, "r+b");
	if(inifile == NULL) {
		kcc.errmsg1 = "can't open";
		kcc.errmsg2 = inifilename;
		kcc.s = KADC_START_CANT_OPEN_INI_FILE;
		return kcc;
	}


#ifdef __WIN32__
	if(init_network) {
	status = wsockstart();
		if(status) {
			kcc.errmsg1 = "WSAStartup(MAKEWORD(1, 1), &wsaData) error";
			kcc.errmsg2 = WSAGetLastErrorMessage(status);
			kcc.s = KADC_START_WSASTARTUP_ERROR;
			return kcc;
		}
	}
#endif

	Ocontacts = rbt_new((rbtcomp *)int128lt, (rbtcomp *)int128eq);
	if((ncontacts = overnetinifileread(inifile, &mynode, Ocontacts, maxcontacts)) < 0) {
		static char errnum[16];	/* we don't need to be thread safe here */
		fclose(inifile);
		sprintf(errnum, "%d", ncontacts);
		kcc.errmsg1 = "overnetinifileread() failed";
		kcc.errmsg2 = errnum;
		kcc.s = KADC_START_OVERNET_SECTION_INI_FILE_ERROR;
		return kcc;
	}

#if 0
	Econtacts = rbt_new((rbtcomp *)int128lt, (rbtcomp *)int128eq);
	if((ncontacts = eMuleKADinifileread(kcc.inifile, &mynode, Econtacts, maxcontacts)) < 0) {
		static char errnum[16];	/* we don't need to be thread safe here */
		sprintf(errnum, "%d", ncontacts);
		kcc.errmsg1 = "eMuleKADinifileread() failed";
		kcc.errmsg2 = errnum;
		kcc.s = KADC_START_EMULEKAD_SECTION_INI_FILE_ERROR;
		return kcc;
	}

	Rcontacts = rbt_new((rbtcomp *)int128lt, (rbtcomp *)int128eq);
	if((ncontacts = revconnectinifileread(kcc.inifile, &mynode, Rcontacts, maxcontacts)) < 0) {
		static char errnum[16];	/* we don't need to be thread safe here */
		sprintf(errnum, "%d", ncontacts);
		kcc.errmsg1 = "revconnectinifileread() failed";
		kcc.errmsg2 = errnum;
		kcc.s = KADC_START_REVCONNECT_SECTION_INI_FILE_ERROR;
		return kcc;
	}
#endif

	ip = mynode.ip;
	if(ip == 0xffffffff) {
		fclose(inifile);
		kcc.errmsg1 = "Invalid domain or IP address for local node in INI file";
		kcc.errmsg2 = inifilename;
		kcc.s = KADC_START_BAD_LOCALNODE_ADDRESS;
		return kcc;
	}
	port = mynode.port;

	kcc.pul = malloc(sizeof(UDPIO));
	if(kcc.pul == NULL) {
		fclose(inifile);
		kcc.errmsg1 = "malloc(sizeof(UDPIO)) returned NULL";
		kcc.errmsg2 = "no memory";
		kcc.s = KADC_START_NO_MEMORY;
		return kcc;
	}
	pul = kcc.pul;
	memset(pul, 0, sizeof(UDPIO));
	pul->bufsize = 2048;
	pul->localip = ip;
	pul->localport = port;

	status = startUDPIO(pul);

	if(status) {
		fclose(inifile);
		kcc.errmsg1 = "startUDPIO() failed returning";
#ifdef __WIN32__
		kcc.errmsg2 = WSAGetLastErrorMessageOccurred();
#else
		kcc.errmsg2 = strerror(errno);
#endif
		kcc.s = KADC_START_UDPIO_FAILED;
		return kcc;
	}

	/* Pre-blacklist nodes found in "[blacklisted_nodes]" section */

	if(findsection(inifile, "[blacklisted_nodes]") == 0)
		node_blacklist_load(pul, inifile);

	fclose(inifile);

	/* Here we are all set: to open a session we may send packets with
	   P2PnewSessionsend(pp2ph, buffer, buflen, destip, destport)
	   (and P2Psend for continuing the same session, if necessary)
	   and each received packet will trigger a call to
	   UDPIOdispatcher(UDPIO *pul). Now start the UDP
	   handlers, associating them to the UDPIO object.
	   This will result in service routines being invoked
	   by a separate, dedicated thread (created by startUDPIO()
	   and usually waiting on a recv() on the UDP socket)
	   every time a UDP packet is received.
	 */

	if((kcc.pOKE = startKadEngine(kcc.pul, OVERNET)) == NULL) {
		kcc.errmsg1 = "startKadEngine(kcc.pul, OVERNET) failed";
		kcc.errmsg2 = "";
		kcc.s = KADC_START_OVERNET_KADENGINE_FAILED;
		return kcc;
	}
	pKE = kcc.pOKE;
	pKE->contacts = Ocontacts;
	pKE->maxcontacts = maxcontacts;
	pKE->leafmode = leafmode;
	pKE->localnode = mynode;	/* initialize KE with data read from ini file */
	setup_kba(pKE, 20);	/* 20 k-nodes per k-bucket */

#if 0
	if((kcc.pEKE = startKadEngine(kcc.pul, EMULE)) == NULL) {
		kcc.errmsg1 = "startKadEngine(kcc.pul, EMULE) failed";
		kcc.errmsg2 = "";
		kcc.s = KADC_START_EMULEKAD_KADENGINE_FAILED;
		return kcc;
	}
	pKE = kcc.pEKE;
	pKE->contacts = Econtacts;
	pKE->maxcontacts = maxcontacts;
	pKE->leafmode = leafmode;
	pKE->localnode = mynode;	/* initialize KE with data read from ini file */
	setup_kba(pKE, 100);	/* 20 k-nodes per k-bucket */

	if((kcc.pRKE = startKadEngine(kcc.pul, REVCONNECT)) == NULL) {
		kcc.errmsg1 = "startKadEngine(kcc.pul, REVCONNECT) failed";
		kcc.errmsg2 = "";
		kcc.s = KADC_START_REVCONNECT_KADENGINE_FAILED;
		return kcc;
	}
	pKE = kcc.pRKE;
	pKE->contacts = Rcontacts;
	pKE->maxcontacts = maxcontacts;
	pKE->leafmode = leafmode;
	pKE->localnode = mynode;	/* initialize KE with data read from ini file */
	setup_kba(pKE, 100);	/* 20 k-nodes per k-bucket */
#endif

	if((status = startRTP(pul)) != 0) {
		static char errnum[16];	/* we don't need to be thread safe here */
		sprintf(errnum, "%d", status);
		kcc.errmsg1 = "startRTP(kcc.pul) failed";
		kcc.errmsg2 = errnum;
		kcc.s = KADC_START_RTP_FAILED;
		return kcc;
	}

	pKE = kcc.pOKE;
	pthread_create(&pKE->BGth, NULL, &OvernetBGthread, pKE);
#if 0
	pKE = kcc.pEKE;
	pthread_create(&pKE->BGth, NULL, &eMuleBGthread, pKE);
	pKE = kcc.pRKE;
	pthread_create(&pKE->BGth, NULL, &RevConnectBGthread, pKE);
#endif

	kcc.s = KADC_OK;
	kcc.errmsg1 = "";
	kcc.errmsg2 = "";
	return kcc;
}

int KadC_init_network(void) {
	int status;
#ifdef __WIN32__
	status = wsockstart();
#else
	status = 0;
#endif
	return status;
}

KadC_status KadC_stop(KadCcontext *pkcc) {
	/* char rfilename[256]; */
	int status;
	KadEngine *pOKE;
#if 0
	KadEngine *pEKE, *pRKE;
#endif
	UDPIO *pul;
	char *inifilename;

	if(pkcc == NULL)
		return KADC_NEVER_STARTED;

	pOKE = pkcc->pOKE;
#if 0
	pEKE = pkcc->pEKE;
	pRKE = pkcc->pRKE;
#endif
	pul = pkcc->pul;
	inifilename = pkcc->inifilename;

	status = KadC_write_inifile(pkcc, NULL);

	if (status != KADC_OK) 
		return status;
			
	if(pOKE != NULL)
		pOKE->shutdown = 1;	/* tell background threads to shut down cleanly */
#if 0
	if(pEKE != NULL)
		pEKE->shutdown = 1;	/* tell background threads to shut down cleanly */
	if(pRKE != NULL)
		pRKE->shutdown = 1;	/* tell background threads to shut down cleanly */
#endif

	status = stopUDPIO(pul); /* close UDP I/O */
	if(status) {
		static char errnum[16];	/* we don't need to be thread safe here */
		sprintf(errnum, "%d", status);
		pkcc->errmsg1 = "stopUDPIO(&pkcc-pul) failed:";
#ifdef __WIN32__
		pkcc->errmsg2 = WSAGetLastErrorMessageOccurred();
#else
		pkcc->errmsg2 = strerror(errno);
#endif
		pkcc->s = KADC_STOP_UDPIO_FAILED;
		return pkcc->s;
	}

	if(pOKE != NULL)
		pthread_join(pOKE->BGth, NULL); /* (that thread will in turn wait for its subthreads to terminate) */
#if 0
	if(pEKE != NULL)
		pthread_join(pEKE->BGth, NULL); /* (that thread will in turn wait for its subthreads to terminate) */
	if(pEKE != NULL)
		pthread_join(pRKE->BGth, NULL); /* (that thread will in turn wait for its subthreads to terminate) */
#endif

	if(pOKE != NULL) {
		int rbt_status;
		/* Destroy resources allocated in KadC_start() */
		for(;;) {
			peernode *ppn;
			void *iter;
			
			iter = rbt_begin(pOKE->contacts);
			if(iter == NULL)
				break;
			ppn = rbt_value(iter);
			rbt_erase(pOKE->contacts, iter);
			free(ppn);
		}
		rbt_status = rbt_destroy(pOKE->contacts);
		assert(rbt_status == RBT_STATUS_OK); /* otherwise rbt wasn't empty... */		
	}

#if 0
	if(pEKE != NULL) {
		/* Prepare to replace a new section: open for write a new wfilename */
		wfile = fopen(wfilename, "w+b");
		if(wfile == NULL) {
			pkcc->errmsg1 = "can't create";
			pkcc->errmsg2 = wfilename;
			pkcc->s = KADC_STOP_CANT_CREATE_NEW_INI_FILE;
			return pkcc->s;
		}
		/* open for read as rfilename what used to be the wfile */
		inifile = fopen(rfilename, "r+b");
		if(inifile == NULL) {
			pkcc->errmsg1 = "can't open";
			pkcc->errmsg2 = rfilename;
			pkcc->s = KADC_START_CANT_OPEN_INI_FILE;
			return pkcc->s;
		}
		status = eMuleKADinifileupdate(inifile, wfile, pEKE);
		if(status < 0) {
			static char errnum[16];	/* we don't need to be thread safe here */
			sprintf(errnum, "%d", status);
			pkcc->errmsg1 = "eMuleKADinifileupdate(inifile, wfile, pOKE) failed:";
			pkcc->errmsg2 = errnum;
			pkcc->s = KADC_STOP_EMULEKADINIFILEUPDATE_FAILED;
			return pkcc->s;
		}

		fclose(inifile);
		fclose(wfile);
		unlink(rfilename);	/* if it exists, it's associated with the inifile FILE* */
		rename(wfilename, rfilename);	/* the file just written gets the name of the original .ini */
	}

	if(pRKE != NULL) {
		/* Prepare to replace a new section: open for write a new wfilename */
		wfile = fopen(wfilename, "w+b");
		if(wfile == NULL) {
			pkcc->errmsg1 = "can't create";
			pkcc->errmsg2 = wfilename;
			pkcc->s = KADC_STOP_CANT_CREATE_NEW_INI_FILE;
			return pkcc->s;
		}
		/* open for read as rfilename what used to be the wfile */
		inifile = fopen(rfilename, "r+b");
		if(inifile == NULL) {
			pkcc->errmsg1 = "can't open";
			pkcc->errmsg2 = rfilename;
			pkcc->s = KADC_START_CANT_OPEN_INI_FILE;
			return pkcc->s;
		}
		status = revconnectinifileupdate(inifile, wfile, pRKE);
		if(status < 0) {
			static char errnum[16];	/* we don't need to be thread safe here */
			sprintf(errnum, "%d", status);
			pkcc->errmsg1 = "revconnectinifileupdate(inifile, wfile, pOKE) failed:";
			pkcc->errmsg2 = errnum;
			pkcc->s = KADC_STOP_REVCONNECTINIFILEUPDATE_FAILED;
			return pkcc->s;
		}

		fclose(inifile);
		fclose(wfile);
		unlink(rfilename);	/* if it exists, it's associated with the inifile FILE* */
		rename(wfilename, rfilename);	/* the file just written gets the name of the original .ini */
	}
#endif

	if(pOKE != NULL) {
		destroy_kba(pOKE);
		status = stopKadEngine(pOKE);
		if(status) {
			static char errnum[16];	/* we don't need to be thread safe here */
			sprintf(errnum, "%d", status);
			pkcc->errmsg1 = "stopKadEngine(pOKE) failed:";
			pkcc->errmsg2 = errnum;
			pkcc->s = KADC_STOP_OVERNET_FAILED;
			return pkcc->s;
		}
	}

#if 0
	if(pEKE != NULL) {
		destroy_kba(pEKE);
		status = stopKadEngine(pEKE);
		if(status) {
			static char errnum[16];	/* we don't need to be thread safe here */
			sprintf(errnum, "%d", status);
			pkcc->errmsg1 = "stopKadEngine(pEKE) failed:";
			pkcc->errmsg2 = errnum;
			pkcc->s = KADC_STOP_EMULEKAD_FAILED;
			return pkcc->s;
		}
	}

	if(pRKE != NULL) {
		destroy_kba(pRKE);
		status = stopKadEngine(pRKE);
		if(status) {
			static char errnum[16];	/* we don't need to be thread safe here */
			sprintf(errnum, "%d", status);
			pkcc->errmsg1 = "stopKadEngine(pRKE) failed:";
			pkcc->errmsg2 = errnum;
			pkcc->s = KADC_STOP_REVCONNECT_FAILED;
			return pkcc->s;
		}
	}
#endif

	status = stopRTP(pul);
	if(status) {
		static char errnum[16];	/* we don't need to be thread safe here */
		sprintf(errnum, "%d", status);
		pkcc->errmsg1 = "stopRTP(pul) failed:";
		pkcc->errmsg2 = errnum;
		pkcc->s = KADC_STOP_RTP_FAILED;
		return pkcc->s;
	}

	/* only after all threads doing UDP I/O have joined,
	   and after calls to stopKadEngine etc. which writes
	   to elements of pul->callback[]... */

	pthread_mutex_destroy(&pul->mutex);
	free(pul);

	pkcc->errmsg1 = "";
	pkcc->errmsg2 = "";
	pkcc->s = KADC_OK;
	return pkcc->s;
}

KadC_status KadC_write_inifile(KadCcontext *pkcc, const char *target_file) {
	char tfilename[PATH_MAX];
	KadEngine *pOKE;
	char *inifilename;
	FILE *inifile, *wfile;
	int blacklist_written = 0;
	
	if(pkcc == NULL)
		return KADC_NEVER_STARTED;

	pOKE = pkcc->pOKE;

	inifilename = pkcc->inifilename;
	inifile     = fopen(inifilename, "r+b");

	if(inifile == NULL) {
		pkcc->errmsg1 = "can't open";
		pkcc->errmsg2 = inifilename;
		pkcc->s = KADC_START_CANT_OPEN_INI_FILE;
		return pkcc->s;
	}

	if (target_file != NULL) {
		wfile = fopen(target_file, "w+b");
	} else {
		strncpy(tfilename, inifilename, sizeof(tfilename)-2);
		tfilename[strlen(inifilename)] = '_';
		tfilename[strlen(inifilename)+1] = 0;
		wfile = fopen(tfilename, "w+b");
	}
			
	if(wfile == NULL) {
		fclose(inifile);
		pkcc->errmsg1 = "can't create";
		pkcc->errmsg2 = (char *)target_file;
		pkcc->s = KADC_STOP_CANT_CREATE_NEW_INI_FILE;
		return pkcc->s;
	}

	for (;;) {
		char section[80];
		int status;
		status = tonextsection(inifile, section, sizeof(section));
		if (status == -1) break;
		fprintf(wfile, "%s\n", section);
	#ifdef DEBUG
		KadC_log("KadC_write_inifile: found section %s\n", section);
	#endif
		
		if (strcasecmp(section, "[blacklisted_nodes]") == 0) {
		#ifdef DEBUG
			KadC_log("KadC_write_inifile: writing blacklisted nodes\n");
		#endif
			node_blacklist_dump(pkcc->pul, wfile);	
			blacklist_written = 1;
		} else if (strcasecmp(section, "[overnet_peers]") == 0) {
		#ifdef DEBUG
			KadC_log("KadC_write_inifile: writing overnet_peers\n");
		#endif
			overnetinifilesectionwrite(wfile, pOKE);
		} else {
			copyuntilnextsection(inifile, wfile);
		}
	}  

	if (!blacklist_written) {
		#ifdef DEBUG
			KadC_log("KadC_write_inifile: blacklisted nodes section not found, writing now\n");
		#endif
		fprintf(wfile, "[blacklisted_nodes]\n");
		node_blacklist_dump(pkcc->pul, wfile);
	}

	#ifdef DEBUG
		KadC_log("KadC_write_inifile: done and closing\n");
	#endif
	
	fclose(inifile);
	fclose(wfile);

	if (target_file == NULL) {
	#ifdef DEBUG
		KadC_log("KadC_write_inifile: renaming %s to %s\n", tfilename, inifilename);
	#endif
		remove(inifilename); /* Remove the old one */
		if (rename(tfilename, inifilename)) {
			KadC_log("WARNING: failed rename %s -> %s: %d (%s)\n",
			         tfilename, inifilename, errno, strerror(errno));
		}
	}
	
	return KADC_OK;
}


int KadC_getnknodes(KadCcontext *pkcc) {
	KadEngine *pKE;
	int nknodes = 0;

	if(pkcc == NULL)
		return 0;

	pKE = pkcc->pOKE;
	if(pKE != NULL) {
		nknodes += knodes_count(pKE);
	}

	pKE = pkcc->pEKE;
	if(pKE != NULL) {
		nknodes += knodes_count(pKE);
	}

	pKE = pkcc->pRKE;
	if(pKE != NULL) {
		nknodes += knodes_count(pKE);
	}

	return nknodes;
}


int KadC_getncontacts(KadCcontext *pkcc) {
	KadEngine *pKE;
	int ncontacts = 0;

	if(pkcc == NULL)
		return 0;

	pKE = pkcc->pOKE;
	if(pKE != NULL) {
		pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK contacts \\\\\\ */
		ncontacts += rbt_size(pKE->contacts);
		pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK contacts ///// */
	}

	pKE = pkcc->pEKE;
	if(pKE != NULL) {
		pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK contacts \\\\\\ */
		ncontacts += rbt_size(pKE->contacts);
		pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK contacts ///// */
	}

	pKE = pkcc->pRKE;
	if(pKE != NULL) {
		pthread_mutex_lock(&pKE->cmutex);	/* \\\\\\ LOCK contacts \\\\\\ */
		ncontacts += rbt_size(pKE->contacts);
		pthread_mutex_unlock(&pKE->cmutex);	/* ///// UNLOCK contacts ///// */
	}
	return ncontacts;
}

/******** FIXME: Most functions below refer only to the
          Overnet flavour. If/when the other flavors will
          be added, they should be extended as appropriate. ********/


/* returns
	0 if status not checked
	1 if we are TCP-firewalled
	2 if we are not TCP-firewalled
 */
int KadC_getfwstatus(KadCcontext *pkcc) {
	KadEngine *pKE;

	if(pkcc == NULL || (pKE = pkcc->pOKE) == NULL)
		return 0;

	if(pKE->fwstatuschecked == 0)
		return 0;
	else if(pKE->notfw == 0)
		return 1;
	else
		return 2;
}

/* returns our IP address as seen by other peers */
unsigned long int KadC_getextIP(KadCcontext *pkcc) {
	KadEngine *pKE;

	if(pkcc == NULL || (pKE = pkcc->pOKE) == NULL)
		return 0;

	return pKE->extip;
}

/* returns our local hash ID (as pointer to a 16-byte buffer) */
int128 KadC_getourhashID(KadCcontext *pkcc) {
	KadEngine *pKE;
	static unsigned char zero[16] = {0};

	if(pkcc == NULL || (pKE = pkcc->pOKE) == NULL)
		return zero;

	return pKE->localnode.hash;
}

/* returns our UDP port number */
unsigned short int KadC_getourUDPport(KadCcontext *pkcc) {
	KadEngine *pKE;

	if(pkcc == NULL || (pKE = pkcc->pOKE) == NULL)
		return 0;
	return pKE->localnode.port;
}

/* returns our TCP port number */
unsigned short int KadC_getourTCPport(KadCcontext *pkcc) {
	KadEngine *pKE;

	if(pkcc == NULL || (pKE = pkcc->pOKE) == NULL)
		return 0;

	return pKE->localnode.tport;
}

#ifndef OLD_SEARCH_ONLY

KadCfind_params *KadCfind_init(KadCfind_params *p) {
	memset(p, 0, sizeof(KadCfind_params));	
	return p;
}

/* search index [filter [nthreads [duration]]] */
void *KadC_find(KadCcontext *pkcc, const char *index, const char *filter, int nthreads, int maxhits, int duration) {
	KadCfind_params fpar;
	KadCfind_init(&fpar);
	
	fpar.filter   = filter;
	fpar.threads  = nthreads;
	fpar.max_hits = maxhits;
	fpar.duration = duration;
	
	return KadC_find2(pkcc, index, &fpar);
}

void *KadC_find2(KadCcontext *pkcc, const char *index, KadCfind_params *pfpar) {
	KadEngine *pKE;
	unsigned char hashbuf[16];
	kstore *pks;
	int nhits;
	unsigned char *pnsf;
	unsigned char *psf;
	unsigned char *psfilter;
	int sfilterlen;
	KadC_parsedfilter pf;
	time_t stoptime;
	void *rbt;

	if(pkcc == NULL || (pKE = pkcc->pOKE) == NULL || pfpar == NULL)
		return NULL;

	if(pfpar->threads < 1)
		pfpar->threads = 10;

	if(pfpar->duration < 1)
		pfpar->duration = 15;	/* default 15s of search */

	stoptime = time(NULL)+pfpar->duration;

	if(index[0] == '#') {
		if(index[1] != 0)
			string2int128(hashbuf, (char *)index+1);
		else
			memmove(hashbuf, pKE->localnode.hash, 16);
	}
	else
		MD4((unsigned char *)hashbuf, (unsigned char *)index, strlen(index));

	KadC_log("Searching for hash ");
	KadC_int128flog(stdout, hashbuf);
	KadC_log("...\n");

	/* pnsf = make_nsfilter(filter); */
	pf = KadC_parsefilter((char *)pfpar->filter);
	if(pf.err) {
		printf("Parsing failure: %s%s\n", pf.errmsg1, pf.errmsg2);
		return NULL;
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

	pks = Overnet_find2(pKE, hashbuf, 1, psfilter, sfilterlen, &stoptime, pfpar);

	nhits = rbt_size(pks->rbt);

	if(pnsf != NULL)
		free(pnsf);

	rbt = pks->rbt;

	pthread_mutex_destroy(&pks->mutex);
	free(pks);	/* we don't need the wrapper anymore */

	/* If callback used, do not return rbt but NULL. Free
	   it automatically
	 */
	if (pfpar->hit_callback != NULL &&
	    pfpar->hit_callback_mode != KADC_COLLECT_HITS) {
#ifdef VERBOSE_DEBUG
		KadC_log("hit_callback != NULL, destroying rbt and returning NULL, nhits: %d\n", nhits);
#endif
		assert(nhits == 0);
		rbt_destroy(rbt);
		rbt = NULL;
	}
	return rbt;
}
#else
/* search index [filter [nthreads [duration]]] */
void *KadC_find(KadCcontext *pkcc, const char *index, const char *filter, int nthreads, int maxhits, int duration) {
	KadEngine *pKE;
	unsigned char hashbuf[16];
	kstore *pks;
	int nhits;
	unsigned char *pnsf;
	unsigned char *psf;
	unsigned char *psfilter;
	int sfilterlen;
	KadC_parsedfilter pf;
	time_t stoptime;
	void *rbt;

	if(pkcc == NULL || (pKE = pkcc->pOKE) == NULL)
		return NULL;

	if(nthreads < 1)
		nthreads = 10;

	if(duration < 1)
		duration = 15;	/* default 15s of search */

	stoptime = time(NULL)+duration;

	if(index[0] == '#') {
		if(index[1] != 0)
			string2int128(hashbuf, (char *)index+1);
		else
			memmove(hashbuf, pKE->localnode.hash, 16);
	}
	else
		MD4((unsigned char *)hashbuf, (unsigned char *)index, strlen(index));

	KadC_log("Searching for hash ");
	KadC_int128flog(stdout, hashbuf);
	KadC_log("...\n");

	/* pnsf = make_nsfilter(filter); */
	pf = KadC_parsefilter((char *)filter);
	if(pf.err) {
		printf("Parsing failure: %s%s\n", pf.errmsg1, pf.errmsg2);
		return NULL;
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

	pks = Overnet_find(pKE, hashbuf, 1, psfilter, sfilterlen, &stoptime, maxhits, nthreads);

	nhits = rbt_size(pks->rbt);

	if(pnsf != NULL)
		free(pnsf);

	rbt = pks->rbt;

	pthread_mutex_destroy(&pks->mutex);
	free(pks);	/* we don't need the wrapper anymore */

	return rbt;
}
#endif

int KadC_republish(KadCcontext *pkcc, const char *index, const char *value, const char *metalist,  int nthreads, int duration) {
	KadEngine *pKE;
	unsigned char khashbuf[16];
	unsigned char vhashbuf[16];
	time_t stoptime = time(NULL)+duration;
	kobject *pko;
	int n_published;

	if(pkcc == NULL || (pKE = pkcc->pOKE) == NULL)
		return -1;

	if(index[0] == '#') {
		string2int128(khashbuf, (char *)index+1);
	} else {
		MD4((unsigned char *)khashbuf, (unsigned char *)index, strlen(index));
	}

	if(value[0] == '#') {
		if(value[1] != 0)
			string2int128(vhashbuf, (char *)value+1);
	} else {
		MD4((unsigned char *)vhashbuf, (unsigned char *)value, strlen(value));
	}

	pko = make_kobject(khashbuf, vhashbuf, metalist);
	if(pko == NULL) {
		return -1;
	}
/*
	KadC_log("Publishing k-object ");
	kobject_dump(pko, ";");
	KadC_log("\n");
*/
	/* this is an "instant publishing": pko is not stored for
	   periodic auto-republishing by BGthread (that is to be implemented) */
	n_published = overnet_republishnow(pKE, pko, &stoptime, nthreads);

	kobject_destroy(pko);
	return n_published;
}


int128 KadCdictionary_gethash(KadCdictionary *pkd) {
	kobject *pko = pkd;
	if(pko == NULL || pko->size < 32)
		return NULL;
	else
		return (int128)&(pko->buf[16]);
}

static KadCtag_type decodenext(KadCtag_iter *iter);

KadCtag_type KadCtag_begin(KadCdictionary *pkd, KadCtag_iter *iter) {
	kobject *pko = pkd;
	unsigned char *p;
	if(pkd == NULL)
		return KADCTAG_NOTFOUND;
	if(pko->size < 16+16+4)
		return KADCTAG_INVALID;
	iter->bufend = pko->buf+pko->size;
	iter->khash = pko->buf;
	iter->vhash = pko->buf+16;
	p = pko->buf+16+16;
	iter->tagsleft = getulongle(&p);
	iter->pb = p;
	return decodenext(iter);
}

KadCtag_type KadCtag_next(KadCtag_iter *iter) {
	return decodenext(iter);
}

KadCtag_type KadCtag_find(KadCdictionary *pkd, char *name, KadCtag_iter *iter) {
	KadCtag_type ktype;
	int namelen;
	if(pkd == NULL || name == NULL || iter == NULL)
		return KADCTAG_INVALID;
	namelen = strlen(name);
	ktype = KadCtag_begin(pkd, iter);
	for(;;) {
		if(ktype == KADCTAG_NOTFOUND || ktype == KADCTAG_INVALID)
			break;
		if((namelen == 1 && name[0] == iter->tagname[0]) || /* specials */
		   (namelen > 1 && strcasecmp(name, iter->tagname) == 0))
			return iter->tagtype;
		ktype = decodenext(iter);
	}
	return ktype;
}

static KadCtag_type decodenext(KadCtag_iter *iter) {
	int nslen;
	int tagtype;

	if(iter->tagsleft <= 0)
		return KADCTAG_NOTFOUND;
	if(iter->pb+1+2 > iter->bufend)	/* space for type and name length */
		return KADCTAG_INVALID;
	tagtype = *(iter->pb)++;
	nslen = getushortle((unsigned char **)&(iter->pb));			/* name length */
	if(iter->pb+nslen > iter->bufend)	/* space for type and name length */
		return KADCTAG_INVALID;
	iter->tagname[0] = 0;
	if(nslen == 1) {	/* maybe a special? */
		int i;
		for(i = 0; sxt[i].name != NULL; i++) {
			if(sxt[i].code[0] == *(iter->pb)) {
				strcpy(iter->tagname, sxt[i].name);
				break;
			}
		}
	}
	if(iter->tagname[0] == 0) {	/* if not already replaced by special or its expansion */
		memcpy(iter->tagname, iter->pb, nslen);
		iter->tagname[nslen] = 0;	/* add ASCIIZ terminator */
	}
	iter->pb += nslen;	/* skip copied name string */

	if(tagtype == EDONKEY_MTAG_HASH) {
		if(iter->pb+16 > iter->bufend)	/* space for hash */
			return KADCTAG_INVALID;
		memcpy(iter->tagvalue, iter->pb, 16);
		iter->pb += 16;				/* skip copied hash */
		iter->tagtype = KADCTAG_HASH;
	} else if(tagtype == EDONKEY_MTAG_STRING) {
		if(iter->pb+2 > iter->bufend)	/* space for name length */
			return KADCTAG_INVALID;
		nslen = getushortle((unsigned char **)&iter->pb);
		if(iter->pb+nslen > iter->bufend)	/* space for string */
			return KADCTAG_INVALID;
		memcpy(iter->tagvalue, iter->pb, nslen);
		iter->tagvalue[nslen] = 0;	/* add ASCIIZ terminator */
		iter->pb += nslen;	/* skip copied name string */
		iter->tagtype = KADCTAG_STRING;
	} else if(tagtype == EDONKEY_MTAG_DWORD) {
		unsigned long int ul = getulongle((unsigned char **)&iter->pb);	/* also skip ulong */
		*(unsigned long int *)iter->tagvalue = ul;	/* endianity independent */
		iter->tagtype = KADCTAG_ULONGINT;
	} else {
		iter->tagtype = KADCTAG_INVALID;
	}
	return iter->tagtype;
}

void KadCdictionary_destroy(KadCdictionary *pkd) {
	free( ((kobject *)pkd)->buf);
	free(pkd);
}

void KadCdictionary_dump(KadCdictionary *pkd) {
	KadCtag_iter iter;
	int i;

	for(i = 0, KadCtag_begin(pkd, &iter); i < iter.tagsleft; i++, KadCtag_next(&iter)) {
		if(i > 0)
			KadC_log(";");
		KadC_log("%s=", iter.tagname);
		if(iter.tagtype == KADCTAG_HASH)
			int128print(stdout, (int128)iter.tagvalue);
		else if(iter.tagtype == KADCTAG_STRING)
			KadC_log("%s", (char *)iter.tagvalue);
		else if(iter.tagtype == KADCTAG_ULONGINT)
			KadC_log("%lu", *(unsigned long int *)iter.tagvalue);
		else {
			KadC_log("*INVALID TAG*");
			break;
		}
	}
}

int KadC_is_blacklisted(KadCcontext *pkcc, unsigned long int ip, unsigned short int port) {
	return node_is_blacklisted((UDPIO *)(pkcc->pul), ip, port);
}

int KadC_blacklist(KadCcontext *pkcc, unsigned long int ip, unsigned short int port, int howmanysecs) {
	return node_blacklist((UDPIO *)(pkcc->pul), ip, port, howmanysecs);
}

int KadC_unblacklist(KadCcontext *pkcc, unsigned long int ip, unsigned short int port) {
	return node_unblacklist((UDPIO *)(pkcc->pul), ip, port);
}

void KadC_listkbuckets(KadCcontext *pkcc) {

	dump_kba((KadEngine *)(pkcc->pOKE));
}

void KadC_emptykbuckets(KadCcontext *pkcc) {
	erase_knodes((KadEngine *)pkcc->pOKE);
#if 0
	erase_knodes((KadEngine *)pkcc->pEKE);
#endif
#if 0
	erase_knodes((KadEngine *)pkcc->pRKE);
#endif
}
