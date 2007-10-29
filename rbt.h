/* Public API for Red-Black Trees table

   Application must define types for key and data and suitable
   rbt_compLT() and rbt_compEQ() comparison functions operating
   on pointers to keys. Then it has to create a table with
   rbt_new(), passing it the addressess of key comparison functions
   and of a suitable implementation of realloc() (e.g., the one
   defined in <stdlib.h>). rbt_new() returns a void* pointer to
   the opaque table object; most table handling functions
   require such pointer to be passed as first parameter.

   To destroy a table without memory leaks, the caller has to
   iterate through all the keys, free any dynamic data pointed by
   rbt_value(iter), call rbt_erase() to remove each node, and
   finally free the table itself.

   This package refers to keys and data trough void * pointers,
   for maximum flexibility.

   Implementation derived from Thomas Niemann's public domain
   Red-Black Trees at http://epaperpress.com/sortsearch/txt/rbt.txt
   See: http://epaperpress.com/sortsearch/

 */
/* All the primitives in this module use no writeable static data,
   and are thought to be MT-safe, PROVIDED that accesses to a same
   rbt by multiple threads be protected by mutex locking
 */

typedef enum {
	RBT_STATUS_OK,
	RBT_STATUS_MEM_EXHAUSTED,
	RBT_STATUS_DUPLICATE_KEY,
	RBT_STATUS_KEY_NOT_FOUND,
	RBT_STATUS_INVALID_ARGUMENT,
	RBT_STATUS_RBT_NOT_EMPTY
} rbt_StatusEnum;

/* Comparison functions to be implemented by caller and referenced
   in rbt_new(). The semantics must reflect the particular data
   type used for keys:
	int rbt_compLT(void *a, void *b); / * true if *a < *b * /
	int rbt_compEQ(void *a, void *b); / * true if *a == *b * /
 */

/* useful to cast pointers, as in "rbtcomp *c_eq" */
typedef int rbtcomp(void *a, void *b);

/* create a new table; returns pointer to table's root */
void *rbt_new(int (*rbt_compLT)(void *a, void *b),
			  int (*rbt_compEQ)(void *a, void *b));

/* same as free(rbt), but if rbt_size(rbt) != 0 it returns RBT_STATUS_RBT_NOT_EMPTY
   and does NOT free the rbt */
rbt_StatusEnum rbt_destroy(void *rbt);

/* insert a new record; if duplicateOK is 0, any attempt to insert
   records with a duplicate key will be rejected returning RBT_STATUS_DUPLICATE_KEY */
rbt_StatusEnum rbt_insert(void *rbt, void *pkey, void *pvalue, int duplicateOK);
rbt_StatusEnum rbt_eraseKey(void *rbt, void *pkey);

/* Iterator primitives. Iterators are opaque void * pointers. */
void *rbt_begin(void *rbt); /* returns iterator pointing to record w/ lowest key */
void *rbt_end(void *rbt);	/* returns iterator pointing to record w/ highest key */
void *rbt_next(void *rbt, void *iter);
void *rbt_previous(void *rbt, void *i);

/* rbt_find(rbt, key, &iter) sets iter to point to the (first) record with that
   key or, if RBT_STATUS_KEY_NOT_FOUND is returned, to the immediately
   following one, if any: otherwise, to NULL
   If iter == NULL, just check the presence of the record with that key. */
rbt_StatusEnum rbt_find(void *rbt, void *pkey, void **iter);

/* remove record pointed by iter
   AFTER AN ERASE, iter IS NOT GUARANTEED TO BE CORRECT:
   ALWAYS CALL AGAIN rbt_begin() or rbt_find().
 */
rbt_StatusEnum rbt_erase(void *rbt, void *iter);

/* diagnostic, mostly: return the distance from the root of the node pointed by iter */
int rbt_depth(void *rbt, void *iter);
int rbt_isleaf(void *rbt, void *iter);

/* returns pointer to key of that iter */
void *rbt_key(void *iter);
/* returns pointer to data of that iter */
void *rbt_value(void *iter);
/* returns number of elements currently in the rbt */
int rbt_size(void* rbt);


/* IMPORTANT NOTES ON THE USE OF ITERATORS

   1. While the functions defined above are generally MT-safe if
      called on different rbt's for each thread, they are not if
      separate threads operate on the same rbt. This is
      unavoidable when using iterators, as obtaining and then
      using one of them is achieved through two separate API calls,
      and even if the execution of both is atomic, the sequence,
      obviously, cannot be. Therefore, mutexes must be used to
      coordinate the access to a same rbt among threads.

   2. Calling rbt_erase(rbt, iter), or modifying the content of the
      rbt in any other way, makes iter invalid: before reusing it
      for other purposes one has to reset it to a meaningful value
      through rbt_begin(), rbt_end() or rbt_find(). For instance,
      the right way of emptying the rbt from its content is:

		for(;;) {
			void *val;
			void *iter = rbt_begin(rbt);	// repeated at every iteration!
			if(iter == NULL)
				break;						// rbt empty, exit
			val = rbt_value(iter);			// get object referenced by iterator
			assert(val != NULL);					// checking is not a bad idea
			rbt_status = rbt_erase(rbt, iter);	// detach object from rbt
			assert(rbt_status == RBT_STATUS_OK);	// another useful check
			free(val);	// or whatever is appropriate to the object pointed by val
		}

	  ...whereas the following procedure, although intuitive and perfectly
	  fine (and more efficient) for read-only access to the rbt, is WRONG:

	  	for(iter=rbt_begin(rbt); iter != NULL; iter=rbt_next(rbt, iter)) {
			val = rbt_value(iter);			// get object referenced by iterator
			assert(val != NULL);
			rbt_status = rbt_erase(rbt, iter);	// THIS MAKES iter INVALID FOR SUBSEQUENT rbt_next()!!!
			assert(rbt_status == RBT_STATUS_OK);
			free(val);
		}

 */
