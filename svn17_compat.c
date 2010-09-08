/*
 * svn17_compat.c :   a library to make Subversion 1.6 look like 1.7.
 *
 * ====================================================================
 *    This file is derived from code licensed to the Apache
 *    Software Foundation (ASF) under one or more contributor
 *    license agreements.  See the NOTICE file distributed with
 *    this file for additional information regarding copyright
 *    ownership.  The ASF licenses those portions to you under
 *    the Apache License, Version 2.0 (the "License"); you may
 *    not use those portions except in compliance with the
 *    License.  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 *
 *    Any code in this file not licensed from the ASF is
 *    original code in the public domain.  You may freely use,
 *    modify, distribute, and relicense such code.
 * ====================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <apr.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_lib.h>
#include <apr_errno.h>
#include <apr_pools.h>

#include <svn_error.h>
#include <svn_config.h>
#include <svn_io.h>
#include <svn_dirent_uri.h>
#include <svn_path.h>
#include <svn_ctype.h>
#include "svn17_compat.h"

/* TRUE if s is the canonical empty path, FALSE otherwise
   From libsvn_subr/dirent_uri.c. */
#define SVN_PATH_IS_EMPTY(s) ((s)[0] == '\0')

/* Path type definition. Used only by internal functions.
   From libsvn_subr/dirent_uri.c. */
typedef enum {
  type_uri,
  type_dirent,
  type_relpath
} path_type_t;

/* Here is the BNF for path components in a URI. "pchar" is a
   character in a path component.

      pchar       = unreserved | escaped |
                    ":" | "@" | "&" | "=" | "+" | "$" | ","
      unreserved  = alphanum | mark
      mark        = "-" | "_" | "." | "!" | "~" | "*" | "'" | "(" | ")"

   Note that "escaped" doesn't really apply to what users can put in
   their paths, so that really means the set of characters is:

      alphanum | mark | ":" | "@" | "&" | "=" | "+" | "$" | ","

   From libsvn_subr/path.c. */
