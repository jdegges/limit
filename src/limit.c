/******************************************************************************
 * Copyright (c) 2010 Joey Degges
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *****************************************************************************/

#include <errno.h>
#include <getopt.h>
#include <libxml/parser.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>


static struct {
  int resource;
  char *str;
  char *unit;
  struct rlimit rlim;
} rlimits[] = {
  {.resource = RLIMIT_AS,         .str = "AS",          .unit = "bytes"},
  {.resource = RLIMIT_CORE,       .str = "CORE",        .unit = "bytes"},
  {.resource = RLIMIT_CPU,        .str = "CPU",         .unit = "seconds"},
  {.resource = RLIMIT_DATA,       .str = "DATA",        .unit = "bytes"},
  {.resource = RLIMIT_FSIZE,      .str = "FSIZE",       .unit = "bytes"},
  {.resource = RLIMIT_LOCKS,      .str = "LOCKS",       .unit = NULL},
  {.resource = RLIMIT_MEMLOCK,    .str = "MEMLOCK",     .unit = "bytes"},
  {.resource = RLIMIT_MSGQUEUE,   .str = "MSGQUEUE",    .unit = "bytes"},
  {.resource = RLIMIT_NICE,       .str = "NICE",        .unit = NULL},
  {.resource = RLIMIT_NOFILE,     .str = "NOFILE",      .unit = NULL},
  {.resource = RLIMIT_NPROC,      .str = "NPROC",       .unit = NULL},
  {.resource = RLIMIT_RSS,        .str = "RSS",         .unit = NULL},
  {.resource = RLIMIT_RTPRIO,     .str = "RTPRIO",      .unit = NULL},
  {.resource = RLIMIT_SIGPENDING, .str = "SIGPENDING",  .unit = NULL},
  {.resource = RLIMIT_STACK,      .str = "STACK",       .unit = "bytes"},
  {.resource = -1}
};


static void
query_limits (void)
{
  int i;

  for (i = 0; 0 <= rlimits[i].resource; i++)
    {
      if (0 != getrlimit (rlimits[i].resource, &rlimits[i].rlim))
        {
          fprintf (stderr, "%s\n", strerror (errno));
          continue;
        }

      printf ("Resource  : %s", rlimits[i].str);

      if (rlimits[i].unit)
        printf (" (%s)\n", rlimits[i].unit);
      else
        printf ("\n");

      printf ("Soft limit: %ld\n", rlimits[i].rlim.rlim_cur);
      printf ("Hard limit: %ld\n", rlimits[i].rlim.rlim_max);
      printf ("\n");
    }
}

static int
strtoind (char *resource)
{
  int index;

  for (index = 0; 0 <= rlimits[index].resource; index++)
    {
      if (!strcasecmp (rlimits[index].str, resource))
        break;
    }

  if (0 <= rlimits[index].resource)
    return index;

  return -1;
}

static int
parse_resource (xmlDocPtr doc, xmlNodePtr cur)
{
  int index = -1;
  rlim_t soft;
  int soft_set = 0;
  rlim_t hard;
  int hard_set = 0;
  
  cur = cur->xmlChildrenNode;
  while (NULL != cur)
    {
      if (!xmlStrcmp (cur->name, (const xmlChar *) "name"))
        {
          xmlChar *key = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
          index = strtoind ((char *) key);
          xmlFree (key);
        }
      else if (!xmlStrcmp (cur->name, (const xmlChar *) "soft"))
        {
          xmlChar *key = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
          soft = strtoul ((char *) key, NULL, 10);
          xmlFree (key);

          if (EINVAL == errno || ERANGE == errno)
            {
              fprintf (stderr, "Invalid value.\n");
              return 1;
            }

          soft_set = 1;
        }
      else if (!xmlStrcmp (cur->name, (const xmlChar *) "hard"))
        {
          xmlChar *key = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
          hard = strtoul ((char *) key, NULL, 10);
          xmlFree (key);

          if (EINVAL == errno || ERANGE == errno)
            {
              fprintf (stderr, "Invalid value.\n");
              return 1;
            }

          hard_set = 1;
        }
      cur = cur->next;
    }

  if (index < 0)
    {
      fprintf (stderr, "Invalid or missing resource name given.\n");
      return 1;
    }

  if (!soft_set && !hard_set)
    {
      fprintf (stderr, "No soft/hard limit set.\n");
      return 1;
    }

  if (soft_set)
    rlimits[index].rlim.rlim_cur = soft;

  if (hard_set)
    rlimits[index].rlim.rlim_max = hard;

  if (0 != setrlimit (rlimits[index].resource, &rlimits[index].rlim))
    {
      fprintf (stderr, "%s\n", strerror (errno));
      return 1;
    }

  return 0;
}

