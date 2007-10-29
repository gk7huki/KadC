/* red-black tree routines

   Implementation derived by Enzo Michelangeli from Thomas Niemann's
   public domain Red-Black Trees at
   http://epaperpress.com/sortsearch/txt/rbt.txt
   See: http://epaperpress.com/sortsearch/

   This code is in the public domain as well.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <KadCalloc.h>

#include <rbt.h>

/* Red-Black tree description */

typedef struct NodeTag {
	struct NodeTag *left;	   /* left child */
	struct NodeTag *right;	  /* right child */
	struct NodeTag *parent;	 /* parent */
	enum { BLACK, RED } color;	/* node color (BLACK, RED) */
	void *pkey;				/* key used for searching */
	void *prec;				/* user data */
} NodeType;

#define NIL &(((rbtType *)rbt)->sentinel)		   /* all leafs are sentinels */

typedef struct _rbtType {
	NodeType *root;   /* root of Red-Black tree */
	int (*compLT)(void *a, void *b);
	int (*compEQ)(void *a, void *b);
	NodeType sentinel;
	int size;
} rbtType;

void *rbt_new(int (*rbt_compLT)(void *a, void *b),
			  int (*rbt_compEQ)(void *a, void *b)) {
	rbtType *rbt = malloc(sizeof(rbtType));
	if(rbt != NULL) {
		rbt->compLT = rbt_compLT;
		rbt->compEQ = rbt_compEQ;
		rbt->root = NIL;
		rbt->sentinel.left = &rbt->sentinel;
		rbt->sentinel.right = &rbt->sentinel;
		rbt->sentinel.parent = NULL;
		rbt->sentinel.color = BLACK;
		rbt->sentinel.pkey = NULL;
		rbt->sentinel.prec = NULL;
		rbt->size = 0;
	}
	return (void *)rbt;
}

rbt_StatusEnum rbt_destroy(void *p) {
	rbtType *rbt = (rbtType *)p;
	if(rbt->size != 0)
		return RBT_STATUS_RBT_NOT_EMPTY;
	free(p);
	return RBT_STATUS_OK;
}

static void rotateLeft(rbtType *rbt, NodeType *x) {

   /**************************
	*  rotate node x to left *
	**************************/

	NodeType *y = x->right;

	/* establish x->right link */
	x->right = y->left;
	if (y->left != NIL) y->left->parent = x;

	/* establish y->parent link */
	if (y != NIL) y->parent = x->parent;
	if (x->parent) {
		if (x == x->parent->left)
			x->parent->left = y;
		else
			x->parent->right = y;
	} else {
		rbt->root = y;
	}

	/* link x and y */
	y->left = x;
	if (x != NIL) x->parent = y;
}

static void rotateRight(rbtType *rbt, NodeType *x) {

   /****************************
	*  rotate node x to right  *
	****************************/

	NodeType *y = x->left;

	/* establish x->left link */
	x->left = y->right;
	if (y->right != NIL) y->right->parent = x;

	/* establish y->parent link */
	if (y != NIL) y->parent = x->parent;
	if (x->parent) {
		if (x == x->parent->right)
			x->parent->right = y;
		else
			x->parent->left = y;
	} else {
		rbt->root = y;
	}

	/* link x and y */
	y->right = x;
	if (x != NIL) x->parent = y;
}

static void insertFixup(rbtType *rbt, NodeType *x) {

   /*************************************
	*  maintain Red-Black tree balance  *
	*  after inserting node x		    *
	*************************************/

	/* check Red-Black properties */
	while (x != rbt->root && x->parent->color == RED) {
		/* we have a violation */
		if (x->parent == x->parent->parent->left) {
			NodeType *y = x->parent->parent->right;
			if (y->color == RED) {

				/* uncle is RED */
				x->parent->color = BLACK;
				y->color = BLACK;
				x->parent->parent->color = RED;
				x = x->parent->parent;
			} else {

				/* uncle is BLACK */
				if (x == x->parent->right) {
					/* make x a left child */
					x = x->parent;
					rotateLeft(rbt, x);
				}

				/* recolor and rotate */
				x->parent->color = BLACK;
				x->parent->parent->color = RED;
				rotateRight(rbt, x->parent->parent);
			}
		} else {

			/* mirror image of above code */
			NodeType *y = x->parent->parent->left;
			if (y->color == RED) {

				/* uncle is RED */
				x->parent->color = BLACK;
				y->color = BLACK;
				x->parent->parent->color = RED;
				x = x->parent->parent;
			} else {

				/* uncle is BLACK */
				if (x == x->parent->left) {
					x = x->parent;
					rotateRight(rbt, x);
				}
				x->parent->color = BLACK;
				x->parent->parent->color = RED;
				rotateLeft(rbt, x->parent->parent);
			}
		}
	}
	rbt->root->color = BLACK;
}