static const char svn_uri__char_validity[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 1, 0, 0, 1, 0, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 1, 0, 0,

  /* 64 */
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,

  /* 128 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,

  /* 192 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

/* From libsvn_subr/dirent_uri.c. */
static svn_boolean_t
svn_uri_is_canonical(const char *uri, apr_pool_t *pool)
{
  const char *ptr = uri, *seg = uri;
  const char *schema_data = NULL;

  /* URI is canonical if it has:
   *  - no '.' segments
   *  - no closing '/', unless for the root path '/' itself
   *  - no '//'
   *  - lowercase URL scheme
   *  - lowercase URL hostname
   */

  if (*uri == '\0')
    return TRUE;

  /* Maybe parse hostname and scheme. */
  if (*ptr != '/')
    {
      while (*ptr && (*ptr != '/') && (*ptr != ':'))
        ptr++;

      if (*ptr == ':' && *(ptr+1) == '/' && *(ptr+2) == '/')
        {
          /* Found a scheme, check that it's all lowercase. */
          ptr = uri;
          while (*ptr != ':')
            {
              if (*ptr >= 'A' && *ptr <= 'Z')
                return FALSE;
              ptr++;
            }
          /* Skip :// */
          ptr += 3;

          /* This might be the hostname */
          seg = ptr;
          while (*ptr && (*ptr != '/') && (*ptr != '@'))
            ptr++;

          if (! *ptr)
            return TRUE;

          if (*ptr == '@')
            seg = ptr + 1;

          /* Found a hostname, check that it's all lowercase. */
          ptr = seg;
          while (*ptr && *ptr != '/')
            {
              if (*ptr >= 'A' && *ptr <= 'Z')
                return FALSE;
              ptr++;
            }

          schema_data = ptr;
        }
      else
        {
          /* Didn't find a scheme; finish the segment. */
          while (*ptr && *ptr != '/')
            ptr++;
        }
    }

#ifdef SVN_USE_DOS_PATHS
  if (schema_data && *ptr == '/')
    {
      /* If this is a file url, ptr now points to the third '/' in
         file:///C:/path. Check that if we have such a URL the drive
         letter is in uppercase. */
      if (strncmp(uri, "file:", 5) == 0 &&
          ! (*(ptr+1) >= 'A' && *(ptr+1) <= 'Z') &&
          *(ptr+2) == ':')
        return FALSE;
    }
#endif /* SVN_USE_DOS_PATHS */

  /* Now validate the rest of the URI. */
  while(1)
    {
      apr_size_t seglen = ptr - seg;

      if (seglen == 1 && *seg == '.')
        return FALSE;  /*  /./   */

      if (*ptr == '/' && *(ptr+1) == '/')
        return FALSE;  /*  //    */

      if (! *ptr && *(ptr - 1) == '/' && ptr - 1 != uri)
        return FALSE;  /* foo/  */

      if (! *ptr)
        break;

      if (*ptr == '/')
        ptr++;
      seg = ptr;


      while (*ptr && (*ptr != '/'))
        ptr++;
    }

  if (schema_data)
    {
      ptr = schema_data;

      while (*ptr)
        {
          if (*ptr == '%')
            {
              char digitz[3];
              int val;

              /* Can't use svn_ctype_isxdigit() because lower case letters are
                 not in our canonical format */
              if (((*(ptr+1) < '0' || *(ptr+1) > '9'))
                  && (*(ptr+1) < 'A' || *(ptr+1) > 'F'))
                return FALSE;
              else if (((*(ptr+2) < '0' || *(ptr+2) > '9'))
                  && (*(ptr+2) < 'A' || *(ptr+2) > 'F'))
                return FALSE;

              digitz[0] = *(++ptr);
              digitz[1] = *(++ptr);
              digitz[2] = '\0';
              val = (int)strtol(digitz, NULL, 16);

              if (svn_uri__char_validity[val])
                return FALSE; /* Should not have been escaped */
            }
          else if (*ptr != '/' && !svn_uri__char_validity[(unsigned char)*ptr])
            return FALSE; /* Character should have been escaped */
          ptr++;
        }
    }

  return TRUE;
}

/* From libsvn_subr/dirent_uri.c. */
static svn_boolean_t
svn_uri_is_absolute(const char *uri)
{
  /* uri is absolute if it starts with '/' */
  if (uri && uri[0] == '/')
    return TRUE;

  /* URLs are absolute. */
  return svn_path_is_url(uri);
}

/* Calculates the length occupied by the schema defined root of URI
   From libsvn_subr/dirent_uri.c. */
static apr_size_t
uri_schema_root_length(const char *uri, apr_size_t len)
{
  apr_size_t i;

  for (i = 0; i < len; i++)
    {
      if (uri[i] == '/')
        {
          if (i > 0 && uri[i-1] == ':' && i < len-1 && uri[i+1] == '/')
            {
              /* We have an absolute uri */
              if (i == 5 && strncmp("file", uri, 4) == 0)
                return 7; /* file:// */
              else
                {
                  for (i += 2; i < len; i++)
                    if (uri[i] == '/')
                      return i;

                  return len; /* Only a hostname is found */
                }
            }
          else
            return 0;
        }
    }

  return 0;
}

/* From libsvn_subr/dirent_uri.c. */
char *
svn_uri_join(const char *base, const char *component, apr_pool_t *pool)
{
  apr_size_t blen = strlen(base);
  apr_size_t clen = strlen(component);
  char *path;

  assert(svn_uri_is_canonical(base, pool));
  assert(svn_uri_is_canonical(component, pool));

  /* If either is empty return the other */
  if (SVN_PATH_IS_EMPTY(base))
    return apr_pmemdup(pool, component, clen + 1);
  if (SVN_PATH_IS_EMPTY(component))
    return apr_pmemdup(pool, base, blen + 1);

  /* If the component is absolute, then return it.  */
  if (svn_uri_is_absolute(component))
    {
      if (*component != '/')
        return apr_pmemdup(pool, component, clen + 1);
      else
        {
          /* The uri is not absolute enough; use only the root from base */
          apr_size_t n = uri_schema_root_length(base, blen);

          path = apr_palloc(pool, n + clen + 1);

          if (n > 0)
            memcpy(path, base, n);

          memcpy(path + n, component, clen + 1); /* Include '\0' */

          return path;
        }
    }

  if (blen == 1 && base[0] == '/')
    blen = 0; /* Ignore base, just return separator + component */

  /* Construct the new, combined path. */
  path = apr_palloc(pool, blen + 1 + clen + 1);
  memcpy(path, base, blen);
  path[blen] = '/';
  memcpy(path + blen + 1, component, clen + 1);

  return path;
}

/* From libsvn_subr/dirent_uri.c. */
static svn_boolean_t
svn_relpath_is_canonical(const char *relpath,
                         apr_pool_t *pool)
{
  const char *ptr = relpath, *seg = relpath;

  /* RELPATH is canonical if it has:
   *  - no '.' segments
   *  - no start and closing '/'
   *  - no '//'
   */

  if (*relpath == '\0')
    return TRUE;

  if (*ptr == '/')
    return FALSE;

  /* Now validate the rest of the path. */
  while(1)
    {
      apr_size_t seglen = ptr - seg;

      if (seglen == 1 && *seg == '.')
        return FALSE;  /*  /./   */

      if (*ptr == '/' && *(ptr+1) == '/')
        return FALSE;  /*  //    */

      if (! *ptr && *(ptr - 1) == '/')
        return FALSE;  /* foo/  */

      if (! *ptr)
        break;

      if (*ptr == '/')
        ptr++;
      seg = ptr;

      while (*ptr && (*ptr != '/'))
        ptr++;
    }

  return TRUE;
}

/* From libsvn_subr/dirent_uri.c. */
const char *
svn_relpath_basename(const char *relpath,
                     apr_pool_t *pool)
{
  apr_size_t len = strlen(relpath);
  apr_size_t start;

  assert(!pool || svn_relpath_is_canonical(relpath, pool));

  start = len;
  while (start > 0 && relpath[start - 1] != '/')
    --start;

  if (pool)
    return apr_pstrmemdup(pool, relpath + start, len - start);
  else
    return relpath + start;
}

/* Return the length of substring necessary to encompass the entire
 * previous relpath segment in RELPATH, which should be a LEN byte string.
 *
 * A trailing slash will not be included in the returned length.
 * From libsvn_subr/dirent_uri.c.
 */
static apr_size_t
relpath_previous_segment(const char *relpath,
                         apr_size_t len)
{
  if (len == 0)
    return 0;

  --len;
  while (len > 0 && relpath[len] != '/')
    --len;

  return len;
}

/* From libsvn_subr/dirent_uri.c. */
char *
svn_relpath_dirname(const char *relpath,
                    apr_pool_t *pool)
{
  apr_size_t len = strlen(relpath);

  assert(svn_relpath_is_canonical(relpath, pool));

  return apr_pstrmemdup(pool, relpath,
                        relpath_previous_segment(relpath, len));
}

/* From libsvn_subr/dirent_uri.c. */
const char *
svn_dirent_basename(const char *dirent, apr_pool_t *pool)
{
  apr_size_t len = strlen(dirent);
  apr_size_t start;

  assert(!pool || svn_dirent_is_canonical(dirent, pool));

  if (svn_dirent_is_root(dirent, len))
    return "";
  else
    {
      start = len;
      while (start > 0 && dirent[start - 1] != '/'
#ifdef SVN_USE_DOS_PATHS
             && dirent[start - 1] != ':'
#endif
            )
        --start;
    }

  if (pool)
    return apr_pstrmemdup(pool, dirent + start, len - start);
  else
    return dirent + start;
}

/* Return the string length of the longest common ancestor of PATH1 and PATH2.
 * Pass type_uri for TYPE if PATH1 and PATH2 are URIs, and type_dirent if
 * PATH1 and PATH2 are regular paths.
 *
 * If the two paths do not share a common ancestor, return 0.
 *
 * New strings are allocated in POOL.
 * From libsvn_sbur/dirent_uri.c.
 */
static apr_size_t
get_longest_ancestor_length(path_type_t types,
                            const char *path1,
                            const char *path2,
                            apr_pool_t *pool)
{
  apr_size_t path1_len, path2_len;
  apr_size_t i = 0;
  apr_size_t last_dirsep = 0;
#ifdef SVN_USE_DOS_PATHS
  svn_boolean_t unc = FALSE;
#endif

  path1_len = strlen(path1);
  path2_len = strlen(path2);

  if (SVN_PATH_IS_EMPTY(path1) || SVN_PATH_IS_EMPTY(path2))
    return 0;

  while (path1[i] == path2[i])
    {
      /* Keep track of the last directory separator we hit. */
      if (path1[i] == '/')
        last_dirsep = i;

      i++;

      /* If we get to the end of either path, break out. */
      if ((i == path1_len) || (i == path2_len))
        break;
    }

  /* two special cases:
     1. '/' is the longest common ancestor of '/' and '/foo' */
  if (i == 1 && path1[0] == '/' && path2[0] == '/')
    return 1;
  /* 2. '' is the longest common ancestor of any non-matching
   * strings 'foo' and 'bar' */
  if (types == type_dirent && i == 0)
    return 0;

  /* Handle some windows specific cases */
#ifdef SVN_USE_DOS_PATHS
  if (types == type_dirent)
    {
      /* don't count the '//' from UNC paths */
      if (last_dirsep == 1 && path1[0] == '/' && path1[1] == '/')
        {
          last_dirsep = 0;
          unc = TRUE;
        }

      /* X:/ and X:/foo */
      if (i == 3 && path1[2] == '/' && path1[1] == ':')
        return i;

      /* Cannot use SVN_ERR_ASSERT here, so we'll have to crash, sorry.
       * Note that this assertion triggers only if the code above has
       * been broken. The code below relies on this assertion, because
       * it uses [i - 1] as index. */
      assert(i > 0);

      /* X: and X:/ */
      if ((path1[i - 1] == ':' && path2[i] == '/') ||
          (path2[i - 1] == ':' && path1[i] == '/'))
          return 0;
      /* X: and X:foo */
      if (path1[i - 1] == ':' || path2[i - 1] == ':')
          return i;
    }
#endif /* SVN_USE_DOS_PATHS */

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte, and is guaranteed to be <= the length of
     whichever path is shorter.
     If one of the paths is the common part return that. */
  if (((i == path1_len) && (path2[i] == '/'))
           || ((i == path2_len) && (path1[i] == '/'))
           || ((i == path1_len) && (i == path2_len)))
    return i;
  else
    {
      /* Nothing in common but the root folder '/' or 'X:/' for Windows
         dirents. */
#ifdef SVN_USE_DOS_PATHS
      if (! unc)
        {
          /* X:/foo and X:/bar returns X:/ */
          if ((types == type_dirent) &&
              last_dirsep == 2 && path1[1] == ':' && path1[2] == '/'
                               && path2[1] == ':' && path2[2] == '/')
            return 3;
#endif /* SVN_USE_DOS_PATHS */
          if (last_dirsep == 0 && path1[0] == '/' && path2[0] == '/')
            return 1;
#ifdef SVN_USE_DOS_PATHS
        }
#endif
    }

  return last_dirsep;
}

/* From libsvn_subr/dirent_uri.c. */
char *
svn_relpath_get_longest_ancestor(const char *relpath1,
                                 const char *relpath2,
                                 apr_pool_t *pool)
{
  return apr_pstrndup(pool, relpath1,
                      get_longest_ancestor_length(type_relpath, relpath1,
                                                  relpath2, pool));
}

/* From libsvn_subr/dirent_uri.c. */
const char *
svn_relpath_skip_ancestor(const char *parent_relpath,
                          const char *child_relpath)
{
  apr_size_t len = strlen(parent_relpath);

  if (0 != memcmp(parent_relpath, child_relpath, len))
    return child_relpath; /* parent_relpath is no ancestor of child_relpath */

  if (child_relpath[len] == 0)
    return ""; /* parent_relpath == child_relpath */

  if (len == 1 && child_relpath[0] == '/')
    return child_relpath + 1;

  if (child_relpath[len] == '/')
    return child_relpath + len + 1;

  return child_relpath;
}

/* From libsvn_subr/dirent_uri.c. */
char *
svn_relpath_join(const char *base,
                 const char *component,
                 apr_pool_t *pool)
{
  apr_size_t blen = strlen(base);
  apr_size_t clen = strlen(component);
  char *path;

  assert(svn_relpath_is_canonical(base, pool));
  assert(svn_relpath_is_canonical(component, pool));

  /* If either is empty return the other */
  if (blen == 0)
    return apr_pmemdup(pool, component, clen + 1);
  if (clen == 0)
    return apr_pmemdup(pool, base, blen + 1);

  path = apr_palloc(pool, blen + 1 + clen + 1);
  memcpy(path, base, blen);
  path[blen] = '/';
  memcpy(path + blen + 1, component, clen + 1);

  return path;
}

/* Locale insensitive tolower() for converting parts of dirents and urls
   while canonicalizing.  From libsvn_subr/dirent_uri.c. */
static char
canonicalize_to_lower(char c)
{
  if (c < 'A' || c > 'Z')
    return c;
  else
    return c - 'A' + 'a';
}

/* Locale insensitive toupper() for converting parts of dirents and urls
   while canonicalizing.  From libsvn_subr/dirent_uri.c. */
static char
canonicalize_to_upper(char c)
{
  if (c < 'a' || c > 'z')
    return c;
  else
    return c - 'a' + 'A';
}


/* Return the canonicalized version of PATH, of type TYPE, allocated in
 * POOL.  From libsvn_subr/dirent_uri.c.
 */
static const char *
canonicalize(path_type_t type, const char *path, apr_pool_t *pool)
{
  char *canon, *dst;
  const char *src;
  apr_size_t seglen;
  apr_size_t schemelen = 0;
  apr_size_t canon_segments = 0;
  svn_boolean_t url = FALSE;
  char *schema_data = NULL;

  /* "" is already canonical, so just return it; note that later code
     depends on path not being zero-length.  */
  if (SVN_PATH_IS_EMPTY(path))
    return "";

  dst = canon = apr_pcalloc(pool, strlen(path) + 1);

  /* If this is supposed to be an URI and it starts with "scheme://", then
     copy the scheme, host name, etc. to DST and set URL = TRUE. */
  src = path;
  if (type == type_uri && *src != '/')
    {
      while (*src && (*src != '/') && (*src != ':'))
        src++;

      if (*src == ':' && *(src+1) == '/' && *(src+2) == '/')
        {
          const char *seg;

          url = TRUE;

          /* Found a scheme, convert to lowercase and copy to dst. */
          src = path;
          while (*src != ':')
            {
              *(dst++) = canonicalize_to_lower((*src++));
              schemelen++;
            }
          *(dst++) = ':';
          *(dst++) = '/';
          *(dst++) = '/';
          src += 3;
          schemelen += 3;

          /* This might be the hostname */
          seg = src;
          while (*src && (*src != '/') && (*src != '@'))
            src++;

          if (*src == '@')
            {
              /* Copy the username & password. */
              seglen = src - seg + 1;
              memcpy(dst, seg, seglen);
              dst += seglen;
              src++;
            }
          else
            src = seg;

          /* Found a hostname, convert to lowercase and copy to dst. */
          while (*src && (*src != '/'))
            *(dst++) = canonicalize_to_lower((*src++));

          /* Copy trailing slash, or null-terminator. */
          *(dst) = *(src);

          /* Move src and dst forward only if we are not
           * at null-terminator yet. */
          if (*src)
            {
              src++;
              dst++;
              schema_data = dst;
            }

          canon_segments = 1;
        }
    }

  /* Copy to DST any separator or drive letter that must come before the
     first regular path segment. */
  if (! url && type != type_relpath)
    {
      src = path;
      /* If this is an absolute path, then just copy over the initial
         separator character. */
      if (*src == '/')
        {
          *(dst++) = *(src++);

#ifdef SVN_USE_DOS_PATHS
          /* On Windows permit two leading separator characters which means an
           * UNC path. */
          if ((type == type_dirent) && *src == '/')
            *(dst++) = *(src++);
#endif /* SVN_USE_DOS_PATHS */
        }
#ifdef SVN_USE_DOS_PATHS
      /* On Windows the first segment can be a drive letter, which we normalize
         to upper case. */
      else if (type == type_dirent &&
               ((*src >= 'a' && *src <= 'z') ||
                (*src >= 'A' && *src <= 'Z')) &&
               (src[1] == ':'))
        {
          *(dst++) = canonicalize_to_upper(*(src++));
          /* Leave the ':' to be processed as (or as part of) a path segment
             by the following code block, so we need not care whether it has
             a slash after it. */
        }
#endif /* SVN_USE_DOS_PATHS */
    }

  while (*src)
    {
      /* Parse each segment, find the closing '/' */
      const char *next = src;
      while (*next && (*next != '/'))
        ++next;

      seglen = next - src;

      if (seglen == 0 || (seglen == 1 && src[0] == '.'))
        {
          /* Noop segment, so do nothing. */
        }
#ifdef SVN_USE_DOS_PATHS
      /* If this is the first path segment of a file:// URI and it contains a
         windows drive letter, convert the drive letter to upper case. */
      else if (url && canon_segments == 1 && seglen == 2 &&
               (strncmp(canon, "file:", 5) == 0) &&
               src[0] >= 'a' && src[0] <= 'z' && src[1] == ':')
        {
          *(dst++) = canonicalize_to_upper(src[0]);
          *(dst++) = ':';
          if (*next)
            *(dst++) = *next;
          canon_segments++;
        }
#endif /* SVN_USE_DOS_PATHS */
      else
        {
          /* An actual segment, append it to the destination path */
          if (*next)
            seglen++;
          memcpy(dst, src, seglen);
          dst += seglen;
          canon_segments++;
        }

      /* Skip over trailing slash to the next segment. */
      src = next;
      if (*src)
        src++;
    }

  /* Remove the trailing slash if there was at least one
   * canonical segment and the last segment ends with a slash.
   *
   * But keep in mind that, for URLs, the scheme counts as a
   * canonical segment -- so if path is ONLY a scheme (such
   * as "https://") we should NOT remove the trailing slash. */
  if ((canon_segments > 0 && *(dst - 1) == '/')
      && ! (url && path[schemelen] == '\0'))
    {
      dst --;
    }

  *dst = '\0';

#ifdef SVN_USE_DOS_PATHS
  /* Skip leading double slashes when there are less than 2
   * canon segments. UNC paths *MUST* have two segments. */
  if ((type == type_dirent) && canon[0] == '/' && canon[1] == '/')
    {
      if (canon_segments < 2)
        return canon + 1;
      else
        {
          /* Now we're sure this is a valid UNC path, convert the server name
             (the first path segment) to lowercase as Windows treats it as case
             insensitive.
             Note: normally the share name is treated as case insensitive too,
             but it seems to be possible to configure Samba to treat those as
             case sensitive, so better leave that alone. */
          dst = canon + 2;
          while (*dst && *dst != '/')
            *(dst++) = canonicalize_to_lower(*dst);
        }
    }
#endif /* SVN_USE_DOS_PATHS */

  /* Check the normalization of characters in a uri */
  if (schema_data)
    {
      int need_extra = 0;
      src = schema_data;

      while (*src)
        {
          switch (*src)
            {
              case '/':
                break;
              case '%':
                if (!svn_ctype_isxdigit(*(src+1)) ||
                    !svn_ctype_isxdigit(*(src+2)))
                  need_extra += 2;
                else
                  src += 2;
                break;
              default:
                if (!svn_uri__char_validity[(unsigned char)*src])
                  need_extra += 2;
                break;
            }
          src++;
        }

      if (need_extra > 0)
        {
          apr_size_t pre_schema_size = (apr_size_t)(schema_data - canon);

          dst = apr_palloc(pool, (apr_size_t)(src - canon) + need_extra + 1);
          memcpy(dst, canon, pre_schema_size);
          canon = dst;

          dst += pre_schema_size;
        }
      else
        dst = schema_data;

      src = schema_data;

      while (*src)
        {
          switch (*src)
            {
              case '/':
                *(dst++) = '/';
                break;
              case '%':
                if (!svn_ctype_isxdigit(*(src+1)) ||
                    !svn_ctype_isxdigit(*(src+2)))
                  {
                    *(dst++) = '%';
                    *(dst++) = '2';
                    *(dst++) = '5';
                  }
                else
                  {
                    char digitz[3];
                    int val;

                    digitz[0] = *(++src);
                    digitz[1] = *(++src);
                    digitz[2] = 0;

                    val = (int)strtol(digitz, NULL, 16);

                    if (svn_uri__char_validity[(unsigned char)val])
                      *(dst++) = (char)val;
                    else
                      {
                        *(dst++) = '%';
                        *(dst++) = canonicalize_to_upper(digitz[0]);
                        *(dst++) = canonicalize_to_upper(digitz[1]);
                      }
                  }
                break;
              default:
                if (!svn_uri__char_validity[(unsigned char)*src])
                  {
                    apr_snprintf(dst, 4, "%%%02X", (unsigned char)*src);
                    dst += 3;
                  }
                else
                  *(dst++) = *src;
                break;
            }
          src++;
        }
      *dst = '\0';
    }

  return canon;
}

/* From libsvn_subr/dirent_uri.c. */
const char *
svn_uri_canonicalize(const char *uri, apr_pool_t *pool)
{
  return canonicalize(type_uri, uri, pool);
}

/* From libsvn_subr/dirent_uri.c. */
const char *
svn_relpath_canonicalize(const char *relpath, apr_pool_t *pool)
{
  return canonicalize(type_relpath, relpath, pool);
}

/* New code (public domain). */
svn_error_t *
svn_io_remove_file2(const char *path,
                   svn_boolean_t ignore_enoent,
                   apr_pool_t *scratch_pool)
{
  svn_error_t *err = svn_io_remove_file(path, scratch_pool);
  if (ignore_enoent && err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  return err;
}

/* From libsvn_subr/cmdline.c. */
svn_error_t *
svn_cmdline__apply_config_options(apr_hash_t *config,
                                  const apr_array_header_t *config_options,
                                  const char *prefix,
                                  const char *argument_name)
{
  int i;

  for (i = 0; i < config_options->nelts; i++)
   {
     svn_config_t *cfg;
     svn_cmdline__config_argument_t *arg =
                          APR_ARRAY_IDX(config_options, i,
                                        svn_cmdline__config_argument_t *);

     cfg = apr_hash_get(config, arg->file, APR_HASH_KEY_STRING);

     if (cfg)
       {
         svn_config_set(cfg, arg->section, arg->option, arg->value);
       }
     else
       {
         svn_error_t *err = svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
             _("Unrecognized file in argument of %s"), argument_name);

         svn_handle_warning2(stderr, err, prefix);
         svn_error_clear(err);
       }
    }

  return SVN_NO_ERROR;
}

/* From libsvn_subr/cmdline.c. */
svn_error_t *
svn_cmdline__parse_config_option(apr_array_header_t *config_options,
                                 const char *opt_arg,
                                 apr_pool_t *pool)
{
  svn_cmdline__config_argument_t *config_option;
  const char *first_colon, *second_colon, *equals_sign;
  apr_size_t len = strlen(opt_arg);
  if ((first_colon = strchr(opt_arg, ':')) && (first_colon != opt_arg))
    {
      if ((second_colon = strchr(first_colon + 1, ':')) &&
          (second_colon != first_colon + 1))
        {
          if ((equals_sign = strchr(second_colon + 1, '=')) &&
              (equals_sign != second_colon + 1))
            {
              config_option = apr_pcalloc(pool, sizeof(*config_option));
              config_option->file = apr_pstrndup(pool, opt_arg,
                                                 first_colon - opt_arg);
              config_option->section = apr_pstrndup(pool, first_colon + 1,
                                                    second_colon - first_colon - 1);
              config_option->option = apr_pstrndup(pool, second_colon + 1,
                                                   equals_sign - second_colon -
1);

              if (! (strchr(config_option->option, ':')))
                {
                  config_option->value = apr_pstrndup(pool, equals_sign + 1,
                                                      opt_arg + len - equals_sign - 1);
                  APR_ARRAY_PUSH(config_options, svn_cmdline__config_argument_t
*)
                                       = config_option;
                  return SVN_NO_ERROR;
                }
            }
        }
    }
  return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                          _("Invalid syntax of argument of --config-option"));
}
