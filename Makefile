svnrdump: *.c
	$(CC) -Wall -Werror -DAPR_POOL_DEBUG -ggdb3 -O0 -o $@ svnrdump.c -lsvn_client-1 -I. -I/usr/include/subversion-1 -I/usr/include/apr-1.0

svnrdump_bench: *.c
	$(CC) -O2 -o $@ svnrdump.c -lsvn_client-1 -I. -I/usr/include/subversion-1 -I/usr/include/apr-1.0

clean:
	$(RM) svnrdump svnrdump_bench
