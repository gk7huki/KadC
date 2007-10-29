/* works on both SunOS4 (iSunSPARC, big-endian) and Linux (i386, little-endian) */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


static void inet_ntoa_r(char *ip, struct in_addr sin_addr) {
        unsigned char *b = (unsigned char *)&sin_addr;
        sprintf(ip, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
}

int main(int ac, char *av[]) {
        char s[32];
        struct in_addr ia;

        ia.s_addr = htonl(0x01020304);
        inet_ntoa_r(s, ia);
        printf("inet_ntoa_r() for 0x01020304 sets s to %s\n", s);
        printf("inet_ntoa()   for 0x01020304 sets s to %s\n", inet_ntoa(ia));
        return 0;
}

