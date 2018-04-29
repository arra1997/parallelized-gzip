#include <zlib.h>
#include "stdlib.h"

int deflate_file(int input_fd, int output_fd, long block_size, int level)
{
  int ret;
  z_stream strm;
  unsigned char *in = calloc(block_size, sizeof(char));
  unsigned char *out = calloc(block_size, sizeof(char));
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit(&strm, level);
  if (ret!=Z_OK)
    return -1;
  return 0;
}
