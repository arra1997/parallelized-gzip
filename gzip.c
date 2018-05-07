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
#include <unistd.h>

#include "string.h"
#include "getopt.h"
#include "stdlib.h"
#include "deflate.h"
#include "utils.h"

#ifndef Z_SUFFIX
#  define Z_SUFFIX ".gz"
#endif

#ifndef MAX_SUFFIX
#  define MAX_SUFFIX 10
#endif

char const *Version = "1.9";
char *program_name = "gzip";
int to_stdout = 0;
int keep = 0;
int force = 0;
long block_size = 128;
int compression_level = 6;
int quiet = 0;
int verbose = 0;
static int decompress = 0;
static char const *z_suffix;
static size_t z_len; /* strlen(z_suffix) */

static char const *const license_msg[] = {
"Copyright (C) 2018 Free Software Foundation, Inc.",
"This is free software.  You may redistribute copies of it under the terms of",
"the GNU General Public License <https://www.gnu.org/licenses/gpl.html>.",
"There is NO WARRANTY, to the extent permitted by law.",
0};

static char const short_options[] = "ckfLhHqvVS123456789";
/* How about a struct to map option characters to integer flags?*/
static const struct option long_options[] =
  {
    {"stdout", no_argument, NULL, 'c'},
    {"help", no_argument, NULL, 'h'},
    {"keep", no_argument, NULL, 'k'},
    {"force", no_argument, NULL, 'f'},
    {"blocksize", required_argument, NULL, 'b'},
    {"license", no_argument, NULL, 'L'},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"quiet", no_argument, NULL, 'q'},
    {"silent", no_argument, NULL, 'q'},
    {"suffix", required_argument, NULL, 'S'},
    {"decompress", no_argument, NULL, 'd'}, 
    {"uncompress", no_argument, NULL, 'd'}, 
    {0, 0, 0, 0} //last element has to be all 0s by convention
  };

void help ()
{
  static char const *const help_msg[] =
    {
       "Compress or uncompress FILEs (by default, compress FILES in-place).",
 "",
 "Mandatory arguments to long options are mandatory for short options too.",
 "",
 "  -c, --stdout      write on standard output, keep original files unchanged",
 "  -d, --decompress  decompress",
 "  -f, --force       force overwrite of output file and compress links",
 "  -h, --help        give this help",
 "  -k, --keep        keep (don't delete) input files",
 "  -L, --license     display software license",
 "  -S, --suffix=SUF  use suffix SUF on compressed files",
 "  -v, --verbose     verbose mode",
 "  -V, --version     display version number",
 "  -q, --quiet       suppress all warnings",
 "  -1, --fast        compress faster",
 "  -9, --best        compress better",
    0};
  char const *const *p = help_msg;
  printf ("Usage: %s [OPTION]... [FILE]...\n", program_name);
  while (*p) 
    printf ("%s\n", *p++);
}

void license ()
{
  char const *const *p = license_msg;
  printf ("%s %s\n", program_name, Version);
  while (*p) printf ("%s\n", *p++);
}

void version ()
{
  license ();
}

void do_exit ()
{
  exit (0);
}

static void finish_out ()
{
  do_exit ();
}

int main (int argc, char **argv)
{
  int index = 0;
  int parse_options = 1;
  z_suffix = Z_SUFFIX;
  z_len = strlen(z_suffix);
  
  while (parse_options)
    {
      int opt = getopt_long (argc, argv, short_options, long_options, NULL);
      switch (opt)
        {
          case 'c':
            to_stdout = 1;
            keep = 1;
            break;
          case 'd':
            decompress = 1;
            break;
          case 'h': case 'H':
            help ();
            finish_out ();
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
          case 'L':
            license ();
            finish_out ();
            break;
          case 'S':
            z_len = strlen(optarg);
            z_suffix = optarg;
            break;
          case 'v':
            verbose++;
            quiet = 0;
            break;
          case 'V':
            version ();
            finish_out ();
            break;
          case 'q':
            quiet = 1;
            verbose = 0;
            break;
          case '1': case '2': case '3': case '4': case '5':
          case '6': case '7': case '8': case '9':
            compression_level = opt - '0';
          case -1:
            parse_options = 0;
        }
    }
  if (z_len == 0 || z_len > MAX_SUFFIX) 
    {
      fprintf(stderr, "%s: invalid suffix '%s'\n", program_name, z_suffix);
      do_exit();
    }
  for (index = optind; index < argc; index++)
    {
      int input_flag = O_RDONLY | (force ? O_NOFOLLOW : 0);
      int output_flag = O_WRONLY | O_CREAT;
      char *input_file = Calloc (strlen (argv[index]), sizeof (char));
      char *output_file = Calloc (strlen (argv[index]+2), sizeof (char));
      strcpy (input_file, argv[index]);
      strcpy (output_file, argv[index]);
      strcat (output_file, z_suffix);
      int i_fd = open (input_file, input_flag);
      int o_fd = open (output_file, output_flag);
      deflate_file (i_fd, o_fd, block_size * 128, compression_level);
      

      if (to_stdout)
      {
        chmod (output_file, S_IRUSR|S_IRGRP|S_IROTH);
        int fd = open (output_file, O_RDONLY);
        char buf[1024];
        int buflen;
        while ((buflen = read (fd, buf, 1024)) > 0)
        {
          write (1, buf, buflen);
        }
        close (fd);
      }
     if (!keep)
      {
          Unlink (input_file);
      }
    free (input_file);
    free (output_file);
    }
}

