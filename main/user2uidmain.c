#include <stdio.h>
#include <stdlib.h>

#include <droppriv.h>

int main(int ac, char *av[]) {
	int uid, gid, status;
	if(ac < 2) {
		printf("usage: %s username [groupname]\n", av[0]);
		exit(1);
	}
	uid = user2uid(av[1]);
	if(uid == -2) {
		printf("user %s does not exist\n", av[1]);
		exit(1);
	} else if(uid == -1) {
		printf("couln't open /etc/passwd\n");
		exit(1);
	} else if(uid == -3) {
		printf("ill-formatted line in /etc/passwd\n");
		exit(1);
	}

	gid = user2gid(av[1]);
	if(gid == -2) {
		printf("user %s does not exist\n", av[1]);
		exit(1);
	} else if(gid == -1) {
		printf("couln't open /etc/passwd\n");
		exit(1);
	} else if(gid == -3) {
		printf("ill-formatted line in /etc/passwd\n");
		exit(1);
	}

	printf("user %s has uid = %d gid = %d\n", av[1], uid, gid);

	if(ac >= 3){
		gid = group2gid(av[1]);
		if(gid == -2) {
			printf("user %s does not exist\n", av[1]);
			exit(1);
		} else if(gid == -1) {
			printf("couln't open /etc/passwd\n");
			exit(1);
		} else if(gid == -3) {
			printf("ill-formatted line in /etc/passwd\n");
			exit(1);
		}

		printf("group %s is resolved to gid = %d\n", av[2], group2gid(av[2]));
	}
	/* now trying to drop root privilege */

	#ifndef __WIN32__
		if((status = droppriv(uid, gid)) != 0) {
			printf("FATAL: Couldn't drop root privilege: ");
			if(status == -1)
				printf("suppl. group IDs reset to non-root GID failed\n");
			else if(status == -2)
				printf("couldn't change to non-privileged GID %d\n", gid);
			else if(status == -3)
				printf("couldn't change to non-root UID %d\n", uid);
			else if(status == -4)
				printf("once non-root, setuid(0) managed to restore root UID!\n");
			exit(1);
		}
	#endif


	return 0;
}
