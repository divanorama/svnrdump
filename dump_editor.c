/* Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_dirent_uri.h"

#include "dumpr_util.h"

#define ARE_VALID_COPY_ARGS(p,r) ((p) && SVN_IS_VALID_REVNUM(r))

/* Make a directory baton to represent the directory was path
   (relative to EDIT_BATON's path) is PATH.

   CMP_PATH/CMP_REV are the path/revision against which this directory
   should be compared for changes.  If either is omitted (NULL for the
   path, SVN_INVALID_REVNUM for the rev), just compare this directory
   PATH against itself in the previous revision.

   PARENT_DIR_BATON is the directory baton of this directory's parent,
   or NULL if this is the top-level directory of the edit.  ADDED
   indicated if this directory is newly added in this revision.
   Perform all allocations in POOL.  */
static struct dir_baton *make_dir_baton(const char *path,
					const char *cmp_path,
					svn_revnum_t cmp_rev,
					void *edit_baton,
					void *parent_dir_baton,
					svn_boolean_t added,
					apr_pool_t *pool)
{
	struct dump_edit_baton *eb = edit_baton;
	struct dir_baton *pb = parent_dir_baton;
	struct dir_baton *new_db = apr_pcalloc(pool, sizeof(*new_db));
	const char *full_path;
	apr_array_header_t *compose_path = apr_array_make(pool, 2, sizeof(const char *));

	/* A path relative to nothing?  I don't think so. */
	SVN_ERR_ASSERT_NO_RETURN(!path || pb);

	/* Construct the full path of this node. */
	if (pb) {
		APR_ARRAY_PUSH(compose_path, const char *) = "/";
		APR_ARRAY_PUSH(compose_path, const char *) = path;
		full_path = svn_path_compose(compose_path, pool);
	}
	else
		full_path = apr_pstrdup(pool, "/");

	/* Remove leading slashes from copyfrom paths. */
	if (cmp_path)
		cmp_path = ((*cmp_path == '/') ? cmp_path + 1 : cmp_path);

	new_db->eb = eb;
	new_db->parent_dir_baton = pb;
	new_db->path = full_path;
	new_db->cmp_path = cmp_path ? apr_pstrdup(pool, cmp_path) : NULL;
	new_db->cmp_rev = cmp_rev;
	new_db->added = added;
	new_db->written_out = FALSE;
	new_db->deleted_entries = apr_hash_make(pool);
	new_db->pool = pool;

	return new_db;
}

/*
 * Write out a node record for PATH of type KIND under EB->FS_ROOT.
 * ACTION describes what is happening to the node (see enum svn_node_action).
 * Write record to writable EB->STREAM, using EB->BUFFER to write in chunks.
 *
 * If the node was itself copied, IS_COPY is TRUE and the
 * path/revision of the copy source are in CMP_PATH/CMP_REV.  If
 * IS_COPY is FALSE, yet CMP_PATH/CMP_REV are valid, this node is part
 * of a copied subtree.
 */
