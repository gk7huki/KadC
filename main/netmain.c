
#ifdef __WIN32__
#include <winsock.h>
#define socklen_t int
#else /* __WIN32__ */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define min(a,b) (((a)<(b))?(a):(b))
#include <errno.h>
#endif /* __WIN32__ */
#include <stdio.h>
#include <pthread.h>

#include <net.h>

/* Callback function, executed by a separate thread
   started by startUDPIO(&ul)
 */
void dgramrecvd(UDPIO *pul) {
	pul->buf[pul->nrecv] = 0;
	printf("from fd %d received %d characters\n", pul->fd, pul->nrecv);
	printf("the sender's address/port was ");
	/* printf("%s:%d\n", pul->remoteip, pul->remoteport); */
	printf("%s:%d\n", htoa(pul->remoteip), pul->remoteport);
	printf("buffer: <%s>\n", pul->buf);
}


int main(int ac, char *av[]) {
	unsigned long int ip;
	int port;
	int status;
	unsigned char buf[48]; /* ###### temp, for test ###### */
	UDPIO ul;

	if(ac <= 2) {
		printf("usage: %s localaddress localport\n", av[0]);
		exit(-1);
	}

#ifdef __WIN32__
	{
#define	sleep(n) Sleep(1000*(n))
		int status;
		WSADATA wsaData;
		status = WSAStartup(MAKEWORD(1, 1), &wsaData);
		if(status) {
			printf("WSAStartup returned %d: error %d - aborting...", status, WSAGetLastError());
			return -1;
		}

	}
#endif

	/* status = domain2ip(av[1], ip);
	if(status != 0) {
		printf("couldn't resolve %s: exiting.\n", av[1]);
		exit(1);
	} else {
		printf("%s resolved as %s\n", av[1], ip);
	} */
	ip = domain2hip(av[1]);
	port = atoi(av[2]);

	ul.buf = buf;
	ul.bufsize = sizeof(buf);
	/* strncpy(ul.localip, ip, sizeof(ul.localip)-1);
	ul.localip[sizeof(ul.localip)-1] = 0; */
	ul.localip = ip;
	ul.localport = port;
	ul.callback = dgramrecvd;

	status = startUDPIO(&ul);

	if(status) {
#ifdef __WIN32__
		printf("startUDPIO() failed returning %s", WSAGetLastErrorMessage(WSAGetLastError()));
#else
		printf("startUDPIO() failed returning %s", strerror(errno));
#endif
		exit(1);
	}

	printf("Waiting for remote to contact us..."); fflush(stdout);
	/* while(ul.remoteip[0] == 0) */
	while(ul.remoteip == 0)
		sleep(1);
	/* printf("\nGot call from %s:%d\n", ul.remoteip, ul.remoteport); */
	printf("\nGot call from %s:%d\n", htoa(ul.remoteip), ul.remoteport);
	while(1)
	{
		char line[512];
		char *p;

		/* printf("Send to %s:%d>", ul.remoteip, ul.remoteport); */
		printf("Send to %s:%d>", htoa(ul.remoteip), ul.remoteport);
		if(fgets(line, sizeof(line) -1, stdin) != NULL) {
			if((p = strrchr(line, '\n')) != NULL) *p = 0;
			if((p = strrchr(line, '\r')) != NULL) *p = 0;
			if(*line == 0)
				continue; /* ignore empty lines */
			if(UDPsend(&ul, (unsigned char *)line, strlen(line), ul.remoteip, ul.remoteport) < 0)
				break;
		} else {
			status = stopUDPIO(&ul);
			if(status) {
#ifdef __WIN32__
				printf("stopUDPIO() failed returning %s\n", WSAGetLastErrorMessage(WSAGetLastError()));
#else
				printf("stopUDPIO() failed returning %s\n", strerror(errno));
#endif
				exit(1);
			}
			goto exit;
		}
	}
	printf("error! UDPsend returned a value < 0");
	return 1;
exit:
	sleep(1);
#ifdef __WIN32__
	{
		int status;
		status = WSACleanup();
		if(status) {
			printf("WSACleanup returned %d: error %d - aborting...", status, WSAGetLastError());
			return -1;
		}

	}
#endif
	printf("Normal exit.\n");
	return 0;
}
