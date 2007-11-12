/****************************************************************\

Copyright 2004, 2006 Enzo Michelangeli, Arto Jalkanen

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

/* primitives to read / save various parameters in INI file */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>

#include "net.h"
#include "logging.h"
#include "rbt.h"

#include "inifiles.h"

typedef char parblock[5][80];

char *
trimfgets( char *line, int linesize, FILE *file )
{
	char *p = fgets( line, linesize, file );
	if(p == NULL)
    {
        /* EOF? */
		return NULL;
	}
    
	if ( ( p = strrchr( line, '\n' ) ) != NULL ) *p = 0;
	if ( ( p = strrchr( line, '\r' ) ) != NULL ) *p = 0;
	return line;
}

/* Scans file from the beginning for a [section] (the parameter MUST include
 * the delimiting brackets). Returns -1 if no such section, or 0 if found
 * in which case the file pointer is left at the beginning of the next line
 */
int
findsection( FILE *file, const char *section )
{
	char line[80];
    
    assert( file != NULL );
    
	rewind(file);
	for ( ; ; )
    {
		char *p = trimfgets( line, sizeof(line), file );
		if ( p == NULL )
			return -1;	/* section not found */
		if( strcasecmp( section, p ) == 0 )
			break;		/* section found */
	}
	return 0;
}

int
parseline( const char *line, parblock pb )
{
	int ntok = sscanf( line, "%40s%40s%40s%40s%40s",
		pb[0], pb[1], pb[2], pb[3], pb[4] );
	return ntok;
}

/* Returns number of parameters in line, and the params themselves in pb[0]..pb[4]
 * - If if finds EOF, returns -1
 * - If it finds another INI section ("[...]") returns 0,
 * the section name (within "[]") in par 0, and the file pointer just before it.
 * In any case, file is left open.
 */
int
startreplacesection( FILE *rfile, const char *section, FILE *wfile )
{
	char line[80];
	int found = 0;

	rewind( rfile );
	rewind( wfile );
	for( ; ; )
    {
		char *p = trimfgets( line, sizeof(line), rfile );
		if ( p == NULL )
        {
			found = 0;	/* section not found */
			break;
		}
		fprintf( wfile, "%s\n", p );	/* copy to wfile */
		if ( strcasecmp( section, p ) == 0 )
        {
			found = 1;	/* section not found */
			break;
		}
	}
	/* now skip all lines in rfile until the new section */
	for( ; ; )
    {
		char *p;
		fpos_t pos;

		fgetpos( rfile, &pos );
		p = trimfgets( line, sizeof(line), rfile );
		if( p == NULL )
        {
			break;
		}
		if( strchr( p, '[' ) != NULL )
        {
            /* if we just read new section start... */
			fsetpos(rfile, &pos);		/* ...then backspace on it */
			break;
		}
	}

	return found;
}

int
endreplacesection( FILE *rfile, FILE *wfile )
{
	char line[80];
	for( ; ; )
    {
		char *p = trimfgets( line, sizeof(line), rfile );
		if ( p == NULL )
        {
			break;
		}
		fprintf( wfile, "%s\n", p );	/* copy to wfile */
	}
	return 0;
}

int
tonextsection( FILE *file, char *section, int section_size )
{
	for( ; ; )
    {
		char *p;
		p = trimfgets( section, section_size, file );
		if ( p == NULL )
        {
			break;
		}
		if ( strchr( p, '[' ) != NULL )
        {
            /* if we just read new section start... */
			return 0;
		}
	}
	
	section[0] = 0;
	return -1;
}

int
copyuntilnextsection( FILE *rfile, FILE *wfile )
{
	char line[80];

	/* Copy all lines in rfile until new section found */
	for( ; ; )
    {
		char *p;
		fpos_t pos;

		fgetpos( rfile, &pos );
		p = trimfgets( line, sizeof(line), rfile );
		if ( p == NULL )
        {
			break;
		}
		if ( strchr( p, '[' ) != NULL )
        {	/* if we just read new section start... */
			fsetpos( rfile, &pos );		/* ...then backspace on it */
			return 0;
		}
		
		fprintf( wfile, "%s\n", p );	/* copy to wfile */		
	}

	/* Arrived at end */
	return 1;
}

