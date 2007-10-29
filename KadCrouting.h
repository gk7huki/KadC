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


void setup_kba(KadEngine *pKE, int kbsize);
void destroy_kba(KadEngine *pKE);
void dump_kba(KadEngine *pKE);
void dump_kspace(KadEngine *pKE);

/* Seek a knode in kspace and kbuckets.
   If not in kbuckets and kspace,
   			if(isalive) try to add it.

   If it's already there:
   			If isalive reset type to 0;
   			If ! isalive , increment type;
   			If type >= 5, remove the corresponding knode from both tables and free it

   In any case, ppn is left alone (not free'd).
   Returns:

   	-1 our own node or non-routable address: nothing done

	if isalive:
	 0 added or left there
     1 could no be added because bucket full

   	if ! isalive:
   	 0 it was, and was left there, or was not there already
   	 1 it was there, but it was removed due to type >= 5

 */
int UpdateNodeStatus(peernode *ppn, KadEngine *pKE, int isalive);

/* count the number of knodes */
int knodes_count(KadEngine *pKE);

/* remove all knodes and return how many they were */
int erase_knodes(KadEngine *pKE);
