#include <config.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <zlib.h>
#include "parallel.h"
#include "utils.h"





// Sliding dictionary size for deflate.
#define DICT 32768U
// Largest power of 2 that fits in an unsigned int. Used to limit requests to
// zlib functions that use unsigned int lengths.
// #define MAXP2 (UINT_MAX - (UINT_MAX >> 1))
#define INBUFS(p) (((p)<<1)+3)
#define OUTPOOL(s) ((s)+((s)>>4)+DICT)
#define RSYNCBITS 12

//TODO MOVE THIS GLOBAL STRUCT TO THE MAIN FILE
struct {
    int cthreads;
    int procs;
    int block;
} g;


struct lock_t
{
  sem_t *semaphore;
  unsigned int count;
};

lock_t *new_lock(unsigned int users)
{
  lock_t *lock;
  lock = Malloc(sizeof(lock_t));
  lock->semaphore = Malloc(sizeof(sem_t));
  lock->count = users;
  assert(sem_init(lock->semaphore, 0, users) == 0);
  return lock;
}

// Destroy the lock.                                                                                   
// This is not safe deletion. Only use this                                                             
// when you know nobody needs the lock anymore.
void free_lock(lock_t* lock)
{
  free(lock->semaphore);
  free(lock);
}

void get_lock(lock_t* lock)
{
  assert(sem_wait(lock->semaphore) == 0);
}

int is_free(lock_t* lock)
{
  int value;
  sem_getvalue(lock->semaphore, &value);
  return (value == lock->count);
}

void release_lock(lock_t* lock)
{
  assert(sem_post(lock->semaphore) == 0);
}

// -- pool of spaces for buffer management --

// These routines manage a pool of spaces. Each pool specifies a fixed size
// buffer to be contained in each space. Each space has a use count, which when
// decremented to zero returns the space to the pool. If a space is requested
// from the pool and the pool is empty, a space is immediately created unless a
// specified limit on the number of spaces has been reached. Only if the limit
// is reached will it wait for a space to be returned to the pool. Each space
// knows what pool it belongs to, so that it can be returned.

// A space (one buffer for each space).
struct space_t
{
  lock_t *use;            // return to pool when unused                                                 
  unsigned char *buf;     // buffer of size size                                                        
  size_t size;            // current size of this buffer                                                
  size_t len;             // for application usage (initially zero)                                     
  pool_t *pool;      // pool to return to                                                               
  space_t *next;     // for pool linked list                                                           
};

space_t *new_space(unsigned int users, int size)
{
  space_t *space;
  space = Malloc(sizeof(space_t));
  space->use = new_lock(users);
  space->buf = Calloc(size, sizeof(unsigned char));
  space->size = size;
  space->len = 0;
  space->pool = NULL;
  space->next = NULL;
  return space;
}

// Destroy the space.     
// This is not safe deletion. Only call this when you know
// nobody needs this space anymore. 
void free_space(space_t *space)
{
  free(space->buf);
  free_lock(space->use);
  free(space);
}


// Pool of spaces (one pool for each type needed).                                                      
struct pool_t
{
  lock_t *have;           // unused spaces available, for list                                          
  lock_t *safe;           //ensures safe use of pool in multithreaded environment                       
  space_t *head;     // linked list of available buffers                                                
  size_t size;            // size of new buffers in this pool                                          
  int limit;              // number of new spaces allowed                                               
  int made;               // number of buffers made  
  int users_per_space;
};

pool_t *new_pool(size_t size, int limit, int users_per_space) {
  pool_t *pool;
  pool = Malloc(sizeof(pool_t));
  pool->have = new_lock(limit);
  pool->safe = new_lock(1);
  pool->head = NULL;
  pool->size = size;
  pool->limit = limit;
  pool->made = 0;
  pool->users_per_space = users_per_space;
  return pool;
}


space_t *get_space(pool_t *pool)
{
  space_t *space;
  get_lock(pool->have);
  get_lock(pool->safe);

  // if a space is available, pull it from the free list and return it                                   
  if (pool->head != NULL)
    {
      space = pool->head;
      get_lock(space->use);
      pool->head = space->next;
      space->len = 0;
      release_lock(pool->safe);
      return space;
    }

  // create a new space                                                                                  
  assert(pool->made != pool->limit);
  pool->made++;
  space = new_space(pool->users_per_space, pool->size);
  space->pool = pool;
  release_lock(pool->safe);
  return space;
}


