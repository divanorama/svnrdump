/* Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_repos.h"
#include "svn_path.h"

static int verbose = 0;
static apr_pool_t *pool = NULL;
static svn_client_ctx_t *ctx = NULL;
static svn_ra_session_t *session = NULL;

static svn_error_t *open_connection(const char *url)
{
	SVN_ERR(svn_config_ensure (NULL, pool));
	SVN_ERR(svn_client_create_context (&ctx, pool));
	SVN_ERR(svn_ra_initialize(pool));

	SVN_ERR(svn_config_get_config(&(ctx->config), NULL, pool));

	/* Default authentication providers for non-interactive use */
	SVN_ERR(svn_cmdline_create_auth_baton(&(ctx->auth_baton), TRUE,
					      NULL, NULL, NULL, FALSE,
					      FALSE, NULL, NULL, NULL,
					      pool));
	SVN_ERR(svn_client_open_ra_session(&session, url, ctx, pool));
	return SVN_NO_ERROR;
}

static svn_error_t *replay_range(svn_revnum_t start_revision, svn_revnum_t end_revision)
{
	return SVN_NO_ERROR;
}

static svn_error_t *usage(FILE *out_stream)
{
	fprintf(out_stream,
		"usage: svnrdump URL [-r LOWER[:UPPER]]\n\n"
		"Dump the contents of repository at remote URL to stdout in a 'dumpfile'\n"
		"v3 portable format.  Dump revisions LOWER rev through UPPER rev.\n"
		"LOWER defaults to 1 and UPPER defaults to the highest possible revision\n"
		"if omitted.\n");
	return SVN_NO_ERROR;
}


int main(int argc, const char **argv)
{
	int i;
	const char *url = NULL;
	char *revision_cut = NULL;
	svn_revnum_t start_revision = svn_opt_revision_unspecified;
	svn_revnum_t end_revision = svn_opt_revision_unspecified;

	if (svn_cmdline_init ("svnrdump", stderr) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	pool = svn_pool_create(NULL);

	for (i = 1; i < argc; i++) {
		if (!strncmp("-r", argv[i], 2)) {
			revision_cut = strchr(argv[i] + 2, ':');
			if (revision_cut) {
				start_revision = (svn_revnum_t) strtoul(argv[i] + 2, &revision_cut, 10);
				end_revision = (svn_revnum_t) strtoul(revision_cut + 1, NULL, 10);
			}
			else
				start_revision = (svn_revnum_t) strtoul(argv[i] + 2, NULL, 10);
		} else if (!strcmp("-v", argv[i]) || !strcmp("--verbose", argv[i])) {
			verbose = 1;
		} else if (!strcmp("help", argv[i]) || !strcmp("--help", argv[i])) {
			SVN_INT_ERR(usage(stdout));
			return EXIT_SUCCESS;
		} else if (*argv[i] == '-' || url) {
			SVN_INT_ERR(usage(stderr));
			return EXIT_FAILURE;
		} else
			url = argv[i];
	}

	if (!url || !svn_path_is_url(url)) {
		usage(stderr);
		return EXIT_FAILURE;
	}
	SVN_INT_ERR(open_connection(url));

	/* Have sane start_revision and end_revision defaults if unspecified */
	if (start_revision == svn_opt_revision_unspecified)
		start_revision = 1;
	if (end_revision == svn_opt_revision_unspecified)
		SVN_INT_ERR(svn_ra_get_latest_revnum(session, &end_revision, pool));

	SVN_INT_ERR(replay_range(start_revision, end_revision));

	svn_pool_destroy(pool);
	
	return 0;
}
