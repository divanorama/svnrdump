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
	struct dir_baton *pb = parent_baton;
	const char *mypath = apr_pstrdup(pb->pool, path);

	/* Some pending properties to dump? */
	SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

	/* remember this path needs to be deleted */
	apr_hash_set(pb->deleted_entries, mypath, APR_HASH_KEY_STRING, pb);

	return SVN_NO_ERROR;
}

static svn_error_t *add_directory(const char *path,
				  void *parent_baton,
				  const char *copyfrom_path,
				  svn_revnum_t copyfrom_rev,
				  apr_pool_t *pool,
				  void **child_baton)
{
	struct dir_baton *pb = parent_baton;
	void *val;
	struct dir_baton *new_db
		= make_dir_baton(path, copyfrom_path, copyfrom_rev, pb->eb, pb, TRUE, pool);

	/* Some pending properties to dump? */
	SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

	/* This might be a replacement -- is the path already deleted? */
	val = apr_hash_get(pb->deleted_entries, path, APR_HASH_KEY_STRING);

	/* Detect an add-with-history */
	pb->eb->is_copy = ARE_VALID_COPY_ARGS(copyfrom_path, copyfrom_rev);

	/* Dump the node */
	SVN_ERR(dump_node(pb->eb, path,
	                  svn_node_dir,
	                  val ? svn_node_action_replace : svn_node_action_add,
	                  pb->eb->is_copy ? copyfrom_path : NULL,
	                  pb->eb->is_copy ? copyfrom_rev : SVN_INVALID_REVNUM,
	                  pool));

	if (val)
		/* Delete the path, it's now been dumped */
		apr_hash_set(pb->deleted_entries, path, APR_HASH_KEY_STRING, NULL);

	new_db->written_out = TRUE;

	*child_baton = new_db;
	return SVN_NO_ERROR;
}

static svn_error_t *open_directory(const char *path,
				   void *parent_baton,
				   svn_revnum_t base_revision,
				   apr_pool_t *pool,
				   void **child_baton)
{
	struct dir_baton *pb = parent_baton;
	struct dir_baton *new_db;
	const char *cmp_path = NULL;
	svn_revnum_t cmp_rev = SVN_INVALID_REVNUM;
	apr_array_header_t *compose_path = apr_array_make(pool, 2, sizeof(const char *));

	/* Some pending properties to dump? */
	SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

	/* If the parent directory has explicit comparison path and rev,
	   record the same for this one. */
	if (pb && ARE_VALID_COPY_ARGS(pb->cmp_path, pb->cmp_rev)) {
		APR_ARRAY_PUSH(compose_path, const char *) = pb->cmp_path;
		APR_ARRAY_PUSH(compose_path, const char *) = svn_relpath_basename(path, pool);
		cmp_path = svn_path_compose(compose_path, pool);
		cmp_rev = pb->cmp_rev;
	}

	new_db = make_dir_baton(path, cmp_path, cmp_rev, pb->eb, pb, FALSE, pool);
	*child_baton = new_db;
	return SVN_NO_ERROR;
}

static svn_error_t *close_directory(void *dir_baton,
				    apr_pool_t *pool)
{
	struct dir_baton *db = dir_baton;
	struct dump_edit_baton *eb = db->eb;
	apr_hash_index_t *hi;
	apr_pool_t *subpool = svn_pool_create(pool);

	/* Some pending properties to dump? */
	SVN_ERR(dump_props(eb, &(eb->dump_props_pending), TRUE, pool));

	/* Dump the directory entries */
	for (hi = apr_hash_first(pool, db->deleted_entries); hi;
	     hi = apr_hash_next(hi)) {
		const void *key;
		const char *path;
		apr_hash_this(hi, &key, NULL, NULL);
		path = key;

		svn_pool_clear(subpool);

		SVN_ERR(dump_node(db->eb, path,
		                  svn_node_unknown, svn_node_action_delete,
		                  NULL, SVN_INVALID_REVNUM, subpool));
	}

	svn_pool_destroy(subpool);
	return SVN_NO_ERROR;
}

static svn_error_t *add_file(const char *path,
			     void *parent_baton,
			     const char *copyfrom_path,
			     svn_revnum_t copyfrom_rev,
			     apr_pool_t *pool,
			     void **file_baton)
{
	struct dir_baton *pb = parent_baton;
	void *val;

	/* Some pending properties to dump? */
	SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

	/* This might be a replacement -- is the path already deleted? */
	val = apr_hash_get(pb->deleted_entries, path, APR_HASH_KEY_STRING);

	/* Detect add-with-history. */
	pb->eb->is_copy = ARE_VALID_COPY_ARGS(copyfrom_path, copyfrom_rev);

	/* Dump the node. */
	SVN_ERR(dump_node(pb->eb, path,
	                  svn_node_file,
	                  val ? svn_node_action_replace : svn_node_action_add,
	                  pb->eb->is_copy ? copyfrom_path : NULL,
	                  pb->eb->is_copy ? copyfrom_rev : SVN_INVALID_REVNUM,
	                  pool));

	if (val)
		/* delete the path, it's now been dumped. */
		apr_hash_set(pb->deleted_entries, path, APR_HASH_KEY_STRING, NULL);

	/* Build a nice file baton to pass to change_file_prop and apply_textdelta */
	pb->eb->changed_path = path;
	*file_baton = pb->eb;

	return SVN_NO_ERROR;
}

