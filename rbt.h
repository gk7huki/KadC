/* Public API for Red-Black Trees table

Application must define types for key and data and suitable
comparison functions operating on pointers to keys. Then it
has to create a table with rbtNew(), passing it the addresses
of key comparison functions and of a suitable implementation of
realloc() (e.g., the one defined in <stdlib.h>). rbtNew()
returns a void* pointer to the opaque table object; most table
handling functions require such pointer to be passed as first
parameter.

To destroy a table without memory leaks, the caller has to
iterate through all the keys, free any dynamic data pointed by
rbtValue(rbt, iter), call rbtErase() to remove each node, and
finally free the table itself.

This package refers to keys and data trough void * pointers,
for maximum flexibility.

Implementation derived from Thomas Niemann's public domain
Red-Black Trees at :
Header: http://epaperpress.com/sortsearch/rbtrh.txt
Impl: http://epaperpress.com/sortsearch/rbtr.txt
Docs: http://epaperpress.com/sortsearch/
*/
#ifndef RBT_H
#define RBT_H

typedef enum {
    RBT_STATUS_OK,
    RBT_STATUS_MEM_EXHAUSTED,
    RBT_STATUS_DUPLICATE_KEY,
    RBT_STATUS_KEY_NOT_FOUND
} RbtStatus;

typedef void *RbtIterator;
typedef void *RbtHandle;

RbtHandle rbtNew(int(*compare)(const void *a, const void *b));
// create red-black tree
// parameters:
//     compare  pointer to function that compares keys
//              return 0   if a == b
//              return < 0 if a < b
//              return > 0 if a > b
// returns:
//     handle   use handle in calls to rbt functions


void rbtDelete(RbtHandle h);
// destroy red-black tree

RbtStatus rbtInsert(RbtHandle h, void *key, void *value);
// insert key/value pair

RbtStatus rbtErase(RbtHandle h, RbtIterator i);
// delete node in tree associated with iterator
// this function does not free the key/value pointers

RbtStatus rbtEraseKey(RbtHandle h, void* key);
// delete node in tree associated with key
// this function just call rbtFind, then rbtErase the result.

RbtIterator rbtNext(RbtHandle h, RbtIterator i);
// return ++i

RbtIterator rbtPrevious(RbtHandle h, RbtIterator i);
// return --i

RbtIterator rbtBegin(RbtHandle h);
// return pointer to first node

RbtIterator rbtEnd(RbtHandle h);
// return pointer to one past last node

void rbtKeyValue(RbtHandle h, RbtIterator i, void **key, void **value);
// returns key/value pair associated with iterator

void* rbtKey(RbtHandle h, RbtIterator i);
// returns key/value pair associated with iterator

void* rbtValue(RbtHandle h, RbtIterator i);
// returns key/value pair associated with iterator

RbtIterator rbtFind(RbtHandle h, void *key);
// returns iterator associated with key

int rbtSize(RbtHandle h);

#endif
