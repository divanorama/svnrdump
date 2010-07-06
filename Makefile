svnrdump: *.c *.h
	$(CC) -Wall -Werror -DAPR_POOL_DEBUG -ggdb3 -O0 -o $@ svnrdump.c debug_editor.c dump_editor.c dumpr_util.c -lsvn_client-1 -I. -I/usr/include/subversion-1 -I/usr/include/apr-1.0

svnrdump_bench: *.c *.h
	$(CC) -O2 -o $@ svnrdump.c debug_editor.c dump_editor.c dumpr_util.c -lsvn_client-1 -I. -I/usr/include/subversion-1 -I/usr/include/apr-1.0

clean:
	$(RM) svnrdump svnrdump_bench
