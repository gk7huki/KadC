/* typedefs for structures used to hold _parsed_ DNS messages */

/* TYPE values */
typedef enum{
	A = 1,      /* a host address */
	NS,       /* an authoritative name server */
	MD,       /* a mail destination (Obsolete - use MX) */
	MF,       /* */
	CNAME,    /* the canonical name for an alias */
	SOA,      /* marks the start of a zone of authority  */
	MB,       /* a mailbox domain name (EXPERIMENTAL) */
	MG,       /* */
	MR,       /* */
	NUL,      /* */
	WKS,      /* a well known service description */
	PTR,      /* a domain name pointer */
	HINFO,    /* host information */
	MINFO,    /* mailbox or mail list information */
	MX,       /* mail exchange */
	TXT,      /* text strings */

	AAAA = 0x1c /* IPv6 A */
	} dns_type_t;

/* CLASS values */
typedef enum{
	I = 1,         /* the Internet */
    CS,
    CH,
    HS
} dns_class_t;

typedef struct _dns_question{
  char name[256];
  dns_type_t type;
  dns_class_t class;
  char *raw;
  int raw_length;
} dns_question;

typedef struct _dns_rr{
  char name[256];
  dns_type_t type;
  dns_class_t class;
  unsigned long int ttl;
  unsigned short int rdatalen;
  char *rdata;	/* undecoded */
} dns_rr;

/* OPCODE values */
typedef enum {
	QUERY,
    IQUERY,
    STATUS
} dns_opcode_t;

/* RCODE values (only those specified by RFC1035) */

typedef enum {
	RCODE_NO_ERROR,
	RCODE_FORMAT_ERROR,
	RCODE_SERVER_FAILURE,
	RCODE_NAME_ERROR,			/* only from auth servers */
	RCODE_NOT_IMPLEMENTED,
	RCODE_REFUSED
} dns_rcode_t;

typedef enum {
	OK,
	ILL_FORMED_INPUT,
	NAME_TOO_LONG,
	UNSUPPORTED_TYPE,
	UNSUPPORTED_CLASS
} dns_parse_status;

typedef struct _dns_msg {
	/* header */
	unsigned short int id;
	unsigned char qr;
	dns_opcode_t opcode;
	unsigned char aa;
	unsigned char tc;
	unsigned char rd;
	unsigned char ra;
	unsigned char z;
	unsigned char ad;
	unsigned char cd;
	dns_rcode_t rcode;
	unsigned short int nquestions;
	unsigned short int nanswer_rr;
	unsigned short int nauth_rr;
	unsigned short int naddit_rr;
	dns_question **questions;	/* array of nquestions pointers to dns_rr */
	dns_rr **answer_rr;
	dns_rr **auth_rr;
	dns_rr **addit_rr;
	dns_parse_status error;			/* set by parser if parsing failed */
} dns_msg;

/* parses a DNS UDP payload into a newly-allocated dns_msg structure */
dns_msg *dns_parse(char *buf, int buflen);

/* packs a dns_msg into buf, and returns the number of bytes used, or
   0 if that number would exceed bufsize
   If buf== NULL or bufsize == 0 it just calculates the number of bytes
   that would be used, so that a malloc() can allocate a right-sized buf
   Note: if the parsed fields are unavailable, e.g. because dns_parse
   did't know their format, the raw versions saved by dns_parse are
   used verbatim. */
int dns_pack(char *buf, int bufsize, dns_msg *dp);

typedef struct _packedname {
	char *buf;
	int buflen;
} packedname;

/* pack a domain name into a RFC1035 representation as sequence of length-
   prefixed labels terminated by a zero byte.
   returns a struct (NOT a pointer to struct) containing:
   in the member buf a pointer to the malloc'd packed domain name (NULL if error),
   and in the member buflen the number of bytes used by buf */
packedname dns_domain_pack(char *name);

/* deallocates a dns_msg structure with all its subcomponents */
void dns_msg_destroy(dns_msg *dp);



