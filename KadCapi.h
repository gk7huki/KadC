/* Possible status values in KadCcontext.s */
typedef enum {
	KADC_OK,
	KADC_START_CANT_OPEN_INI_FILE,
	KADC_START_WSASTARTUP_ERROR,
	KADC_START_OVERNET_SECTION_INI_FILE_ERROR,
	KADC_START_EMULEKAD_SECTION_INI_FILE_ERROR,
	KADC_START_REVCONNECT_SECTION_INI_FILE_ERROR,
	KADC_START_BAD_LOCALNODE_ADDRESS,
	KADC_START_NO_MEMORY,
	KADC_START_UDPIO_FAILED,
	KADC_START_OVERNET_KADENGINE_FAILED,
	KADC_START_EMULEKAD_KADENGINE_FAILED,
	KADC_START_REVCONNECT_KADENGINE_FAILED,
	KADC_START_RTP_FAILED,
	KADC_NEVER_STARTED,
	KADC_STOP_OVERNETINIFILEUPDATE_FAILED,
	KADC_STOP_OVERNET_FAILED,
	KADC_STOP_EMULEKAD_FAILED,
	KADC_STOP_REVCONNECT_FAILED,
	KADC_STOP_RTP_FAILED,
	KADC_STOP_UDPIO_FAILED,
	KADC_STOP_CANT_CREATE_NEW_INI_FILE
} KadC_status;

/* This structure holds pointers to context data.
   It is returned by value (no need to free() it) by
   KadC_start(), and its address is passed as first
   parameter to all the other API calls, up to the
   final KadC_stop(). Only the first three fields
   are public, and represent status information. */
typedef struct _KadCcontext {
	KadC_status s;
	char *errmsg1;
	char *errmsg2;
	/* === private fields below, not for user API consumption === */
	/* UDPIO */ void *pul;
	/* KadEngine */ void *pOKE;
	/* KadEngine */ void *pEKE;
	/* KadEngine */ void *pRKE;
	/* char */ void *inifilename;
	/* FILE */ /*void *inifile;*/
} KadCcontext;

/* Allocates a context and makes the necessary initializations.
   The inifilename ASCIIZ string is a filepath to a INI file
   defining local parameters (addresses/ports to bind to,
   hash ID) and a list of "contact nodes" that gets updated
   when KadC_stop() is called.
   If leafmode is non-zero, the node will behave as if it were
   behind a NATting device, avoiding the participation to
   the distributed storage function for the global DHT.
   If init_network is non-zero, some platform-dependent
   initialization will be performed here. At present, this is
   only useful on WIN32 ("MinGW mode" compiling with -mno-cygwin)
   to call WSAStartup() and put in place an "atexit" handler to
   call WSACleanup() upon exit. In that case, Winsock 1.1 is
   requested, as it suffices for KadC. Use 0 as third parameter
   if Winsock was initialized elsewhere before calling KadC_start().
 */
KadCcontext KadC_start(char *inifilename, int leafmode, int init_network);

/* Initializes the network in alternative to setting to one the \
   third parameter passed to KadC_start(). Must be called before
   KadC_start. In other words, the sequence:
   		KadC_init_network();
   		KadC_start(kadc.ini, l, 0);
   is equivalent to:
   		KadC_start(kadc.ini, l, 1);
   The returned value is 0 in case of success, or a platform-specific
   error code.
 */
int KadC_init_network(void);

/* Stops any activity and releases resources initially allocated
   with the call to KadC_start().
 */
KadC_status KadC_stop(KadCcontext *pkcc);

/* Writes a new inifile with updated node infomation
   to the target file. If target_file is NULL writes
   to the original inifile by creating a temporary
   file first. Call only after KadC_start and before KadC_end.
 */
KadC_status KadC_write_inifile(KadCcontext *pkcc, const char *target_file);

/* returns number of nodes in k-buckets */
int KadC_getnknodes(KadCcontext *pkcc);

/* returns number of nodes in contacts list */
int KadC_getncontacts(KadCcontext *pkcc);

/* Checks the TCP firewalled status. Returns:
	0 if status not yet checked (maybe due to network unreachable)
	1 if we are TCP-firewalled
	2 if we are not TCP-firewalled
 */
