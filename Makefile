#-----------------------------------------------------------------------------
# makefile for *NIX/cygwin/mingw native compile for MS Windows
# ** REQUIRES GNU MAKE **
#
# To create objects and library ($(PROJ).a, where PROJ is the name of 
# the current directory containing all .c, .h and the resulting 
# object files), use "make"
# Each module may have a test main() to exercise its functions
# in a console application. In that case, the main must be in
# a file $(module)main.c in the subdirectory ../main ; 
# To create a test executable for a specific module, use:
#  make MAIN=module
# e.g. "make MAIN=MD4" compiles main/MD4main.c, links it 
# with the library, and creates the executable main/MD4
# "make clean" will remove all the *.o and the library $(PROJ).a, all
# the *stackdump files, and will strip all the executables in
# the main/ subdir, then will invoke tar to create an archive
# of the $(PROJ) directory in the parent directory. The archive's
# filename will be the concatenation of $(PROJ) and the current date.
#
# Uncommenting the line "WIN32 = YES" (or defining WIN32 on make's
# command line, e.g. with:
#  make WIN32=Y
# will perform compilations and optional linking in MingW mode.
# Some magic involving the file ./WIN32 forces a recompilation of
# all object files after a change of mode, in order to ensure
# that all the objects present after any run were compiled in 
# the same mode.
#
# Instead of kernel-based POSIX threads, it is possible to use
# GNU Pth in POSIX emulation mode. In that case, if Pth is installed
# in nonstandard locations, please make sure to add a -I option 
# for the directory containing Pth's pthread.h, a
# -L option for the diregtory containing Pth's libpthread.a, and
# have the directory containing libpthread.so in the loader's path
# (e.g., by listing it in /etc/ld.so.conf and running ldconfig)
# or change the makefile to link statically, if you wish.
# 
#
# Project started: EM 28 Apr 2004
#
#-----------------------------------------------------------------------------

# uncomment one of the following debug options
# optimisation (-O3) or debugging symbols (-g)
#DEBUG = -g
#PROF = -pg
DEBUG = -O3
PROF =

# enable MinGW mode
#WIN32 = YES

# MinGW cross-compile under linux
ifdef WIN32
PROGSPREFIX = i686-w64-mingw32-
INCPATH =
PLATFORM = win32
else
PROGSPREFIX =
INCPATH =
PLATFORM = linux
endif

####### No user configurable parameters below this line #######

#for Cygwin/mingw under win32
#PROGSPREFIX=
#INCPATH=

#uncomment the following for mingw32 cross-compile under linux (UNTESTED!)
#PROGSPREFIX=i586-mingw32msvc-
#INCPATH=-I/usr/i586-mingw32msvc/include/

#Installation notes for third-party libraries using Cygwin tools on Windows

# Normal Cygwin:
#
#  Pthreads: standard with Cygwin
#    libpthread.a     in \cygwin\lib
#    pthread.h        in \cygwin\usr\include
#
#  Zlib: standard with Cygwin:
#    libz.a           in \cygwin\lib
#    zlib.h           in \cygwin\usr\include

# Compilation for Win32 (-mno-cygwin)
#
#  Pthreads (http://sources.redhat.com/pthreads-win32/ ):
#    libpthreadGC.a   in \cygwin\lib\mingw
#    pthread.h        in \cygwin\usr\include\mingw
#    sched.h          in \cygwin\usr\include\mingw 
#   At runtime:
#    pthreadGC.DLL    in \WINDOWS (or anyway in the path) 
#
#  Zlib: standard with Cygwin:
#    libz.a           in \cygwin\lib\mingw
#    zlib.h           in \cygwin\usr\include\mingw
#   At runtime:
#    mgwz.dll         in \WINDOWS (or in anyway the path) 
#
###################################################

AR = $(PROGSPREFIX)ar
ARFLAGS = cru
RANLIB = $(PROGSPREFIX)ranlib
CC = $(PROGSPREFIX)gcc

INCLUDE_DIRS = -I.
CFLAGS = $(DEBUG) $(PROF) $(DEFINES) -D_REENTRANT -m32 -fno-pie -std=gnu90 -Wall $(INCLUDE_DIRS)
LDFLAGS = -m32 -no-pie
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))
TESTOBJ = main/$(MAIN)main.o
TESTBIN = main/$(MAIN)
PROJ := $(shell basename $$(pwd))
TODAY := $(shell date +%d%b%y)
LIB = lib$(PROJ).a
TARGET = build/$(PLATFORM)

.PHONY: all
.PHONY: detectwin32
ifndef MAIN
all: detectwin32 $(LIB) 
else
all: detectwin32 $(TESTBIN)
endif

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $(TARGET)/$@ $(addprefix $(TARGET)/,$(OBJS))
	$(RANLIB) $(TARGET)/$@

$(OBJS): %.o: %.c WIN32 config.h
	$(CC) -c $(CFLAGS) $< -o $(TARGET)/$@

$(TESTBIN): $(TESTOBJ) $(LIB) config.h
	$(CC) $(PROF) -o main/$(MAIN) $(TESTOBJ) $(LIB) $(LIBS) $(LDFLAGS)

$(TESTOBJ): %.o: %.c WIN32 config.h
	$(CC) -c $(CFLAGS) $< -o $@
	
config.h: WIN32
	./tinyconfig

.PHONY: clean
clean:
	rm -f *.o main/*main.o main/*.map $(LIB) *.stackdump config.h
	strip main/*.exe

.PHONY: tar
tar: clean 
	tar --exclude priv --exclude old -zcf ../$(PROJ)-$(TODAY).tgz -C .. $(PROJ)

ifndef WIN32 
LIBS = -lpthread -lz
detectwin32:
	@if [ ! -s WIN32 ] || [ `cat WIN32`  = 'YES' ]; then  echo 'NO' >WIN32; rm -f $(LIB); rm -f main/*main.o; fi 
else
LIBS = -lpthread -lwsock32 -lz
detectwin32:
	@if [ ! -s WIN32 ] || [ `cat WIN32` != 'YES' ]; then  echo 'YES' >WIN32; rm -f $(LIB); rm -f main/*main.o; fi 
endif
