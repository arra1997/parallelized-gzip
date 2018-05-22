#include <config.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <zlib.h>
#include "parallel.h"
#include "utils.h"
#include "try.h"


// Sliding dictionary size for deflate.
#define DICT 32768U
// Largest power of 2 that fits in an unsigned int. Used to limit requests to
// zlib functions that use unsigned int lengths.
#define MAXP2 (UINT_MAX - (UINT_MAX >> 1))
#define INBUFS(p) (((p)<<1)+3)
#define OUTPOOL(s) ((s)+((s)>>4)+DICT)
#define RSYNCBITS 12
#define OPAQUE Z_NULL
#define ZALLOC Z_NULL
#define ZFREE Z_NULL

long zlib_vernum(void) {
    char const *ver = zlibVersion();
    long num = 0;
    int left = 4;
    int comp = 0;
    do {
        if (*ver >= '0' && *ver <= '9')
            comp = 10 * comp + *ver - '0';
        else {
            num = (num << 4) + (comp > 0xf ? 0xf : comp);
            left--;
            if (*ver != '.')
                break;
            comp = 0;
        }
        ver++;
    } while (left);
    return left < 2 ? num << (left << 2) : -1;
}


// Assured memory allocation.
void *alloc(void *ptr, size_t size) {
    ptr = realloc(ptr, size);
    if (ptr == NULL)
        throw(ENOMEM, (char *) "not enough memory");
    return ptr;
}


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

void free_lock(lock_t* lock)
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
  space->use = new_lock(1);
  space->buf = Calloc(size, sizeof(unsigned char));
  space->size = size;
  space->len = 0;
  space->pool = NULL;
  space->next = NULL;
  return space;
}

// Pool of spaces (one pool for each type needed).
struct pool_t
{
  lock_t *have;           // unused spaces available, for list
  space_t *head;     // linked list of available buffers
  size_t size;            // size of new buffers in this pool
  int limit;              // number of new spaces allowed, or -1
  int made;               // number of buffers made
  int users_per_space;
};

void new_pool(pool_t *pool, size_t size, int limit, int users_per_space) {
  pool = Malloc(sizeof(pool_t));
  pool->have = new_lock(limit);
  pool->head = NULL;
  pool->size = size;
  pool->limit = limit;
  pool->made = 0;
  pool->users_per_space = users_per_space;
}


space_t *get_space(pool_t *pool)
{
  space_t *space;
  get_lock(pool->have);

  // if a space is available, pull it from the free list and return it
  if (pool->head != NULL)
    {
      space = pool->head;
      get_lock(space->use);
      pool->head = space->next;
      space->len = 0;
      free_lock(pool->have);
      return space;
    }

  // create a new space
  assert(pool->limit != 0);
  if (pool->limit > 0)
    pool->limit--;
  pool->made++;
  space = new_space(pool->users_per_space, pool->size);
  space->pool = pool;
  free_lock(pool->have);
  return space;
}


void drop_space(space_t* space)
{
  if (space == NULL)
    return;
  pool_t *pool = space->pool;
  get_lock(pool->have);
  free_lock(space->use);
  if (is_free(space->use))
    {
      space->next = pool->head;
      pool->head = space;
      space->len = 0;
    }
  free_lock(pool->have);
}

// Compute next size up by multiplying by about 2**(1/3) and rounding to the
// next power of 2 if close (three applications results in doubling). If small,
// go up to at least 16, if overflow, go to max size_t value.
size_t grow(size_t size) {
    size_t was, top;
    int shift;

    was = size;
    size += size >> 2;
    top = size;
    for (shift = 0; top > 7; shift++)
        top >>= 1;
    if (top == 7)
        size = (size_t)1 << (shift + 3);
    if (size < 16)
        size = 16;
    if (size <= was)
        size = (size_t)0 - 1;
    return size;
}

// Increase the size of the buffer in space.
void grow_space(space_t *space) {
    size_t more;

    // compute next size up
    more = grow(space->size);
    if (more == space->size)
        throw(ERANGE, (char *) "overflow");

    // reallocate the buffer
    space->buf = alloc(space->buf, more);
    space->size = more;
}


// Free the memory resources of a pool (unused buffers)
void free_pool(pool_t* pool)
{
  space_t *space;
  //int count = 0;
  get_lock(pool->have);
  if (pool->head == NULL)
    {
      free_lock(pool->have);
      return;
    }
  do
    {
      space = pool->head;
      pool->head = space->next;
      free(space->buf);
      free(space);
      pool->made--;
    } while (pool->head != NULL);
  free_lock(pool->have);
}
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

