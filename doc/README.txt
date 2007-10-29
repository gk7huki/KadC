Welcome to KadC, a C library for publishing and retrieving records
in Kademlia-based Distributed Hash Tables. Possible uses include 
publishing a client's IP address for other peers to connect to (e.g.,
Internet phones, serverless IM programs); replacements for DNS; search
engine for BitTorrent clients; replacements for LDAP directories; etc.
For other ideas, see my postings at 
http://zgp.org/pipermail/p2p-hackers/2004-March/001780.html
http://lists.virus.org/cryptography-0405/msg00039.html

A test main is provided to exercise the main functions through console I/O.
To start playing as soon as possible, please read Quickstart.txt, which also
gives an idea of the structure of the (meta)data that may be published and
retrieved.

PLATFORMS

KadC requires zlib (www.zlib.org) and a POSIX threads package. This may
be replaced by the GNU Pth package (http://www.ossp.org/pkg/lib/pth/ ) 
built with --enable-pthread passed to ./configure. The option
--enable-syscall-hard is NOT required by the library, but it may be 
by the end user's calling program if the latter contains blocking
I/O calls (see Pth's docs for the details). Also, the Makefile
may need minor changes to add to a -I option for the directory 
containing Pth's pthread.h, a -L option for the directory containing 
Pth's libpthread.a, and reference the directory containing 
libpthread.so so that the loader knows where to find it at
runtime (a simple way is to listing it in /etc/ld.so.conf and 
run ldconfig).

KadC has been successfully built and run on the following platforms:

- Windows:
Cygwin tools, either in POSIX or WIN32 (MinGW) mode. In the second case,
the Pthreads package from http://sources.redhat.com/pthreads-win32/
needs to be installed (see the Makefile for the details). The commands
to build library (KadC.a) and test executable are:
 POSIX mode:	make MAIN=KadC
 WIN32 mode:	make MAIN=KadC WIN32=1

- Linux RedHat 7.0, either with Native pthreads or GNU Pth 2.0.1, with
 make MAIN=KadC
 
- NetBSD with GNU Pth
 make MAIN=KadC
 
- MacOS X
 make MAIN=KadC

If you succeed on other platforms, or do not, please let me know. My
e-mail addresses are in the header of all the source files.

