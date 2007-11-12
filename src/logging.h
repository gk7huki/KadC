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
#ifndef KADC_KADCLOG_H
#define KADC_KADCLOG_H

/** @file logging.h
 * This files provide a logging facility to KadC.
 *
 * This file implements a mutex-protected output facility,
 * with an ability to switch the output log file.
 */
#include <int128.h>

/** 
 * A message's log level.
 */
typedef enum {
    KADC_LOG_VERBOSE,
    KADC_LOG_DEBUG,
    KADC_LOG_NORMAL,
    KADC_LOG_ALERT
} kc_logLevel;

/** 
 * Opens a file from name for logging purposes.
 *
 * This function opens filename in append mode and uses it as logfile
 * (internally calls kc_logSetFile()).
 *
 * @param filename The name of the file to use as logfile.
 * @return A pointer to a FILE.
 */
FILE *
kc_logOpen( char *filename );

/** 
 * Sets the logging facility output to a specific file.
 *
 * This function sets the FILE* used by the logging functions below to
 * the f parameter. If not set here or by kc_logOpen(), logging output defaults to stdout.
 *
 * @param f A pointer to an open()ed FILE.
 */
void
kc_logSetFile( FILE *f );

/** 
 * Print a message to a specific FILE.
 *
 * This function is equivalent to kc_logPrint, except it allows to specify another FILE as output.
 *
 * @see kc_logPrint().
 */
void
kc_logFile( FILE *f, kc_logLevel lvl, const char *fmt, ... );

/** 
 * Print a message to the logging output.
 *
 * This function print the printf()-compatible fmt with the provided varargs to the log file.
 *
 * @see kc_logTime().
 * @param lvl The level of the message.
 * @param fmt The format string of the message.
 * @param ... The varargs specified in fmt.
 */
void
kc_logPrint( kc_logLevel lvl, const char *fmt, ... );

/** Print a message to the logging output, with a timestamp.
 *
 * This function does the same thing that kc_logPrint, except it prepends it with a timestamp.
 * @see kc_logPrint().
 */
void
kc_logTime( kc_logLevel lvl, const char *fmt, ... );

#if UNUSED
/* like fgets(s, size, stdin) but mutex-locking with log output */
char *
KadC_getsn(char *s, int size);

/* like fprintf for int128 data types in %32x format and mutex-locking with log output */
#if 0
void KadC_int128flog(FILE *f, int128 i128);
void KadC_int128log(int128 i128);
#endif /* 0 */
#endif /* UNUSED */

#endif /* KADC_KADCLOG_H */