void new_job (job_t *job, long seq, space_t *in, space_t *out, space_t *lens)
{
  job->seq = seq;
  job->in = in;
  job->out = out;
  job->lens = lens;
  job->check = 0;
  job->calc = new_lock(1);
  job->next = NULL;
}

void free_job (job_t *job)
{
  free_lock (job->calc);
}

struct job_queue_t
{
  job_t *head;     // linked list of jobs
  job_t *tail;
  int len;         // length of job linked list
  lock_t *use;
};

void new_job_queue (job_queue_t *job_q)
{
  job_q->head = NULL;
  job_q->tail = NULL;
  job_q->len = 0;
  job_q->use = new_lock(1);
}

void free_job_queue (job_queue_t *job_q)// not thread safe
{
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
  if (job_q->head == NULL)
    return NULL;
  job_t *ret = job_q->head;
  job_q->head = job_q->head->next;
  if (job_q->head == NULL)
    job_q->tail = NULL;
  ret->next = NULL;
  return ret;
}

//add a job to the beginning of the job queue
void add_job_bgn (job_queue_t *job_q, job_t *job)
{
  if (job_q->head == NULL)
    job_q->tail = job;
  job->next = job_q->head;
  job_q->head = job;
  return;
}

//add a job to the end of the job queue
void add_job_end (job_queue_t *job_q, job_t *job)
{
  if (job_q->tail == NULL)
  {
    job_q->head = job;
    job_q->tail = job;
    job->next = NULL;
    return;
  }
  job_q->tail->next = job;
  job_q->tail = job;
  job->next = NULL;
  return;
}


