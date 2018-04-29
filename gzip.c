#include "string.h"
#include "getopt.h"
#include "stdlib.h"
#include "deflate.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* How about a struct to map option characters to integer flags?*/
static const struct option long_options[] =
  {
    {"stdout", no_argument, NULL, 'c'},
    {"keep", no_argument, NULL, 'k'},
    {"force", no_argument, NULL, 'f'},
    {"blocksize", required_argument, NULL, 'b'},
    {0, 0, 0, 0} //last element has to be all 0s by convention
  };
static char const short_options[] = "ckf";
int to_stdout = 0;
int keep = 0;
int force = 0;
int level = 6;
long blocksize = 128;

int deflate_file();

int main(int argc, char **argv)
{
  int index = 0;
  int parse_options = 1;
  
  while (parse_options)
    {
      int opt = getopt_long(argc, argv, short_options, long_options, NULL);
      switch(opt)
	{
	case 'c':
	  to_stdout = 1;
	  break;
	case 'k':
	  keep = 1;
	  break;
	case 'f':
	  force = 1;
	  break;
	case 'b':
	  blocksize = *optarg;
	  break;
	case -1:
	  parse_options = 0;
	}
    }
  for (index = optind; index < argc; index++)
    {
      int input_flag = O_RDONLY | (force?O_NOFOLLOW:0);
      int output_flag = O_WRONLY;
      int i_fd = open(argv[index], input_flag);
      int o_fd = open(strcat(argv[index], ".gz"), output_flag);
      deflate_file(i_fd, o_fd, blocksize*128, level);
    }
}