void drop_space(space_t* space)
{
  if (space == NULL)
    return;
  pool_t *pool = space->pool;
  get_lock(pool->safe);
  release_lock(space->use);
  if (is_free(space->use))
    {
      space->next = pool->head;
      pool->head = space;
      space->len = 0;
      release_lock(pool->have);
    }
  release_lock(pool->safe);
}


// Destroy the pool.                                                                                   
// This is not safe deletion. Only call this when you                                                  
// know nobody needs the pool anymore.                                                                  
void free_pool(pool_t* pool)
{
  space_t *space;
  get_lock(pool->safe);
  do
    {
      space = pool->head;
      pool->head = space->next;
      free_space(space);
    } while (pool->head != NULL);
  free_lock(pool->safe);
  free_lock(pool->have);
  free(pool);
}

static pool_t* in_pool;
static pool_t* out_pool;
static pool_t* dict_pool;
static pool_t* lens_pool;

// -- job queue used for parallel compression --                                                         
// Compress or write job (passed from compress list to write list). If seq is                           
// equal to -1, compress_thread is instructed to return; if more is false then                          
// this is the last chunk, which after writing tells write_thread to return.                            
struct job_t
{
  long seq;                   // sequence number                                                        
  int more;                   // true if this is not the last chunk                                     
  space_t *in;                // input data to compress                                                 
  space_t *out;               // dictionary or resulting compressed data                                
  space_t *lens;              // coded list of flush block lengths                                      
  unsigned long check;        // check value for input data                                             
  lock_t *calc;                 // released when check calculation complete                             
  job_t *next;           // next job in the list (either list)                                           
};

job_t *new_job (long seq, pool_t *in_pool, pool_t *out_pool, pool_t *lens_pool)
{
  job_t *job = Malloc(sizeof(job_t));
  job->seq = seq;
  job->in = get_space(in_pool);
  job->out = get_space(out_pool);
  job->lens = get_space(lens_pool);
  job->check = 0;
  job->calc = new_lock(1);
  job->next = NULL;
  return job;
}

void free_job (job_t *job)
{
  free_space(job->in);
  free_space(job->out);
  free_space(job->lens);
  free(job->calc);
  free(job);
}


struct job_queue_t
{
  job_t *head;     // linked list of jobs                                                               
  job_t *tail;
  int len;         // length of job linked list                                                         
  lock_t *use;
};

job_queue_t* new_job_queue()
{
  job_queue_t *job_q = Malloc(sizeof(job_queue_t));
  job_q->head = NULL;
  job_q->tail = NULL;
  job_q->len = 0;
  job_q->use = new_lock(1);
  return job_q;
}


void free_job_queue (job_queue_t *job_q)
{
  get_lock(job_q->use);
  while (job_q->head != NULL)
  {
    job_t *temp = job_q->head;
    job_q->head = job_q->head->next;
    free_job (temp);
  }
  free_lock(job_q->use);
}



//get a job from the beginning of the job queue                                                        
//the job should be freed if not put back to job queue after usage                                     
job_t *get_job_bgn (job_queue_t *job_q)
{
  get_lock(job_q->use);
  job_t *ret = job_q->head;
  if (job_q->head == NULL)
    {
      assert(job_q->len == 0);
      release_lock(job_q->use);
      return NULL;
    }
  job_q->head = job_q->head->next;
  if (job_q->head == NULL)
    job_q->tail = NULL;
  ret->next = NULL;
  --job_q->len;
  release_lock(job_q->use);
  return ret;
}

//add a job to the beginning of the job queue                                                            
void add_job_bgn (job_queue_t *job_q, job_t *job)
{
  get_lock(job_q->use);
  if (job_q->head == NULL)
    job_q->tail = job;
  job->next = job_q->head;
  job_q->head = job;
  ++job_q->len;
  release_lock(job_q->use);
}

//add a job to the end of the job queue                                                                  
void add_job_end (job_queue_t *job_q, job_t *job)
{
  get_lock(job_q->use);
  if (job_q->tail == NULL)
  {
    job_q->head = job;
    job_q->tail = job;
    job->next = NULL;
    ++job_q->len;
  }
  job_q->tail->next = job;
  job_q->tail = job;
  job->next = NULL;
  ++job_q->len;
  release_lock(job_q->use);
}


