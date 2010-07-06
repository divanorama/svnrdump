#ifndef DUMP_EDITOR_H_
#define DUMP_EDITOR_H_

svn_error_t *get_dump_editor(const svn_delta_editor_t **editor,
			     void **edit_baton,
			     svn_revnum_t to_rev,
			     apr_pool_t *pool);
#endif
