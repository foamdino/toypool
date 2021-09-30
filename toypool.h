/* A toy pool-allocator */

#ifndef TOYPOOL_H
#define TOYPOOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

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
inline dlinklist_t *append(dlinklist_t *list, node_t *to_add, void *data)
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

inline dlinklist_t *prepend(dlinklist_t *list, node_t *to_add, void *data)
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

/* pool related */
typedef struct toypool toypool_t;

struct toypool
{
    char name[64];
    dlinklist_t empty_blocks;
    dlinklist_t used_blocks;
    dlinklist_t full_blocks;
    size_t elem_size;
    size_t elems_size; /* allocation size = num elems * elem_size */
    size_t block_size;
    unsigned int elems_per_block;
    uint64_t total_elems;
    uint64_t free_elems;
    uint64_t used_elems;
};

#endif /* TOYPOOL_H */