static svn_error_t *open_file(const char *path,
			      void *parent_baton,
			      svn_revnum_t ancestor_revision,
			      apr_pool_t *pool,
			      void **file_baton)
{
	struct dir_baton *pb = parent_baton;
	const char *cmp_path = NULL;
	svn_revnum_t cmp_rev = SVN_INVALID_REVNUM;

	/* Some pending properties to dump? */
	SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

	apr_array_header_t *compose_path = apr_array_make(pool, 2, sizeof(const char *));
	/* If the parent directory has explicit comparison path and rev,
	   record the same for this one. */
	if (pb && ARE_VALID_COPY_ARGS(pb->cmp_path, pb->cmp_rev)) {
		APR_ARRAY_PUSH(compose_path, const char *) = pb->cmp_path;
		APR_ARRAY_PUSH(compose_path, const char *) = svn_relpath_basename(path, pool);
		cmp_path = svn_path_compose(compose_path, pool);
		cmp_rev = pb->cmp_rev;
	}

	SVN_ERR(dump_node(pb->eb, path,
	                  svn_node_file, svn_node_action_change,
	                  cmp_path, cmp_rev, pool));

	/* Build a nice file baton to pass to change_file_prop and apply_textdelta */
	pb->eb->changed_path = path;
	*file_baton = pb->eb;

	return SVN_NO_ERROR;
}

static svn_error_t *change_dir_prop(void *parent_baton,
				    const char *name,
				    const svn_string_t *value,
				    apr_pool_t *pool)
{
	struct dir_baton *db = parent_baton;

	if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
		return SVN_NO_ERROR;

	value ? apr_hash_set(db->eb->properties, apr_pstrdup(pool, name),
	                     APR_HASH_KEY_STRING, svn_string_dup(value, pool)) :
		apr_hash_set(db->eb->del_properties, apr_pstrdup(pool, name),
		             APR_HASH_KEY_STRING, (void *)0x1);

	/* This function is what distinguishes between a directory that is
	   opened to merely get somewhere, vs. one that is opened because it
	   actually changed by itself  */
	if (! db->written_out) {
		/* If eb->dump_props_pending was set, it means that the
		   node information corresponding to add_directory has already
		   been written; just don't unset it and dump_node will dump
		   the properties before doing anything else. If it wasn't
		   set, node information hasn't been written yet: so dump the
		   node itself before dumping the props */

		SVN_ERR(dump_node(db->eb, db->path,
		                  svn_node_dir, svn_node_action_change,
		                  db->cmp_path, db->cmp_rev, pool));

		SVN_ERR(dump_props(db->eb, NULL, TRUE, pool));
		db->written_out = TRUE;
	}
	return SVN_NO_ERROR;
}

static svn_error_t *change_file_prop(void *file_baton,
				     const char *name,
				     const svn_string_t *value,
				     apr_pool_t *pool)
{
	struct dump_edit_baton *eb = file_baton;

	if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
		return SVN_NO_ERROR;

	apr_hash_set(eb->properties, apr_pstrdup(pool, name),
		     APR_HASH_KEY_STRING, value ?
		     svn_string_dup(value, pool): (void *)0x1);

	/* Dump the property headers and wait; close_file might need
	   to write text headers too depending on whether
	   apply_textdelta is called */
	eb->dump_props_pending = TRUE;

	return SVN_NO_ERROR;
}

static svn_error_t *window_handler(svn_txdelta_window_t *window, void *baton)
{
	struct handler_baton *hb = baton;
	struct dump_edit_baton *eb = hb->eb;
	static svn_error_t *err;

	err = hb->apply_handler(window, hb->apply_baton);
	if (window != NULL && !err)
		return SVN_NO_ERROR;

	if (err)
		SVN_ERR(err);

	/* Write information about the filepath to hb->eb */
	eb->temp_filepath = apr_pstrdup(eb->pool,
					hb->temp_filepath);

	/* Cleanup */
	SVN_ERR(svn_io_file_close(hb->temp_file, hb->pool));
	SVN_ERR(svn_stream_close(hb->temp_filestream));
	svn_pool_destroy(hb->pool);
	return SVN_NO_ERROR;
}

