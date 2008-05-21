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
    
	rewind( file );
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

kc_contact *
parseContact( parblock pb, int oldStyle )
{
    char * address;
    char * port;
    
    if( oldStyle )
    {
        address = pb[1];
        port = pb[2];
    }
    else
    {
        address = pb[0];
        port = pb[1];
    }
    
    return kc_contactInitFromChar( address, port );
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
kc_iniParseLocalSection( FILE * inifile, kc_contact ** contact, kc_hash ** hash )
{
    char line[132];
	parblock pb;
    int oldStyle = 0;
    assert( contact != NULL );
    
	/* Read local params from INI file */
	if( findsection( inifile, "[local]" ) != 0 )
    {
		kc_logDebug( "can't find [local] section in inifile" );
        
		return -1;
	}
    
    for( ; ; )
    {
        int npars;
        
        char *p = trimfgets( line, sizeof(line), inifile );
        if( p == NULL )
        {
            kc_logDebug( "Can't find data under [local] section of inifile" );
            
            return -2;  /* EOF */
        }
        
        npars = parseline( line, pb );
        if( npars < 1 || pb[0][0] == '#' )
            continue;	/* skip comments and blank lines */
        if(pb[0][0] == '[')
        {
            kc_logAlert( "Can't find data under [local] section of inifile" );
            
            return -2;		/* start of new section */
        }
        if( npars == 5 )
        {
            kc_logVerbose( "This looks like an old-style local list..." );
            oldStyle = 1;
        }
        else if( npars != 3 )
        {
            kc_logAlert( "Bad format for local node data: skipping..." );
            continue;
        }
        
        break;
    }
    
    /* Parse what we found... */
    if( hash != NULL )
        *hash = atohash( pb[0] );
    
    if( contact != NULL )
    {
        *contact = parseContact( pb, 1 );
    }
    
    return ( *contact != NULL );
}

kc_contact **
kc_iniParseNodeSection( FILE * iniFile, const char * secName, int * nodeCount )
{
    char line[132];
	parblock pb;
    int oldStyle = 0;
    
    assert( nodeCount != NULL );
    
    *nodeCount = 0;
    
	/* Read contacts from INI file */
	if( findsection( iniFile, secName ) != 0 )
    {
		kc_logError( "Can't find %s section in .ini file", secName );
		return NULL;
	}
    
    kc_contact **nodes = malloc( sizeof(kc_contact*) );
    *nodes = NULL;
    
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
            kc_logVerbose( "This looks like an old-style node list..." );
            oldStyle = 1;
        }
        else if( npars != 2 && oldStyle == 0 )
        {
            kc_logAlert( "Bad format for contact %d lines after %s: skipping...", *nodeCount, secName );
            continue;
        }
        
        kc_contact * contact = parseContact( pb, oldStyle );
        if( contact == NULL )
            continue;
        
        void *tmp;
        
        tmp = realloc( nodes, sizeof(kc_contact*) * ( (*nodeCount) + 1 ) );
        if( tmp == NULL )
        {
            kc_logError( "Failed realloc()ating nodes array !" );
            free( *nodes );
            free( nodes );
            return NULL;
        }
        nodes = tmp;
        
        nodes[*nodeCount] = contact;
        
        (*nodeCount)++;
    }
    
    if( *nodeCount == 0 )
    {
        kc_logError( "Can't find data under %s section of KadCmain.ini", secName );
        free( *nodes );
        free( nodes );
        return NULL;  /* EOF */
    }
    
    kc_logVerbose( "Read %d nodes from the %s section of KadCmain.ini", *nodeCount, secName );
    
    return nodes;
}

int
kc_iniParseCommand( FILE * commandFile, kc_dht * dht )
{
    assert( commandFile != NULL );
    assert( dht != NULL );
    
    printf( "(kadc) " );
    
    char command[256];
    char *test = trimfgets( command, 256, stdin );
    if( test == NULL )
        return -1;
    
    parblock args;
    memset( args, 0, sizeof(args) );
    int argCount = parseline( command, args );
    
    if( strcmp( args[0], "printState" ) == 0 )
        kc_dhtPrintState( dht );
    else if( strcmp( args[0], "printTree" ) == 0 )
        kc_dhtPrintTree( dht );
    else if( strcmp( args[0], "printKeys" ) == 0 )
        kc_dhtPrintKeys( dht );
    else if( strcmp( args[0], "addNode" ) == 0 )
    {
        if( argCount != 3 )
        {
            kc_logNormal("addNode requires 2 arguments" );
            kc_logNormal("addNode addr port" );
            return 0;
        }
        kc_contact * contact = kc_contactInitFromChar( args[1], args[2] );
        kc_dhtCreateNode( dht, contact );
    }
    else if( strcmp( args[0], "" ) == 0 )
    {
        // Do nothing
    }
    else
    {
        kc_logNormal( "unknown command: %s", args[0] );
    }
    return 0;
}
