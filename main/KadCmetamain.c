#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>
#include <assert.h>

#include <rbt.h>
#include <int128.h>
#include <MD4.h>
#include <KadCalloc.h>
#include <KadClog.h>
#include <opcodes.h>
#include <bufio.h>

#include <KadCmeta.h>


unsigned char kob1[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,	/* index hash */
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,	/* related hash */
	3,0,0,0,	/* number of metatags in following list */

	EDONKEY_MTAG_STRING,	/* = 0x02: a string-valued metatag will follow */
	1,0,					/* its name has length 1 */
	EDONKEY_STAG_NAME,		/* = 0x01: means "filename" */
	16,0,					/* its value has length 16 */
	'M','y',' ','f','i','l','e','-','n','a','m','e','.','m','p','3', /* filename value */

	EDONKEY_MTAG_DWORD,		/* = 0x03: next tag is a DWORD (unsigned long int) */
	1,0,					/* its name has length 1 */
	EDONKEY_STAG_SIZE,		/* = 0x02: means "filesize" */
	0x80,0x0d,0,0,			/* its filesize is 0x0d80 = 3456 bytes */

	EDONKEY_MTAG_STRING,	/* = 0x02: a string-valued metatag will follow */
	1,0,					/* its name has length 1 */
	EDONKEY_STAG_TYPE,		/* = 0x03: means "type" */
	5,0,
	'a','u','d','i','o'	/* type value */
};

unsigned char kob2[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,	/* index hash */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,	/* related hash */
	4,0,0,0,	/* number of metatags in following list */

	EDONKEY_MTAG_DWORD,		/* = 0x03: next tag is a DWORD (unsigned long int) */
	1,0,					/* its name has length 1 */
	EDONKEY_STAG_SIZE,		/* = 0x03: means "filesize" */
	0x00,0x08, 0x00, 0x00,	/* its filesize is 0x800 = 2048 bytes */

	EDONKEY_MTAG_STRING,	/* = 0x02: a string-valued metatag will follow */
	1,0,					/* its name has length 1 */
	EDONKEY_STAG_NAME,		/* = 0x01: means "filename" */
	12,0,					/* its value has length 12 */
	'f','i','l','e','n','a','m','e','.','t','x','t', /* filename value */

	EDONKEY_MTAG_STRING,	/* = 0x02: a string-valued metatag will follow */
	1,0,					/* its name has length 1 */
	EDONKEY_STAG_TYPE,		/* = 0x03: means "type" */
	3,0,					/* its value has length 3 */
	'd','o','c',			/* type value */

	EDONKEY_MTAG_STRING,	/* = 0x02: a string-valued metatag will follow */
	1,0,					/* its name has length 1 */
	EDONKEY_STAG_FORMAT,		/* = 0x04: means "format" */
	3,0,
	't','x','t'				/* format value */
};

unsigned char sf1[] = {
	EDONKEY_SEARCH_BOOL, EDONKEY_SEARCH_AND,
		EDONKEY_SEARCH_BOOL, EDONKEY_SEARCH_AND,
			EDONKEY_SEARCH_BOOL, EDONKEY_SEARCH_AND,
				EDONKEY_SEARCH_NAME,	/* = 01 */
				4, 0,					/* 4-byte value follows */
				'f', 'i', 'l', 'e',		/* keyword's value is "file" */

				EDONKEY_SEARCH_META,	/* = 02 */
				05, 00,					/* 5-byte value follows */
				'a', 'u', 'd', 'i', 'o', /* value is "audio" */
				1, 0,					/* 1-byte tagname follows */
				EDONKEY_STAG_TYPE,	/* = 03 */

			EDONKEY_SEARCH_LIMIT,		/* = 03, numeric limit follows */
			0xd2, 0x04, 0x00, 0x00,		/* limit is 0x4d2 = 1234 */
			EDONKEY_SEARCH_MIN,			/* = 01, min value */
			1, 0,						/* 1-byte tagname follows */
			EDONKEY_STAG_SIZE,			/* = 02 */


		EDONKEY_SEARCH_LIMIT,		/* = 03, numeric limit follows */
		0x2e, 0x16, 0x00, 0x00,		/* limit is 0x162e = 5678 */
		EDONKEY_SEARCH_MAX,			/* = 01, min value */
		1, 0,						/* 1-byte tagname follows */
		EDONKEY_STAG_SIZE			/* = 02 */
};

