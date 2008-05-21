/*
 *  kadc.h
 *  KadC
 *
 *  Created by Etienne on 16/01/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#ifdef __WIN32__
#include <windows.h>
#include <winsock.h>
#define socklen_t int
#else
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#define min( a, b ) ( a < b ? a : b )
#endif

#include <event2/event.h>
#include <event2/bufferevent.h>

#include "logging.h"
#include "utils.h"
#include "hash.h"
#include "bufio.h"
#include "queue.h"
#include "rbt.h"
#include "contact.h"
#include "inifiles.h"
#include "message.h"
#include "session.h"
#include "dht.h"
#include "net.h"