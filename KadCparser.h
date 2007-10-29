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
/* possible status codes returned in the err field of a KadC_parsedfilter
   The concatenation of errmsg1 and errmsg2 gives more detail */
typedef enum {
	PARSING_OK,
	PARSING_LEXICAL_ERROR,
	PARSING_UNEXPECTED_INPUT,
	PARSING_UNEXPECTED_EOF,
	PARSING_MISSING_STRING_OPERAND,
	PARSING_MISSING_RIGHT_PAREN,
	PARSING_EXPECTED_RELEX_OR_PAREN_BOOLEX,
	PARSING_INVALID_STRING_TAGNAME,
	PARSING_EXPECTED_NUMERIC_OPERAND,
	PARSING_EXTRA_GARBAGE,
	PARSING_UNKNOWN_OPCODE_IN_PASS_2,
	PARSING_OUT_OF_MEMORY
} parsing_error;


typedef struct _KadC_parsedfilter {
	unsigned char *nsf;	/* the returned ns_filter or NULL if parsing unsuccessful */
	parsing_error err;			/* PARSING_OK (0) if parsing successful. In that case, ns must be freed later */
	const char *errmsg1;		/* string relevant to error, if errcode != PARSING_OK */
	const char *errmsg2;		/* string relevant to error, if errcode != PARSING_OK */
} KadC_parsedfilter;

/* smart replacement for make_nsfilter. Accepts infix notation expressions
   and returns in the field nsf a pointer to a malloc'd nsfilter
   (to be destroyed with free() after use).
   NOTE: returns by value a struct, NOT a pointer to it. The returned value
   needs not be free'd . */

KadC_parsedfilter KadC_parsefilter(char *stringex);