int
kc_iniParseLocalSection( FILE * inifile, in_addr_t * addr, in_port_t * port )
{
    char line[132];
	parblock pb;
    int oldStyle = 0;
    
    
	/* Read local params from INI file */
	if( findsection( inifile, "[local]" ) != 0 )
    {
		kc_logPrint( KADC_LOG_DEBUG, "can't find [local] section in inifile" );
        
		return -1;
	}
    
    for( ; ; )
    {
        int npars;
        
        char *p = trimfgets( line, sizeof(line), inifile );
        if( p == NULL )
        {
            kc_logPrint( KADC_LOG_DEBUG, "Can't find data under [local] section of inifile" );
            
            return -2;  /* EOF */
        }
        
        npars = parseline( line, pb );
        if( npars < 1 || pb[0][0] == '#' )
            continue;	/* skip comments and blank lines */
        if(pb[0][0] == '[')
        {
            kc_logPrint( KADC_LOG_DEBUG, "Can't find data under [local] section of inifile" );
            
            return -2;		/* start of new section */
        }
        if( npars == 5 )
        {
            kc_logPrint( KADC_LOG_VERBOSE, "This looks like an old-style local list..." );
            oldStyle = 1;
        }
        else if( npars != 2 )
        {
            kc_logPrint( KADC_LOG_DEBUG, "bBad format for local node data: skipping..." );
            continue;
        }
        
        break;
    }
    
    // Parse what we found...
    if( oldStyle )
    {
        if( addr != NULL )
            *addr = gethostbyname_s(pb[1]);
        
        if( port != NULL )
            *port = atoi(pb[2]);
    }
    else
    {
        if( addr != NULL )
            *addr = gethostbyname_s(pb[0]);
        
        if( port != NULL )
            *port = atoi(pb[1]);
    }
    
    return 0;
}

int
kc_iniParseNodeSection( FILE * iniFile, const char * secName, in_addr_t ** nodeAddr, in_port_t ** nodePort, int * nodeCount )
{
    char line[132];
	parblock pb;
    in_addr_t   * addrs = NULL;
    in_port_t   * ports = NULL;
    int oldStyle = 0;
    
    assert( nodeCount != NULL );
    assert( nodeAddr  != NULL );
    assert( nodePort  != NULL );
    
    *nodeCount = 0;
    
	/* Read contacts from INI file */
	if( findsection( iniFile, secName ) != 0 )
    {
		kc_logPrint(KADC_LOG_DEBUG, "Can't find %s section in KadCmain.ini", secName );
        
		return -1;
	}
    
    while( 1 )
    {
        int npars;
        
        char *p = trimfgets( line, sizeof(line), iniFile );
        
        if(p == NULL)
            break; /* EOF, we return */
        
        npars = parseline(line, pb);
        
        if( pb[0][0] == '[' )
            break;		/* start of new section, we return */
        
        if(pb[0][0] == '#')
            continue;	/* we found a comment, read next line */
        
        if( npars == 4 && oldStyle == 0 )
        {
            kc_logPrint( KADC_LOG_VERBOSE, "This looks like an old-style node list..." );
            oldStyle = 1;
        }
        else if( npars != 2 && oldStyle == 0 )
        {
            kc_logPrint( KADC_LOG_DEBUG, "Bad format for contact %d lines after %s: skipping...", *nodeCount, secName );
            
            continue;
        }
        
        if( addrs == NULL )
            addrs = malloc( sizeof(in_addr_t) );
        
        if( ports == NULL )
            ports = malloc( sizeof(in_port_t) );
        
        void *tmp;
        
        if( ( tmp = realloc( addrs, sizeof(in_addr_t) * ( (*nodeCount) + 1 ) ) ) == NULL )
        {
            free( addrs );
            kc_logPrint( KADC_LOG_ALERT, "Failed realloc()ating node array !" );
            return -3;
        }
        addrs = tmp;
        
        if( ( tmp = realloc( ports, sizeof(in_port_t) * ( (*nodeCount) + 1 ) ) ) == NULL )
        {
            free( ports );
            kc_logPrint( KADC_LOG_ALERT, "Failed realloc()ating node array !" );
            return -3;
        }
        ports = tmp;
        if( oldStyle )
        {
            addrs[*nodeCount] = gethostbyname_s( pb[1] );
            ports[*nodeCount] = atoi( pb[2] );

        }
        else
        {
            addrs[*nodeCount] = gethostbyname_s( pb[0] );
            ports[*nodeCount] = atoi( pb[1] );            
        }
        
        (*nodeCount)++;
    }

    if( *nodeCount == 0 )
    {
        kc_logPrint( KADC_LOG_DEBUG, "Can't find data under %s section of KadCmain.ini", secName );
        
        return -2;  /* EOF */
    }
    
    kc_logPrint( KADC_LOG_VERBOSE, "Read %d nodes from the %s section of KadCmain.ini", *nodeCount, secName );
    
    *nodeAddr = addrs;
    *nodePort = ports;
    
    return *nodeCount;
}