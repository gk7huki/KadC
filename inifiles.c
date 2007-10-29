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

#include <stdio.h>
#include <string.h>
#include <inifiles.h>

char *trimfgets(char *line, int linesize, FILE *file) {
	char *p = fgets(line, linesize, file);
	if(p == NULL) {  /* EOF? */
		return NULL;
	}
	if((p = strrchr(line, '\n')) != NULL) *p = 0;
	if((p = strrchr(line, '\r')) != NULL) *p = 0;
	return line;
}

/* Scans file from the beginning for a [section]
   (the parameter MUST include the delimiting brackets).
   Returns -1 if no such section, or 0 if found
   in which case the file pointer is left at
   the beginning of the next line */
int findsection(FILE *file, const char *section) {
	char line[80];
	rewind(file);
	for(;;) {
		char *p = trimfgets(line, sizeof(line), file);
		if(p == NULL)
			return -1;	/* section not found */
		if(strcasecmp(section, p) == 0)
			break;		/* section found */
	}
	return 0;
}


/* Returns number of parameters in line,
   and the params themselves in pb[0]..pb[4]
   - If if finds EOF, returns -1
   - If it finds another INI section ("[...]") returns 0,
   the section name (within "[]") in par 0,
   and the file pointer just before it.
   In any case, file is left open.
 */

int parseline(const char *line, parblock pb) {
	int ntok = sscanf(line, "%40s%40s%40s%40s%40s",
		pb[0], pb[1], pb[2], pb[3], pb[4]);
	return ntok;
}

int startreplacesection(FILE *rfile, const char *section, FILE *wfile) {
	char line[80];
	int found = 0;

	rewind(rfile);
	rewind(wfile);
	for(;;) {
		char *p = trimfgets(line, sizeof(line), rfile);
		if(p == NULL) {
			found = 0;	/* section not found */
			break;
		}
		fprintf(wfile, "%s\n", p);	/* copy to wfile */
		if(strcasecmp(section, p) == 0) {
			found = 1;	/* section not found */
			break;
		}
	}
	/* now skip all lines in rfile until the new section */
	for(;;) {
		char *p;
		fpos_t pos;

		fgetpos(rfile, &pos);
		p = trimfgets(line, sizeof(line), rfile);
		if(p == NULL) {
			break;
		}
		if(strchr(p, '[') != NULL) {	/* if we just read new section start... */
			fsetpos(rfile, &pos);		/* ...then backspace on it */
			break;
		}
	}

	return found;
}

int endreplacesection(FILE *rfile, FILE *wfile) {
	char line[80];
	for(;;) {
		char *p = trimfgets(line, sizeof(line), rfile);
		if(p == NULL) {
			break;
		}
		fprintf(wfile, "%s\n", p);	/* copy to wfile */
	}
	return 0;
}

int tonextsection(FILE *file, char *section, int section_size) {
	for(;;) {
		char *p;
		p = trimfgets(section, section_size, file);
		if(p == NULL) {
			break;
		}
		if(strchr(p, '[') != NULL) {	/* if we just read new section start... */
			return 0;
		}
	}
	
	section[0] = 0;
	return -1;
}

int copyuntilnextsection(FILE *rfile, FILE *wfile) {
	char line[80];

	/* Copy all lines in rfile until new section found */
	for(;;) {
		char *p;
		fpos_t pos;

		fgetpos(rfile, &pos);
		p = trimfgets(line, sizeof(line), rfile);
		if(p == NULL) {
			break;
		}
		if(strchr(p, '[') != NULL) {	/* if we just read new section start... */
			fsetpos(rfile, &pos);		/* ...then backspace on it */
			return 0;
		}
		
		fprintf(wfile, "%s\n", p);	/* copy to wfile */		
	}
	/* Arrived at end */
	return 1;
}
