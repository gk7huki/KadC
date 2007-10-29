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

/*
- insert(k-object)	// replacing objects with the same hash, if any
					// the key hash occupies the first 16 bytes of tyhe k-object
- k-object_list = find(hash, search_filter, list_max_size, timeout)

The same metaphor applies to a local store where objects published by
others will be stored and retrieved when answering to searches. However,
local find() operations will not need a timeout and will never return
more than one result (as no duplicates hashes are allowed in the local
store) so the last two parameters won't be necessary:

- local_insert(k-object) // replacing objects with the same hash, if any
- local_find(hash, search_filter)

A good choice for the format of a k-object is an image of a
OVERNET_PUBLISH packet, starting with the first hash. This will be
indexed in an rbt, with both key and data pointing to its first byte.
The reason is that the index will be the first hash. Only one
index is required, because only records where the first hash is
close to the local node ID will be stored locally...

The endianity will be the one defined by Overnet (little for
counters, big for IP addresses and hashes). Usage by other networks
like eMule KAD may require conversion shims.

*/
#define DEBUG 1

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <Debug_pthreads.h>
#include <assert.h>

#include <rbt.h>
#include <int128.h>
#include <MD4.h>
#include <KadCalloc.h>
#include <KadClog.h>
#include <opcodes.h>
#include <bufio.h>
#include <queue.h>
#include <net.h>
#include <KadCthread.h>

#include <KadCmeta.h>

#define arraysize(a) (sizeof(a)/sizeof(a[0]))

const static pthread_mutex_t __mutex = PTHREAD_MUTEX_INITIALIZER;

/* The following table lists eDonkey tagnames known to be "special".
   A special tagname may have a type (either EDONKEY_MTAG_STRING
   or EDONKEY_MTAG_DWORD, or EDONKEY_MTAG_UNKNOWN when the type is
   unknown) and in many cases a 1-byte version, to be used in Meta
   lists and search filters: there is at least one special tagname,
   "bitrate", that has a type (EDONKEY_MTAG_DWORD) but not a 1-byte
   code.
   Special tagnames recognized as such will be replaced by their
   one-byte version, and their types will be checked vs the relational
   operator: EDONKEY_MTAG_DWORD ypes will only be allowed with
   >=, <=. > and < operators; EDONKEY_MTAG_STRING only with = and
   != operators. This check is additional to the one performed on the
   numericity of tagvalues for >=, <=. > and < operators. */

const specialxtable sxt[] = {
	{"NAME", {EDONKEY_STAG_NAME, 0}, EDONKEY_MTAG_STRING},
	{"SIZE", {EDONKEY_STAG_SIZE, 0}, EDONKEY_MTAG_DWORD},
	{"TYPE", {EDONKEY_STAG_TYPE, 0}, EDONKEY_MTAG_STRING},
	{"FORMAT", {EDONKEY_STAG_FORMAT, 0}, EDONKEY_MTAG_STRING},
	{"COLLECTION", {EDONKEY_STAG_COLLECTION, 0}, EDONKEY_MTAG_STRING},
	{"PART_PATH", {EDONKEY_STAG_PART_PATH, 0}, EDONKEY_MTAG_STRING},
	{"PART_HASH", {EDONKEY_STAG_PART_HASH, 0}, EDONKEY_MTAG_HASH},
	{"COPIED", {EDONKEY_STAG_COPIED, 0}, EDONKEY_MTAG_DWORD},
	{"GAP_START", {EDONKEY_STAG_GAP_START, 0}, EDONKEY_MTAG_DWORD},
	{"GAP_END", {EDONKEY_STAG_GAP_END, 0}, EDONKEY_MTAG_DWORD},
	{"DESCRIPTION", {EDONKEY_STAG_DESCRIPTION, 0}, EDONKEY_MTAG_STRING},
	{"PING", {EDONKEY_STAG_PING, 0}, EDONKEY_MTAG_UNKNOWN},
	{"FAIL", {EDONKEY_STAG_FAIL, 0}, EDONKEY_MTAG_DWORD},
	{"PREFERENCE", {EDONKEY_STAG_PREFERENCE, 0}, EDONKEY_MTAG_DWORD},
	{"PORT", {EDONKEY_STAG_PORT, 0}, EDONKEY_MTAG_DWORD},
	{"IP", {EDONKEY_STAG_IP, 0}, EDONKEY_MTAG_UNKNOWN},
	{"VERSION", {EDONKEY_STAG_VERSION, 0}, EDONKEY_MTAG_DWORD},
	{"TEMPFILE", {EDONKEY_STAG_TEMPFILE, 0}, EDONKEY_MTAG_STRING},
	{"PRIORITY", {EDONKEY_STAG_PRIORITY, 0}, EDONKEY_MTAG_DWORD},
	{"STATUS", {EDONKEY_STAG_STATUS, 0}, EDONKEY_MTAG_DWORD},
	{"AVAILABILITY", {EDONKEY_STAG_AVAILABILITY, 0}, EDONKEY_MTAG_DWORD},
	{"QTIME", {EDONKEY_STAG_QTIME, 0}, EDONKEY_MTAG_UNKNOWN},
	{"PARTS", {EDONKEY_STAG_PARTS, 0}, EDONKEY_MTAG_DWORD},
	{"bitrate", {EDONKEY_STAG_UNKNOWN, 0}, EDONKEY_MTAG_DWORD},
	{"length", {EDONKEY_STAG_UNKNOWN, 0}, EDONKEY_MTAG_STRING},
	{"pr", {EDONKEY_STAG_UNKNOWN, 0}, EDONKEY_MTAG_DWORD},
	{NULL}
};



static char *strlower(char *s) {
	char *p = s, c;
	while(*p) {
		c = *p;
		*p++ = tolower(c);
	}
	return s;
}

/* same as strdup but for memory buffers of given size */
char *memdup(const char *p, size_t size) {
	void *p1 = malloc(size);
	if(p1 != NULL) {
		memmove(p1, p, size);
	}
	return p1;
}

/* same as strdup but for strings prefixed by length as little-endian ushort */
char *nstrdup(const char *p) {
	int length;
	void *p1;
	if(p == NULL)
		return NULL;
	length = ((unsigned char *)p)[0] + (((unsigned char *)p)[1] << 8);
	p1 = malloc(length+2);
	if(p1 != NULL) {
		memmove(p1, p, length+2);
	}
	return p1;
}