unsigned char sf2[] = {
	EDONKEY_SEARCH_BOOL, EDONKEY_SEARCH_AND,
		EDONKEY_SEARCH_BOOL, EDONKEY_SEARCH_AND,
			EDONKEY_SEARCH_BOOL, EDONKEY_SEARCH_AND,
				EDONKEY_SEARCH_NAME,	/* = 01 */
				8, 0,					/* 8-byte value follows */
				'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e',	/* keyword's value is "filename" */

				EDONKEY_SEARCH_META,	/* = 02 */
				3, 0,					/* 3-byte value follows */
				't', 'x', 't', 			/* value is "txt" */
				1, 0,					/* 1-byte tagname follows */
				EDONKEY_STAG_FORMAT,	/* = 03 */

			EDONKEY_SEARCH_LIMIT,		/* = 03, numeric limit follows */
			0x01, 0x00, 0x00, 0x00,		/* limit is 1 */
			EDONKEY_SEARCH_MIN,			/* = 01, min value */
			1, 0,						/* 1-byte tagname follows */
			EDONKEY_STAG_SIZE,			/* = 02 */


		EDONKEY_SEARCH_LIMIT,		/* = 03, numeric limit follows */
		0x2e, 0x16, 0x00, 0x00,		/* limit is 0x162e = 5678 */
		EDONKEY_SEARCH_MAX,			/* = 01, min value */
		1, 0,						/* 1-byte tagname follows */
		EDONKEY_STAG_SIZE			/* = 02 */
};


