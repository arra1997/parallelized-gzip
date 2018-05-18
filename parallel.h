
struct lock_t;
struct space_t;
struct pool_t;
struct job_t;

typedef struct lock_t lock_t;
typedef struct space_t space_t;
typedef struct pool_t pool_t;
typedef struct job_t job_t;

lock_t *new_lock(unsigned int users);
void get_lock(lock_t* lock);
int is_free(lock_t* lock);
void free_lock(lock_t* lock);

space_t *new_space(unsigned int users, int size);
void new_pool(pool_t *pool, size_t size, int limit, int users_per_space);
space_t *get_space(pool_t *pool);
void drop_space(space_t* space);
void free_pool(pool_t* pool);
