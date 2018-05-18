#include "lock.h"
#include "space.h"
#include "utils.h"

// Pool of spaces (one pool for each type needed).
struct pool_type
{
  lock_t *have;           // unused spaces available, for list
  space_t *head;     // linked list of available buffers
  size_t size;            // size of new buffers in this pool
  int limit;              // number of new spaces allowed, or -1
  int made;               // number of buffers made
  int users_per_space;
};

typedef struct pool_type pool_t;

//Initialize a new pool
void new_pool(pool_t *pool, size_t size, int limit, int users_per_space);

//Get space from a pool
space_t *get_space(pool_t *pool);

// Return space to its pool
void drop_space(space_t* space);

// Free the memory resources of a pool (unused buffers)
void free_pool(pool_t* pool);