/* same as strcasecmp but for strings prefixed by length as little-endian ushort */
int nstrcasecmp(const unsigned char *p1, const unsigned char *p2) {
	int i, len1, len2, minlen;

	if(p1 == NULL && p2 == NULL)
		return 0;
	else if(p1 == NULL && p2 != NULL)
		return -1;
	else if(p1 != NULL && p2 == NULL)
		return 1;
	else {
		len1 = ((unsigned char *)p1)[0] + (((unsigned char *)p1)[1] << 8);
		len2 = ((unsigned char *)p2)[0] + (((unsigned char *)p2)[1] << 8);
		minlen = (len1 < len2 ? len1 : len2);
		for(i=0; i < minlen; i++) {
			unsigned char c1 = tolower((unsigned char)p1[i+2]);
			unsigned char c2 = tolower((unsigned char)p2[i+2]);
			if(c1 < c2)
				return -1;
			else if(c1 > c2)
				return 1;
		}
		/* strings equal up to length of shorter of the two. compare lengths */
		if(len1 < len2)
			return -1;
		else if(len1 > len2)
			return 1;
		else
			return 0;
	}
}


/* creates, from a standard ASCIIZ string, a malloc'd "eDonkey nstring"
   prefixed by length (as little-endian ushort). */
unsigned char *str2nstr(const char *p) {
	int length;
	unsigned char *p1;
	if(p == NULL)
		return NULL;
	length = strlen(p);
	p1 = malloc(length+2);
	if(p1 != NULL) {
		p1[0] = (unsigned char)(length & 0xff);
		p1[0] = (unsigned char)((length >> 8) & 0xff);
		memmove(p1+2, p, length);
	}
	return p1;
}

/* create a new k-object from a buffer and its size
   the buffer is copied to newly malloc'ed memory, not referenced.
   returns the k-object, or NULL for no memory */

kobject *kobject_new(unsigned char *buf, size_t size) {
	kobject *pk1 = malloc(sizeof(kobject));
	if(pk1 != NULL) {
		memset(pk1, 0, sizeof(kobject));
		pk1->size = size;
		pk1->buf = malloc(size);
		if(pk1->buf!= NULL) {
			memmove(pk1->buf, buf, size);
		} else {
			free(pk1);
		}
	}
	return pk1;
}

void kobject_destroy(kobject *pk) {
	free(pk->buf);
	free(pk);
}

/* same as strdup but for k-objects. Makes a deep copy. */
kobject *kobjdup(kobject *pk) {
	kobject *pk1 = malloc(sizeof(kobject));
	if(pk1 != NULL) {
		pk1->buf = malloc(pk->size);
		if(pk1->buf!= NULL) {
			memmove(pk1->buf, pk->buf, pk->size);
		} else {
			free(pk1);
		}
	}
	return pk1;
}

/* like strdup, but duplicates up to the first n characters */
static char *local_strndup(char *s, int n) {
	char *s1;
	int len;
	if(s == NULL)
		return NULL;

	for(len=0; len < n; len++)
		if(s[len] == 0)
			break;

	s1 = malloc(len+1);
	if(s1 != NULL) {
		memmove(s1, s, len);
		s1[len] = 0;	/* we put the "Z" in ASCIIZ */
	}
	return s1;
}



/* returns length of nstring, or < 0 for errors
   translations = 0: print verbatim (tagvalues)
   translations = 1: look for specials (tagnames)
   translations = 2: "" -> .TRUE. (keywords)      */
static int print_nstring(unsigned char **pb, unsigned char *bufend, int translations) {
	unsigned short int nslen;
	char *s1;

	if(*pb + sizeof(unsigned short int) > bufend)
		return -1;
	nslen = getushortle(pb);
	if(nslen == 0) {
		if(translations == 2)
			KadC_log(".TRUE.");
		return 0;
	}
	if(*pb + nslen > bufend)
		return -2;
	if(nslen == 1 && translations == 1) {
		int i;
		for(i = 0; sxt[i].name != NULL; i++) {
			if(sxt[i].code[0] == **pb) {
				KadC_log(sxt[i].name);
				(*pb)++;
				return 0;
			}
		}
		if(**pb < ' ')
			/* one-byte, < ' ', but not in table: generate on the fly... */
			KadC_log("0x%02x", **pb);
		else
			KadC_log("%c", **pb);

		(*pb)++;
		return 0;
	}
	s1 = local_strndup((char *)*pb, nslen);
	*pb += nslen;
	KadC_log(s1);
	free(s1);

	return nslen;
}

int print_mtag(unsigned char **ppb, unsigned char *bufend, char *sep) {
	unsigned char mtagtype;
	unsigned long int dword;

	/* print tagname, which should be a string */
	if(*ppb + sizeof(unsigned char) > bufend)
		return 1;
	mtagtype = *(*ppb)++;
	/* print name */
	if(print_nstring(ppb, bufend, 1) < 0)	/* may be a special */
			return 2;
	KadC_log("=");
	switch(mtagtype) {
	case EDONKEY_MTAG_STRING:
		if(print_nstring(ppb, bufend, 0) < 0)	/* can't be a special */
			return 3;
		break;
	case EDONKEY_MTAG_DWORD:
		if(*ppb + sizeof(unsigned long int) > bufend)
			return 4;
		dword = getulongle(ppb);
		KadC_log("%lu", dword);
		break;
	case EDONKEY_MTAG_HASH:
		if(*ppb + 16 > bufend)
			return 5;
		{
			char asciizhash[33];
			int128sprintf(asciizhash, *ppb);
			ppb += 16;
			KadC_log(asciizhash);
		}
		break;
	default:
		return 2;	/* don't know how to handle other types... */
	}
	KadC_log(sep);
	return 0;
}

