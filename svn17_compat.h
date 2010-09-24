/**
 * @copyright
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
 * @endcopyright
 *
 * @file compat.h
 * @brief SVN 1.7 compatibility routines.
 */

#ifndef SVN17_COMPAT_H
#define SVN17_COMPAT_H

#include <apr.h>
#include <apr_pools.h>

#include <svn_types.h>

/** Join a valid base uri (@a base) with a relative path or uri
 * (@a component), allocating the result in @a pool. @a component need
 * not be a single component: it can be a relative path or a '/'
 * prefixed relative path to join component to the root path of @a base.
 *
 * If @a component is the empty path, then @a base will be copied and
 * returned.
 *
 * If the @a component is an absolute uri, then it is copied and returned.
 *
 * If @a component starts with a '/' and @a base contains a scheme, the
 * scheme defined joining rules are applied.
 *
 * From svn_dirent_uri.h.
 */
char *
svn_uri_join(const char *base,
             const char *component,
             apr_pool_t *pool);

/** Join a base relpath (@a base) with a component (@a component), allocating
 * the result in @a pool. @a component need not be a single component.
 *
 * If either @a base or @a component is the empty path, then the other
 * argument will be copied and returned.  If both are the empty path the
 * empty path is returned.
 *
 * From svn_dirent_uri.h.
 */
char *
svn_relpath_join(const char *base,
                 const char *component,
                 apr_pool_t *pool);

/** Return the longest common path shared by two relative paths,
 * @a relpath1 and @a relpath2.  If there's no common ancestor, return the
 * empty path.
 *
 * From svn_dirent_uri.h.
 */
char *
svn_relpath_get_longest_ancestor(const char *relpath1,
                                 const char *relpath2,
                                 apr_pool_t *pool);

/** Return the relative path part of @a child_relpath that is below
 * @a parent_relpath, or just "" if @a parent_relpath is equal to
 * @a child_relpath. If @a child_relpath is not below @a parent_relpath,
 * return @a child_relpath.
 *
 * From svn_dirent_uri.h.
 */
const char *
svn_relpath_skip_ancestor(const char *parent_relpath,
                          const char *child_relpath);

/** Get the basename of the specified canonicalized @a relpath.  The
 * basename is defined as the last component of the relpath.  If the @a
 * relpath has only one component then that is returned. The returned
 * value will have no slashes in it.
 *
 * Example: svn_relpath_basename("/trunk/foo/bar") -> "bar"
 *
 * The returned basename will be allocated in @a pool. If @a
 * pool is NULL a pointer to the basename in @a relpath is returned.
 *
 * @note If an empty string is passed, then an empty string will be returned.
 *
 * From svn_dirent_uri.h.
 */
const char *
svn_relpath_basename(const char *uri,
                     apr_pool_t *pool);

/** Get the dirname of the specified canonicalized @a relpath, defined as
 * the relpath with its basename removed.
 *
 * If @a relpath is empty, "" is returned.
 *
 * The returned relpath will be allocated in @a pool.
 *
 * From svn_dirent_uri.h.
 */
char *
svn_relpath_dirname(const char *relpath,
                    apr_pool_t *pool);

/** Gets the name of the specified canonicalized @a dirent as it is known
 * within its parent directory. If the @a dirent is root, return "". The
 * returned value will not have slashes in it.
 *
 * Example: svn_dirent_basename("/foo/bar") -> "bar"
 *
 * The returned basename will be allocated in @a pool. If @a pool is NULL
 * a pointer to the basename in @a dirent is returned.
 *
 * @note If an empty string is passed, then an empty string will be returned.
 *
 * From svn_dirent_uri.h.
 */
const char *
svn_dirent_basename(const char *dirent,
                    apr_pool_t *pool);

/** Return a new uri like @a uri, but transformed such that some types
 * of uri specification redundancies are removed.
 *
 * This involves collapsing redundant "/./" elements, removing
 * multiple adjacent separator characters, removing trailing
 * separator characters, and possibly other semantically inoperative
 * transformations.
 *
 * If @a uri starts with a schema, this function also normalizes the
 * escaping of the path component by unescaping characters that don't
 * need escaping and escaping characters that do need escaping but
 * weren't.
 *
 * This functions supports URLs.
 *
 * The returned uri may be statically allocated or allocated from @a pool.
 *
 * From svn_dirent_uri.h.
 */
const char *
svn_uri_canonicalize(const char *uri,
                     apr_pool_t *pool);

/** Return a new relpath like @a relpath, but transformed such that some types
 * of relpath specification redundancies are removed.
 *
 * This involves collapsing redundant "/./" elements, removing
 * multiple adjacent separator characters, removing trailing
 * separator characters, and possibly other semantically inoperative
 * transformations.
 *
 * This functions supports relpaths.
 *
 * The returned relpath may be statically allocated or allocated from @a
 * pool.
 *
 * From svn_dirent_uri.h.
 */
const char *
svn_relpath_canonicalize(const char *uri,
                         apr_pool_t *pool);

/** Remove file @a path, a utf8-encoded path.  This wraps apr_file_remove(),
 * converting any error to a Subversion error. If @a ignore_enoent is TRUE, and
 * the file is not present (APR_STATUS_IS_ENOENT returns TRUE), then no
 * error will be returned.
 *
 * From svn_io.h.
 */
svn_error_t *
svn_io_remove_file2(const char *path,
                   svn_boolean_t ignore_enoent,
                   apr_pool_t *scratch_pool);


/** @defgroup apr_hash_utilities APR Hash Table Helpers
 * These functions enable the caller to dereference an APR hash table index
 * without type casts or temporary variables.
 *
 * ### These are private, and may go away when APR implements them natively.
 *
 * From svn_types.h.
 * @{
 */

/** Return the key of the hash table entry indexed by @a hi. */
const void *
svn__apr_hash_index_key(const apr_hash_index_t *hi);

/** Return the value of the hash table entry indexed by @a hi. */
void *
svn__apr_hash_index_val(const apr_hash_index_t *hi);

/** @} */

/* From svn_private_config.h.
 */
#define PACKAGE_NAME "subversion"
#define N_(x) x
#include <locale.h>
#include <libintl.h>
#define _(x) dgettext(PACKAGE_NAME, x)

/** Sets the config options in @a config_options, an apr array containing
 * svn_cmdline__config_argument_t* elements to the configuration in @a cfg,
 * a hash mapping of <tt>const char *</tt> configuration file names to
 * @c svn_config_t *'s. Write warnings to stderr.
 *
 * Use @a prefix as prefix and @a argument_name in warning messages.
 *
 * From private/svn_cmdline_private.h.
 */
svn_error_t *
svn_cmdline__apply_config_options(apr_hash_t *config,
                                  const apr_array_header_t *config_options,
                                  const char *prefix,
                                  const char *argument_name);

/** Container for config options parsed with svn_cmdline__parse_config_option
 *
 * From private/svn_cmdline_private.h.
 */
typedef struct svn_cmdline__config_argument_t
{
  const char *file;
  const char *section;
  const char *option;
  const char *value;
} svn_cmdline__config_argument_t;

/** Parser for 'FILE:SECTION:OPTION=[VALUE]'-style option arguments.
 *
 * Parses @a opt_arg and places its value in @a config_options, an apr array
 * containing svn_cmdline__config_argument_t* elements, allocating the option
 * data in @a pool
 *
 * From private/svn_cmdline_private.h.
 */
svn_error_t *
svn_cmdline__parse_config_option(apr_array_header_t *config_options,
                                 const char *opt_arg,
                                 apr_pool_t *pool);


#endif /* SVN17_COMPAT_H */
