/*
 * include/common/mini-clist.h
 * Circular list manipulation macros and structures.
 *
 * Copyright (C) 2002-2014 Willy Tarreau - w@1wt.eu
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, version 2.1
 * exclusively.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _COMMON_MINI_CLIST_H
#define _COMMON_MINI_CLIST_H

#include <common/config.h>

/* these are circular or bidirectionnal lists only. Each list pointer points to
 * another list pointer in a structure, and not the structure itself. The
 * pointer to the next element MUST be the first one so that the list is easily
 * cast as a single linked list or pointer.
 */
struct list {
    struct list *n;	/* next */
    struct list *p;	/* prev */
};

/* a back-ref is a pointer to a target list entry. It is used to detect when an
 * element being deleted is currently being tracked by another user. The best
 * example is a user dumping the session table. The table does not fit in the
 * output buffer so we have to set a mark on a session and go on later. But if
 * that marked session gets deleted, we don't want the user's pointer to go in
 * the wild. So we can simply link this user's request to the list of this
 * session's users, and put a pointer to the list element in ref, that will be
 * used as the mark for next iteration.
 */
struct bref {
	struct list users;
	struct list *ref; /* pointer to the target's list entry */
};

/* a word list is a generic list with a pointer to a string in each element. */
struct wordlist {
	struct list list;
	char *s;
};

/* this is the same as above with an additional pointer to a condition. */
struct cond_wordlist {
	struct list list;
	void *cond;
	char *s;
};

/* First undefine some macros which happen to also be defined on OpenBSD,
 * in sys/queue.h, used by sys/event.h
 */
#undef LIST_HEAD
#undef LIST_INIT
#undef LIST_NEXT

/* ILH = Initialized List Head : used to prevent gcc from moving an empty
 * list to BSS. Some older version tend to trim all the array and cause
 * corruption.
 */
#define ILH		{ .n = (struct list *)1, .p = (struct list *)2 }

#define LIST_HEAD(a)	((void *)(&(a)))

#define LIST_INIT(l) ((l)->n = (l)->p = (l))

#define LIST_HEAD_INIT(l) { &l, &l }

/* adds an element at the beginning of a list ; returns the element */
#define LIST_ADD(lh, el) ({ (el)->n = (lh)->n; (el)->n->p = (lh)->n = (el); (el)->p = (lh); (el); })

/* adds an element at the end of a list ; returns the element */
#define LIST_ADDQ(lh, el) ({ (el)->p = (lh)->p; (el)->p->n = (lh)->p = (el); (el)->n = (lh); (el); })

/* removes an element from a list and returns it */
#define LIST_DEL(el) ({ typeof(el) __ret = (el); (el)->n->p = (el)->p; (el)->p->n = (el)->n; (__ret); })

/* removes an element from a list, initializes it and returns it.
 * This is faster than LIST_DEL+LIST_INIT as we avoid reloading the pointers.
 */
#define LIST_DEL_INIT(el) ({ \
	typeof(el) __ret = (el);                        \
	typeof(__ret->n) __n = __ret->n;                \
	typeof(__ret->p) __p = __ret->p;                \
	__n->p = __p; __p->n = __n;                     \
	__ret->n = __ret->p = __ret;                    \
	__ret;                                          \
})

/* returns a pointer of type <pt> to a structure containing a list head called
 * <el> at address <lh>. Note that <lh> can be the result of a function or macro
 * since it's used only once.
 * Example: LIST_ELEM(cur_node->args.next, struct node *, args)
 */
#define LIST_ELEM(lh, pt, el) ((pt)(((void *)(lh)) - ((void *)&((pt)NULL)->el)))

/* checks if the list head <lh> is empty or not */
#define LIST_ISEMPTY(lh) ((lh)->n == (lh))

/* checks if the list element <el> was added to a list or not. This only
 * works when detached elements are reinitialized (using LIST_DEL_INIT)
 */
#define LIST_ADDED(el) ((el)->n != (el))

/* returns a pointer of type <pt> to a structure following the element
 * which contains list head <lh>, which is known as element <el> in
 * struct pt.
 * Example: LIST_NEXT(args, struct node *, list)
 */
#define LIST_NEXT(lh, pt, el) (LIST_ELEM((lh)->n, pt, el))


