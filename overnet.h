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
/* puts in the buffer pointed by *ppb our IP and port.
   If pKE->leafmode is true, always puts 0; otherwise,
   if external IP is available, put that one, else
   put the internal one.
   Setting pKE->leafmode to true allows to appear as
   NATted (and therefore reduce the bandwidth) even
   if we are not. */
/* static unsigned char *overnetputourIPport(unsigned char **ppb, KadEngine *pKE); */


SessionObject *sendOvernetBootReq(KadEngine *pKE, unsigned long int bootip, int bootport);
SessionObject *sendOvernetHelloReq(KadEngine *pKE, unsigned long int bootip, int bootport);
SessionObject *sendOvernetFwReq(KadEngine *pKE, int mytcpport, unsigned long int recipientip, int recipientport);

int OvernetDumppkt(packet *pkt);
void OvernetServerThread(SessionObject *psession, packet *pkt);
int isOvernetREQ(unsigned char opcode);

/* read overnet nodes for boot from INI file; return number of peers read,
   or a negative error code in case of errors */
int overnetinifileread(FILE *inifile, peernode *pmynode, void *contacts, int maxcontacts);

/* Save contacts under the section "[overnet_peers]", then free
   them and destroy the rbt. NEVER USE CONTACTS RBT AFTER CALLING THIS!! */
int overnetinifileupdate(FILE *inifile, FILE *wfile, KadEngine *pKE);

/* Just writes the inifile section without freeing anything. */
int overnetinifilesectionwrite(FILE *wfile, KadEngine *pKE);

/* build an eDonkey/Overnet TCP Hello message for this node */
int makeHelloMsg(char *buf, char *bufend, KadEngine *pKE);

void *OvernetBGthread(void *p);

int OvernetCommandLoop(KadEngine *pKE);
int Overnet_ping(KadEngine *pKE, unsigned long int ip, unsigned short int port, int millistimeout);
