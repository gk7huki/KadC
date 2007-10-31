/****************************************************************\

Copyright 2004-2006 Enzo Michelangeli, Arto Jalkanen

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

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <Debug_pthreads.h>
#include <stdarg.h>
#include <time.h>

#include "int128.h"
#include "logging.h"

static FILE *logf = NULL;
static pthread_mutex_t console_io_mutex = PTHREAD_MUTEX_INITIALIZER;

FILE *
kc_logOpen( char *filename )
{
	FILE *f;
	f = fopen( filename, "ab" );
	kc_logSetFile( f );
	return f;
}

void
kc_logSetFile( FILE *f )
{
	logf = f;
}

#if 0
void
KadC_flog( kc_logLevel lvl, FILE *f, const char *fmt, ... )
{
	va_list ap;
    
	va_start(ap, fmt);

	pthread_mutex_lock( &console_io_mutex );
	
    vfprintf( f, fmt, ap );
    
	if( logf == NULL || *logf == stdout )
		fflush( stdout );
    
	pthread_mutex_unlock( &console_io_mutex );
    
	va_end( ap );
}
#endif

void
kc_log( kc_logLevel lvl, const char * fmt, va_list ap, int stamp )
{
    char  * dbgMsg;
    char  * dbgLvl;
    vasprintf( &dbgMsg, fmt, ap );
    
    switch ( lvl )
    {
        case KADC_LOG_VERBOSE:
#ifdef VERBOSEDEBUG
            dbgLvl = "(VERBOSEDEBUG): ";
#endif
        case KADC_LOG_DEBUG:
#ifdef DEBUG
            dbgLvl = "(DEBUG): ";
#endif
            break;
            
        case KADC_LOG_NORMAL:
            dbgLvl = "";
            break;
            
        case KADC_LOG_ALERT:
            dbgLvl = "!!!ALERT!!! ";
            break;
            
        default:
            break;
    }
	pthread_mutex_lock( &console_io_mutex );
    if( logf == NULL )
        logf = stdout;
    if ( stamp == 1 )
    {
        time_t now = time( NULL );
        fprintf( logf, "%s%s: %s\n", dbgLvl, ctime(&now), dbgMsg );
    }
    else
        fprintf( logf, "%s%s\n", dbgLvl, dbgMsg );
	
    if ( logf != stdout )
        fflush( logf );
	pthread_mutex_unlock( &console_io_mutex );
    
    free( dbgMsg );
}

void
kc_logPrint( kc_logLevel lvl, const char *fmt, ...)
{
	va_list ap;
    
	va_start( ap, fmt );

    kc_log( lvl, fmt, ap, 0 );
    
	va_end( ap );
}

void
kc_logTime( kc_logLevel lvl, const char *fmt, ... )
{
	va_list ap;
    
	va_start( ap, fmt );

    kc_log( lvl, fmt, ap, 1 );

	va_end( ap );
}

char *KadC_getsn(char *s, int size) {
	int n;

	n = read(0, s, size-2);
	if(n<=0) {
		s = NULL;
#ifdef __WIN32__
		printf("\n");	/* don't ask me... in MinGW mode, first output line after EOF on input is lost */
#endif
	}
	else
		s[n] = 0;

	return s;
}

#if 0
void
KadC_int128flog( FILE *f, int128 i128 )
{
	int i;
	if( i128 == NULL )
		KadC_flog( f, "(NULL)" );
	else
		for( i=0; i<16; i++ )
			KadC_flog( f, "%02x", i128[i] );
}

void
KadC_int128log( int128 i128 )
{
	int i;
	if( i128 == NULL )
		kc_logPrint( "(NULL)" );
	else
		for( i=0; i<16; i++ )
			kc_logPrint( "%02x", i128[i] );
}
#endif