// Setup job lists (call from main thread).
static void setup_pools(pool_t* in_pool, pool_t* out_pool, pool_t* dict_pool, pool_t* lens_pool) {
    // initialize buffer pools (initial size for out_pool not critical, since
    // buffers will be grown in size if needed -- the initial size chosen to
    // make this unlikely, the same for lens_pool)
    new_pool(in_pool, g.block, INBUFS(g.procs), 1);
    new_pool(out_pool, OUTPOOL(g.block), -1, 1);
    new_pool(dict_pool, DICT, -1, 1);
    new_pool(lens_pool, g.block >> (RSYNCBITS - 1), -1, 1);
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

// Compress all strm->avail_in bytes at strm->next_in to out->buf, updating
// out->len, grow the size of the buffer (out->size) if necessary. Respect the
// size limitations of the zlib stream data types (size_t may be larger than
// unsigned).
void deflate_engine(z_stream *strm, space_t *out, int flush) {
    size_t room;

    do {
        room = out->size - out->len;
        if (room == 0) {
            grow_space(out);
            room = out->size - out->len;
        }
        strm->next_out = out->buf + out->len;
        strm->avail_out = room < UINT_MAX ? (unsigned)room : UINT_MAX;
        (void)deflate(strm, flush);
        out->len = (size_t)(strm->next_out - out->buf);
    } while (strm->avail_out == 0);
    assert(strm->avail_in == 0);
}

typedef struct compress_options
{
  job_queue_t *job_queue;
  pool_t* out_pool;
  int level;
  int setdict;
}compress_options;

// Get the next compression job from the head of the list, compress and compute
// the check value on the input, and put a job in the write list with the
// results. Keep looking for more jobs, returning when a job is found with a
// sequence number of -1 (leave that job in the list for other incarnations to
// find).
void* compress_thread(void *(options)) {
    struct job_t *job;                // job pulled and working on
    struct job_t *here, **prior;      // pointers for inserting in write list
    unsigned long check;            // check value of input
    unsigned char *next;            // pointer for blocks, check value data
    size_t left;                    // input left to process
    size_t len;                     // remaining bytes to compress/check
#if ZLIB_VERNUM >= 0x1260
    int bits;                       // deflate pending bits
#endif
    struct space_t *temp = NULL;      // temporary space for zopfli input
    int ret;                        // zlib return code
    z_stream strm;                  // deflate stream
    //ball_t err;                     // error information from throw()



    compress_options* opts = (compress_options*) options;
    pool_t *out_pool = opts->out_pool;
    int level = opts->level;
    int setdict = opts->setdict;
    job_queue_t *job_queue = opts->job_queue;

     try {
        // initialize the deflate stream for this thread
        strm.zfree = ZFREE;
        strm.zalloc = ZALLOC;
        strm.opaque = OPAQUE;
        ret = deflateInit2(&strm, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
        if (ret == Z_MEM_ERROR)
            throw(ENOMEM, (char *) "not enough memory");
        if (ret != Z_OK)
            throw(EINVAL, (char *) "internal error");


//THIS IS WHERE I STOPPED SHUHAO **************************************************


        // keep looking for work
        for (;;) {
            // get a job (like I tell my son)
            job = get_job_bgn(job_queue);
            assert(job != NULL);
            if (job->seq == -1)
                break;

            // got a job -- initialize and set the compression level (note that
            // if deflateParams() is called immediately after deflateReset(),
            // there is no need to initialize input/output for the stream)
            // Trace(("-- compressing #%ld", job->seq)); TODO: LOG
            if (level <= 9) {
              (void)deflateReset(&strm);
              (void)deflateParams(&strm, level, Z_DEFAULT_STRATEGY);
            }
            else {
              if (temp == NULL)
                temp = get_space(out_pool);
              temp->len = 0;
            }


            // set dictionary if provided, release that input or dictionary
            // buffer (not NULL if g.setdict is true and if this is not the
            // first work unit)
            if (job->out != NULL) {
                len = job->out->len;
                left = len < DICT ? len : DICT;
                if (level <= 9)
                    deflateSetDictionary(&strm, job->out->buf + (len - left),
                                         (unsigned)left);
                else {
                    memcpy(temp->buf, job->out->buf + (len - left), left);
                    temp->len = left;
                }
                drop_space(job->out);
            }

            // set up input and output
            job->out = get_space(out_pool);
            if (level <= 9) {
              strm.next_in = job->in->buf;
              strm.next_out = job->out->buf;
            }
            else {
              memcpy(temp->buf + temp->len, job->in->buf, job->in->len);
            }

            // compress each block, either flushing or finishing
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

                if (level <= 9) {
                    // run MAXP2-sized amounts of input through deflate -- this
                    // loop is needed for those cases where the unsigned type
                    // is smaller than the size_t type, or when len is close to
                    // the limit of the size_t type
                    while (len > MAXP2) {
                        strm.avail_in = MAXP2;
                        deflate_engine(&strm, job->out, Z_NO_FLUSH);
                        len -= MAXP2;
                    }

                    // run the last piece through deflate -- end on a byte
                    // boundary, using a sync marker if necessary, or finish
                    // the deflate stream if this is the last block
                    strm.avail_in = (unsigned)len;
                    if (left || job->more) {
#if ZLIB_VERNUM >= 0x1260
                        if (zlib_vernum() >= 0x1260) {
                            deflate_engine(&strm, job->out, Z_BLOCK);

                            // add enough empty blocks to get to a byte
                            // boundary
                            (void)deflatePending(&strm, Z_NULL, &bits);
                            if ((bits & 1) || !setdict)
                                deflate_engine(&strm, job->out, Z_SYNC_FLUSH);
                            else if (bits & 7) {
                                do {        // add static empty blocks
                                    bits = deflatePrime(&strm, 10, 2);
                                    assert(bits == Z_OK);
                                    (void)deflatePending(&strm, Z_NULL, &bits);
                                } while (bits & 7);
                                deflate_engine(&strm, job->out, Z_BLOCK);
                            }
                        }
                        else
#endif
                        {
                            deflate_engine(&strm, job->out, Z_SYNC_FLUSH);
                        }
                        if (!setdict)     // two markers when independent
                            deflate_engine(&strm, job->out, Z_FULL_FLUSH);
                    }
                    else
                        deflate_engine(&strm, job->out, Z_FINISH);
                }
            } while (left);
            drop_space(job->lens);
            job->lens = NULL;
            //Trace(("-- compressed #%ld%s", job->seq,
                   //job->more ? "" : " (last)"));
            /****** STOPPED HERE -- ZACH ************/
            // reserve input buffer until check value has been calculated
            use_space(job->in);

            // insert write job in list in sorted order, alert write thread
            possess(write_first);
            prior = &write_head;
            while ((here = *prior) != NULL) {
                if (here->seq > job->seq)
                    break;
                prior = &(here->next);
            }
            job->next = here;
            *prior = job;
            twist(write_first, TO, write_head->seq);

            // calculate the check value in parallel with writing, alert the
            // write thread that the calculation is complete, and drop this
            // usage of the input buffer
            len = job->in->len;
            next = job->in->buf;
            check = CHECK(0L, Z_NULL, 0);
            while (len > MAXP2) {
                check = CHECK(check, next, MAXP2);
                len -= MAXP2;
                next += MAXP2;
            }
            check = CHECK(check, next, (unsigned)len);
            drop_space(job->in);
            job->check = check;
            //Trace(("-- checked #%ld%s", job->seq, job->more ? "" : " (last)"));
            possess(job->calc);
            twist(job->calc, TO, 1);

            // done with that one -- go find another job
        }

        // found job with seq == -1 -- return to join
#ifndef NOZOPFLI
        drop_space(temp);
#endif
        release(compress_have);
        (void)deflateEnd(&strm);
    } 
    catch (err) {
        //THREADABORT(err);
        //throw("")
      return 1 
    }
}
