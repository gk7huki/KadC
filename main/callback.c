#include <opcodes.h>
/* Callback function, executed by a separate thread
   started by startUDPIO(&ul)
 */
void dgramrecvd(UDPIO *pul) {
	switch(pul->buf[0]) {
	case OP_KADEMLIAHEADER:
		pthread_mutex_lock(p2ph.mutex);

		break;
	default:
/*
	pul->buf[pul->nrecv] = 0;
	printf("from fd %d received %d characters\n", pul->fd, pul->nrecv);
	printf("the sender's address/port was ");
	printf("%s:%d\n", pul->remoteip, pul->remoteport);
	printf("buffer: <%s>\n", pul->buf);
 */
}