/* returns a pointer of type <pt> to a structure preceding the element
 * which contains list head <lh>, which is known as element <el> in
 * struct pt.
 */
#undef LIST_PREV
#define LIST_PREV(lh, pt, el) (LIST_ELEM((lh)->p, pt, el))

/*
 * Simpler FOREACH_ITEM macro inspired from Linux sources.
 * Iterates <item> through a list of items of type "typeof(*item)" which are
 * linked via a "struct list" member named <member>. A pointer to the head of
 * the list is passed in <list_head>. No temporary variable is needed. Note
 * that <item> must not be modified during the loop.
 * Example: list_for_each_entry(cur_acl, known_acl, list) { ... };
 */ 
#define list_for_each_entry(item, list_head, member)                      \
	for (item = LIST_ELEM((list_head)->n, typeof(item), member);     \
	     &item->member != (list_head);                                \
	     item = LIST_ELEM(item->member.n, typeof(item), member))

/*
 * Same as list_for_each_entry but starting from current point
 * Iterates <item> through the list starting from <item>
 * It's basically the same macro but without initializing item to the head of
 * the list.
 */
#define list_for_each_entry_from(item, list_head, member) \
	for ( ; &item->member != (list_head); \
	     item = LIST_ELEM(item->member.n, typeof(item), member))

/*
 * Simpler FOREACH_ITEM_SAFE macro inspired from Linux sources.
 * Iterates <item> through a list of items of type "typeof(*item)" which are
 * linked via a "struct list" member named <member>. A pointer to the head of
 * the list is passed in <list_head>. A temporary variable <back> of same type
 * as <item> is needed so that <item> may safely be deleted if needed.
 * Example: list_for_each_entry_safe(cur_acl, tmp, known_acl, list) { ... };
 */ 
#define list_for_each_entry_safe(item, back, list_head, member)           \
	for (item = LIST_ELEM((list_head)->n, typeof(item), member),     \
	     back = LIST_ELEM(item->member.n, typeof(item), member);     \
	     &item->member != (list_head);                                \
	     item = back, back = LIST_ELEM(back->member.n, typeof(back), member))


/*
 * Same as list_for_each_entry_safe but starting from current point
 * Iterates <item> through the list starting from <item>
 * It's basically the same macro but without initializing item to the head of
 * the list.
 */
#define list_for_each_entry_safe_from(item, back, list_head, member) \
	for (back = LIST_ELEM(item->member.n, typeof(item), member);     \
	     &item->member != (list_head);                                \
	     item = back, back = LIST_ELEM(back->member.n, typeof(back), member))

#include <common/hathreads.h>
#define LLIST_BUSY ((struct list *)1)

/*
 * Locked version of list manipulation macros.
 * It is OK to use those concurrently from multiple threads, as long as the
 * list is only used with the locked variants. The only "unlocked" macro you
 * can use with a locked list is LIST_INIT.
 */
#define LIST_ADD_LOCKED(lh, el)                                            \
	do {                                                               \
		while (1) {                                                \
			struct list *n;                                    \
			struct list *p;                                    \
			n = _HA_ATOMIC_XCHG(&(lh)->n, LLIST_BUSY);         \
			if (n == LLIST_BUSY)                               \
			        continue;                                  \
			p = _HA_ATOMIC_XCHG(&n->p, LLIST_BUSY);            \
			if (p == LLIST_BUSY) {                             \
				(lh)->n = n;                               \
				__ha_barrier_store();                      \
				continue;                                  \
			}                                                  \
			(el)->n = n;                                       \
			(el)->p = p;                                       \
			__ha_barrier_store();                              \
			n->p = (el);                                       \
			__ha_barrier_store();                              \
			p->n = (el);                                       \
			__ha_barrier_store();                              \
			break;                                             \
		}                                                          \
	} while (0)

#define LIST_ADDQ_LOCKED(lh, el)                                           \
	do {                                                               \
		while (1) {                                                \
			struct list *n;                                    \
			struct list *p;                                    \
			p = _HA_ATOMIC_XCHG(&(lh)->p, LLIST_BUSY);         \
			if (p == LLIST_BUSY)                               \
			        continue;                                  \
			n = _HA_ATOMIC_XCHG(&p->n, LLIST_BUSY);            \
			if (n == LLIST_BUSY) {                             \
				(lh)->p = p;                               \
				__ha_barrier_store();                      \
				continue;                                  \
			}                                                  \
			(el)->n = n;                                       \
			(el)->p = p;                                       \
			__ha_barrier_store();                              \
			p->n = (el);                                       \
			__ha_barrier_store();                              \
			n->p = (el);                                       \
			__ha_barrier_store();                              \
			break;                                             \
		}                                                          \
	} while (0)

