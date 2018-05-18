
struct lock_t;
struct space_t;
struct pool_t;
struct job_t;
struct job_queue_t;

typedef struct lock_t lock_t;
typedef struct space_t space_t;
typedef struct pool_t pool_t;
typedef struct job_t job_t;
typedef struct job_queue_t job_queue_t;

lock_t *new_lock(unsigned int users);
void get_lock(lock_t* lock);
int is_free(lock_t* lock);
void free_lock(lock_t* lock);

space_t *new_space(unsigned int users, int size);
void new_pool(pool_t *pool, size_t size, int limit, int users_per_space);
space_t *get_space(pool_t *pool);
void drop_space(space_t* space);
void free_pool(pool_t* pool);

void new_job (job_t *job, long seq, space_t *in, space_t *out, space_t *lens);
void free_job (job_t *job);

void new_job_queue (job_queue_t *job_q);
void free_job_queue (job_queue_t *job_q); // not thread safe
job_t *get_job_bgn (job_queue_t *job_q);
void add_job_bgn (job_queue_t *job_q, job_t *job);
void add_job_end (job_queue_t *job_q, job_t *job);
