#include "lock.h"
#include "utils.h"
#include "pool.h"

// A space (one buffer for each space).
struct space_type
{
  lock_t *use;            // return to pool when unused
  unsigned char *buf;     // buffer of size size
  size_t size;            // current size of this buffer
  size_t len;             // for application usage (initially zero)
  pool_t *pool;      // pool to return to
  struct space_type *next;     // for pool linked list
};

typedef struct space_type space_t;

void new_space(space_t *space, unsigned int users, int size);
