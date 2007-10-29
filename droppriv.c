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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef __WIN32__
#ifndef _BSD_SOURCE
#define _BSD_SOURCE 1
#endif
#include <grp.h>
#endif

#include <droppriv.h>
int droppriv(int uid, int gid) {
#ifndef __WIN32__
	gid_t glist[2];

	glist[0] = (gid_t)gid;
	glist[1] = (gid_t)1;	/* catch broken setgroups() */

	if(gid != 0) {
		if(setgroups(1,glist) != 0)
			return -1;		/* couldn't reset supplementary group IDs to non-root gid */
		if(setgid(gid) != 0)
			return -2;		/* couldn't change to non-root gid */
	}
	if(uid != 0) {
		if(setuid(uid) != 0)
			return -3;		/* couldn't change to non-root uid */
		if(setuid(0) == 0)
			return -4;		/* uh oh, going back to root succeeded! */
	}
#endif
	return 0;
}

#ifndef __WIN32__
static int getfield(char *user, int fieldnum, const char *pwfilename) {
	int uid;
	char line[80] = {0};
	char *p;
	FILE *pwfile = fopen(pwfilename, "r");

	if(fieldnum != 1 && fieldnum != 2)
		return -4;	/* only 1 and 2 are acceptable values for fieldnum */
	if(pwfile == NULL)
		return -1;	/* couldn't open /etc/passwd */
	for(;;) {
		int l;
		if(fscanf(pwfile, "%79s", line) != 1)
			break;
		p = line;
		l = strcspn(p, ":");
		if(l <= 0 || l >= strlen(p) || l != strlen(user))
			continue;
		if(strncmp(user, p, l) == 0) {	/* username found! */
			p = strchr(p+l+1, ':');
			if(p == NULL)
				return -3;	/* mangled line */
			/* now p points to the second ':' */
			if(fieldnum == 2) {
				p = strchr(p+1, ':');
				if(p == NULL)
					return -3;	/* mangled line */
				/* now p points to the second ':' */
			}
			uid = atoi(p+1);
			return uid;
		}
	}
	return -2;	/* username not found */
}

int user2uid(char *user) {
	return getfield(user, 1, "/etc/passwd");
}

int user2gid(char *user) {
	return getfield(user, 2, "/etc/passwd");
}

int group2gid(char *group) {
	return getfield(group, 1, "/etc/group");
}

int we_have_root_privilege() {
	if(setuid(0) == 0)
		return 1;	/* we can change UID to 0, which means we had it already */
	else
		return 0;
}
#endif