#define LIST_DEL_LOCKED(el)                                                \
	do {                                                               \
		while (1) {                                                \
			struct list *n, *n2;                               \
			struct list *p, *p2 = NULL;                        \
			n = _HA_ATOMIC_XCHG(&(el)->n, LLIST_BUSY);         \
			if (n == LLIST_BUSY)                               \
			        continue;                                  \
			p = _HA_ATOMIC_XCHG(&(el)->p, LLIST_BUSY);         \
			if (p == LLIST_BUSY) {                             \
				(el)->n = n;                               \
				__ha_barrier_store();                      \
				continue;                                  \
			}                                                  \
			if (p != (el)) {                                   \
			        p2 = _HA_ATOMIC_XCHG(&p->n, LLIST_BUSY);   \
			        if (p2 == LLIST_BUSY) {                    \
			                (el)->p = p;                       \
					(el)->n = n;                       \
					__ha_barrier_store();              \
					continue;                          \
				}                                          \
			}                                                  \
			if (n != (el)) {                                   \
			        n2 = _HA_ATOMIC_XCHG(&n->p, LLIST_BUSY);   \
				if (n2 == LLIST_BUSY) {                    \
					if (p2 != NULL)                    \
						p->n = p2;                 \
					(el)->p = p;                       \
					(el)->n = n;                       \
					__ha_barrier_store();              \
					continue;                          \
				}                                          \
			}                                                  \
			n->p = p;                                          \
			p->n = n;                                          \
			__ha_barrier_store();                              \
			(el)->p = (el);                                    \
			(el)->n = (el);	                                   \
			__ha_barrier_store();                              \
			break;                                             \
		}                                                          \
	} while (0)


/* Remove the first element from the list, and return it */
#define LIST_POP_LOCKED(lh, pt, el)                                        \
	({                                                                 \
		 void *_ret;                                               \
		 while (1) {                                               \
			 struct list *n, *n2;                              \
			 struct list *p, *p2;                              \
			 n = _HA_ATOMIC_XCHG(&(lh)->n, LLIST_BUSY);        \
			 if (n == LLIST_BUSY)                              \
			         continue;                                 \
			 if (n == (lh)) {                                  \
				 (lh)->n = lh;                             \
				 __ha_barrier_store();                     \
				 _ret = NULL;                              \
				 break;                                    \
			 }                                                 \
			 p = _HA_ATOMIC_XCHG(&n->p, LLIST_BUSY);           \
			 if (p == LLIST_BUSY) {                            \
				 (lh)->n = n;                              \
				 __ha_barrier_store();                     \
				 continue;                                 \
			 }                                                 \
			 n2 = _HA_ATOMIC_XCHG(&n->n, LLIST_BUSY);          \
			 if (n2 == LLIST_BUSY) {                           \
				 n->p = p;                                 \
				 __ha_barrier_store();                     \
				 (lh)->n = n;                              \
				 __ha_barrier_store();                     \
				 continue;                                 \
			 }                                                 \
			 p2 = _HA_ATOMIC_XCHG(&n2->p, LLIST_BUSY);         \
			 if (p2 == LLIST_BUSY) {                           \
				 n->n = n2;                                \
				 n->p = p;                                 \
				 __ha_barrier_store();                     \
				 (lh)->n = n;                              \
				 __ha_barrier_store();                     \
				 continue;                                 \
			 }                                                 \
			 (lh)->n = n2;                                     \
			 (n2)->p = (lh);                                   \
			 __ha_barrier_store();                             \
			 (n)->p = (n);                                     \
			 (n)->n = (n);	                                   \
			 __ha_barrier_store();                             \
			 _ret = LIST_ELEM(n, pt, el);                      \
			 break;                                            \
		 }                                                         \
		 (_ret);                                                   \
	 })

#endif /* _COMMON_MINI_CLIST_H */
