/* gzip.c

   Copyright (C) 2018 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "string.h"
#include "getopt.h"
#include "stdlib.h"
#include "deflate.h"

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
long block_size = 128;

int main (int argc, char **argv)
{
  int index = 0;
  int parse_options = 1;
  
  while (parse_options)
    {
      int opt = getopt_long (argc, argv, short_options, long_options, NULL);
      switch (opt)
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
        	  block_size = *optarg;
        	  break;
        	case -1:
        	  parse_options = 0;
      	}
    }
  for (index = optind; index < argc; index++)
    {
      int input_flag = O_RDONLY | (force ? O_NOFOLLOW : 0);
      int output_flag = O_WRONLY | O_CREAT;
      int i_fd = open (argv[index], input_flag);
      int o_fd = open (strcat (argv[index], ".gz"), output_flag);
      deflate_file (i_fd, o_fd, block_size * 128, level);
    }
}

