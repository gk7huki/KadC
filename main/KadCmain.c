#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <int128.h>
#include <rbt.h>
#include <KadCalloc.h>
#include <KadClog.h>
#include <config.h>
#include <queue.h>	/* required by net.h, sigh... */
#include <net.h> /* only for domain2hip() */

#include <KadCapi.h>

static void CommandLoop(KadCcontext *pkcc);

#ifdef OLD_STYLE_SIGNAL

void sighandler(int sig) {
	close(0);	/* so that read(0) in KadC_getsn will fail */
	signal(sig, SIG_IGN);
}

#else

/* This thread waits for TERM and INT signals, and when
   they are found, sets the shutdown_flag */
void* sig_handler_th(void* arg) {
	sigset_t signal_set;
	int sig;
	int done = 0;

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
		if(! done) {
			close(0);
			done = 1;
#ifdef DEBUG
			KadC_log("Signal %d caught, fd 0 closed\n", sig);
#endif
		}
	}

	return NULL;
}

#endif


int main(int ac, char *av[]) {
	KadCcontext kcc;
	KadC_status kcs;
#ifndef OLD_STYLE_SIGNAL
	sigset_t signal_set;
	pthread_t sig_thread;
#endif

	KadC_log("KadC - library version: %d.%d.%d\n",
		KadC_version.major, KadC_version.minor, KadC_version.patchlevel);

	if(ac < 2) {
		KadC_log("usage: %s inifile.ini [leafmode]\n", av[0]);
		return 1;
	}

#ifdef OLD_STYLE_SIGNAL

	signal(SIGTERM, sighandler);	/* for the TERM signal (15, default for kill command)...  */
	signal(SIGINT, sighandler); 	/* .. the INT signal... (Ctrl-C  hit on keyboard) */

#else

	/* block signals we'll wait for */
	sigemptyset(&signal_set);
	sigaddset(&signal_set, SIGTERM);
	sigaddset(&signal_set, SIGINT);

	pthread_sigmask(SIG_BLOCK, &signal_set,	NULL);

	/* create the signal handling thread */
	pthread_create(&sig_thread, NULL, sig_handler_th, NULL);

#endif


	kcc = KadC_start(av[1], ac > 2, 1);
	if(kcc.s != KADC_OK) {
		KadC_log("KadC_start(%s, %d) returned error %d:\n",
			av[1], ac > 2, kcc.s);
		KadC_log("%s %s", kcc.errmsg1, kcc.errmsg2);
		return 2;
	}

	CommandLoop(&kcc); /* temporary */

	KadC_log("Shutting down, please wait...\n");

	kcs = KadC_stop(&kcc);
	if(kcs != KADC_OK) {
		KadC_log("KadC_stop(&kcc) returned error %d:\n", kcc.s);
		KadC_log("%s %s", kcc.errmsg1, kcc.errmsg2);
		return 3;
	}

	KadC_list_outstanding_mallocs(10);
	return 0;
}


#define PARSIZE 16