static svn_error_t *apply_textdelta(void *file_baton, const char *base_checksum,
				    apr_pool_t *pool,
				    svn_txdelta_window_handler_t *handler,
				    void **handler_baton)
{
	struct dump_edit_baton *eb = file_baton;
	apr_status_t apr_err;
	const char *tempdir;

	/* Custom handler_baton allocated in a separate pool */
	apr_pool_t *handler_pool = svn_pool_create(pool);
	struct handler_baton *hb = apr_pcalloc(handler_pool, sizeof(*hb));
	hb->pool = handler_pool;
	hb->eb = eb;

	/* Use a temporary file to measure the text-content-length */
	SVN_ERR(svn_io_temp_dir(&tempdir, hb->pool));

	hb->temp_filepath = svn_dirent_join(tempdir, "XXXXXX", hb->pool);
	apr_err = apr_file_mktemp(&(hb->temp_file), hb->temp_filepath,
				  APR_CREATE | APR_READ | APR_WRITE | APR_EXCL,
				  hb->pool);
	if (apr_err != APR_SUCCESS)
		SVN_ERR(svn_error_wrap_apr(apr_err, NULL));

	hb->temp_filestream = svn_stream_from_aprfile2(hb->temp_file, TRUE, hb->pool);

	/* Prepare to write the delta to the temporary file */
	svn_txdelta_to_svndiff2(&(hb->apply_handler), &(hb->apply_baton),
	                        hb->temp_filestream, 0, hb->pool);
	eb->must_dump_text = TRUE;

	/* The actual writing takes place when this function has finished */
	/* Set the handler and handler_baton */
	*handler = window_handler;
	*handler_baton = hb;

	return SVN_NO_ERROR;
}

static svn_error_t *close_file(void *file_baton,
			       const char *text_checksum,
			       apr_pool_t *pool)
{
	struct dump_edit_baton *eb = file_baton;
	apr_file_t *temp_file;
	svn_stream_t *temp_filestream;
	apr_finfo_t *info = apr_pcalloc(pool, sizeof(apr_finfo_t));

	/* We didn't write the property headers because we were
	   waiting for file_prop_change; write them now */
	SVN_ERR(dump_props(eb, &(eb->dump_props_pending), FALSE, pool));

	/* The prop headers have already been dumped in dump_node */
	/* Dump the text headers */
	if (eb->must_dump_text) {
		/* text-delta header */
		SVN_ERR(svn_stream_printf(eb->stream, pool,
					  SVN_REPOS_DUMPFILE_TEXT_DELTA
					  ": true\n"));

		/* Measure the length */
		SVN_ERR(svn_io_stat(info, eb->temp_filepath, APR_FINFO_SIZE, pool));

		/* text-content-length header */
		SVN_ERR(svn_stream_printf(eb->stream, pool,
					  SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH
					  ": %lu\n",
					  (unsigned long)info->size));
		/* text-content-md5 header */
		SVN_ERR(svn_stream_printf(eb->stream, pool,
					  SVN_REPOS_DUMPFILE_TEXT_CONTENT_MD5
					  ": %s\n",
					  text_checksum));
	}

	/* content-length header: if both text and props are absent,
	   skip this block */
	if (eb->must_dump_props || eb->dump_props_pending)
		SVN_ERR(svn_stream_printf(eb->stream, pool,
					  SVN_REPOS_DUMPFILE_CONTENT_LENGTH
					  ": %ld\n\n",
					  (unsigned long)info->size + eb->propstring->len));
	else if (eb->must_dump_text)
		SVN_ERR(svn_stream_printf(eb->stream, pool,
					  SVN_REPOS_DUMPFILE_CONTENT_LENGTH
					  ": %ld\n\n",
					  (unsigned long)info->size));

	/* Dump the props; the propstring should have already been
	   written in dump_node or above */
	if (eb->must_dump_props || eb->dump_props_pending) {
		SVN_ERR(svn_stream_write(eb->stream, eb->propstring->data,
					 &(eb->propstring->len)));

		/* Cleanup */
		eb->must_dump_props = eb->dump_props_pending = FALSE;
		apr_hash_clear(eb->properties);
		apr_hash_clear(eb->del_properties);
	}

	/* Dump the text */
	if (eb->must_dump_text) {

		/* Open the temporary file, map it to a stream, copy
		   the stream to eb->stream, close and delete the
		   file */
		SVN_ERR(svn_io_file_open(&temp_file, eb->temp_filepath, APR_READ, 0600, pool));
		temp_filestream = svn_stream_from_aprfile2(temp_file, TRUE, pool);
		SVN_ERR(svn_stream_copy3(temp_filestream, eb->stream, NULL, NULL, pool));

		/* Cleanup */
		SVN_ERR(svn_io_file_close(temp_file, pool));
		SVN_ERR(svn_stream_close(temp_filestream));
		SVN_ERR(svn_io_remove_file2(eb->temp_filepath, TRUE, pool));
		eb->must_dump_text = FALSE;
	}

	SVN_ERR(svn_stream_printf(eb->stream, pool, "\n\n"));

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