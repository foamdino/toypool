/* A toy pool-allocator */

#ifndef TOYPOOL_H
#define TOYPOOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

enum
{
    TOYPOOL_NAME_LEN = 64,
    TOYPOOL_ALIGNMENT_SZ = 8,
};

/* mmap */
static inline void
*toy_mmap(size_t size)
{
    void *p =
		mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	return p;
}

/* string handling */
static inline size_t
toy_strlcpy(char *dst, const char *src, size_t dstsz)
{
	size_t len;
	len = strlen(src);
	if (len > dstsz)
		len = dstsz;
	memcpy(dst, src, len);
	dst[len] = '\0';
	return len;
}

/* dlinked list structs & inline functions */
typedef struct node node_t;
typedef struct dlinklist dlinklist_t;

struct node
{
    node_t *next;
    node_t *prev;
    void *data;
};

struct dlinklist
{
    node_t *head;
    node_t *tail;
    unsigned int length;
};

/* note: doesn't malloc */
static inline dlinklist_t *toy_append(dlinklist_t *list, node_t *to_add, void *data)
{
    /* set-up node */
    to_add->data = data;
    to_add->next = NULL;
    to_add->prev = list->tail;
    
    /* handle list manip */
    if(list->tail != NULL)
        list->tail->next = to_add;
    else if(list->head == NULL) /*empty list case */
        list->head = to_add;
    list->tail = to_add;
    list->length++;

    return list;
}

node_t *
toy_add_tail(dlinklist_t *list, node_t *node, void *data)
{
	node->data = data;

	node->next = NULL;
	node->prev = list->tail;

	if (list->tail != NULL)
		list->tail->next = node;
	else if (list->head == NULL)
		list->head = node;
	list->tail = node;
	list->length++;

	return node;
}

static inline dlinklist_t *toy_prepend(dlinklist_t *list, node_t *to_add, void *data)
{
    /* set-up node */
    to_add->data = data;
    to_add->prev = NULL;
    to_add->next = list->head;

    if(list->head != NULL)
        list->head->prev = to_add;

    list->head = to_add;
    list->length++;

    return list;
}

/**
 * Move a double-linked list node from one list to the head of another.
 *
 * @param from				Pointer to the list to take the node from
 * @param to				Pointer to the list to add the node to
 * @param node				Pointer to the node to move
 */
static inline void
toy_move_to_list(dlinklist_t *from, dlinklist_t *to, node_t *node)
{
	assert(from != NULL);
	assert(to != NULL);
	assert(node != NULL);

	if (node->next != NULL)
		node->next->prev = node->prev;
	else
		from->tail = node->prev;
	if (node->prev != NULL)
		node->prev->next = node->next;
	else
		from->head = node->next;
	from->length--;

	node->prev = NULL;
	node->next = to->head;
	if (to->head != NULL)
		to->head->prev = node;
	else if (to->tail == NULL)
		to->tail = node;
	to->head = node;
	to->length++;
}

/**
 * Remove a dlink node from a dlink list. The node memory is not freed.
 *
 * @param list				Pointer to the list to remove from
 * @param node				Pointer to the node to remove
 */
void
toy_remove(dlinklist_t *list, node_t *node)
{
	assert(list != NULL);
	assert(node != NULL);

	if (node->next != NULL)
		node->next->prev = node->prev;
	else
		list->tail = node->prev;
	if (node->prev != NULL)
		node->prev->next = node->next;
	else
		list->head = node->next;

	/* Nullify all node pointers. */
	node->next = node->prev = NULL;
	node->data              = NULL;

	list->length--;
}

/** Macro - walk forward along a dlinklist from the head. */
#define DLINK_FORWARD(l, n) for (n = (l); n != NULL; n = n->next)

/** Macro - walk forward along a dlinklist from the head, caching the next node */
#define DLINK_FORWARD_SAFE(l, n, nx)                                                \
	for (n = (l), nx = ((n) ? (n)->next : NULL); n != NULL;                              \
	     n = nx, nx = ((n) ? (n)->next : NULL))


/* pool related */
typedef struct toypool toypool_t;
typedef struct elem_container elem_container_t;
typedef struct memblock memblock_t;

/* Memory pool block containing individual elements. */
struct memblock
{
	node_t self;            /**< Pool attachment node. */
	toypool_t *pool;             /**< Pointer to the pool that owns this block. */
	elem_container_t *next_free_alloc; /**< Next object in the block. */
	void *next_elem;                /**< Next element pointer */
	unsigned int free_elems;        /**< Number of free elements in this block. */
    uintptr_t end_addr;          /**< End address of the memblock */

	/* Element memory at the end. This must be last. */
	void *elems; /**< Elements - this must be last. */
};

/* An element container. */
struct elem_container
{
	memblock_t *block; /**< Pointer to the block that owns this allocation. */
	union {
		elem_container_t
			*next_free;        /**< Pointer to the next free allocation, when unused. */
		unsigned char elem[1]; /**< Memory element, when used. */
		void *padding;         /**< Padding */
	} mem;
};

struct toypool
{
    char name[TOYPOOL_NAME_LEN];
    dlinklist_t empty_blocks;
    dlinklist_t used_blocks;
    dlinklist_t full_blocks;
    size_t elem_size; /* adjusted/padded elem size */
    size_t requested_elem_size; /* originally requested elem size */
    size_t elems_size; /* allocation size = num elems * elem_size */
    size_t block_size;
    unsigned int elems_per_block;
    uint64_t total_elems;
    uint64_t free_elems;
    uint64_t used_elems;
};

/* this struct is used as our test struct */
struct toypool_test_blob
{
    node_t self;        /**< link to self */
    char name[64];              /**< name of pool blob */
	dlinklist_t thing_list;  /**< list of things associated with this blob */
	unsigned short numeric_val; /**< an unsigned short for this blob */

	void *opaque_data; /**< pointer to opaque data */
};

typedef struct toypool_test_blob toypool_test_blob_t;

#endif /* TOYPOOL_H */