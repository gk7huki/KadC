
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <KadCalloc.h>
#include <rbt.h>

int rbtmain_compLT(void *a, void *b) {
	return (*((int *)(a)) < *((int *)(b)));
}

int rbtmain_compEQ(void *a, void *b) {
	return (*((int *)(a)) == *((int *)(b)));
}

int *newint(int value) {
	int *pint = malloc(sizeof(int));
	if(pint == NULL) {
		printf("no memory\n");
		exit(-1);
	}
	*pint = value;
	return pint;
}

int main(int argc, char *argv[]) {
	int maxnum, ct, duplicatesOK = 0;
	int *prec;
	int *pkey;
	int key_to_find;
	rbt_StatusEnum status;
/* assumed maximum depth of a node even for large tables */
#define RANGE 40
#define VRANGE 16
	int hist[RANGE];
	int maxhist, peakdepth, maxdepth, mindepth;

	/* command-line:
	 *
	 *   rbt maxnum
	 *
	 *   rbt 2000
	 *	   process 2000 records
	 *
	 */

	void *rbt;
	void *iter;

	memset(hist, 0, sizeof(hist));
	maxhist = 0;
	maxdepth = 0;
	mindepth = 1000; /* or MAXINT, or whatever */

	if(argc <= 1) {
		printf("usage: %s maxnum [key_to_find [duplicatesOK]]\n", argv[0]);
		exit(1);
	}

	maxnum = atoi(argv[1]);
	printf("maxnum = %d\n", maxnum);

	if(argc > 2)
		key_to_find = atoi(argv[2]);
	else
		key_to_find = 0;

	if(argc > 3) {
		duplicatesOK = 1;
		printf("duplicate keys are allowed\n");
	}

//	rbt = rbt_new(&rbtmain_compLT, &rbtmain_compEQ, &realloc);
	rbt = rbt_new(&rbtmain_compLT, &rbtmain_compEQ);

	for (ct = maxnum; ct; ct--) {
		/* generate a int key */
		if(duplicatesOK)
			pkey = newint((rand() % 90) + 1); /* force duplications */
		else
			pkey = newint( rand() );

		if (!duplicatesOK && ((status = rbt_find(rbt, pkey, &iter)) == RBT_STATUS_OK)) { /* if already there */
			prec = rbt_value(iter);
			if (!rbtmain_compEQ(prec, pkey)) /*...should have value == key... */
				printf("fail rec: *prec = %d, *pkey = %d\n", *(int *)prec, *(int *)pkey);
//			printf("removing %d\n", *pkey);
			free(rbt_value(iter));
			free(rbt_key(iter));
			free(pkey);
			status = rbt_erase(rbt, iter); /* ...then delete entry */
			if (status) printf("rbt_erase fail: status = %d\n", status);
		} else {
			prec = newint(*(int *)pkey);
//			printf("inserting %d\n", *pkey);
			status = rbt_insert(rbt, pkey, prec, duplicatesOK); /* ...else add entry w/ value == key */
			if (status) printf("rbt_insert fail: status = %d\n", status);
		}
	}

	/* output nodes in order */
	{
		void *iter;

		status = rbt_find(rbt, &key_to_find, &iter);
		if (status) {
			if(iter != NULL)
			printf("key %d not found: next was %d with value %d\n",
					key_to_find, *(int *)rbt_key(iter), *(int *)rbt_value(iter));
			else
				printf("key %d not found: no next\n",
					key_to_find);
		}
		while (iter != NULL) {
			int *prec;
			int depth;
			int curhist;

			// if(rbt_isleaf(rbt, iter)) { /* add this to measure only leaf nodes */
				depth = rbt_depth(rbt, iter);
				if(depth >= RANGE)
					depth = RANGE-1;	/* just in case, to prevent buffer overflows */
				if(depth > maxdepth)
					maxdepth = depth;
				if(depth < mindepth)
					mindepth = depth;
				hist[depth]++;
				curhist = hist[depth];
				if(curhist > maxhist) {
					maxhist = curhist;
					peakdepth = depth;
				}
			//}

			prec = rbt_value(iter);
//			printf("%d had depth %d\n", *(int *)prec, depth);
			iter = rbt_next(rbt, iter);
		}
	}

	{
		void *iter;

	/* list table begin to end */
		printf("list in ascending order:\n");
		for(iter = rbt_begin(rbt); iter != NULL; iter = rbt_next(rbt, iter)) {
				printf("K = %5d V = %5d\n", *(int *)rbt_key(iter), *(int *)rbt_value(iter));
		}

	/* list table end to begin */
		printf("list in descending order:\n");
		for(iter = rbt_end(rbt); iter != NULL; iter = rbt_previous(rbt, iter)) {
				printf("K = %5d V = %5d\n", *(int *)rbt_key(iter), *(int *)rbt_value(iter));
		}

	}
	/* destroy table */

	{
		void *iter;
		void *p;
		rbt_StatusEnum s;

		for(iter = rbt_begin(rbt); iter != NULL; iter = rbt_next(rbt, iter)) {
			if((p = rbt_key(iter)) != NULL) free(p);
			if((p = rbt_value(iter)) != NULL) free(p);
			s = rbt_erase(rbt, iter);
			if(s != RBT_STATUS_OK)
				printf("rbt_erase(rbt, iter) returned %d\n", s);
		}
		free(rbt);


	}
	{
		int i,j;
		printf("Max tree depth: %d min depth: %d most common depth: %d (%2d%% of total)",
				maxdepth, mindepth, peakdepth, maxhist*100/maxnum);
		for(j=VRANGE; j>=0; j--) {
			for(i=0; i<RANGE; i++)  {
				if(hist[i]*VRANGE/maxhist > j) {
					printf("*");
				} else {
					printf(" ");
				}
			}
			printf("\n");
		}
	}

	printf("malloc() called %d times, free() called %d times.\n",
			malloc_cnt, free_cnt);
	return 0;
}