/* Dump a k-object to console, for diag purposes. */
void kobject_dump(kobject *pk, char *tagseparatorstring) {
	unsigned char *buf = pk->buf;
	unsigned int len = pk->size;
	unsigned long int nmetas;
	int i;

	if(len < (16+16+4)) {
		KadC_log("** k-object too short: %d bytes (minimum is %d)\n",
				len, 16+16+4);
		return;
	}
	int128print(stdout, buf);
	KadC_log(tagseparatorstring);
	buf += 16;
	int128print(stdout, buf);
	KadC_log(tagseparatorstring);
	buf += 16;
	nmetas = getulongle(&buf);
	for(i=0; i < nmetas; i++) {
		if(print_mtag(&buf, pk->buf + len, tagseparatorstring) != 0) {
			KadC_log("** Malformed k-object!\n");
			break;
		}
	}
}

/* creates an empty k-store of max maxobjs k-objects
   (mldonkey uses 2000 as default for maxobjs) */
void *kstore_new(int maxkobjs) {
	kstore *pks = malloc(sizeof(kstore));
	if(pks != NULL) {
		/* we exploit the fact that the first 16 bytes in the buf of a k-object are the indexing hash */
		pks->rbt = rbt_new((rbtcomp *)&int128lt, (rbtcomp *)&int128eq);
		pks->mutex = __mutex;
		pks->avail = maxkobjs;
	} else {
		free(pks);
	}
	return pks;
}

/* extracts all k-objects, erases their nodes, if destroy_kobjects != 0 destroys them, and finally frees the rbt */
void kstore_destroy(kstore *pks, int destroy_kobjects) {
	void *iter;
	rbt_StatusEnum rbt_status;

	pthread_mutex_lock(&pks->mutex);	/* \\\\\\ LOCK \\\\\\ */
	for(;;) {
		kobject *pk;
		if((iter=rbt_begin(pks->rbt)) == NULL)
			break;
		pk = rbt_value(iter);
		rbt_erase(pks->rbt, iter);
		if(destroy_kobjects)
			kobject_destroy(pk);
	}
	rbt_status = rbt_destroy(pks->rbt);
	assert(rbt_status == RBT_STATUS_OK);
	pthread_mutex_unlock(&pks->mutex);	/* ///// UNLOCK ///// */
	pthread_mutex_destroy(&pks->mutex);
	free(pks);
}


/* inserts a k-object in a k-store WITHOUT COPYING IT
   if which_index == 0, the first hash is used as index
   if which_index != 0, the second hash is used instead.
   Duplicate keys are allowed.
   returns: 0 OK
            1 kstore full
            2 some rbt error
   The key is the first hash, an int128 which occupies the first
   16 bytes of the k-object. Duplicate keys are allowed.
 */
int kstore_insert(kstore *pks, kobject *pkobject, int which_index, int duplkey_allowed) {
	rbt_StatusEnum rbt_status;
	int status;

	pthread_mutex_lock(&pks->mutex);	/* \\\\\\ LOCK \\\\\\ */
	if(pks->avail > 0) {
		if(which_index == 0)
			rbt_status = rbt_insert(pks->rbt, pkobject->buf, pkobject, duplkey_allowed);
		else
			rbt_status = rbt_insert(pks->rbt, pkobject->buf+16, pkobject, duplkey_allowed);
		if(rbt_status == RBT_STATUS_OK) {
			pks->avail--;
			status = 0;	/* kobject inserted in kstore */
		} else {
			status = 2;	/* some rbt error */
		}
	} else {
		status = 1;		/* kstore out of space */
	}
	pthread_mutex_unlock(&pks->mutex);	/* ///// UNLOCK ///// */
	return status;
}


/* scan the list of METAs pointed by *pml (and terminating just before pmlend)
   looking for a EDONKEY_MTAG_STRING meta with name equal to the nstring
   pointed by metaname:
   if not found, the packet is corrupted or unknown METAs are found, return NULL;
   if found, return a pointer to its nstring (string preceded by ushort length) */

static unsigned char *getstringmetavalue(unsigned char *metaname, unsigned char **pml, unsigned char *pmlend) {
	int i, nmetas;
	unsigned short int slength;
	unsigned char *stag;

	if(*pml + 4 > pmlend) /* list starts with number of METAs */
		return NULL;	/* if out of bounds, => malformed META list */
	nmetas = getulongle(pml);
	for(i=0; i<nmetas; i++) {
		if(*pml + 1 > pmlend) /* type of META */
			return NULL;	/* if out of bounds, => malformed META list */
		switch(*(*pml)++) {
		case EDONKEY_MTAG_STRING: /* we are looking for a STRING */
			if(*pml + 2 > pmlend) /* length of META name */
				return NULL;	/* if out of bounds, => malformed META list */
			stag = *pml;		/* points to the nstring containing the name of this meta */
			slength = getushortle(pml);
			if(*pml + slength > pmlend) /* META name within bounds? */
				return NULL;	/* if out of bounds, => malformed META list */
			*pml += slength;	/* skip the name */
			if(*pml + 2 > pmlend) /* length of META value */
				return NULL;	/* if out of bounds, => malformed META list */
			if(nstrcasecmp(metaname, stag) == 0) {	/* found tag with this name? */
				return *pml;	/* yes, return a pointer to the value nstring */
			}
			slength = getushortle(pml);
			if(*pml + slength > pmlend) /* META value */
				return NULL;	/* if out of bounds, => malformed META list */
			*pml += slength;	/* no, another NAME tag: skip value and pass to next tag */
			break;

		case EDONKEY_MTAG_DWORD:	/* but just skip it, we are looking for a STRING */
			if(*pml + 2 > pmlend) 	/* length of META name */
				return NULL;		/* if out of bounds, => malformed META list */
			slength = getushortle(pml);
			if(*pml + slength > pmlend) /* META name */
				return NULL;		/* if out of bounds, => malformed META list */
			*pml += slength;		/* skip meta name */
			if(*pml + 4 > pmlend) 	/* META value */
				return NULL;		/* if out of bounds, => malformed META list */
			*pml += 4;				/* skip dword, pass to next tag */
			break;

		default:	/* for tag type different from STRING and DWORD */
			return NULL;	/* hopefully Overnet only uses STRING and DWORD metatags... */
		}
	}
	return NULL;
}

/* scan the list of METAs pointed by *pml (and terminating just before pmlend)
   looking for a EDONKEY_STAG_SIZE meta:
   if not found, return 0xfffffffeUL;
   if the packet is corrupted or unknown METAs are found, return 0xffffffffUL.
   if found, return it
   BUGS: META's values 0xfffffffeUL and 0xffffffffUL are treated as 0xfffffffdUL .
 */
