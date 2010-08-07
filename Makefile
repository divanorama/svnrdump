CC=gcc
PYTHON=python
LIBTOOL=libtool
LTFLAGS=--tag=CC --silent
CFLAGS=-g -O2 -pthread -Wall -Werror=implicit-function-declaration
EXEEXT=
CPPFLAGS=-DLINUX=2 -D_REENTRANT -D_GNU_SOURCE -D_LARGEFILE64_SOURCE
LDFLAGS=
COMPILE=$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES)
LINK=$(LIBTOOL) $(LTFLAGS) --mode=link $(CC) $(CFLAGS)
LT_COMPILE=$(LIBTOOL) $(LTFLAGS) --mode=compile $(COMPILE) $(CFLAGS)

INCLUDES=-I/usr/include/subversion-1 -I/usr/include/apr-1.0
LIBS=-lsvn_client-1 -lsvn_ra-1 -lsvn_repos-1 -lsvn_delta-1 -lsvn_subr-1 -lapr-1
OBJECTS=dump_editor.lo load_editor.lo svnrdump.lo svn17_compat.lo

.SUFFIXES: .c .lo

svnrdump$(EXEEXT): $(OBJECTS)
	$(LINK) $(LDFLAGS) -o svnrdump$(EXEEXT) $(OBJECTS) $(LIBS)

.c.lo:
	$(LT_COMPILE) -o $@ -c $<

dump_editor.lo: dump_editor.c dump_editor.h svn17_compat.h
load_editor.lo: load_editor.c load_editor.h
svnrdump.lo: svnrdump.c dump_editor.h load_editor.h svn17_compat.h
svn17_compat.lo: svn17_compat.c svn17_compat.h

check: svnrdump$(EXEEXT) svnrdump_tests.py
	$(PYTHON) svnrdump_tests.py

clean:
	$(RM) svnrdump$(EXEEXT)
	$(RM) *.lo *.o