// Setup job lists (call from main thread).
static void setup_pools(void) 
{
    // initialize buffer pools (initial size for out_pool not critical, since
    // buffers will be grown in size if needed -- the initial size chosen to
    // make this unlikely, the same for lens_pool)
    in_pool = new_pool(g.block, INBUFS(g.procs), 1);
    out_pool = new_pool(OUTPOOL(g.block), -1, 1);
    dict_pool = new_pool(DICT, -1, 1);
    lens_pool = new_pool(g.block >> (RSYNCBITS - 1), -1, 1);
}


// Command the compress threads to all return, then join them all (call from
// main thread), free all the thread-related resources.
static void finish_jobs(job_queue_t* job_queue, pool_t* lens_pool, pool_t* dict_pool, pool_t* out_pool, pool_t* in_pool) {
    struct job_t job;

    /*
    // command all of the extant compress threads to return
    possess(compress_have);
    job.seq = -1;
    job.next = NULL;
    compress_head = &job;
    compress_tail = &(job.next);
    twist(compress_have, BY, +1);       // will wake them all up
    */
    job.seq = -1;
    job.next = NULL;
    add_job_bgn(job_queue, &job);

    // join all of the compress threads, verify they all came back
    //TODO ADD join_all() function and then uncomment this line
    //join_all();
    //Trace(("-- joined %d compress threads", caught));
    //assert(caught == g.cthreads);
    g.cthreads = 0;

    // free the resources
    free_pool(lens_pool);
    //Trace(("-- freed %d block lengths buffers", caught));
    free_pool(dict_pool);
    //Trace(("-- freed %d dictionary buffers", caught));
    free_pool(out_pool);
    //Trace(("-- freed %d output buffers", caught));
    free_pool(in_pool);
    //Trace(("-- freed %d input buffers", caught));
    //free_lock(write_first);
    //free_lock(compress_have);
    //compress_have = NULL;

    free_job_queue(job_queue);
}

#ifndef WINDOW_BITS
#  define WINDOW_BITS 15
#endif

#ifndef GZIP_ENCODING
#  define GZIP_ENCODING 16
#endif


struct compress_options {
  job_queue_t *job_queue;
  pool_t *out_pool;
  int level;
  gz_header *header;
};

// Get the next compression job from the head of the list, compress and compute
// the check value on the input, and put a job in the write list with the
// results. Keep looking for more jobs, returning when a job is found with a
// sequence number of -1 (leave that job in the list for other incarnations to
// find).
void compress_thread(void *(opts)) {
  struct job_t *job;              // job pulled and working on
  unsigned char *next;            // pointer for blocks, check value data
  size_t left;                    // input left to process
  size_t len;                     // remaining bytes to compress/check
  int ret;                        // for error checking purposes

  compress_options* options = (compress_options *) opts;
  job_queue_t *job_queue = options->job_queue;
  pool_t *out_pool = options->out_pool;
  int level = options->level;
  gz_header *header = options->header;

  // Initialize the deflate stream
  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit2 (&strm, level, Z_DEFLATED,
                          WINDOW_BITS | GZIP_ENCODING, 8,
                          Z_DEFAULT_STRATEGY);
  if (ret != Z_OK)
    exit (EXIT_FAILURE);

  ret = deflateSetHeader (&strm, header);
  if (ret != Z_OK)
    exit(EXIT_FAILURE);

  // Continuously look for jobs
  for (;;) {
    // Get a job
    job = get_job_bgn(job_queue);
    assert(job != NULL);
    if (job->seq == -1)
      break;

    // Initialize and set compression level.
    (void)deflateReset(&strm);
    (void)deflateParams(&strm, level, Z_DEFAULT_STRATEGY);

    // Set dictionary if provided
    if (job->out != NULL) {
      len = job->out->len;
      left = len < DICT ? len : DICT;
      deflateSetDictionary(&strm, job->out->buf + (len - left), (unsigned)left);
      drop_space(job->out);
    }

    // Set up I/O
    job->out = get_space(out_pool);
    strm.next_in = job->in->buf;
    strm.next_out = job->out->buf;

    // Compress each block, either flushing or finishing.
    next = job->lens == NULL ? NULL : job->lens->buf;
    left = job->in->len;
    job->out->len = 0;

    do {
      // decode next block length from blocks list
      len = next == NULL ? 128 : *next++;
      if (len < 128)                  // 64..32831
          len = (len << 8) + (*next++) + 64;
      else if (len == 128)            // end of list
          len = left;
      else if (len < 192)             // 1..63
          len &= 0x3f;
      else if (len < 224){            // 32832..2129983
          len = ((len & 0x1f) << 16) + ((size_t)*next++ << 8);
          len += *next++ + 32832U;
      }
      else {                          // 2129984..539000895
          len = ((len & 0x1f) << 24) + ((size_t)*next++ << 16);
          len += (size_t)*next++ << 8;
          len += (size_t)*next++ + 2129984UL;
      }
      left -= len;


      // ********** TODO **************
      // run MAXP2-sized amounts of input through deflate -- this
      // loop is needed for those cases where the unsigned type
      // is smaller than the size_t type, or when len is close to
      // the limit of the size_t type

      // ********** TODO **************
      // run the last piece through deflate -- end on a byte
      // boundary, using a sync marker if necessary, or finish
      // the deflate stream if this is the last block


    } while(left);

    drop_space(job->lens);
    job->lens = NULL;

    // ********** TODO ************** - Implement use_space
    // reserve input buffer until check value has been calculated.
    // use_space(job->in);

    // ********** TODO **************
    // insert write job in list in sorted order, alert write thread

    // ********** TODO **************
    // calculate the check value in parallel with writing, alert the
    // write thread that the calculation is complete, and drop this
    // usage of the input buffer

    // done with that one -- go find another job

  }

  // found job with seq == -1 -- return to join
  (void)deflateEnd(&strm);
  return;
}


