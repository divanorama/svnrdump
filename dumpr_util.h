#ifndef DUMPR_UTIL_H_
#define DUMPR_UTIL_H_

struct replay_baton {
	const svn_delta_editor_t *editor;
	void *edit_baton;
};

struct dump_edit_baton {
	svn_stream_t *stream;
	svn_revnum_t current_rev;

	/* pool is for per-edit-session allocations */
	apr_pool_t *pool;

	/* Store the properties that changed */
	apr_hash_t *properties;
	apr_hash_t *del_properties; /* Value is always 0x1 */
	svn_stringbuf_t *propstring;

	/* Was a copy command issued? */
	svn_boolean_t is_copy;

	/* Path of changed file */
	const char *changed_path;

	/* Temporary file to write delta to along with its checksum */
	char *temp_filepath;
	svn_checksum_t *checksum;

	/* Flags to trigger dumping props and text */
	svn_boolean_t must_dump_props;
	svn_boolean_t must_dump_text;
	svn_boolean_t dump_props_pending;
};

struct dir_baton {
	struct dump_edit_baton *eb;
	struct dir_baton *parent_dir_baton;

	/* is this directory a new addition to this revision? */
	svn_boolean_t added;

	/* has this directory been written to the output stream? */
	svn_boolean_t written_out;

	/* the absolute path to this directory */
	const char *path;

	/* the comparison path and revision of this directory.  if both of
	   these are valid, use them as a source against which to compare
	   the directory instead of the default comparison source of PATH in
	   the previous revision. */
	const char *cmp_path;
	svn_revnum_t cmp_rev;

	/* hash of paths that need to be deleted, though some -might- be
	   replaced.  maps const char * paths to this dir_baton.  (they're
	   full paths, because that's what the editor driver gives us.  but
	   really, they're all within this directory.) */
	apr_hash_t *deleted_entries;

	/* pool to be used for deleting the hash items */
	apr_pool_t *pool;
};

void write_hash_to_stringbuf(apr_hash_t *properties,
			     svn_boolean_t deleted,
			     svn_stringbuf_t **strbuf,
			     apr_pool_t *pool);

svn_error_t *dump_props(struct dump_edit_baton *eb,
			svn_boolean_t *trigger_var,
			svn_boolean_t dump_data_too,
			apr_pool_t *pool);

#endif
