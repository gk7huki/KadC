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

typedef char parblock[5][80];
char *trimfgets(char *line, int linesize, FILE *file);
int findsection(FILE *file, const char *section);
int parseline(const char *line, parblock pb);
int startreplacesection(FILE *rfile, const char *section, FILE *wfile);
int endreplacesection(FILE *rfile, FILE *wfile);
int tonextsection(FILE *file, char *section, int section_size);
int copyuntilnextsection(FILE *rfile, FILE *wfile);
