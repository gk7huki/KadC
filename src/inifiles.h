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
/** @file inifiles.h
 * This file provide an abstraction at reading config files.
 *
 */

/**
 * Get IP address and port number from a configuration file.
 *
 * This function is used for reading up a local DHT settings from a configuration file.
 * @param iniFile A opened file descriptor to the settings file.
 * @param addr A pointer to an in_addr_t that will contain the parsed IP address. Can be NULL.
 * @param port A pointer to an in_port_t that will contain the parsed port number. Can be NULL.
 * @return Returns 0 on success,
                  -1 if it fails to find a "[local]" section in the file,
                  -2 if it get a unexpected EOF.
 
 */
int
kc_iniParseLocalSection( FILE * iniFile, in_addr_t * addr, in_port_t * port );

/**
 * Get a list of nodes from a configuration file.
 *
 * This function is used for reading up a list of nodes from a configuration file.
 * @param iniFile A opened file descriptor to the settings file.
 * @param secName The name of the section to parse, including the square brackets.
 * @param nodeAddr A pointer to an array of in_addr_t. Can't be NULL.
 * @param nodePort A pointer to an array of in_port_t. Can't be NULL.
 * @param nodeCount The number of nodes found in the file. Can't be NULL.
 * @return Returns the number of node parsed on success, 
            -1 if it fails to find a secname section in the file,
            -2 if it get a unexpected EOF,
            -3 if there was an error realloc()ating the arrays.
 */
int
kc_iniParseNodeSection( FILE * iniFile, const char * secName, in_addr_t ** nodeAddr, in_port_t ** nodePort, int * nodeCount );

#endif /* KADC_INIFILES_H */