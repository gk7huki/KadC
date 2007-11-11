/****************************************************************\

Copyright 2004,2006 Enzo Michelangeli, Arto Jalkanen

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
#ifndef KADC_INIFILES_H
#define KADC_INIFILES_H

#include <stdio.h>

typedef char parblock[5][80];
char *trimfgets(char *line, int linesize, FILE *file);

/* Scans file from the beginning for a [section] (the parameter MUST include
 * the delimiting brackets). Returns -1 if no such section, or 0 if found
 * in which case the file pointer is left at the beginning of the next line
 */
int findsection(FILE *file, const char *section);

/* Returns number of parameters in line, and the params themselves in pb[0]..pb[4]
 * - If if finds EOF, returns -1
 * - If it finds another INI section ("[...]") returns 0,
 * the section name (within "[]") in par 0, and the file pointer just before it.
 * In any case, file is left open.
 */
int parseline(const char *line, parblock pb);

int startreplacesection(FILE *rfile, const char *section, FILE *wfile);
int endreplacesection(FILE *rfile, FILE *wfile);
int tonextsection(FILE *file, char *section, int section_size);
int copyuntilnextsection(FILE *rfile, FILE *wfile);


int
kc_iniParseLocalSection( FILE * inifile, in_addr_t * addr, in_port_t * port );

int
kc_iniParseNodeSection( FILE * iniFile, const char * secName, in_addr_t ** nodeAddr, in_port_t ** nodePort, int * nodeCount );

#endif /* KADC_INIFILES_H */