rbt_StatusEnum rbt_insert(void *rbt, void *pkey, void *prec, int duplOK) {
	NodeType *current, *parent, *x;
	int dupl, direction=0;
   /***********************************************
	*  allocate node for data and insert in tree  *
	***********************************************/

	/* find future parent */
	if(pkey == NULL)
		return(RBT_STATUS_INVALID_ARGUMENT);
	current = ((rbtType *)rbt)->root;
	parent = 0;
	while (current != NIL) {
		if (((rbtType *)rbt)->compEQ(pkey, current->pkey)) {
			if(duplOK)
				dupl = 1;
			else
				return RBT_STATUS_DUPLICATE_KEY;
		} else {
			dupl = 0;
		}
		parent = current;
		if(dupl) {/* if an (allowed) duplicate key occurred */
		  /* the best is to choose randomly. This is heuristic but
		     appears to reduce the occurrence of unbalanced trees
		     with duplicate keys. E.g. w/ 640K records:
		     non-dupl: maxdepth = 23;
		     dupl: maxdepth = 29;
		     dupl w/randomization: maxdepth = 22 */
			/* static int flipflop = 0;
			flipflop = !flipflop;
		    direction = flipflop;
		    direction = 1;
		    direction = !current->color; */

		    direction = (rand() & 1);
		} else {
			direction = ((rbtType *)rbt)->compLT(pkey, current->pkey);
		}
		current = direction ? current->left : current->right;
	}

	/* setup new node */
	if ((x = malloc(sizeof(*x))) == 0)
		return RBT_STATUS_MEM_EXHAUSTED;
	x->parent = parent;
	x->left = NIL;
	x->right = NIL;
	x->color = RED;
	x->pkey = pkey;
	x->prec = prec;

	/* insert node in tree */
	if(parent) {
		if(direction)
			parent->left = x;
		else
			parent->right = x;
	} else {
		((rbtType *)rbt)->root = x;
	}

	insertFixup(rbt, x);
	((rbtType *)rbt)->size++;
	return RBT_STATUS_OK;
}

static void deleteFixup(void *rbt, NodeType *x) {

   /*************************************
	*  maintain Red-Black tree balance  *
	*  after deleting node x			*
	*************************************/

	while (x != ((rbtType *)rbt)->root && x->color == BLACK) {
		if (x == x->parent->left) {
			NodeType *w = x->parent->right;
			if (w->color == RED) {
				w->color = BLACK;
				x->parent->color = RED;
				rotateLeft (rbt, x->parent);
				w = x->parent->right;
			}
			if (w->left->color == BLACK && w->right->color == BLACK) {
				w->color = RED;
				x = x->parent;
			} else {
				if (w->right->color == BLACK) {
					w->left->color = BLACK;
					w->color = RED;
					rotateRight (rbt, w);
					w = x->parent->right;
				}
				w->color = x->parent->color;
				x->parent->color = BLACK;
				w->right->color = BLACK;
				rotateLeft (rbt, x->parent);
				x = ((rbtType *)rbt)->root;
			}
		} else {
			NodeType *w = x->parent->left;
			if (w->color == RED) {
				w->color = BLACK;
				x->parent->color = RED;
				rotateRight (rbt, x->parent);
				w = x->parent->left;
			}
			if (w->right->color == BLACK && w->left->color == BLACK) {
				w->color = RED;
				x = x->parent;
			} else {
				if (w->left->color == BLACK) {
					w->right->color = BLACK;
					w->color = RED;
					rotateLeft (rbt, w);
					w = x->parent->left;
				}
				w->color = x->parent->color;
				x->parent->color = BLACK;
				w->left->color = BLACK;
				rotateRight (rbt, x->parent);
				x = ((rbtType *)rbt)->root;
			}
		}
	}
	x->color = BLACK;
}

rbt_StatusEnum rbt_erase(void *rbt, void *z) {
	NodeType *x, *y;

	if(z == NULL)
		return(RBT_STATUS_INVALID_ARGUMENT);
	if (((NodeType *)z)->left == NIL || ((NodeType *)z)->right == NIL) {
		/* y has a NIL node as a child */
		y = z;
	} else {
		/* find tree successor with a NIL node as a child */
		y = ((NodeType *)z)->right;
		while (y->left != NIL) y = y->left;
	}

	/* x is y's only child */
	if (y->left != NIL)
		x = y->left;
	else
		x = y->right;

	/* remove y from the parent chain */
	x->parent = y->parent;
	if (y->parent)
		if (y == y->parent->left)
			y->parent->left = x;
		else
			y->parent->right = x;
	else
		((rbtType *)rbt)->root = x;

	if (y != z) {
		((NodeType *)z)->pkey = y->pkey;
		((NodeType *)z)->prec = y->prec;
	}


	if (y->color == BLACK)
		deleteFixup (rbt, x);

	free(y);

	((rbtType *)rbt)->size--;
	return RBT_STATUS_OK;
}

