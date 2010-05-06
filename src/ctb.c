#include <stdlib.h>
#include <stdio.h>

#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <pthread.h>
#include <loomlib/thread_pool.h>

#define BUF_LEN 1024

#define print_error(format, ...) { \
    fprintf (stderr, "[%s:%d in %s] ", __FILE__, __LINE__, __func__); \
    fprintf (stderr, format, ##__VA_ARGS__); \
    fprintf (stderr, "\n"); \
}

static FILE *outfp;
static pthread_mutex_t outfp_mutex;

/*
 * Base | lower case | upper case
 * ------------------------------
 * A    | 0110 0001  | 0100 0001
 * C    | 0110 0011  | 0100 0011
 * G    | 0110 0111  | 0100 0111
 * T    | 0111 0100  | 0101 0100
 *              ^^           ^^
 *               
 * Only the highlighted bits are used:
 * `(char & 0000 0110)` zero's out the other bits
 * `(char & 0000 0110) << 5` shifts the two bits we care about to the MSB
 */
static void
convert (void *vptr)
{
  char *in = vptr;
  char *inp;
  char *out;
  char *outp;
  size_t len = BUF_LEN;

  if (NULL == (out = malloc (len / 4)))
    {
      print_error ("Out of memory");
      return;
    }

  inp = in+len;
  outp = out+len/4;

  while (inp >= in)
    {
      *outp-- = ((*inp-- & 6) << 5)
              | ((*inp-- & 6) << 3)
              | ((*inp-- & 6) << 1)
              | ((*inp   & 6) >> 1);
    }

  free (in);

  pthread_mutex_lock (&outfp_mutex);
  assert (len/4 == fwrite (out, 1, len / 4, outfp));
  pthread_mutex_unlock (&outfp_mutex);

  free (out);
}

static int
ctb (FILE *infp, const size_t parallel)
{
  char *data = NULL;
  struct thread_pool *pool;
  int ret_val;

  assert (pool = thread_pool_new (parallel));

  if (0 != (ret_val = pthread_mutex_init (&outfp_mutex, NULL)))
    {
      print_error ("Failed to init mutex: %d", ret_val);
      return 1;
    }

  if (NULL == (data = malloc ((sizeof *data) * BUF_LEN)))
    {
      print_error ("Out of memory");
      return 1;
    }


  while (BUF_LEN == fread (data, sizeof *data, BUF_LEN, infp))
    {
      assert (thread_pool_push (pool, convert, data));

      if (NULL == (data = malloc ((sizeof *data) * BUF_LEN)))
        {
          print_error ("Out of memory");
          return 1;
        }
    }

  assert (thread_pool_terminate (pool));
  thread_pool_free (pool);

  return 0;
}

static void
usage (void)
{
  printf ("ctb -i input_file -o output_file\n");
}

static FILE*
xfopen (const char *path, const char *mode)
{ 
  FILE *fp = fopen (path, mode);

  if (NULL == fp)
    {
      print_error ("%s", strerror (errno));
      return NULL;
    }

  return fp;
}

int
main (int argc, char **argv)
{
  FILE *infp = outfp = NULL;
  size_t parallel = 1;

  static struct option long_options[] = {
    {"input",     required_argument, 0, 'i'},
    {"output",    required_argument, 0, 'o'},
    {"parallel",  required_argument, 0, 'j'}
  };

  for (;;)
    {
      int option_index = 0;
      int c = getopt_long (argc, argv, "i:o:", long_options, &option_index);

      if (-1 == c)
        break;

      switch (c)
        {
          case 'i':
            if (optarg[0] == '-' && optarg[1] == '\0')
              infp = stdin;
            else
              assert (NULL != (infp = xfopen (optarg, "r")));
            break;
          case 'o':
            if (optarg[0] == '-' && optarg[1] == '\0')
              outfp = stdout;
            else
              assert (NULL != (outfp = xfopen (optarg, "w")));
            break;
          case 'j':
            parallel = strtoul (optarg, NULL, 10);
            if (EINVAL == errno || ERANGE == errno)
              {
                usage ();
                return 1;
              }
            break;
          case '?':
          default:
            usage ();
            return 1;
        }
    }

  if (NULL == infp)
    infp = stdin;
  if (NULL == outfp)
    outfp = stdout;

  return ctb (infp, parallel);
}