static unsigned long int getdwordmetavalue(unsigned char *metaname, unsigned char **pml, unsigned char *pmlend) {
	int i, nmetas;
	unsigned short int slength;
	unsigned char *stag;
	unsigned long int metavalue;

	if(*pml + 4 > pmlend) /* list starts with number of METAs */
		return 0xffffffffUL;	/* if out of bounds, => malformed META list */
	nmetas = getulongle(pml);
	for(i=0; i<nmetas; i++) {
		if(*pml + 1 > pmlend) /* type of META */
			return 0xffffffffUL;	/* if out of bounds, => malformed META list */
		switch(*(*pml)++) {
		case EDONKEY_MTAG_STRING: /* but just skip it, we are looking for a DWORD */
			if(*pml + 2 > pmlend) /* length of META name */
				return 0xffffffffUL;	/* if out of bounds, => malformed META list */
			slength = getushortle(pml);
			if(*pml + slength > pmlend) /* META name */
				return 0xffffffffUL;	/* if out of bounds, => malformed META list */
			*pml += slength;	/* skip the name */
			if(*pml + 2 > pmlend) /* length of META value */
				return 0xffffffffUL;	/* if out of bounds, => malformed META list */
			slength = getushortle(pml);
			if(*pml + slength > pmlend) /* META value */
				return 0xffffffffUL;	/* if out of bounds, => malformed META list */
			*pml += slength;	/* skip the value */
			break;

		case EDONKEY_MTAG_DWORD:
			if(*pml + 2 > pmlend) /* length of META name */
				return 0xffffffffUL;	/* if out of bounds, => malformed META list */
			stag = *pml;			/* pointer to meta name */
			slength = getushortle(pml);
			if(*pml + slength > pmlend) /* META name */
				return 0xffffffffUL;	/* if out of bounds, => malformed META list */
			*pml += slength;		/* skip meta name */
			if(*pml + 4 > pmlend) /* META value */
				return 0xffffffffUL;	/* if out of bounds, => malformed META list */
			if(nstrcasecmp(metaname, stag) == 0) { /* names match! return value */
				metavalue = getulongle(pml);	/* convert and return dword */
				if(metavalue > 0xfffffffdUL)
					metavalue = 0xfffffffdUL;	/* ...e and ...f are reserved... */
				return metavalue;
			}
			*pml += 4;	/* no, another DWORD tag: skip dword and pass to next tag */
			break;

		default:	/* for tag type different from STRING and DWORD */
			return 0xffffffffUL;	/* hopefully Overnet only uses STRING and DWORD metatags... */
		}
	}
	return 0xfffffffeUL;
}

/* return 1 if the limit on a DWORD META is respected, or if that META is not in
   the list pointed by *pml;
   return 0 if the limit is exceeded;
   return -1 if the filter was corrupted
 */
static int checklimit(unsigned char *metaname, unsigned char **pml, unsigned char *pmlend, int minmax, unsigned long int limit) {
	unsigned long int metavalue = getdwordmetavalue(metaname, pml, pmlend);
	if(metavalue == 0xffffffffUL)
		return -1;	/* always fail is packet corrupted */
	else if(metavalue == 0xfffffffeUL)
		return 0;	/* always fail if filesize META is not present - changed 24 Aug 2004 to be consistent w/ other overnet clients */
	else {
		if(minmax == EDONKEY_SEARCH_MIN) /* if "check if file is at least limit */
			return (metavalue >= limit);
		else if(minmax == EDONKEY_SEARCH_MAX) /* if "check if file is not bigger than limit */
			return (metavalue <= limit);
		else
			return -1;	/* invalid minmax parameter */
	}
}

/* return 1 if the conditions specified by the search tree in filter
   are satisfied by the metadata in the k-object pkobject, 0 if not.
   The pointer to the search filter *ps is updated to point after
   the filter. This allows a recursive parsing of boolean search trees.
   The use of filterend allows to enforce checks against buffer overflows
   if the filter is malformed, in which case -1 is returned and the pointer
   to the filter becomes invalid.
   Note: to replicate the behaviour of other clients, it is assumed that:
   - zero-length keywords always match
   - metatags with zero-length metaname always fail
   This allows to implement the NOT operator as ("" AND_NOT condition).
 */
