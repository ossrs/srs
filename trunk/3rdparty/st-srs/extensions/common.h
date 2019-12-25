#ifndef _STX_COMMON_H_
#define _STX_COMMON_H_

#include <stddef.h>
#include <stdlib.h>


#define STX_BEGIN_MACRO  {
#define STX_END_MACRO    }


/*****************************************
 * Circular linked list definitions
 */

typedef struct _stx_clist {
  struct _stx_clist *next;
  struct _stx_clist *prev;
} stx_clist_t;

/* Insert element "_e" into the list, before "_l" */
#define STX_CLIST_INSERT_BEFORE(_e,_l) \
    STX_BEGIN_MACRO              \
        (_e)->next = (_l);       \
        (_e)->prev = (_l)->prev; \
        (_l)->prev->next = (_e); \
        (_l)->prev = (_e);       \
    STX_END_MACRO

/* Insert element "_e" into the list, after "_l" */
#define STX_CLIST_INSERT_AFTER(_e,_l) \
    STX_BEGIN_MACRO              \
        (_e)->next = (_l)->next; \
        (_e)->prev = (_l);       \
        (_l)->next->prev = (_e); \
        (_l)->next = (_e);       \
    STX_END_MACRO

/* Append an element "_e" to the end of the list "_l" */
#define STX_CLIST_APPEND_LINK(_e,_l) STX_CLIST_INSERT_BEFORE(_e,_l)

/* Remove the element "_e" from it's circular list */
#define STX_CLIST_REMOVE_LINK(_e)      \
    STX_BEGIN_MACRO                    \
        (_e)->prev->next = (_e)->next; \
        (_e)->next->prev = (_e)->prev; \
    STX_END_MACRO

/* Return the head/tail of the list */
#define STX_CLIST_HEAD(_l) (_l)->next
#define STX_CLIST_TAIL(_l) (_l)->prev

/* Return non-zero if the given circular list "_l" is empty, */
/* zero if the circular list is not empty */
#define STX_CLIST_IS_EMPTY(_l) \
    ((_l)->next == (_l))

/* Initialize a circular list */
#define STX_CLIST_INIT_CLIST(_l) \
    STX_BEGIN_MACRO        \
        (_l)->next = (_l); \
        (_l)->prev = (_l); \
    STX_END_MACRO


/*****************************************
 * Useful macros
 */

#ifndef offsetof
#define offsetof(type, identifier) ((size_t)&(((type *)0)->identifier))
#endif

#define STX_MIN(a, b) (((a) < (b)) ? (a) : (b))

#endif /* !_STX_COMMON_H_ */

