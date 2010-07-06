#ifndef DUMPR_UTIL_H_
#define DUMPR_UTIL_H_

struct replay_baton {
	const svn_delta_editor_t *editor;
	void *edit_baton;
};

struct dump_edit_baton {
	svn_stream_t *stream;
	svn_revnum_t current_rev;
};

#endif