static int
set_limits (char *conf)
{
  xmlDocPtr doc;
  xmlNodePtr cur;
  int i;

  for (i = 0; 0 <= rlimits[i].resource; i++)
    {
      if (0 != getrlimit (rlimits[i].resource, &rlimits[i].rlim))
        {
          fprintf (stderr, "%s\n", strerror (errno));
          return 1;
        }
    }

  doc = xmlParseFile (conf);
  if (NULL == doc)
    {
      fprintf (stderr, "Document not parsed successfully.\n");
      return 1;
    }

  cur = xmlDocGetRootElement (doc);
  if (NULL == cur)
    {
      fprintf (stderr, "Empty document\n");
      return 1;
    }

  if (xmlStrcmp (cur->name, (const xmlChar *) "limits"))
    {
      fprintf (stderr, "Document of the wrong type, root node != limits");
      return 1;
    }

  cur = cur->xmlChildrenNode;
  while (cur != NULL)
    {
      if (!xmlStrcmp (cur->name, (const xmlChar *) "resource"))
        if (parse_resource (doc, cur))
          return 1;

      cur = cur->next;
    }

  xmlFreeDoc (doc);
  xmlCleanupParser ();

  return 0;
}

static void
usage (void)
{
  printf (
"Syntax: limit [-h | -q] [-c <string> -e <string>]\n"
"\n"
"Options:\n"
"  --conf, -c <string>      Path to limit configuration file.\n"
"  --exec, -e <string>      Command to execute with specified limits.\n"
"  --help, -h               Display this help menu.\n"
"  --query, -q              Query for current limits.\n"
"\n"
"Examples:\n"
"  $ limit --config /etc/limits.conf --exec /bin/bash\n"
"  $ limit --query\n"
"\n");
}

int
main (int argc, char **argv)
{
  char *conf = NULL;
  char *exec = NULL;

  static struct option long_options[] = {
    {"conf", required_argument, 0, 'c'},
    {"exec", required_argument, 0, 'e'},
    {"help", no_argument, 0, 'h'},
    {"query", no_argument, 0, 'q'},
    {0, 0, 0, 0}
  };

  for (;;)
    {
      int option_index = 0;
      int c = getopt_long (argc, argv, "c:e:hq", long_options, &option_index);

      if (-1 == c)
        break;

      switch (c)
        {
          case 'c':
            if (NULL != conf)
              {
                fprintf (stderr, "Duplicate `--conf' options passed.\n");
                usage ();
                return 1;
              }

            conf = malloc (strlen (optarg) + 1);
            strcpy (conf, optarg);
            break;
          case 'e':
            if (NULL != exec)
              {
                fprintf (stderr, "Duplicate `--exec' options passed.\n");
                usage ();
                return 1;
              }

            exec = malloc (strlen (optarg) + 1);
            strcpy (exec, optarg);
            break;
          case 'h':
            usage ();
            return 0;
          case 'q':
            query_limits ();
            return 0;
          default:
            fprintf (stderr, "Unknown option.\n");
            usage ();
            return 1;
        }
    }

  if (NULL == conf || NULL == exec)
    {
      fprintf (stderr, "Unknown/missing option.\n");
      usage ();
      return 1;
    }

  if (0 != set_limits (conf))
    {
      fprintf (stderr, "Unable to set resource.\n");
      usage ();
      return 1;
    }

  execv (exec, NULL);

  fprintf (stderr, "%s\n", strerror (errno));
  return -1;
}
