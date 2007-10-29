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

/* same as strdup but for memory buffers of given size */
char *memdup(const char *p, size_t size);

/* same as strdup but for strings prefixed by length as little-endian ushort */
char *nstrdup(const char *p);

/* same as strcasecmp but for strings prefixed by length as little-endian ushort */
int nstrcasecmp(const unsigned char *p1, const unsigned char *p2);

/* creates, from a standard ASCIIZ string, a malloc'd "eDonkey nstring"
   prefixed by length (as little-endian ushort). */
unsigned char *str2nstr(const char *p);

typedef struct _kobject {
	size_t size;
	unsigned char *buf;
	time_t t_created;
	time_t t_republished;
} kobject;

/* create a new k-object from a buffer and its size
   the buffer is copied to newly malloc'ed memory, not referenced.
   returns the k-object, or NULL for no memory */
kobject *kobject_new(unsigned char *buf, size_t size);

void kobject_destroy(kobject *pk);

/* same as strdup but for k-objects. Makes a deep copy. */
kobject *kobjdup(kobject *pk);

int print_mtag(unsigned char **ppb, unsigned char *bufend, char *sep);

/* Dump a k-object to console, for diag purposes. */
void kobject_dump(kobject *pk, char *tagseparatorstring);

kobject *make_kobject(int128 hash1, int128 hash2, const char *smetalist);

typedef struct _kstore {
	void *rbt;
	pthread_mutex_t mutex;
	int avail;
} kstore;

/* creates an empty k-store of max maxobjs k-objects
   (mldonkey uses 2000 as default for maxobjs) */
void *kstore_new(int maxkobjs);

/* extracts all k-objects, erases their nodes, if destroy_kobjects != 0 destroys them, and finally frees the rbt */
void kstore_destroy(kstore *pks, int destroy_kobjects);

/* inserts a k-object in a k-store WITHOUT COPYING IT
   if which_index == 0, the first hash is used as index
   if which_index != 0, the second hash is used instead.
   Duplicate keys are allowed.
   returns: 0 OK
            1 kstore full
            2 some rbt error
   The key is the first hash, an int128 which occupies the first
   16 bytes of the k-object. Duplicate keys are allowed if last
   parameter is != 0.
 */
int kstore_insert(kstore *pks, kobject *pkobject, int which_index, int duplkey_allowed);

/* return 1 if the conditions specified by the search tree in filter
   are satisfied by the metadata in the k-object pkobject, 0 if not.
   The pointer to the search filter *ps is updated to point after
   the filter. This allows a recursive parsing of boolean search trees.
   The use of filterend allows to enforce checks against buffer overflows
   if the filter is malformed, in which case -1 is returned and the pointer
   to the filter becomes invalid.
 */
int s_filter(kobject *pkobject, unsigned char **ps, unsigned char *filterend);

/* Just like s_filter(), but the search filter is an nstring
 */
int ns_filter(kobject *pkobject, unsigned char *nps);

/* Generates an s-filter a newly malloc'd nstring from an ASCIIZ string
   with the syntax:

   [...keyword],[name=value],[name<=value],[name>=value],...

    Terms are separated by semicolons;
    white-spaces may occur as components of terms and are NEVER
    ignored (so don't use them unless you want them to be part of the tags.
    Values are always forced to lowercase; names containing only
    uppercase characters are first checked against a list of special
    names (NAME, SIZE, TYPE, FORMAT...) and, if matching, are translated
    into their special eDonkey equivalents (see sxt[] translation
    table at the beginning of this file). Names starting with "0x"
    or "0X" are treated as hex encodings of special eDonkey tagnames;
    therefore, "TYPE", "0x3", "0x03" are all equivalent names.

	If one term is present, a single search is produced; else, a
	pairwise AND of all the terms:  AND(AND(AND A B) C) D) which
	means a number of prefixed "AND" equal to the number of terms minus 1.

	NOTE: the s-filter is returned as nstring, i.e prefixed by its length
	expressed as unsigned short in little-endian format. Therefore, it
	is handier to use with ns_filter() than with s_filter. A typical call
	would be like:

	ps = make_nsfilter("paolo,conte,SIZE<=8000000,FORMAT=mp3");
	pass = ns_filter(pko, ps);
	free(ps);
	if(pass)...

	After use, remember to free(ps);
 */
unsigned char *make_nsfilter(char *stringex);

void putmeta(unsigned char **ppb, char *name, char *value);

/* Dump to console an ASCII expression of the filter in infix notation
   returns >=0 if OK, or < 0 if filter is malformed */
int s_filter_dump(unsigned char **ps, unsigned char *filterend);

/* Just like s_filter_dump(), but the search filter is a constant nstring
   returns >=0 if OK, or < 0 if filter is malformed */
int ns_filter_dump(unsigned char *ps);

/* runs a search on the local k-store ks, creating a new k-store
   containing the results (if any). Results are first found
   by indexing hash, then filtered trough the Overnet search tree
   No more than maxresults are *copied* into the returned k-store
   After use, the returned kstore may be destroyed deeply
   with kstore_destroy(pksresults, 1);
   As the local keystore is indexed on FIRST HASH, also the results
   keystore is filled in with first-hash indexing. */
kstore *kstore_find(kstore *pks, int128 hash, unsigned char *filter, unsigned char *filterend, int maxresults);

typedef struct _specialxtable {
	const char *name;
	const char code[2];
	const int type;
} specialxtable;

/* print an eDonkey/Overnet TCP Hello message */
void printHelloMsg(char *buf, char *bufend, unsigned long peerIP);

