#include <zlib.h>
#include "stdlib.h"
#include <stdio.h>
#include <assert.h>

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

int deflate_file(int input_fd, int output_fd, long block_size, int level)
{
  long buffer_size = block_size; //for now
  int ret;
  int read_count;
  int write_count;
  z_stream strm;
  unsigned char *in = safe_calloc(buffer_size, sizeof(char));
  unsigned char *out = safe_calloc(buffer_size, sizeof(char));
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit(&strm, level);
  if (ret!=Z_OK)
    return -1;

  do
    {
      read_count = read(input_fd, in, buffer_size);
      assert(read_count != -1);

      if (read_count>0)
	{
	  strm.avail_in = buffer_size;
	  strm.next_in = in;
	  strm.avail_out = buffer_size;
	  strm.next_out = out;
	  ret = deflate(&strm, Z_NO_FLUSH);
	  assert(ret!=Z_STREAM_ERROR);
	  write_count = write(output_fd, out, buffer_size - strm.avail_out);
          assert(write_count != -1);
	}
    }while (read_count != 0);

  free(in);
  free(out);
  return 0;
}


