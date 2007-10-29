/* Caller must allocate state and digest as unsigned char[16],
   invoke MD4_init() once, process input data with MD4_update(),
   and retrieve digest with MD4_digest().
 */

typedef struct {
	unsigned long A,B,C,D, count;
	unsigned long len1, len2;
	unsigned char buf[64];
} MD4_state;

void MD4_init (MD4_state *state);
void MD4_update (MD4_state *state, const unsigned char *buf, int len);
unsigned char *MD4_digest (const MD4_state *state, unsigned char *digest);

/* alternatively, caller may just allocate digest as unsigned char[16],
   and call MD4(), which will return digest's address and the MD4
   hash inside it. */
unsigned char *MD4(unsigned char *digest, const unsigned char *buf, int len);