rbt_StatusEnum rbt_eraseKey(void *rbt, void *pkey) {
	NodeType *z;

	if(pkey == NULL)
		return(RBT_STATUS_INVALID_ARGUMENT);
	/* find node in tree */
	z = ((rbtType *)rbt)->root;
	while(z != NIL) {
		if(((rbtType *)rbt)->compEQ(pkey, z->pkey))
			break;
		else
			z = ((rbtType *)rbt)->compLT(pkey, z->pkey) ? z->left : z->right;
	}
	if (z == NIL) return RBT_STATUS_KEY_NOT_FOUND;
	return rbt_erase(rbt, z);
}

void *rbt_next(void *rbt, void *i) {
	if(i == NULL)
		return NULL;
	if (((NodeType *)i)->right != NIL) {
		/* go right 1, then left to the end */
		for (i = ((NodeType *)i)->right;
		     ((NodeType *)i)->left != NIL;
		     i = ((NodeType *)i)->left);
	} else {
		/* while you're the right child, chain up parent link */
		NodeType *p = ((NodeType *)i)->parent;
		while (p && i == p->right) {
			i = p;
			p = p->parent;
		}

		/* return the "inorder" node */
		i = p;
	}
	return i;
}

void *rbt_previous(void *rbt, void *i) {
	if(i == NULL)
		return NULL;
	if (((NodeType *)i)->left != NIL) {
		/* go left 1, then right to the end */
		for (i = ((NodeType *)i)->left;
		     ((NodeType *)i)->right != NIL;
		     i = ((NodeType *)i)->right);
	} else {
		/* while you're the left child, chain up parent link */
		NodeType *p = ((NodeType *)i)->parent;
		while (p && i == p->left) {
			i = p;
			p = p->parent;
		}

		/* return the "inorder" node */
		i = p;
	}
	return i;
}

void *rbt_begin(void *rbt) {
	/* return pointer to first value, or NULL if table empty */
	NodeType *i;
	for (i = ((rbtType *)rbt)->root; i->left != NIL; i = i->left);
	if(i == NIL)
		i = NULL;
	return i;
}

void *rbt_end(void *rbt) {
	/* return pointer to last value, or NULL if table empty */
	NodeType *i;
	for (i = ((rbtType *)rbt)->root; i->right != NIL; i = i->right);
	if(i == NIL)
		i = NULL;
	return i;
}


/* modif by EM to set *iter anyway to the best match >= key
   if all entries are smaller than key, return NULL
   if iter = NULL, just check presence of the key */
rbt_StatusEnum rbt_find(void *rbt, void *pkey, void **iter) {
	NodeType *current;
	void *dummy_iter;

	if(pkey == NULL)
		return(RBT_STATUS_INVALID_ARGUMENT);
	if(iter == NULL)
		iter = &dummy_iter;	/* if iter's value is not requested, use a temp one */
	current = ((rbtType *)rbt)->root;
	*iter = NULL;
	if(current->pkey == NULL)
		return RBT_STATUS_KEY_NOT_FOUND;
	for(;;) {
		if(current == NIL && *iter != NULL) {
			if(((rbtType *)rbt)->compLT( ((NodeType *)(*iter))->pkey, pkey))
				*iter = rbt_next(rbt, *iter);
			return RBT_STATUS_KEY_NOT_FOUND;
		}
		*iter = current;
		if(((rbtType *)rbt)->compEQ(pkey, current->pkey)) {
			return RBT_STATUS_OK;
		} else {
			if(((rbtType *)rbt)->compLT (pkey, current->pkey)) {
				current = current->left;
			} else {
				current =  current->right;
			}
		}
	}
}

int rbt_depth(void *rbt, void *iter) {
	int i;
	NodeType *r = ((rbtType *)rbt)->root;
	NodeType *n = (NodeType *)iter;

	for(i=0;;i++) {
		if (n == r)
			return i;
		else
			n = n->parent;
	}
}

int rbt_isleaf(void *rbt, void *iter) {
	return(	((NodeType *)iter)->left == NIL &&
			((NodeType *)iter)->right == NIL);
}

void *rbt_value(void *iter) {
	return ((NodeType *)iter)->prec;  /* a void* pointer to data */
}

void *rbt_key(void *iter) {
	return ((NodeType *)iter)->pkey;  /* a void* pointer to key */
}

int rbt_size(void* rbt) {
	return ((rbtType *)rbt)->size;
}