int KadC_getfwstatus(KadCcontext *pkcc);

/* returns our IP address as seen by other peers */
unsigned long int KadC_getextIP(KadCcontext *pkcc);

/* returns our local hash ID (as pointer to a 16-byte buffer) */
int128 KadC_getourhashID(KadCcontext *pkcc);

/* returns our UDP port number */
unsigned short int KadC_getourUDPport(KadCcontext *pkcc);

/* returns our TCP port number */
unsigned short int KadC_getourTCPport(KadCcontext *pkcc);

/* Performs a one-time publishing of the k-object described by the
   ASCIIZ string metalist in format tagname=tagvalue[;tagname=tagvalue...]
   The ASCIIZ strings index and value will ultimately be resolved
   into 128-bit numbers. They may be formatted either with a leading
   "#" followed by up to 32 hex digits (missing digits will be
   treated as zeroes), or another string that will be MD4-hashed.
   nthreads represents the number of parallel threads that will be
   created to handle the publishing process, and may range from 1
   to 10 (default: 10).
   The parameter duration (in seconds) limits the time spent
   during the "node lookup" phase of the publishing process
   (default: 15 seconds).
   The calling process should periodically republish records
   every hour or so, to ensure their persistence.
   Returns: -1 in case of some error (i.e., invalid parameter);
   or number of peernodes where the k-object was actually stored:
   ideally 20, but possibly less if few results were returned by the
   node lookup, or some of the returned nodes failed to send an ACK
   to the store request. In that case, the caller may choose to repeat
   the call.
 */
int KadC_republish(KadCcontext *pkcc, const char *index, const char *value, const char *metalist,  int nthreads, int duration);

typedef void KadCdictionary;

/* Performs a search for k-objects stored in the global DHT. The
   objects are searched using the index ASCIIZ string (same syntax
   as in KadC_republish() ) and filtering the hits with the boolean
   filter expressed by the ASCIIZ string filter. The parameters
   nthreads and duration have the same meaning as in KadC_republish().
   The syntax for the boolean filter is defined in the module
   KadCparser. For concrete examples, see the file doc/Quickstart.txt .
   Returns an rbt (see rbt.h) of KadCdictionary objects: opaque,
   dynamically allocated types destroyed with free() and only
   accessed through suitable read-only functions (see below).
   The number of threads used in the search, nthreads, is internally
   limited to 20 (values higher than that treated as 20).
 */
void *KadC_find(KadCcontext *pkcc, const char *index, const char *filter, int nthreads, int maxhits, int duration);

#ifndef OLD_SEARCH_ONLY
/* A new extended search primitive. Extra options are given in 
   KadCfind_params structure. Use KadCfind_init to initialize
   the structure with defaults before using.
 */
typedef struct _KadCfind_params {
	const char *filter;
	int threads;
	int max_hits;
	int duration;
	
	/* A callback function can be specified which will
	   be called for every hit found during the search.
	   In this case there won't be results in the returned
	   rbt. The callback is responsible for collecting
	   the required information.
	   
	   Take into consideration: 
	   1) the callback can be called from several threads, 
	   possibly at the same time. If synchronization 
	   needed it must be implemented in the callback. 
	   2) Unless KADC_HITS_CALLBAC_COLLECT mode is used,
	   KadCdictionary that is passed to the callback
	   is not valid anymore after the callback exits.
	   Any data in it must be copied if it is needed
	   after the callback. 
	    
	   The callback should return 0 to continue, 
	   and 1 if no more hits wanted
	 */
	int (*hit_callback)(KadCdictionary *, void *context);
	void  *hit_callback_context;
	/* There are two modes if hit_callback is used:
	   1) the default, which does not collect the hits
	      to rbt that is returned by the end of the function.
	      In this mode also all the hits are reported to
	      the caller, even the duplicates. The callback must
	      handle duplicates somehow if necessary.
	   2) a mode that collects the hits to a returned rbt,
	      just like KadC_find. In this mode only the nodes
	      only unique hits are reported to the callback.
	      In this case max_hits parameter determines how
	      many hits are reported.
	*/
	enum {
		KADC_DONT_COLLECT_HITS = 0,
		KADC_COLLECT_HITS
	} hit_callback_mode;
		
} KadCfind_params;

