#!/bin/sh
usage(){
	sed 's/..//' <<USAGE
		Usage: validate.sh [--svnadmin-dump=<file>] [--svnrdump-dump=<file>]
		                   [--repos=<url-or-path>] [-r<revision>]
		                   [--ignore-existing-dump] [--make]
		                   [generate] [validate]
USAGE
}

svnadmin_dump=
svnrdump_dump=
repos_url=
repos_path=
end_rev=
do_make=
do_both=1
do_generate=1
do_validate=1
do_ignore_existing_svnadmin_dump=
while test "$#" -gt 0; do
	case "$1" in
	--svnadmin-dump=*)
		svnadmin_dump="${1#*=}"
		;;
	--svnrdump-dump=*)
		svnrdump_dump="${1#*=}"
		;;
	--ignore-existing|--ignore-existing-dump)
		do_ignore_existing_svnadmin_dump=1
		;;
	--repos=*)
		repos_url="${1#*=}"
		repos_protocol="${repos_url%://*}"
		repos_path="${repos_url#file://}"
		if test "$repos_protocol" = "$repos_path"; then
			repos_protocol=
			repos_url=
		elif test ! "$repos_protocol" = 'file'; then
			repos_path=
		fi
		;;
	-r*)
		end_rev="${1#-r}"
		;;
	--make)
		do_make=1
		;;
	generate)
		do_generate=1
		test -n "$do_both" && do_validate=
		do_both=
		;;
	validate)
		do_validate=1
		test -n "$do_both" && do_generate=
		do_both=
		;;
	-h|--help)
		usage
		exit
		;;
	*)
		echo "unknown option $1" >&2
		usage >&2
		exit 1
	esac
	shift
done

if test -z "$svnrdump_dump"; then
	if test -z "$repos_url"; then
		if test -n "$repos_path"; then
			repos_url="file://$(readlink -f "$repos_path")"
			if test $? -ne 0; then
				echo "error: unable to derive repos url from path" >&2
				echo "--svnrdump_dump=<file> or a local (file://) --repos=<url> is required" >&2
				exit 1
			fi
		else
				echo "--svnrdump_dump=<file> or a local (file://) --repos=<url> is required" >&2
				exit 1
		fi
	fi
fi

svnadmin_dump_cut="t/svnadmin-$end_rev.dump"
svnrdump_dump_cut="t/svnrdump-$end_rev.dump"
mkdir t 2>/dev/null

if test -z "$svnadmin_dump"; then
	if test -z "$repos_path"; then
		echo "--svnadmin_dump=<file> or a local (file://) --repos=<url> is required" >&2
		usage >&2
		exit 1
	fi

	svnadmin_dump="$svnadmin_dump_cut"
	if test -z "$do_ignore_existing_svnadmin_dump" && test -r "$svnadmin_dump"; then
		echo "Using existing $svnadmin_dump"
	else
		echo "Generating $svnadmin_dump ..."

		r=
		if test -n "$end_rev"; then
			r="-r0:$end_rev"
		else
			r="-r0:HEAD"
		fi

		svnadmin dump --deltas $r "$repos_path" > "$svnadmin_dump"
		if test $? -ne 0; then
			echo "error: failed to create canonical dump for comparison" >&2
			exit 1
		fi
	fi
else
	echo "Using specified $svnadmin_dump"
fi

if test -z "$svnrdump_dump"; then
	svnrdump_dump="$svnrdump_dump_cut"

	if test -n "$do_make"; then
		make svnrdump > /dev/null;
		if test $? -ne 0; then
			echo "error: Make failed. Check the program." >&2
			exit 1;
		fi
	fi

	echo "Generating $svnrdump_dump ..."

	r=
	test -n "$end_rev" && r="-r0:$end_rev"

	./svnrdump -v $r "$repos_url" > "$svnrdump_dump"
	if test $? -ne 0; then
		echo "error: failed to create dump for validation" >&2
		exit 1
	fi
else
	echo "Using specified $svnrdump_dump"
fi

cut_dump(){
	r="$1"
	test -z "$r" && r=-1

	gawk '
		BEGIN {
			max='"$r"'
			hit_max=0
		}
		/^Revision-number: [0-9][0-9]*$/ {
		rev=$2
			if (rev == max) {
				hit_max=1
			} else if ( hit_max ) {
				exit
			}
		}
		{ print $0 }
		END {
			if (max == -1 || hit_max) {
				exit 0
			}
			exit 1
		}'
}

if test ! "$svnadmin_dump" = "$svnadmin_dump_cut"; then
	cut_dump "$end_rev" <"$svnadmin_dump" >"$svnadmin_dump_cut"
	if test $? -ne 0; then
		echo "error: failed to cut canonical dump $svnadmin_dump for comparison" >&2
		exit 1
	fi
	echo "Successfully generated cut canonical dump $svnadmin_dump_cut for comparison" >&2
fi

echo "Comparing canonical and svnrdump-based dumps..."
diff -au "$svnadmin_dump_cut" "$svnrdump_dump" > t/dump-diff.error
gawk \
	'$0 !~ "Prop-delta: true|Text-delta-base-|sha1|Text-copy-source-|^-$" && $0 ~ "^+|^-" { print; }' \
	t/dump-diff.error >t/dump-diff-filtered.error;

if test -n "$do_generate"; then
	echo "Generating canonical import logs..."

	rm -rf t/repo;
	mkdir t/repo;
	svnadmin create t/repo;

	svnadmin load t/repo < "$svnadmin_dump_cut" \
	    1>"$svnadmin_dump_cut.import.log" 2>"$svnadmin_dump_cut.import.error";
	if test $? -ne 0; then
		echo "error: Load $end_rev failed. See $svnadmin_dump_cut.import.* for details" >&2
		exit 1
	fi
	echo "Successfully generated canonical repository for comparison."
fi

if test -n "$do_validate"; then
	echo "Generating svnrdump-based import logs..."

	log_prefix=t/svnrdump-$end_rev

	rm -rf t/repo;
	mkdir t/repo;
	svnadmin create t/repo;

	svnadmin load t/repo < "$svnrdump_dump" 1>"$log_prefix.import.log" 2>"$log_prefix.import.error";
	if test $? -ne 0; then
		echo "error: Load $end_rev failed. See $log_prefix.import.* for details" >&2
		exit 1
	fi
	echo "Successfully loaded svnrdump-based repository for validation"

	echo "Comparing canonical and svnrdump-based import logs..."
	diff -au "$svnadmin_dump_cut.import.log" "$log_prefix.import.log" > t/import-diff.error;
	if test $? -ne 0; then
		echo "Validation failed. See t/import-diff.error for details." >&2
		exit 1
	fi
	echo "Validation successful"
fi
