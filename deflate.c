#include <zlib.h>
#include "stdlib.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define windowBits 15
#define GZIP_ENCODING 16

void *safe_calloc(size_t nelem, size_t elsize)
{
  void *addr = calloc(nelem, elsize);
  if (addr==NULL)
    {
      printf("Insufficient memory");
      assert(addr!=NULL);
    }
  return addr;
}

static void strm_init (z_stream * strm, int level)
{
    strm->zalloc = Z_NULL;
    strm->zfree  = Z_NULL;
    strm->opaque = Z_NULL;
    int ret = deflateInit2 (strm, level, Z_DEFLATED,
                             windowBits | GZIP_ENCODING, 8,
                             Z_DEFAULT_STRATEGY);
    if (ret!=Z_OK)
      exit (EXIT_FAILURE);
}

int deflate_file(int input_fd, int output_fd, long block_size, int level)
{
  long buffer_size = block_size; //for now
  int ret;
  int read_count;
  int write_count;
  z_stream strm;
  strm_init (& strm, level);
  unsigned char *in = safe_calloc(buffer_size, sizeof(char));
  unsigned char *out = safe_calloc(buffer_size, sizeof(char));
    do {
        read_count = read(input_fd, in, buffer_size);
        assert(read_count != -1);

        if (read_count>0) {
            strm.next_in = in;
            strm.next_out = out;
            strm.avail_in = read_count;
            strm.avail_out = buffer_size;

            if(read_count < buffer_size) {
                int ret = deflate(&strm, Z_FINISH);
                assert(ret!=Z_STREAM_ERROR);
                write_count = write(output_fd, out, buffer_size - strm.avail_out);
                assert(write_count != -1);
                break;
            }

            int ret = deflate(&strm, Z_SYNC_FLUSH);
            assert(ret!=Z_STREAM_ERROR);
            write_count = write(output_fd, out, buffer_size - strm.avail_out);
            assert(write_count != -1);
        }
    } while (read_count > 0);

  deflateEnd (& strm);
  free(in);
  free(out);
  return 0;
}