KadCfind_params *KadCfind_init(KadCfind_params *p);

void *KadC_find2(KadCcontext *pkcc, const char *index, KadCfind_params *params);
#endif /* OLD_SEARCH_ONLY */

/* API to deal with KadCtags, the objects inside the rbt's returned by
   KadC_find() */

typedef enum {
	KADCTAG_INVALID,
	KADCTAG_NOTFOUND,	/* type <= KADCTAG_NOTFOUND is bad news */
	KADCTAG_HASH,
	KADCTAG_STRING,
	KADCTAG_ULONGINT
} KadCtag_type;

typedef struct _KadCtag_iter {
	unsigned long int tagsleft;	/* tells how many tags are left after pb */
	const unsigned char *pb;			/* points to next tag in buffer */
	const unsigned char *bufend;		/* points immediately after end of buffer */
	const unsigned char *khash;
	const unsigned char *vhash;
	KadCtag_type tagtype;
	char tagname[256];
	char tagvalue[256];
} KadCtag_iter;

/* returns the hash in the KadCdictionary passed as parameter
   or NULL for any error
 */
int128 KadCdictionary_gethash(KadCdictionary *pkd);

/* to be called first to position the iterator to the first field
   and set tagsleft, khash and vhash, plus the first curname and curvalue
 */
KadCtag_type KadCtag_begin(KadCdictionary *pkd, KadCtag_iter *iter);

/* to decode next field, returning its type, or KADCTAG_NOTFOUND if EOF,
   or KADCTAG_INVALID if some decoding error occurred */
KadCtag_type KadCtag_next(KadCtag_iter *iter);

/* equivalent to KadCtag_begin() followed by as many KadCtag_next() as
   necessary until the tag with the given name is found. Special names
   are correctly looked up, and the parameter is the all-caps string version
 */
KadCtag_type KadCtag_find(KadCdictionary *pkd, char *name, KadCtag_iter *iter);

/* prints a text representation of the KadCdictionary through KadC_log
 */
void KadCdictionary_dump(KadCdictionary *pkd);

/* deallocates a KadCdictionary object
 */
void KadCdictionary_destroy(KadCdictionary *pkd);

/* Node blacklisting sub-API
   A node is a (IP, UDPport) pair. If the node is blacklisted, any
   UDP packet arriving from it is discarded right away; this allows to
   deal with nodes recognized as hostile (sending fake information,
   attempting DoS attacks etc.).
 */

/* If (ip, port) is currently blacklisted, return for how many seconds it'll remain so;
   otherwise, return 0 */
int KadC_is_blacklisted(KadCcontext *pkcc, unsigned long int ip, unsigned short int port);

/* blacklist (ip, port) for a number of seconds from now equal to howmanysecs;
   return 0 if it was not blacklisted already, or else return
   the number of seconds it would have remained blacklisted without this call */
int KadC_blacklist(KadCcontext *pkcc, unsigned long int ip, unsigned short int port, int howmanysecs);

/* unblacklist (ip, port) and return 0 if it was not blacklisted, or else
   the number of seconds it would have remained blacklisted without this call */
int KadC_unblacklist(KadCcontext *pkcc, unsigned long int ip, unsigned short int port);

void KadC_listkbuckets(KadCcontext *pkcc);

void KadC_emptykbuckets(KadCcontext *pkcc);

/*
Version numbers have the form major.minor.patch.
The major version number identifies the API version.
A release that changes the API in a way that breaks
backwards compatibility will increment the major
version number and reset the minor and patch versions
to zero.
The minor version number identifies the backwards
compatible revision of the API. An API version that
adds new elements to the API will have the same
major version, increment the minor version and set
the patch version to zero.
The patch version number identifies revisions that
do not change the API. A release that fixes bugs
or refactors implementation details without changing
the API will have the same minor and major versions
as the previous release and increment the patch number.
*/

typedef struct _KadC_version_t {
	const unsigned char major;
	const unsigned char minor;
	const unsigned char patchlevel;
} KadC_version_t;

extern const KadC_version_t KadC_version;
