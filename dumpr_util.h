#ifndef DUMPR_UTIL_H_
#define DUMPR_UTIL_H_

struct dump_edit_baton {
	svn_stream_t *stream;
	svn_revnum_t current_rev;
};

#endif
