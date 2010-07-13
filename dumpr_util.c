/* Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_repos.h"

#include "dumpr_util.h"

void write_hash_to_stringbuf(apr_hash_t *properties,
			     svn_boolean_t deleted,
			     svn_stringbuf_t **strbuf,
			     apr_pool_t *pool)
{
	apr_hash_index_t *this;
	const void *key;
	void *val;
	apr_ssize_t keylen;
	svn_string_t *value;

	if (!deleted) {
		for (this = apr_hash_first(pool, properties); this;
		     this = apr_hash_next(this)) {
			/* Get this key and val. */
			apr_hash_this(this, &key, &keylen, &val);
			value = val;

			/* Output name length, then name. */
			svn_stringbuf_appendcstr(*strbuf,
						 apr_psprintf(pool, "K %" APR_SSIZE_T_FMT "\n",
							      keylen));

			svn_stringbuf_appendbytes(*strbuf, (const char *) key, keylen);
			svn_stringbuf_appendbytes(*strbuf, "\n", 1);

			/* Output value length, then value. */
			svn_stringbuf_appendcstr(*strbuf,
						 apr_psprintf(pool, "V %" APR_SIZE_T_FMT "\n",
							      value->len));

			svn_stringbuf_appendbytes(*strbuf, value->data, value->len);
			svn_stringbuf_appendbytes(*strbuf, "\n", 1);
		}
	}
	else {
		/* Output a "D " entry for each deleted property */
		for (this = apr_hash_first(pool, properties); this;
		     this = apr_hash_next(this)) {
			/* Get this key */
			apr_hash_this(this, &key, &keylen, NULL);

			/* Output name length, then name */
			svn_stringbuf_appendcstr(*strbuf,
						 apr_psprintf(pool, "D %" APR_SSIZE_T_FMT "\n",
							      keylen));

			svn_stringbuf_appendbytes(*strbuf, (const char *) key, keylen);
			svn_stringbuf_appendbytes(*strbuf, "\n", 1);
		}
	}
}

svn_error_t *dump_props(struct dump_edit_baton *eb,
			svn_boolean_t *trigger_var,
			svn_boolean_t dump_data_too,
			apr_pool_t *pool)
{
	if (trigger_var && !*trigger_var)
		return SVN_NO_ERROR;

	/* Build a propstring to print */
	svn_stringbuf_setempty(eb->propstring);
	write_hash_to_stringbuf(eb->properties,
				FALSE,
				&(eb->propstring), eb->pool);
	write_hash_to_stringbuf(eb->del_properties,
				TRUE,
				&(eb->propstring), eb->pool);
	svn_stringbuf_appendbytes(eb->propstring, "PROPS-END\n", 10);

	/* prop-delta header */
	SVN_ERR(svn_stream_printf(eb->stream, pool,
				  SVN_REPOS_DUMPFILE_PROP_DELTA
				  ": true\n"));

	/* prop-content-length header */
	SVN_ERR(svn_stream_printf(eb->stream, pool,
				  SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH
				  ": %" APR_SIZE_T_FMT "\n", eb->propstring->len));

	if (dump_data_too) {
		/* content-length header */
		SVN_ERR(svn_stream_printf(eb->stream, pool,
					  SVN_REPOS_DUMPFILE_CONTENT_LENGTH
					  ": %" APR_SIZE_T_FMT "\n\n",
					  eb->propstring->len));

		/* the properties themselves */
		SVN_ERR(svn_stream_write(eb->stream, eb->propstring->data,
					 &(eb->propstring->len)));

		/* Cleanup so that data is never dumped twice */
		apr_hash_clear(eb->properties);
		apr_hash_clear(eb->del_properties);
		if (trigger_var)
			*trigger_var = FALSE;
	}
	return SVN_NO_ERROR;
}
