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

/* Only skeleton for now: it just dumps the received packets */
/* This is rally part of the calling application, not of the KadC framework */
#include <pthread.h>
#include <stdio.h>
#include <Debug_pthreads.h>

#include <queue.h>
#include <net.h>
#include <KadCalloc.h>
#include <int128.h>
#include <KadClog.h>
#include <RTP.h>

/* called by UDPIOdispatcher if first byte is RTP_FLAG */
void RTP_UDPlistener(UDPIO *pul){
	int i;

	/* pul->arg[pul->buf[0]] contains NULL, or a void* preset by startRTP() */

	KadC_log("RTPin: received %d bytes\n", pul->nrecv);
	KadC_log("the sender's address/port was ");
	KadC_log("%s:%d", htoa(pul->remoteip), pul->remoteport);
	for(i=0; i < pul->nrecv; i++) {
		if((i % 16) == 0)
			KadC_log("\n");
		KadC_log("%02x ", pul->buf[i]);
	}
	KadC_log("\n================================\n");

}

/* Hook the RTP_UDPlistener to the UDPIO's UDP listener
   of the UDPIO referenced by pul. Packets starting with
   RTP_FLAG as first byte will cause a call to RTP_UDPlistener
   Optionally, a single reference to any data block may be placed in
   pul->callback[RTP_FLAG] and retrieved by the listener as (void *).
 */
int startRTP(UDPIO *pul) {

	/* setup hardware, launch thread to handle mike and send RTP stream */
	/* (not implemented yet) */

	/* Then hook up UDP listener: */

	pul->arg[RTP_FLAG] = NULL;	/* or some pointer to useful data to pass to RTPin */
	pul->callback[RTP_FLAG] = RTP_UDPlistener;

	return 0;
}

/* stop the RTP in callback and RTP out thread */

int stopRTP(UDPIO *pul) {

	/* Unhook UDP listener */
	pul->callback[RTP_FLAG] = NULL;
	pul->arg[RTP_FLAG] = NULL;	/* unhook from udp_io_thread */

	/* Now stop mike thread, and shut down audio-related stuff */
	/* (not implemented yet) */

	return 0;
}