// Write len bytes, repeating write() calls as needed. Return len.
size_t writen(int desc, void const *buf, size_t len) {
    char const *next = buf;
    size_t left = len;

    while (left) {
        size_t const max = SIZE_MAX >> 1;       // max ssize_t
        ssize_t ret = write(desc, next, left > max ? max : left);
        next += ret;
        left -= (size_t)ret;
    }
    return len;
}


// Assured memory allocation.
void *alloc(void *ptr, size_t size) {
    ptr = realloc(ptr, size);
    if (ptr == NULL)
        throw(ENOMEM, "not enough memory");
    return ptr;
}



unsigned put(int out, ...) {
    // compute the total number of bytes
    unsigned count = 0;
    int n;
    va_list ap;
    va_start(ap, out);
    while ((n = va_arg(ap, int)) != 0) {
        va_arg(ap, val_t);
        count += (unsigned)abs(n);
    }
    va_end(ap);

    // allocate memory for the data
    unsigned char *wrap = alloc(NULL, count);
    unsigned char *next = wrap;

    // write the requested data to wrap[]
    va_start(ap, out);
    while ((n = va_arg(ap, int)) != 0) {
        val_t val = va_arg(ap, val_t);
        if (n < 0) {            // big endian
            n = -n << 3;
            do {
                n -= 8;
                *next++ = (unsigned char)(val >> n);
            } while (n);
        }
        else                    // little endian
            do {
                *next++ = (unsigned char)val;
                val >>= 8;
            } while (--n);
    }
    va_end(ap);

    // write wrap[] to out and return the number of bytes written
    writen(out, wrap, count);
    free(wrap);
    return count;
}


length_t put_header(int outfd, char* name, time_t mtime, int level) {
    length_t len;

    len = put(outfd,
        1, (val_t)31,
        1, (val_t)139,
        1, (val_t)8,            // deflate
        1, (val_t)(name != NULL ? 8 : 0),
        4, (val_t)mtime,
        1, (val_t)(level >= 9 ? 2 : level == 1 ? 4 : 0),
        1, (val_t)3,            // unix
        0);
    if (name != NULL)
        len += writen(outfd, name, strlen(name) + 1);
}

/*
void write_thread(void* dummy) {
    long seq;
    struct job_t* job;
    size_t input_len;
    int more;
    length_t header_len;
    length_t ulen;
    length_t clen;
    unsigned long check;

    (void)dummy;



}
*/
.