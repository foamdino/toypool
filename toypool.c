/* A toy pool-allocator */

#include <stdio.h>
#include <assert.h>
#include "toypool.h"

static int num_blobs         = 100;
static unsigned int num_runs = 20;

void
block_new(toypool_t *pool)
{
    assert(pool != NULL);

    /* Allocate a new block. Note use mmap() here, so that memory can be returned to the
	 * system when blocks are released within the application lifecycle. */
	memblock_t *block = toy_mmap(pool->block_size);
	block->pool = pool;
	block->free_elems = pool->elems_per_block;
	block->next_elem = &block->elems;

	/* Record the actual end_addr so we can accurately free later */
	block->end_addr = (uintptr_t)(block + pool->block_size);

	/* Attach the block to the pool. */
	block->pool->free_elems += block->free_elems;
	block->pool->total_elems += block->free_elems;
	toy_prepend(&pool->empty_blocks, &block->self, block);
}

memblock_t *
find_elem_block(toypool_t *pool, void *elem)
{
	assert(pool != NULL);
	assert(elem != NULL);

	node_t *n;
	uintptr_t e = (uintptr_t)elem;

	/* Check the used_blocks list first on the balance of probabilities that the elem is
	 * here */
	printf("Checking used blocks for elem...\n");
	DLINK_FORWARD (pool->used_blocks.head, n)
	{
		memblock_t *b = n->data;
		printf( "block: %p end_addr: %lu\n", b, b->end_addr);
		if (e > (uintptr_t)b && e < (uintptr_t)b->end_addr)
		{
			printf("found elem address in current used block: %p\n", elem);
			return b;
		}
	}

	/* Check the full blocks (less likely case) */
	printf("Checking full blocks for elem...\n");
	DLINK_FORWARD (pool->full_blocks.head, n)
	{
		memblock_t *b = n->data;
		printf("block: %p end_addr: %lu\n", b, b->end_addr);
		if (e > (uintptr_t)b && e < (uintptr_t)b->end_addr)
		{
			printf("found elem address in current full block: %p\n", elem);
			return b;
		}
	}

	/* elem address is not contained in any of the used or full blocks */
	printf("Unable to find elem in blocks!\n");
	return NULL;
}

toypool_t *
pool_new(const char *name,
        size_t elem_size,
        unsigned int elems_per_block)
{
    printf("Creating new pool: %s with size: %lu and num elems: %u\n", 
        name, elem_size, elems_per_block);

    size_t requested_elem_size = elem_size;

    /* The requested element size may be something quite obscure. We need it to be
	 * at least sizeof(elem_alloc_t) and be aligned. */
	elem_size = __builtin_offsetof(elem_alloc_t, mem.elem) + elem_size;
	if (elem_size < sizeof(elem_alloc_t))
		elem_size = sizeof(elem_alloc_t);
	if (elem_size % TOYPOOL_ALIGNMENT_SZ)
		elem_size =
			elem_size + TOYPOOL_ALIGNMENT_SZ - (elem_size % TOYPOOL_ALIGNMENT_SZ);
	if (elem_size < TOYPOOL_ALIGNMENT_SZ)
		elem_size = TOYPOOL_ALIGNMENT_SZ;

    assert((elem_size % TOYPOOL_ALIGNMENT_SZ) == 0);
	printf("Adjusted elem_size (should be 0): %lu\n", (elem_size % TOYPOOL_ALIGNMENT_SZ));

    /* Create the pool and save our individual element size and the number of elements per
	 * block. Our elems_size is therefore (elem_size * elems_per_block).
	 *
	 * The block size itself is the size of the block header (up to the elems member),
	 * plus the calculated elems size. */

	toypool_t *pool = malloc(sizeof(toypool_t));
	toy_strlcpy(pool->name, name, sizeof(pool->name));
	pool->requested_elem_size = requested_elem_size;
	pool->elem_size = elem_size;
	pool->elems_per_block = elems_per_block;
	pool->elems_size = pool->elem_size * pool->elems_per_block;
	pool->block_size = __builtin_offsetof(memblock_t, elems) + pool->elems_size;

    return pool;
}

