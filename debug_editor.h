#ifndef DEBUG_EDITOR_H_
#define DEBUG_EDITOR_H_

svn_error_t *svn_delta__get_debug_editor(const svn_delta_editor_t **editor,
					 void **edit_baton,
					 const svn_delta_editor_t *wrapped_editor,
					 void *wrapped_edit_baton,
					 apr_pool_t *pool);

#endif