int s_filter(kobject *pkobject, unsigned char **ps, unsigned char *filterend) {
	unsigned char *pml = &pkobject->buf[32];	/* pointer to meta list, after the two hashes */
	unsigned char *pmlend = &pkobject->buf[pkobject->size]; /* pointer immediately after meta list end */
	int value = 0;	/* default: invalid filter causes "false" to be returned */
	int value1, value2; /* temp vars */
	/* Now parse the search tree and check conditions against the k-object */
	if(*ps > filterend)
		return 0;	/* if out of bounds, => malformed filter => return false */
	if(*ps == filterend)
		return 1;	/* a zero-length filter is a pass-all filter */
	switch(*(*ps)++) {
	case EDONKEY_SEARCH_BOOL:
		if(*ps + 1 > filterend)
			return -1;	/* if out of bounds, => malformed filter => return false */
		switch(*(*ps)++) {
		case EDONKEY_SEARCH_AND:
			value1 = s_filter(pkobject, ps, filterend);
			if(value1 < 0)
				return value1;	/* abort recursion */
			value2 = s_filter(pkobject, ps, filterend);
			if(value2 < 0)
				return value2;	/* abort recursion */
			value = value1 && value2;
			break;
		case EDONKEY_SEARCH_OR:
			value1 = s_filter(pkobject, ps, filterend);
			if(value1 < 0)
				return value1;	/* abort recursion */
			value2 = s_filter(pkobject, ps, filterend);
			if(value2 < 0)
				return value2;	/* abort recursion */
			value = value1 || value2;
			break;
		case EDONKEY_SEARCH_ANDNOT:
			value1 = s_filter(pkobject, ps, filterend);
			if(value1 < 0)
				return value1;	/* abort recursion */
			value2 = s_filter(pkobject, ps, filterend);
			if(value2 < 0)
				return value2;	/* abort recursion */
			value = value1 && (! value2);
			break;
		}
		break;
	case EDONKEY_SEARCH_NAME: /* find occurrences of keyword in filename */
		{
			unsigned char *keyword;
			unsigned short int keywordlength;
			int i;
			unsigned short int slength;
			int nmetas;

			if(*ps + 2 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			keywordlength = getushortle(ps);
			if(keywordlength == 0)
				return 1;	/* null keyword is anywhere - EM 9 Sep 2004 */
			keyword  = *ps;
			if(*ps + keywordlength > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			*ps += keywordlength;
			/* now search in k-object's META list for a EDONKEY_MTAG_STRING value
			   containing keyword as substring.

			   NOTE: whereas for publishing purposes only
			   keywords extracted from the title are used (by MD4-hashing them and
			   using them as indices), and the keywords substrings must be
			   delimited by no-alphanumerics, for filtering purposes ALL metavalues
			   of type EDONKEY_MTAG_STRING are scanned for substrings equal to the
			   specified keywords. */

			/* Scan all EDONKEY_MTAG_STRING metas for match with the keyword */
			if(pml + 4 > pmlend) /* list starts with number of METAs */
				return -1;	/* if out of bounds, => malformed META list */
			nmetas = getulongle(&pml);
			for(i=0; i<nmetas; i++) {
				char *haystack, *needle, *p;
				unsigned char *stag;
				if(pml + 1 > pmlend) /* type of META */
					return -1;	/* if out of bounds, => malformed META list */
				switch(*pml++) {
				case EDONKEY_MTAG_STRING: /* we are looking for a STRING */
					if(pml + 2 > pmlend) /* length of META name */
						return -1;	/* if out of bounds, => malformed META list */
					stag = pml;		/* points to the nstring containing the name of this meta */
					slength = getushortle(&pml);
					if(pml + slength > pmlend) /* META name within bounds? */
						return -1;	/* if out of bounds, => malformed META list */
					pml += slength;	/* skip the name */
					if(pml + 2 > pmlend) /* length of META value */
						return -1;	/* if out of bounds, => malformed META list */
					slength = getushortle(&pml);
					if(pml + slength > pmlend) /* META value */
						return -1;	/* if out of bounds, => malformed META list */
					if(slength > 0) { /* null value could't match anything: 9 Sep 2004 */
						/* now scan tagvalue pointd by pml and slength long to find
						   occurences of the substring pointed by keyword and keywordlength long */
						haystack = local_strndup((char *)pml, slength);
						strlower(haystack);
						needle = local_strndup((char *)keyword, keywordlength);
						strlower(needle);
						p = strstr(haystack, needle);
						free(needle);
						free(haystack);
						if(p != NULL)
							return 1;	/* found match! */
						pml += slength;	/* skip value and pass to next tag */
					}
					break;

				case EDONKEY_MTAG_DWORD:	/* but just skip it, we are looking for a STRING */
					if(pml + 2 > pmlend) 	/* length of META name */
						return -1;		/* if out of bounds, => malformed META list */
					slength = getushortle(&pml);
					if(pml + slength > pmlend) /* META name */
						return -1;		/* if out of bounds, => malformed META list */
					pml += slength;		/* skip meta name */
					if(pml + 4 > pmlend) 	/* META value */
						return -1;		/* if out of bounds, => malformed META list */
					pml += 4;				/* skip dword, pass to next tag */
					break;

				default:	/* for tag type different from STRING and DWORD */
					return -1;	/* hopefully Overnet only uses STRING and DWORD metatags... */
				}
			}
			return 0;	/* sorry, arrived till end of tags with no match for kw in values */
		}
		break;
	case EDONKEY_SEARCH_META:
		{
			unsigned char *metaname;
			unsigned char *metavalue;
			unsigned short int metanamelength, metavaluelength;
			unsigned char *metavalueinkobj;

			metavalue = *ps;
			if(*ps + 2 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			metavaluelength = getushortle(ps);
			if(*ps + metavaluelength > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			*ps += metavaluelength;
			metaname = *ps;
			if(*ps + 2 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			metanamelength = getushortle(ps);
			if(metanamelength == 0)
				return 0;	/* null tagname always fails - added 24 Aug 2004 */
			if(*ps + metanamelength > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			*ps += metanamelength;
			/* now searchk in k-object's META list for a META with this same name */
			metavalueinkobj = getstringmetavalue(metaname, &pml, pmlend);
			if(metavalueinkobj == NULL) /* if no such name in META list of k-object... */
				return 0;	/* ...filter failed */
			value = (nstrcasecmp(metavalueinkobj, metavalue) == 0); /* otherwise, filter succeeds if values match */
		}
		break;
	case EDONKEY_SEARCH_LIMIT:
		{ /* LIMIT[dword] MINMAX[1] NAME[nstring] */
			unsigned char *metaname;
			unsigned short int metanamelength;
			unsigned long int limit;
			int minmax;

			if(*ps + 4 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			limit = getulongle(ps);
			if(*ps + 1 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			minmax = *(*ps)++;
			if(*ps + 2 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			metaname = *ps;
			metanamelength = getushortle(ps);
			if(metanamelength == 0)
				return 0;	/* null tagname always fails - added 24 Aug 2004 */
			if((*ps + metanamelength) > filterend) /* *ps + size of nstring */
				return -1;	/* if out of bounds, => malformed filter => return false */
			/* here metaname points to a nstring containing the tagname; limit and minmax define the test. */
			/* now scan the k-object looking for a numeric META (EDONKEY_MTAG_DWORD)
			   with tagname nstring-equal to **ps, and check its value */
			*ps += metanamelength;
			value = checklimit(metaname, &pml, pmlend, minmax, limit);
		}
		break;
	}
	return value;
}

/*  returns >=0 if OK, or < 0 if filter is malformed */
int s_filter_dump(unsigned char **ps, unsigned char *filterend) {
	int n;
	if(*ps > filterend)
		return -1;	/* if out of bounds, => malformed filter => return -1 */
	if(*ps == filterend)
		return 1;	/* a zero-length filter is a pass-all filter */
	switch(*(*ps)++) {
	case EDONKEY_SEARCH_BOOL:
		if(*ps + 1 > filterend)
			return -1;	/* if out of bounds, => malformed filter => return false */
		switch(*(*ps)++) {
		case EDONKEY_SEARCH_AND:
			KadC_log("(");
			if(s_filter_dump(ps, filterend) < 0)
				return -1;
			KadC_log(" AND ");
			if(s_filter_dump(ps, filterend) < 0)
				return -1;
			KadC_log(")");
			break;
		case EDONKEY_SEARCH_OR:
			KadC_log("(");
			if(s_filter_dump(ps, filterend) < 0)
				return -1;
			KadC_log(" OR ");
			if(s_filter_dump(ps, filterend) < 0)
				return -1;
			KadC_log(")");
			break;
		case EDONKEY_SEARCH_ANDNOT:
			KadC_log("(");
			n = s_filter_dump(ps, filterend);
			if(n < 0)
				return -1;
			else
				KadC_log(" AND_NOT ");
			if(s_filter_dump(ps, filterend) < 0)
				return -1;
			KadC_log(")");
			break;
		}
		break;
	case EDONKEY_SEARCH_NAME: /* find occurrences of keyword in filename */
		{
			return print_nstring(ps, filterend, 2);
		}
		break;
	case EDONKEY_SEARCH_META:
		{
			unsigned char *metaname;
			unsigned char *metavalue;
			unsigned short int metanamelength, metavaluelength;

			metavalue = *ps;
			if(*ps + 2 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			metavaluelength = getushortle(ps);
			if(*ps + metavaluelength > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			*ps += metavaluelength;
			metaname = *ps;
			if(*ps + 2 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			metanamelength = getushortle(ps);
			if(*ps + metanamelength > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			*ps += metanamelength;
			if(print_nstring(&metaname, filterend, 1) < 0)
				return -1;
			KadC_log("=");
			if(print_nstring(&metavalue, filterend, 0) < 0)
				return -1;

		}
		break;
	case EDONKEY_SEARCH_LIMIT:
		{ /* LIMIT[dword] MINMAX[1] NAME[nstring] */
			unsigned char *metaname;
			unsigned short int metanamelength;
			unsigned long int limit;
			int minmax;

			if(*ps + 4 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			limit = getulongle(ps);
			if(*ps + 1 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			minmax = *(*ps)++;
			if(*ps + 2 > filterend)
				return -1;	/* if out of bounds, => malformed filter => return false */
			metaname = *ps;
			metanamelength = getushortle(ps);
			if((*ps + metanamelength) > filterend) /* *ps + size of nstring */
				return -1;	/* if out of bounds, => malformed filter => return false */
			*ps += metanamelength;
			/* here metaname points to a nstring containing the tagname; limit and minmax define the test. */
			if(print_nstring(&metaname, filterend, 1) < 0)
				return -1;
			if(minmax == EDONKEY_SEARCH_MIN)
				KadC_log(">");
			else
				KadC_log("<");
			KadC_log("%lu", limit);
		}
		break;
	}
	return 1;

}


/* Just like s_filter(), but the search filter is an nstring
 */
int ns_filter(kobject *pkobject, unsigned char *nps) {
	unsigned short filterlen;
	if(nps == NULL)
		return 1;	/* NULL filter is always satisfied */
	filterlen = getushortle(&nps);

	/* now nps points at the beginning of the filter */
	return s_filter(pkobject, &nps, nps+filterlen);
}

/* Just like s_filter_dump(), but the search filter is an nstring
   returns >=0 if OK, or < 0 if filter is malformed
 */
int ns_filter_dump(unsigned char *ps) {
	unsigned short filterlen;
	if(ps == NULL)
		return 0;	/* NULL filter is not malformed */
	filterlen = getushortle(&ps);

	if(filterlen == 0)
		return 0;	/* nor is an empty filter */

	/* now nps points at the beginning of the filter */
	return s_filter_dump(&ps, ps+filterlen);
}

/* runs a search on the local k-store ks, creating a new k-store
   containing the results (if any). Results are first found
   by indexing hash, then filtered trough the Overnet search tree
   No more than maxresults are *copied* into the returned k-store
   After use, the returned kstore may be destroyed deeply
   with kstore_destroy(pksresults, 1);
   As the local keystore is indexed on FIRST HASH, also the results
   keystore is filled in with first-hash indexing. */
kstore *kstore_find(kstore *pks, int128 hash, unsigned char *filter, unsigned char *filterend, int maxresults) {
	rbt_StatusEnum rbt_status;
	void *iter;
	kstore *results = kstore_new(maxresults);
	int status;

	if(results != NULL) {
		pthread_mutex_lock(&pks->mutex);	/* \\\\\\ LOCK \\\\\\ */
		rbt_status = rbt_find(pks->rbt, hash, &iter);
		for(;iter != NULL; iter = rbt_next(pks->rbt, iter)) {
			kobject *pkobject;
			if(int128eq(rbt_key(iter), hash) == 0)	/* no more records with this key? */
				break;	/* then stop */
			pkobject = rbt_value(iter);
			status = s_filter(pkobject, &filter, filterend);
#ifdef DEBUG
			if(status < 0)
				KadC_log("Uh oh, s_filter(pkobject, &filter, filterend) returned %d...\n",
							status);
#endif
			if(status == 1){ /* if filtering successful... */
				/* ...try to store a COPY of the k-object into results kstore */
				kobject *pkobject1 = kobjdup(pkobject);
				if(pkobject1 == NULL) { /* if no memory, bad news, so... */
					kstore_destroy(results, 1);
					results = NULL;
					break;	/* ...return NULL */
				}
				/* otherwise try to store the copy in results kstore */
				if(kstore_insert(results, pkobject1, 0, 0) != 0) /* if kstore_insert fails... */
					break;		/* ...exit, probably results is full enough results */
			}
		}
		pthread_mutex_unlock(&pks->mutex);	/* ///// UNLOCK ///// */
	}
	return results;
}


static int isnum(char *s) {
	char c;
	while((c = *s++) != 0) {
		if(!isdigit(c))
			return 0;
	}
	return 1;
}

/* Writes to a buffer *p an nstring obtained form the ASCIIZ string s.
   The characters are also converted to lowercase. *p is left pointing
   after the end of the nstring
 */
static void putnstring(unsigned char **p, char *s) {
	unsigned char c;
	int len = strlen(s);
	putushortle(p, len);
	while((c = *s++) != 0)
		*(*p)++ = tolower(c);
}

#define istermseparator(c) ((c) == ';')
#define ispairseparator(c) ((c) == '=' || (c) == '<' || (c) == '>')


/* similar to putnstring(), but, if the input string contains only
   uppercase characters, a check is made to see if it matches a list
   of "special names" (NAME, SIZE, TYPE and FORMAT etc.): in that
   case it's converted to the corresponding EDONKEY_STAG_... single-byte
   nstring. Metanames with at least one lowercase character are
   treated as non-special, even when
   they match a special (e.g., "size").
 */
static int putmetaname(unsigned char **p, char *s) {
	char *s1;
	unsigned char c;
	int i;
	int type = EDONKEY_MTAG_UNKNOWN;	/* by default */

	/* here any pair separator in s has been replaced by a terminating 0 */

	if(strncasecmp(s, "0x", 2) == 0) {	/* tags starting witx "0x..." */
		unsigned int n;
		sscanf(s+2, "%x", &n);
		*(*p)++ = 1;
		*(*p)++ = 0;
		*(*p)++ = (unsigned char)n;

		for(i=0; sxt[i].name != NULL; i++) {
			if(n == sxt[i].code[0])
				return sxt[i].type;
		}
		return EDONKEY_MTAG_UNKNOWN;
	}

	/* not "0x..." Replace terminator with a zero */

	for(s1 = s; (c = *s1) != 0; s1++) {
		if(ispairseparator(c)) {
			*s1 = 0;
			break;
		}
	}

	/* See if the tagname is special, AND besides a type it has a 1-byte code */
	for(s1=s, i=0; sxt[i].name != NULL; i++) {
		if(sxt[i].code != EDONKEY_STAG_UNKNOWN && strcmp(s, sxt[i].name) == 0) {
			if(sxt[i].code[0] != EDONKEY_STAG_UNKNOWN)	/* if there is a 1-byte code... */
				s1 = (char *)sxt[i].code;	/* ...then use it instead of the string */
			type = sxt[i].type;		/* anyway, jot down the type */
			break;
		}
	}
	putnstring(p, s1);
	return type;
}

/* Generates an s-filter a newly malloc'd nstring from an ASCIIZ string
   with the syntax:

   [...keyword],[name=value],[name<value],[name>value],...

    Terms are separated by semicolons;
    white-spaces may occur as components of terms and are NEVER
    ignored (so don't use them unless you want them to be part of the tags.
    Values are always forced to lowercase; names containing
    uppercase characters are first checked against a list of special
    names (NAME, SIZE, TYPE, FORMAT...) and, if matching, are translated
    into their special eDonkey equivalents (see sxt[] translation
    table at the beginning of this file). Names starting with "0x"
    or "0X" are treated as hex encodings of special eDonkey tagnames;
    therefore, "TYPE", "Type", "tYpe", "0x3", "0x03" are all
    equivalent names.

	If one term is present, a single search is produced; else, a
	pairwise AND of all the terms:  AND(AND(AND A B) C) D) which
	means a number of prefixed "AND" equal to the number of terms minus 1.

	NOTE: the s-filter is returned as nstring, i.e prefixed by its length
	expressed as unsigned short in little-endian format. Therefore, it
	is handier to use with ns_filter() than with s_filter. A typical call
	would be like:

	ps = make_nsfilter("paolo,conte,filesize<=8000000,fileformat=mp3");
	pass = ns_filter(pko, ps);
	free(ps);
	if(pass)...

	After use, remember to free(ps);
 */

unsigned char *make_nsfilter(char *stringex) {

	int i, termc;
	int slen;
	char **terms;
	unsigned char *sfilter;
	int sfilterlen;
	char *s;
	unsigned char *p;

	if(stringex == NULL || strlen(stringex) < 3)
		return NULL;

	s = strdup(stringex);	/* because the original may be a const */
	if(s == NULL)
		return NULL;

	slen = strlen(s);
	terms = malloc(slen*sizeof(unsigned char *)); /* that MUST be enough! */
	if(terms == NULL) {
		free(s);
		return NULL;
	}

	for(i=0, termc=0; i<slen; i++) {
		if(i == 0 || (!istermseparator(s[i]) && s[i-1] == '\0'))
			terms[termc++] = &s[i];
		if(istermseparator(s[i]))
			s[i] = 0;
	}
	/* now termc and terms[] are kind of like argc and argv */

	/* allocate space for sfilter: at least the same as stringex, plus
	   two ushort per term (for nstring counters) or one ushort and one
	   long for integers. Add 128 just in case...
	*/
	sfilterlen = slen+6*termc+128;	/* should be plenty */


	sfilter = malloc(sfilterlen);
	if(sfilter == NULL) {
		free(s);
		free(terms);
		return NULL;
	}

	p = &sfilter[2];
	for(i = 1; i < termc; i++) {		/* termc-1 "OP AND" */
		*(p++) = EDONKEY_SEARCH_BOOL;	/* 0x00 */
		*(p++) = EDONKEY_SEARCH_AND;	/* 0x00 */
	}

	for(i = 0; i < termc; i++) {
		char *poper;
		unsigned long int ivalue;

		if((poper = strstr(terms[i], "<")) != NULL) { /* "<=" ? */
			if(!isnum(poper+1))
				goto error;
			ivalue = atoi(poper+1);
			*(p++) = EDONKEY_SEARCH_LIMIT;
			putulongle(&p, ivalue);
			*(p++) = EDONKEY_SEARCH_MAX;
			if(putmetaname(&p, terms[i]) != EDONKEY_MTAG_DWORD)
				goto error;
		} else if((poper = strstr(terms[i], ">")) != NULL) { /* ">=" ? */
			if(!isnum(poper+1))
				goto error;
			ivalue = atoi(poper+1);
			*(p++) = EDONKEY_SEARCH_LIMIT;
			putulongle(&p, ivalue);
			*(p++) = EDONKEY_SEARCH_MIN;
			if(putmetaname(&p, terms[i]) != EDONKEY_MTAG_DWORD)
				goto error;
		} else if((poper = strchr(terms[i], '=' )) != NULL) { /* "="  ? */
			*(p++) = EDONKEY_SEARCH_META;
			putnstring(&p, poper+1);
			if(putmetaname(&p, terms[i]) == EDONKEY_MTAG_DWORD)
				goto error;	/* in filters, anything meta != EDONKEY_MTAG_DWORD is treated as string meta */
		} else { /* simple keyword? */
			/*if(strlen(terms[i]) < 3)
				continue;	/ * skip keywords shorter than 3 chars */
			*(p++) = EDONKEY_SEARCH_NAME;
			putnstring(&p, terms[i]);
		}

	} /* end forall(terms) */
	assert(p < &sfilter[sfilterlen]);
	sfilterlen = p - sfilter;	/* this is the actual (vs malloc's) length */
	p = sfilter;
	putushortle(&p, sfilterlen-2);
	sfilter = realloc(sfilter, sfilterlen);	/* useful? */
	free(s);
	free(terms);
	return sfilter;
error:
	free(s);
	free(sfilter);
	free(terms);
	return NULL;
}

/* writes a metatag. If the type can be inferred looking up the metaname
   in sxt[] (special tagnames), then that type is used; else, if
   the first character of the value string is "+" or "-", a DWORD
   tag is written, if it is '#' a HASH tag is written, and in
   all other cases a STRING tag is written. */

void putmeta(unsigned char **ppb, char *name, char *value) {
	unsigned char *ptype;
	int tagtype;
	unsigned char hashbuf[16];

	ptype = *ppb;	/* jot down for fixup */
	*ppb += 1;
	tagtype = putmetaname(ppb, name);
	if(tagtype == EDONKEY_MTAG_UNKNOWN) {
		if(*value == '#') {
			tagtype = EDONKEY_MTAG_HASH;
			value++;
		} else if(*value == '+' || *value == '-') {
			tagtype = EDONKEY_MTAG_DWORD;
			value++;
		} else {
			tagtype = EDONKEY_MTAG_STRING;	/* let's assume string unless we are sure */
		}
	}
	*ptype = tagtype;
	if(value != NULL) {	/* otherwise the caller writes the value by itself */
		if(tagtype == EDONKEY_MTAG_STRING)
			putnstring(ppb, value);
		else if(tagtype == EDONKEY_MTAG_HASH)
			putint128n(ppb, string2int128(hashbuf, value));
		else if(tagtype == EDONKEY_MTAG_DWORD) {
			putulongle(ppb, atoi(value));
		}
	}
}


/* Make a k-object from two hashes and a string representation of a
   metatag list in the format: name1=value1;name2=...
   The tagvalue is inspected for non-decimal characters, and if it contains
   one, the tag is treated as EDONKEY_MTAG_STRING, else as EDONKEY_MTAG_DWORD.
   (exception: tagvalues starting with "0x" are converted from hex and
   also treated as DWORD).
   Then, if the tagname is lowercase, it's used verbatim; if it contains at
   least one uppercase character, it's looked up in the sxt table for
   specials, and the type is checked for compliance with the tagname.
   */
kobject *make_kobject(int128 hash1, int128 hash2, const char *smetalist) {
	kobject *pko;
	int i, termc;
	char *s;	/* string version of metalist */
	unsigned char kbuf[4096];	/* temp buffer for k-object  */
	int kbuflen = arraysize(kbuf);
	unsigned char *pb = kbuf;	/* pointer inside kbuf */
	int slen;
	char **terms;

	s = strdup((char *)smetalist);	/* so we can modify it */
	if(s == NULL) {
		return NULL;
	}

	slen = strlen(s);

	if(slen > 2) {
		terms = malloc(slen*sizeof(unsigned char *)); /* that MUST be enough! */
		if(terms == NULL) {
			free(s);
			return NULL;
		}

		for(i=0, termc=0; i<slen; i++) {
			if((s[i] != ';') && (i == 0 || s[i-1] == '\0'))
				terms[termc++] = &s[i];
			if(s[i] == ';')
				s[i] = 0;				/* replace ';' with 0 */
		}
	} else {
		termc =0;
		terms = NULL;
	}
	/* now termc and terms[] are kind of like argc and argv */

	putint128n(&pb, hash1);
	putint128n(&pb, hash2);
	pb += 4;	/* leave room for termc counter, fixed up later */


	/* convert ASCII metalist into sequence of metas */
	for(i = 0; i < termc; i++) {
		char *pv = strstr(terms[i], "=");

		if(pv == NULL) {	/* hmmm... missing "=" between name and value! */
			free(s);
			free(terms);
			return NULL;	/* error: malformed string */
		}
		*pv++ = 0; /* now pv points to value string, terms[i] to name string */
		/* KadC_log("tagname: %s tagvalue: %s\n", terms[i], pv); */
		putmeta(&pb, terms[i], pv);
	}

	if(terms != NULL) {
		free(terms);
	}
	free(s);
	kbuflen = pb - kbuf;
	pb = &kbuf[16+16];
	putulongle(&pb, termc);		/* termc fixup */
	pko = kobject_new(kbuf, kbuflen);	/* copy valid portion of kbuf in new k-object */
	return pko;
}

/* prints an eDonkey/Overnet TCP Hello message */
void printHelloMsg(char *buf, char *bufend, unsigned long peerIP) {
	unsigned char *p = (unsigned char *)buf+1;
	int packetlen = getulongle(&p);
	int128 peerDeclaredhash;
	unsigned long int peerDeclaredIP;
	unsigned short int peerDeclaredport;
	int nmetas, i;

	if(buf+packetlen > bufend)
		goto err;
	if(buf+1+4+2+16+4+2+4 > bufend)
		goto err;
	if(*p++ != 0x01 || *p++ != 0x10)
		goto err;

	KadC_log("TCP Client hello from %s: \n", htoa(peerIP));
	peerDeclaredhash = p;
	p += 16;
	peerDeclaredIP = getipn(&p);
	peerDeclaredport = getushortle(&p);
	nmetas = getulongle(&p);
	int128print(stdout, peerDeclaredhash);
	KadC_log(", %s:%u, ", htoa(peerDeclaredIP), peerDeclaredport);

	for(i=0; i < nmetas; i++) {
		if(print_mtag(&p, (unsigned char *)buf+5+packetlen, "; ") != 0) {
			KadC_log("** Malformed k-object!\n");
			break;
		}
	}
	KadC_log("\n");
	return;
err:
	KadC_log("Malformed Client-hello message from %s\n", htoa(peerIP));
}

