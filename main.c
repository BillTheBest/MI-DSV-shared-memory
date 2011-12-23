#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <netdb.h>
#include <getopt.h>

#define BUF_SIZE 500
#define MAX_CLIENTS_NO 10

static char master_flag = 0;

int main(int argc, char *argv[])
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s;
	struct sockaddr_storage peer_addr;
	socklen_t peer_addr_len;
	ssize_t nread;
	char buf[BUF_SIZE];
	char *local_port = NULL;
	char *target_addr = NULL;
	char *target_port = NULL;

	/* getopt options */
	static struct option long_options[] = {
		/* These options set a flag. */
		{"master", no_argument, &master, 1},
		{"target", required_argument, 0, 't'},
		{"port", required_argument, 0, 'p'},
		{"lport", required_argument, 0, 'l'},
		{0, 0, 0, 0}
	};
	/* getopt_long stores the option index here. */
	int option_index = 0;
	int c;
	while (1) {
		c = getopt_long(argc, argv, "B:hf:gt:i:m:qd:I:r:R:T:NvF:",
				long_options, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 0:
			/* If this option set a flag, do nothing else now. */
			if (long_options[option_index].flag != 0)
				break;
			printf("option %s", long_options[option_index].name);
			if (optarg) {
				printf(" with arg %s", optarg);
			}
			printf("\n");
			break;
		case 'p':
			target_port = optarg;
			break;
		case 't':
			target_addr = optarg;
			break;
		case 'l':
			local_port = optarg;
			break;
		}
	}

	if (argc != 2) {
		fprintf(stderr, "Usage: %s port\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;	/* Any protocol */
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	s = getaddrinfo(NULL, local_port, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully bind(2).
	   If socket(2) (or bind(2)) fails, we (close the socket
	   and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;	/* Success */

		close(sfd);
	}

	if (rp == NULL) {	/* No address succeeded */
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);	/* No longer needed */

	if (listen(sfd, MAX_CLIENTS_NO) != 0) {
		close(sfd);
		fprintf(stderr, "Cannot listen\n");
		return 2;
	}

	/* Read datagrams and echo them back to sender */

	/* if I am a master - initial node, enter mainloop
	 * otherwise connect to node structure */

	if (master_flag == 0) {
		/* connect to structure */
	}

	system("read");
	//for (;;) {
	//      peer_addr_len = sizeof(struct sockaddr_storage);
	//      nread = recvfrom(sfd, buf, BUF_SIZE, 0,
	//                       (struct sockaddr *)&peer_addr, &peer_addr_len);
	//      if (nread == -1)
	//              continue;       /* Ignore failed request */

	//      char host[NI_MAXHOST], service[NI_MAXSERV];

	//      s = getnameinfo((struct sockaddr *)&peer_addr,
	//                      peer_addr_len, host, NI_MAXHOST,
	//                      service, NI_MAXSERV, NI_NUMERICSERV);
	//      if (s == 0)
	//              printf("Received %ld bytes from %s:%s\n",
	//                     (long)nread, host, service);
	//      else
	//              fprintf(stderr, "getnameinfo: %s\n", gai_strerror(s));

	//      if (sendto(sfd, buf, nread, 0,
	//                 (struct sockaddr *)&peer_addr,
	//                 peer_addr_len) != nread)
	//              fprintf(stderr, "Error sending response\n");
	//}
	close(sfd);
}

#ifdef client

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define BUF_SIZE 500

int main(int argc, char *argv[])
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s, j;
	size_t len;
	ssize_t nread;
	char buf[BUF_SIZE];

	if (argc < 3) {
		fprintf(stderr, "Usage: %s host port msg...\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* Obtain address(es) matching host/port */

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;	/* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_DGRAM;	/* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;	/* Any protocol */

	s = getaddrinfo(argv[1], argv[2], &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully connect(2).
	   If socket(2) (or connect(2)) fails, we (close the socket
	   and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;	/* Success */

		close(sfd);
	}

	if (rp == NULL) {	/* No address succeeded */
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);	/* No longer needed */

	/* Send remaining command-line arguments as separate
	   datagrams, and read responses from server */

	for (j = 3; j < argc; j++) {
		len = strlen(argv[j]) + 1;
		/* +1 for terminating null byte */

		if (len + 1 > BUF_SIZE) {
			fprintf(stderr,
				"Ignoring long message in argument %d\n", j);
			continue;
		}

		if (write(sfd, argv[j], len) != len) {
			fprintf(stderr, "partial/failed write\n");
			exit(EXIT_FAILURE);
		}

		nread = read(sfd, buf, BUF_SIZE);
		if (nread == -1) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		printf("Received %ld bytes: %s\n", (long)nread, buf);
	}

	exit(EXIT_SUCCESS);
}

#endif
