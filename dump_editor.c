/* Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_dirent_uri.h"

#include "dumpr_util.h"

static svn_error_t *open_root(void *edit_baton,
			      svn_revnum_t base_revision,
			      apr_pool_t *pool,
			      void **root_baton)
{
	*root_baton = NULL;
	return SVN_NO_ERROR;
}

static svn_error_t *delete_entry(const char *path,
				 svn_revnum_t revision,
				 void *parent_baton,
				 apr_pool_t *pool)
{
	return SVN_NO_ERROR;
}

static svn_error_t *add_directory(const char *path,
				  void *parent_baton,
				  const char *copyfrom_path,
				  svn_revnum_t copyfrom_rev,
				  apr_pool_t *pool,
				  void **child_baton)
{
	*child_baton = NULL;
	return SVN_NO_ERROR;
}

static svn_error_t *open_directory(const char *path,
				   void *parent_baton,
				   svn_revnum_t base_revision,
				   apr_pool_t *pool,
				   void **child_baton)
{
	*child_baton = NULL;
	return SVN_NO_ERROR;
}

static svn_error_t *close_directory(void *dir_baton,
				    apr_pool_t *pool)
{
	return SVN_NO_ERROR;
}

static svn_error_t *add_file(const char *path,
			     void *parent_baton,
			     const char *copyfrom_path,
			     svn_revnum_t copyfrom_rev,
			     apr_pool_t *pool,
			     void **file_baton)
{
	*file_baton = NULL;
	return SVN_NO_ERROR;
}

static svn_error_t *open_file(const char *path,
			      void *parent_baton,
			      svn_revnum_t ancestor_revision,
			      apr_pool_t *pool,
			      void **file_baton)
{
	*file_baton = NULL;
	return SVN_NO_ERROR;
}

static svn_error_t *change_dir_prop(void *parent_baton,
				    const char *name,
				    const svn_string_t *value,
				    apr_pool_t *pool)
{
	return SVN_NO_ERROR;
}

static svn_error_t *change_file_prop(void *file_baton,
				     const char *name,
				     const svn_string_t *value,
				     apr_pool_t *pool)
{
	return SVN_NO_ERROR;
}

static svn_error_t *apply_textdelta(void *file_baton, const char *base_checksum,
				    apr_pool_t *pool,
				    svn_txdelta_window_handler_t *handler,
				    void **handler_baton)
{
	*handler = svn_delta_noop_window_handler;
	*handler_baton = NULL;
	return SVN_NO_ERROR;
}

static svn_error_t *close_file(void *file_baton,
			       const char *text_checksum,
			       apr_pool_t *pool)
{
	return SVN_NO_ERROR;
}

static svn_error_t *close_edit(void *edit_baton, apr_pool_t *pool)
{
	return SVN_NO_ERROR;
}

svn_error_t *get_dump_editor(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_revnum_t from_rev,
                             apr_pool_t *pool)
{
	struct dump_edit_baton *eb = apr_pcalloc(pool, sizeof(struct dump_edit_baton));
	eb->current_rev = from_rev;
	SVN_ERR(svn_stream_for_stdout(&(eb->stream), pool));
	svn_delta_editor_t *de = svn_delta_default_editor(pool);
	
	de->open_root = open_root;
	de->delete_entry = delete_entry;
	de->add_directory = add_directory;
	de->open_directory = open_directory;
	de->close_directory = close_directory;
	de->change_dir_prop = change_dir_prop;
	de->change_file_prop = change_file_prop;
	de->apply_textdelta = apply_textdelta;
	de->add_file = add_file;
	de->open_file = open_file;
	de->close_file = close_file;
	de->close_edit = close_edit;

	/* Set the edit_baton and editor */
	*edit_baton = eb;
	*editor = de;

	return SVN_NO_ERROR;
}
