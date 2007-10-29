/****************************************************************\

Copyright 2004 Enzo Michelangeli

This file is part of the KadC library.

KadC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

KadC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with KadC; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

In addition, closed-source licenses for this software may be granted
by the copyright owner on commercial basis, with conditions negotiated
case by case. Interested parties may contact Enzo Michelangeli at one
of the following e-mail addresses (replace "(at)" with "@"):

 em(at)em.no-ip.com
 em(at)i-t-vision.com

\****************************************************************/

/* opens filename in append mode and uses it as logfile
   (internally calls KadClog_setfile() */
FILE *KadClog_open(char *filename);

/* use as logfile the open stream pointed by the parameter
   if not set here or by KadClog_open(), logfile defaults to stdout */
void KadClog_setfile(FILE **f);
/* like fprintf(f, fmt, ...) but mutex-locking with log output */
void KadC_flog(FILE *f, const char *fmt, ...);
/* like fprintf(logfile, fmt, ...) but mutex-locking with log output */
void KadC_log(const char *fmt, ...);
/* as above, but line is prefixed by ctime(NULL) */
void KadC_logt(const char *fmt, ...);
/* like fgets(s, size, stdin) but mutex-locking with log output */
char *KadC_getsn(char *s, int size);
/* like fprintf for int128 data types in %32x format and mutex-locking with log output */
void KadC_int128flog(FILE *f, int128 i128);
void KadC_int128log(int128 i128);


