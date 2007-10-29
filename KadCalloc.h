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

void *KadC_realloc(void *p, size_t size, char *sf, int ln);
void *KadC_malloc(size_t size, char *sf, int ln);
void KadC_free(void *p, char *sf, int ln);
void *KadC_calloc(size_t nelem, size_t elsize, char *sf, int ln);
char *KadC_strdup(char *s, char *sf, int ln);
void KadC_list_outstanding_mallocs(int maxentries);

#ifndef __KADC_DO_NOT_REDEFINE_ALLOC_MACROS__
#undef realloc
#define realloc(a,b) KadC_realloc((a),(b), __FILE__, __LINE__)
#undef malloc
#define malloc(a) KadC_malloc((a), __FILE__, __LINE__)
#undef free
#define free(a) KadC_free((a), __FILE__, __LINE__)
#undef calloc
#define calloc(a,b) KadC_calloc((a),(b), __FILE__, __LINE__)
#undef strdup
#define strdup(s) KadC_strdup(s, __FILE__, __LINE__)
#endif