void * 
pool_alloc(toypool_t *pool)
{
	assert(pool != NULL);

	memblock_t *block = NULL;
	elem_alloc_t *alloc = NULL;

	/* The most likely use case is that we have a block that has space in it. */
	if (pool->used_blocks.head != NULL) 
		block = pool->used_blocks.head->data;
	else
	{
		/* If there are no empty blocks, we need to allocate a new one. */
		if (pool->empty_blocks.head == NULL)
		{
			assert(pool->free_elems == 0);
			/* after creating a new block, the empty block will be the head of empty_blocks list */
			block_new(pool);
		}

		/* We can now get the next empty block and move it into the used_block list. */
		assert(pool->empty_blocks.head != NULL);
		block = pool->empty_blocks.head->data;
		toy_move_to_list(&pool->empty_blocks, &pool->used_blocks, &block->self);
	}

	assert(block != NULL);
	assert(block->free_elems > 0);

	/* We might have a pointer to the next free allocation. */
	if (block->next_free_alloc != NULL)
	{
		alloc = block->next_free_alloc;
		block->next_free_alloc = alloc->mem.next_free;
		alloc->mem.next_free   = NULL;
		assert(alloc->block == block);
	}
	else
	{
		/* Bounds check. */
		if ((block->next_elem + pool->elem_size) > (void *)(&block->elems + pool->elems_size))
		{
			printf("Mempool out of bounds: next_elem %p elem_size %lu (%p) exceeds address %p\n",
				block->next_elem,
				pool->elem_size,
				(block->next_elem + pool->elem_size),
				(&block->elems + pool->elems_size));
			exit(1);
		}

		alloc = block->next_elem;
		block->next_elem += pool->elem_size;
		alloc->block = block;
		alloc->mem.next_free = NULL;
	}

	assert(block->pool->free_elems > 0);

	block->free_elems--;
	block->pool->free_elems--;
	block->pool->used_elems++;

	/* If this block is now at capacity, move it into the full_blocks list. */
	if (block->free_elems == 0)
	{
		toy_move_to_list(&pool->used_blocks, &pool->full_blocks, &block->self);
		// printf("Moved full block=%p onto full_blocks in pool=%p(%s)\n",
		// 		block,
		// 		pool,
		// 		pool->name);
	}

	/* To enforce bounds with cheri apis we need a pointer to the elem itself
	
	void *elem = &(alloc->mem.elem);

	Then we perform the resizing of the bounds for this elem.  
	Initially the bounds will be from address of elem to end of block from which the elem was obtained.
	We need to reset the upper bound to be the exact length of the elem

	elem = cheri_bounds_set(elem, pool->requested_elem_size);

	now we can return the elem we have just reset the bounds on
	return elem;
	*/

	/* Return pointer to the allocation element. */
	assert(alloc->block != NULL);
	return &(alloc->mem.elem);
}

void
pool_release(toypool_t *pool, void *elem)
{
	assert(pool != NULL);
	assert(elem != NULL);

	elem_alloc_t *alloc;

	/* Handle cheri specific logic - resetting bounds, finding containing block etc.. */
	/*
	First we must find the block that contains the elem we want to free:
	
	memblock_t *container_block = find_elem_block(pool, elem);

	Next we need the bounds of the block that contains the elem:

	uintptr_t elem_cap_addr = 0;
	elem_cap_addr = cheri_address_get(elem);

	This allows us to reset the bounds of the elem to the same as the block:

	elem = cheri_address_set(container_block, elem_cap_addr);
	*/

	/* actually release the allocation.. */
	alloc = (elem - __builtin_offsetof(elem_alloc_t, mem.elem));
	assert(alloc->block != NULL);

	alloc->mem.next_free          = alloc->block->next_free_alloc;
	alloc->block->next_free_alloc = alloc;

	assert(alloc->block->pool->used_elems > 0);

	alloc->block->pool->used_elems--;
	alloc->block->pool->free_elems++;
	alloc->block->free_elems++;

	/* Does this allocation block now need to move lists? */
	if (alloc->block->free_elems == alloc->block->pool->elems_per_block)
	{
		/* This block was used, but is now empty. */
		toy_move_to_list(&alloc->block->pool->used_blocks,
		                      &alloc->block->pool->empty_blocks,
		                      &alloc->block->self);

		/* Reset the guts of this block to defragment it, in case it gets used again. */
		alloc->block->next_free_alloc = NULL;
		alloc->block->next_elem       = &alloc->block->elems;
	}
	else if (alloc->block->free_elems == 1)
	{
		/* This block was full, but is now only used. */
		toy_move_to_list(&alloc->block->pool->full_blocks,
		                      &alloc->block->pool->used_blocks,
		                      &alloc->block->self);
	}
}

static void alloc_blobs(toypool_t *pool, dlinklist_t *blobs)
{
	assert(pool != NULL);
	assert(blobs != NULL);

	/* allocate a bunch of crap */
	for (int i = 0; i < num_blobs; i++)
	{
		toypool_test_blob_t *a_blob = pool_alloc(pool);
		toy_append(blobs, &a_blob->self, a_blob);
	}
	printf("finished allocating test blobs; num blobs on list [%u]\n", blobs->length);
}

static void release_blobs(toypool_t *pool, dlinklist_t *blobs)
{
	assert(pool != NULL);
	assert(blobs != NULL);
	assert(blobs->head != NULL);

	int released = 0;
	node_t *n, *nx;
	DLINK_FORWARD_SAFE (blobs->head, n, nx)
	{
		toypool_test_blob_t *a_blob = n->data;
		toy_remove(blobs, n);
	 	pool_release(pool, a_blob);
		released++;
	}

	printf("Num of released: [%d]\n", released);
}

int main(void)
{
    toypool_t *pool = pool_new("test-pool", sizeof(toypool_test_blob_t), 10);
    printf("Emtpy pool, number of free blocks: %u\n", pool->empty_blocks.length);

	dlinklist_t blobs = {.head = NULL, .tail = NULL, .length = 0};
	alloc_blobs(pool, &blobs);
	printf("releasing time!\n");
	release_blobs(pool, &blobs);
}