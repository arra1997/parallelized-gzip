struct lock_t;
struct condition_t;
struct space_t;
struct pool_t;
struct job_t;
struct job_queue_t;
struct compress_options;
struct write_opts;

typedef struct lock_t lock_t;
typedef struct condition_t condition_t;
typedef struct space_t space_t;
typedef struct pool_t pool_t;
typedef struct job_t job_t;
typedef struct job_queue_t job_queue_t;
typedef struct compress_options compress_options;
typedef unsigned long length_t;
typedef length_t val_t;
typedef struct write_opts write_opts;

lock_t *new_lock(unsigned int users, int fixed_size);
void get_lock(lock_t* lock);
void release_lock(lock_t* lock);
void increment_lock(lock_t* lock);
void free_lock(lock_t* lock);

condition_t *new_condition(void);
void wait_condition(condition_t *condition);
void broadcast_condition(condition_t *condition);
void signal_condition(condition_t *condition);
void reset_condition(condition_t *condition);
void free_condition(condition_t *condition);

space_t *new_space(int size);
void free_space(space_t *space);
pool_t* new_pool(size_t size, int limit);
space_t *get_space(pool_t *pool);
void drop_space(space_t* space);
void free_pool(pool_t* pool);

job_t *new_job (long seq, pool_t *in_pool, pool_t *out_pool);
void set_last_job (job_t *job);
int load_job (job_t *job, int input_fd);
void finished_processing (job_t *job);
void free_job (job_t *job);
void set_dictionary (job_t *prev_job, job_t *next_job, pool_t *dict_pool);

job_queue_t* new_job_queue (int num_threads, int ordered);
void close_job_queue (job_queue_t *job_q);
void free_job_queue (job_queue_t *job_q); // not thread safe
job_t *get_job_bgn (job_queue_t *job_q);
job_t* get_job_seq (job_queue_t* job_q, int seq);

void add_job_bgn (job_queue_t *job_q, job_t *job);
void add_job_end (job_queue_t *job_q, job_t *job);

write_opts *new_write_options(job_queue_t *job_queue, int outfd, char *name, time_t mtime, int level);
compress_options *new_compress_options (job_queue_t *job_queue, job_queue_t* write_job_queue, int level);
void free_compress_options(compress_options *copts);
void free_write_options(write_opts *wopts);
void deflate_engine (z_stream *strm, job_t *job);
void *compress_thread(void *dummy);

size_t writen(int desc, void const *buf, size_t len);
unsigned put(int out, ...);
length_t put_header(int outfd, char* name, time_t mtime, int level);
void put_trailer(int outfd, length_t ulen, unsigned long check);
void *write_thread(void *opts);
