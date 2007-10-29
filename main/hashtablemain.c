/* hash table */

#include <stdio.h>
#include <stdlib.h>

/* implementation dependent declarations */
typedef int keyType;            /* type of key */

/* user data stored in hash table */
typedef struct {
    int stuff;                  /* optional related data */
} recType;

typedef int hashIndexType;      /* index into hash table */

#define compEQ(a,b) (a == b)


/* implementation independent declarations */
typedef enum {
    STATUS_OK,
    STATUS_MEM_EXHAUSTED,
    STATUS_KEY_NOT_FOUND
} statusEnum;

typedef struct nodeTag {
    struct nodeTag *next;       /* next node */
    keyType key;                /* key */
    recType rec;                /* user data */
} nodeType;

nodeType **hashTable;
int hashTableSize;

hashIndexType hash(keyType key) {

   /***********************************
    *  hash function applied to data  *
    ***********************************/

    return (key % hashTableSize);
}

statusEnum insert(keyType key, recType *rec) {
    nodeType *p, *p0;
    hashIndexType bucket;

   /************************************************
    *  allocate node for data and insert in table  *
    ************************************************/

    /* insert node at beginning of list */
    bucket = hash(key);
    if ((p = malloc(sizeof(nodeType))) == 0)
        return STATUS_MEM_EXHAUSTED;
    p0 = hashTable[bucket];
    hashTable[bucket] = p;
    p->next = p0;
    p->key = key;
    p->rec = *rec;
    return STATUS_OK;
}

statusEnum delete(keyType key) {
    nodeType *p0, *p;
    hashIndexType bucket;

   /********************************************
    *  delete node containing data from table  *
    ********************************************/

    /* find node */
    p0 = 0;
    bucket = hash(key);
    p = hashTable[bucket];
    while (p && !compEQ(p->key, key)) {
        p0 = p;
        p = p->next;
    }
    if (!p) return STATUS_KEY_NOT_FOUND;

    /* p designates node to delete, remove it from list */
    if (p0)
        /* not first node, p0 points to previous node */
        p0->next = p->next;
    else
        /* first node on chain */
        hashTable[bucket] = p->next;

    free (p);
    return STATUS_OK;
}

statusEnum find(keyType key, recType *rec) {
    nodeType *p;

   /*******************************
    *  find node containing data  *
    *******************************/

    p = hashTable[hash(key)];
    while (p && !compEQ(p->key, key))
        p = p->next;
    if (!p) return STATUS_KEY_NOT_FOUND;
    *rec = p->rec;
    return STATUS_OK;
}

int main(int argc, char **argv) {
    int i, maxnum, random;
    recType *rec;
    keyType *key;
    statusEnum err;

    /* command-line:
     *
     *   has maxnum hashTableSize [random]
     *
     *   has 2000 100
     *       processes 2000 records, tablesize=100, sequential numbers
     *   has 4000 200 r
     *       processes 4000 records, tablesize=200, random numbers
     *
     */
    maxnum = atoi(argv[1]);
    hashTableSize = atoi(argv[2]);
    random = argc > 3;

    if ((rec = malloc(maxnum * sizeof(recType))) == 0) {
        fprintf (stderr, "out of memory (rec)\n");
        exit(1);
    }
    if ((key = malloc(maxnum * sizeof(keyType))) == 0) {
        fprintf (stderr, "out of memory (key)\n");
        exit(1);
    }

    if ((hashTable = calloc(hashTableSize, sizeof(nodeType *))) == 0) {
        fprintf (stderr, "out of memory (hashTable)\n");
        exit(1);
    }

    if (random) { /* random */
        /* fill "key" with unique random numbers */
        for (i = 0; i < maxnum; i++) key[i] = rand();
        printf ("ran ht, %d items, %d hashTable\n", maxnum, hashTableSize);
    } else {
        for (i=0; i<maxnum; i++) key[i] = i;
        printf ("seq ht, %d items, %d hashTable\n", maxnum, hashTableSize);
    }


    for (i = 0; i < maxnum; i++) {
        err = insert(key[i], &rec[i]);
        if (err) printf("pt1, i=%d\n", i);
    }

    for (i = maxnum-1; i >= 0; i--) {
        err = find(key[i], &rec[i]);
        if (err) printf("pt2, i=%d\n", i);
    }

    for (i = maxnum-1; i >= 0; i--) {
        err = delete(key[i]);
        if (err) printf("pt3, i=%d\n", i);
    }
    return 0;
}