static svn_error_t *dump_node(struct dump_edit_baton *eb,
			      const char *path,    /* an absolute path. */
			      svn_node_kind_t kind,
			      enum svn_node_action action,
			      const char *cmp_path,
			      svn_revnum_t cmp_rev,
			      apr_pool_t *pool)
{
	/* Write out metadata headers for this file node. */
	SVN_ERR(svn_stream_printf(eb->stream, pool,
				  SVN_REPOS_DUMPFILE_NODE_PATH ": %s\n",
				  (*path == '/') ? path + 1 : path));

	if (kind == svn_node_file)
		SVN_ERR(svn_stream_printf(eb->stream, pool,
		                          SVN_REPOS_DUMPFILE_NODE_KIND ": file\n"));
	else if (kind == svn_node_dir)
		SVN_ERR(svn_stream_printf(eb->stream, pool,
		                          SVN_REPOS_DUMPFILE_NODE_KIND ": dir\n"));

	/* Remove leading slashes from copyfrom paths. */
	if (cmp_path)
		cmp_path = ((*cmp_path == '/') ? cmp_path + 1 : cmp_path);

	switch (action) {
		/* Appropriately handle the four svn_node_action actions */

	case svn_node_action_change:
		SVN_ERR(svn_stream_printf(eb->stream, pool,
		                          SVN_REPOS_DUMPFILE_NODE_ACTION
		                          ": change\n"));
		break;

	case svn_node_action_replace:
		if (!eb->is_copy) {
			/* a simple delete+add, implied by a single 'replace' action. */
			SVN_ERR(svn_stream_printf(eb->stream, pool,
			                          SVN_REPOS_DUMPFILE_NODE_ACTION
			                          ": replace\n"));

			eb->dump_props_pending = TRUE;
			break;
		}
		/* More complex case: eb->is_copy is true, and
		   cmp_path/ cmp_rev are present: delete the original,
		   and then re-add it */

		/* the path & kind headers have already been printed;  just
		   add a delete action, and end the current record.*/
		SVN_ERR(svn_stream_printf(eb->stream, pool,
		                          SVN_REPOS_DUMPFILE_NODE_ACTION
		                          ": delete\n\n"));

		/* recurse:  print an additional add-with-history record. */
		SVN_ERR(dump_node(eb, path, kind, svn_node_action_add,
		                  cmp_path, cmp_rev, pool));

		/* we can leave this routine quietly now, don't need to dump
		   any content;  that was already done in the second record. */
		eb->must_dump_props = FALSE;
		eb->is_copy = FALSE;
		break;

	case svn_node_action_delete:
		SVN_ERR(svn_stream_printf(eb->stream, pool,
		                          SVN_REPOS_DUMPFILE_NODE_ACTION
		                          ": delete\n"));

		/* we can leave this routine quietly now, don't need to dump
		   any content. */
		SVN_ERR(svn_stream_printf(eb->stream, pool, "\n\n"));
		eb->must_dump_props = FALSE;
		break;

	case svn_node_action_add:
		SVN_ERR(svn_stream_printf(eb->stream, pool,
		                          SVN_REPOS_DUMPFILE_NODE_ACTION ": add\n"));

		if (!eb->is_copy) {
			/* eb->dump_props_pending for files is handled in
			   close_file which is called immediately.
			   However, directories are not closed until
			   all the work inside them have been done;
			   eb->dump_props_pending for directories is
			   handled in all the functions that can
			   possibly be called after add_directory:
			   add_directory, open_directory,
			   delete_entry, close_directory, add_file,
			   open_file and change_dir_prop;
			   change_dir_prop is a special case
			   ofcourse */

			eb->dump_props_pending = TRUE;
			break;
		}

		SVN_ERR(svn_stream_printf(eb->stream, pool,
		                          SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV
		                          ": %ld\n"
		                          SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH
		                          ": %s\n",
		                          cmp_rev, cmp_path));

		/* Dump the text only if apply_textdelta sets
		   eb->must_dump_text */

		/* UGLY hack: If a directory was copied from a
		   previous revision, nothing else can be done, and
		   close_file won't be called to write two blank
		   lines; write them here */
		if (kind == svn_node_dir)
			SVN_ERR(svn_stream_printf(eb->stream, pool, "\n\n"));

		eb->is_copy = FALSE;

		break;
	}

	/* Dump property headers */
	SVN_ERR(dump_props(eb, &(eb->must_dump_props), FALSE, pool));

	return SVN_NO_ERROR;
}

static svn_error_t *open_root(void *edit_baton,
			      svn_revnum_t base_revision,
			      apr_pool_t *pool,
			      void **root_baton)
{
	/* Allocate a special pool for the edit_baton to avoid pool
	   lifetime issues */
	struct dump_edit_baton *eb = edit_baton;
	eb->pool = svn_pool_create(pool);
	eb->properties = apr_hash_make(eb->pool);
	eb->del_properties = apr_hash_make(eb->pool);
	eb->propstring = svn_stringbuf_create("", eb->pool);
	eb->is_copy = FALSE;

	*root_baton = make_dir_baton(NULL, NULL, SVN_INVALID_REVNUM,
	                             edit_baton, NULL, FALSE, pool);
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
	struct dump_edit_baton *eb = edit_baton;
	svn_pool_destroy(eb->pool);
	(eb->current_rev) ++;

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
