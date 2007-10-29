Contains some libraries that are needed under Windows to
use KadC. Depending on the compiling environment you might
have the necessary files already.

For zlib:
 mgwz.dll
 
For pthreads:
 pthread* sched.h

POSIX Threads for WIN32 package developed and made available
under LGPL by by RedHat (http://sources.redhat.com/pthreads-win32/ ).
Those files were copied from their official site:
ftp://sources.redhat.com/pub/pthreads-win32/pthreads-dll-2004-06-22/
Their sources, if desired, can be downloaded from:
ftp://sources.redhat.com/pub/pthreads-win32/pthreads-snap-2004-06-22.tar.gz

The precompiled WIN32 executables contained in this archives were
compiled against that version of the library; in order to be executed,
they need pthreadGC.dll in the Windows directory or anyway in the path.