int main(int ac, char *av[]) {
	int status;
	kobject *pko1, *pko2, *pkores;
	unsigned char *psf;
	kstore *pks, *pkres;
	void *iter;
	unsigned char searchhash1[16] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f	/* file hash */
	};
	unsigned char *pnsf;
	char *stringex;
	int i;
	int l;
	unsigned char *pc;
	char sbuf[256];

	pko1 = kobject_new(kob1, sizeof(kob1));
	KadC_log("k-object pko1:\n");
	kobject_dump(pko1, "; ");
	KadC_log("\n");

	pko2 = kobject_new(kob2, sizeof(kob2));
	KadC_log("k-object pko2:\n");
	kobject_dump(pko2, "; ");
	KadC_log("\n");

	KadC_log("Filter sf1:\n");
	psf = sf1;
	if(s_filter_dump(&psf, psf+sizeof(sf1)))
		KadC_log("--Malformed filter!");
	KadC_log("\n");

	KadC_log("Filter sf2:\n");
	psf = sf2;
	if(s_filter_dump(&psf, psf+sizeof(sf2)))
		KadC_log("--Malformed filter!");
	KadC_log("\n");


	psf = sf1;
	status = s_filter(pko1, &psf, psf+sizeof(sf1));
	KadC_log("s_filter(pko1, &psf, psf+sizeof(sf1)) returned %d\n", status);

	psf = sf1;
	status = s_filter(pko2, &psf, psf+sizeof(sf1));
	KadC_log("s_filter(pko2, &psf, psf+sizeof(sf1)) returned %d\n", status);

	psf = sf2;
	status = s_filter(pko1, &psf, psf+sizeof(sf2));
	KadC_log("s_filter(pko1, &psf, psf+sizeof(sf2)) returned %d\n", status);

	psf = sf2;
	status = s_filter(pko2, &psf, psf+sizeof(sf2));
	KadC_log("s_filter(pko2, &psf, psf+sizeof(sf2)) returned %d\n", status);

	/* this should be like sf1 */
	stringex = "file;TYPE=audio;SIZE>=1234;SIZE<=5678";
	pnsf = make_nsfilter(stringex);

	if(pnsf == NULL) {
		KadC_log("%s line %d: Malformed filter! Aborting.\n", __FILE__, __LINE__);
		exit(0);
	}

	pc = pnsf;
	l = getushortle(&pc);
	if(l != sizeof(sf1))
		KadC_log("pnsf has lenght %d, sf1 %d!\n", l, sizeof(sf1));
	for(i=0; i<l && i< sizeof(sf1); i++) {
		if(pnsf[i+2] != sf1[i]) {
			KadC_log("pnsf[%d+2] = %u, sf1[%d] = %u\n",
					i, pnsf[i+2], i, sf1[i]);
			break;
		}
	}

	status = ns_filter(pko1, pnsf);
	KadC_log("ns_filter(pko1, \"%s\") returned %d\n", stringex, status);

	status = ns_filter(pko2, pnsf);
	KadC_log("ns_filter(pko2, \"%s\") returned %d\n", stringex, status);

	free(pnsf);

	/* this should be like sf2 */
	stringex = "filename;FORMAT=txt;SIZE>=1;SIZE<=5678";
	pnsf = make_nsfilter(stringex);

	if(pnsf == NULL) {
		KadC_log("%s line %d: Malformed filter! Aborting.\n", __FILE__, __LINE__);
		exit(0);
	}

	pc = pnsf;
	l = getushortle(&pc);
	if(l != sizeof(sf2))
		KadC_log("pnsf has lenght %d, sf2 %d!\n", l, sizeof(sf2));
	for(i=0; i<l && i< sizeof(sf2); i++) {
		if(pnsf[i+2] != sf2[i]) {
			KadC_log("pnsf[%d+2] = %u, sf2[%d] = %u\n",
					i, pnsf[i+2], i, sf2[i]);
			break;
		}
	}

	status = ns_filter(pko1, pnsf);
	KadC_log("ns_filter(pko1, \"%s\") returned %d\n", stringex, status);

	status = ns_filter(pko2, pnsf);
	KadC_log("ns_filter(pko2, \"%s\") returned %d\n", stringex, status);

	free(pnsf);

	/* play with k-store */

	pks = kstore_new(2);

	assert(pks != NULL);

	status = kstore_insert(pks, pko1, 0, 0);
	if(status != 0)
		KadC_log("kstore_insert(pks, pko1, 0, 0) returned %d\n", status);

	status = kstore_insert(pks, pko2, 0, 0);
	if(status != 0)
		KadC_log("kstore_insert(pks, pko2, 0, 0) returned %d\n", status);

	pkres = kstore_find(pks, searchhash1, sf1, sf1+sizeof(sf1), 3);
	assert(pkres != NULL);

	for(iter = rbt_begin(pkres->rbt); iter != NULL; iter = rbt_next(pkres->rbt, iter)) {
		pkores = rbt_value(iter);
		KadC_log("Found k-object with hash starting with %02x %02x %02x...",
				pkores->buf[0], pkores->buf[1], pkores->buf[2]);
	}
	kstore_destroy(pkres, 1);	/* will also destroy result object(s) */


	for(;;) {
		char *p;
		kobject *pko;
		unsigned char khashbuf[16], vhashbuf[16];

		KadC_log("Enter filter: "); fflush(stdout);
		stringex = KadC_getsn(sbuf, sizeof(sbuf)-1);

		if(stringex == NULL)
			break;	/* exit for EOF */
		if((p = strrchr(stringex, '\n')) != NULL) *p = 0;
		if((p = strrchr(stringex, '\r')) != NULL) *p = 0;
		if(*stringex == 0)
			continue;	/* ignore empty lines */

		pnsf = make_nsfilter(sbuf);
		if(pnsf == NULL) {
			KadC_log("Malformed filter!\n");
			continue;
		}
		KadC_log("Your filter \"%s\" is parsed as: ", stringex);
		if(ns_filter_dump(pnsf)) {
			KadC_log(" *** MALFORMED\n");
			continue;
		}
		KadC_log("\n");

		KadC_log("Enter meta list: "); fflush(stdout);
		stringex = KadC_getsn(sbuf, sizeof(sbuf)-1);

		pko = make_kobject( string2int128(khashbuf, "1234"),
							string2int128(vhashbuf, "5678"),
							stringex);
		if(pko == NULL) {
			KadC_log("Malformed metatag list expression!\n");
			continue;
		}
		KadC_log("k-object pko:\n");
		kobject_dump(pko, "; ");
		KadC_log("\n");


		KadC_log("Applying filter to pko1: ");
		kobject_dump(pko1, "; ");
		KadC_log(" returns ");
		status = ns_filter(pko1, pnsf);
		KadC_log(" %d\n", status);

		KadC_log("Applying filter to pko2: ");
		kobject_dump(pko2, "; ");
		KadC_log(" returns ");
		status = ns_filter(pko2, pnsf);
		KadC_log(" %d\n", status);

		KadC_log("Applying filter to the just defined pko: ");
		kobject_dump(pko, "; ");
		KadC_log(" returns ");
		status = ns_filter(pko, pnsf);
		KadC_log(" %d\n", status);

		if(pnsf != NULL)
			free(pnsf);

		kobject_destroy(pko);
	}

	kstore_destroy(pks, 1);		/* will also destroy pko1 and pko2 */

	KadC_log("\nNormal exit: \n");
	KadC_list_outstanding_mallocs(1);
	return 0;
}
