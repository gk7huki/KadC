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
	pthread_mutex_lock( &console_io_mutex );
	logf = f;
    pthread_mutex_unlock( &console_io_mutex );
}

void
kc_logFile( FILE *f, kc_logLevel lvl, const char *fmt, ... )
{
    assert( f == NULL );
    
	va_list ap;
    
	va_start(ap, fmt);

	pthread_mutex_lock( &console_io_mutex );
	
    vfprintf( f, fmt, ap );
    
	if( f != stdout )
		fflush( f );
    
	pthread_mutex_unlock( &console_io_mutex );
    
	va_end( ap );
}

void
kc_log( kc_logLevel lvl, const char * fmt, va_list ap, int stamp )
{
	pthread_mutex_lock( &console_io_mutex );
    char  * dbgMsg;
    char  * dbgLvl = "";
    
    vasprintf( &dbgMsg, fmt, ap );
    if( dbgMsg == NULL )
    {
        printf( "Failed allocating memory for message: %s", fmt );
        return;
    }
    
    /* FIXME: This will need a fix in non-debug mode */
    /* Maybe handle this as a global log level instead of a preprocessor macro... */    
    
    switch ( lvl )
    {
        case KADC_LOG_VERBOSE:

#ifdef VERBOSEDEBUG
            dbgLvl = "(VERBOSEDEBUG) ";
#endif
            break;
        case KADC_LOG_DEBUG:
#ifdef DEBUG
            dbgLvl = "(DEBUG) ";
#endif
            break;
            
        case KADC_LOG_NORMAL:
            dbgLvl = "";
            break;
            
        case KADC_LOG_ALERT:
            dbgLvl = "!ALERT! ";
            break;
            
        case KADC_LOG_ERROR:
            dbgLvl = "!!!ERROR!!! ";
            break;
            
        default:
            break;
    }
    if( logf == NULL )
        logf = stdout;
    if ( stamp == 1 )
    {
        time_t now = time( NULL );
        fprintf( logf, "%s%s: %s\n", dbgLvl, ctime( &now ), dbgMsg );
    }
    else
        fprintf( logf, "%s%s\n", dbgLvl, dbgMsg );
	
    if ( logf != stdout )
        fflush( logf );
    
    free( dbgMsg );
	pthread_mutex_unlock( &console_io_mutex );
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
KadC_int128flog( FILE *f, kc_hash i128 )
{
	int i;
	if( i128 == NULL )
		KadC_flog( f, "(NULL)" );
	else
		for( i=0; i<16; i++ )
			KadC_flog( f, "%02x", i128[i] );
}

void
KadC_int128log( kc_hash i128 )
{
	int i;
	if( i128 == NULL )
		kc_logPrint( "(NULL)" );
	else
		for( i=0; i<16; i++ )
			kc_logPrint( "%02x", i128[i] );
}
#endif
