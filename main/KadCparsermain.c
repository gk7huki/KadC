#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <KadCalloc.h>
#include <int128.h>
#include <bufio.h>
#include <KadCmeta.h>
#include <KadCversion.h>

#include <KadCparser.h>

int main(int ac, char *av[]) {
	char line[512];

	printf("KadC_parsefilter test main. KadC library version: %u.%u.%u\n",
			KadC_version.major, KadC_version.minor, KadC_version.patchlevel);

	for(;;) {
		char *p;
		KadC_parsedfilter pf;

		printf("Enter filter expr: ");fflush(stdout);
		p = fgets(line, sizeof(line)-2, stdin);

		if(p == NULL)
			break;
		if(p[0] == 0 || p[0] == '\n')
			continue;

		/* Now use the parser to build a ns_filter */

		pf = KadC_parsefilter(line);
		if(pf.err) {
			printf("Parsing failure: %s%s\n", pf.errmsg1, pf.errmsg2);
			continue;
		}

		/* parsing successful: the ns_filter is pointed by pf.ns */

		printf("Dump of filter produced by parse_nsfilter():\n");
		{
			unsigned char *p = pf.nsf;
			unsigned short ul = getushortle(&p);
			while(ul--) {
				printf("%02x ", *p++);
			}
			printf("\n");
		}

		if(ns_filter_dump(pf.nsf) < 0) {
			printf("*** MALFORMED ***\n");
		}
		free(pf.nsf);
		printf("\n");
	}

	printf("\nNormal exit: \n");
	KadC_list_outstanding_mallocs(5);

	return 0;
}