static void CommandLoop(KadCcontext *pkcc) {
	for(;;) {
		char line[4096];
		int linelen;
		char *p;
		char *par[PARSIZE];
		int ntok;
		int i;
		int fwstatus;
		char ourhash[34] = {"#"};
		unsigned long int extip;

		int128sprintf(ourhash+1, KadC_getourhashID(pkcc));
		extip = KadC_getextIP(pkcc);
		fwstatus = KadC_getfwstatus(pkcc);
		KadC_log("%4d/%4d%s ",
			KadC_getnknodes(pkcc), KadC_getncontacts(pkcc),
			(fwstatus>0 ? (fwstatus>1 ? ">" : "]") : "?"));

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

		if(strcmp(par[0], "s") == 0 || strcmp(par[0], "search") == 0) {
			void *iter;
			KadCdictionary *pkd;
			char *index = par[1];
			char *filter = par[2];
			int nthreads = atoi(par[3]);
			int duration = atoi(par[4]);
			int maxhits = atoi(par[5]);
			time_t starttime = time(NULL);
			void *resdictrbt;
			int nhits;

			if(maxhits == 0)
				maxhits = 100;	/* default to max 100 hits */

			/* search index [filter [nthreads [duration]]] */

			if(strcmp(index, "#") == 0)
				index = ourhash;

			resdictrbt = KadC_find(pkcc, index, filter, nthreads, maxhits, duration);

			nhits = rbt_size(resdictrbt);

			/* list each KadCdictionary returned in the rbt */
			for(iter = rbt_begin(resdictrbt); iter != NULL; iter = rbt_next(resdictrbt, iter)) {
				pkd = rbt_value(iter);

				KadC_log("Found: ");
				KadC_int128flog(stdout, KadCdictionary_gethash(pkd));
				KadC_log("\n");
				KadCdictionary_dump(pkd);
				KadC_log("\n");
			}
			KadC_log("Search completed in %d seconds - %d hit%s returned\n",
				time(NULL)-starttime, nhits, (nhits == 1 ? "" : "s"));

			for(iter = rbt_begin(resdictrbt); iter != NULL; iter = rbt_begin(resdictrbt)) {
				pkd = rbt_value(iter);
				rbt_erase(resdictrbt, iter);
				KadCdictionary_destroy(pkd);
			}
			rbt_destroy(resdictrbt);

		} else if(strcmp(par[0], "p") == 0 || strcmp(par[0], "publish") == 0) {
			/* publish {#[khash]|key} {#[vhash]|value} [meta-list [nthreads [nsecs]]] */
			char *index = par[1];
			char *value = par[2];
			char *metalist = par[3];
			int nthreads = atoi(par[4]);
			int duration = atoi(par[5]);
			int status;

			if(nthreads < 1)
				nthreads = 10;

			if(duration < 1)
				duration = 15;	/* default 15s of lookup */

			if(strcmp(index, "#") == 0)
				index = ourhash;

			if(strcmp(value, "#") == 0)
				value = ourhash;

			status = KadC_republish(pkcc, index, value, metalist, nthreads, duration);
			if(status == 1) {
				KadC_log("Syntax error preparing search. Try: p key #hash [tagname=tagvalue[;...]]\n");
			}
		} else if(strcmp(par[0], "d") == 0 || strcmp(par[0], "dump") == 0) {
			KadC_listkbuckets(pkcc);
		} else if(strcmp(par[0], "w") == 0 || strcmp(par[0], "write_inifile") == 0) {
			KadC_write_inifile(pkcc, NULL);
		} else if(strcmp(par[0], "q") == 0 || strcmp(par[0], "quit") == 0) {
			break;
		} else if(strcmp(par[0], "b") == 0 || strcmp(par[0], "blacklist") == 0) {
			KadC_blacklist(pkcc, domain2hip(par[1]), atoi(par[2]), atoi(par[3]));
			KadC_log("%s:%s is now blacklisted for %d seconds\n",
				par[1], par[2], KadC_is_blacklisted(pkcc, domain2hip(par[1]), atoi(par[2])) );

		} else if(strcmp(par[0], "u") == 0 || strcmp(par[0], "unblacklist") == 0) {
			KadC_log("%s:%s was blacklisted for %d seconds, now isn't\n",
				par[1], par[2], KadC_unblacklist(pkcc, domain2hip(par[1]), atoi(par[2])) );
		} else if(strcmp(par[0], "z") == 0 || strcmp(par[0], "zeroize") == 0) {
			/* clear k-bucket table */
			KadC_emptykbuckets(pkcc);
		} else {
			#define getbyte(n,m) (((m)>>(8*(3-n))) & 0xff)
			KadC_log("Our hash ID: %s, our ext IP: %d.%d.%d.%d\n",
				ourhash+1,
				getbyte(0, extip),getbyte(1, extip),getbyte(2, extip),getbyte(3, extip));
			KadC_log("our UDP port: %u, our TCP port: %u\n",
				KadC_getourUDPport(pkcc),
				KadC_getourTCPport(pkcc));
			KadC_log("Commands:\n");
			KadC_log(" dump\n");
			/*
			KadC_log(" hello   peerIP peerUDPport\n");
			KadC_log(" fwcheck peerIP peerUDPport\n");
			 */
			KadC_log(" s[earch]  {#[hash]|keyw} {<boolean_filter>|""} [nthreads [nsecs [maxhits]]]\n");
			KadC_log(" p[ublish] {#[khash]|key} {#[vhash]|value} [name1=value1;[name2=value2...] [nthreads [nsecs]]]\n");
			KadC_log(" w[rite_inifile]\n");			
			KadC_log(" q[uit]\n");
		}
	}
}

