/*
 * Author: Tomas Cejka <cejkato2@fit.cvut.cz>
 * Organization: FIT - CTU
 * Date: 2011-2012
 */

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
#include <locale.h>

#define BUF_SIZE 5000
#define MAX_CLIENTS_NO 10
#define DEFAULT_MEMORY_SIZE 10
#define DEFAULT_CHUNK_SIZE 10

/*| entry of list of clients */
typedef struct clientl {
	int sd;
	size_t addrlen;
	struct sockaddr *addr;
} clientl_t;

static int master_flag = 0;
static char *local_port = NULL;
static char *target_addr = NULL;
static char *target_port = NULL;
static uint32_t memory_size = DEFAULT_MEMORY_SIZE;
static uint32_t chunk_size = DEFAULT_CHUNK_SIZE;
static clientl_t clientlist[MAX_CLIENTS_NO];
static char *shared_memory = NULL;
static fd_set fdclientset;
static struct timeval timeout = { 1, 0 };

/* TODO signal for ending */
static int is_terminated = 0;

/*|
 * \brief Handle incomming message from buffer
 * \param[in] sd - socket descriptor of client
 * \param[in] bf - buffer with incomming message
 * \param[in] bs - size of incomming message
 */
void handle_message(int sd, char *bf, size_t bs)
{
	void *p;
	fprintf(stderr, "from %i got message len %i: %s\n", sd, bs, bf);
	if (strncmp(bf, "m", bs) == 0) {
		/* memory configuration */
	} else if (strncmp(bf, "h", bs) == 0) {
		/* host record */
	} else if (strncmp(bf, "w", bs) == 0) {
		/* write */
	} else if (strncmp(bf, "r", bs) == 0) {
		/* read */
	}
}

/*!
 * \brief randomly write or read from shared memory
 *
 * read is local, write is shared and causes replication
 */
void handle_send()
{
	fprintf(stderr, "send\n");
	if (random() >= 0.5) {
		/* write */
	} else {
		/* read */
	}
}

int main(int argc, char *argv[])
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	/*! sfd - local server socket descriptor */
	int sfd;
	int s;
	struct sockaddr_storage peer_addr;
	socklen_t peer_addr_len;
	ssize_t nread;
	char buf[BUF_SIZE];
	int clientcount = 0;
	int i, rv;

	/* getopt options */
	static struct option long_options[] = {
		/* These options set a flag. */
		{"master", no_argument, &master_flag, 1},
		{"target", required_argument, 0, 't'},
		{"port", required_argument, 0, 'p'},
		{"lport", required_argument, 0, 'l'},
		{"chunk", required_argument, 0, 'c'},
		{"memory", required_argument, 0, 'm'},
		{0, 0, 0, 0}
	};
	/* getopt_long stores the option index here. */
	int option_index = 0;
	int c;
	setlocale(LC_ALL, "");
	while (1) {
		c = getopt_long(argc, argv, "p:t:l:c:1m:h",
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
		case 'c':
			sscanf(optarg, "%i", &chunk_size);
			break;
		case '1':
			master_flag = 1;
			break;
		case 'm':
			sscanf(optarg, "%i", &memory_size);
			break;
		case 'h':
			puts("--port | -p <target_port>\n"
			     "--target | -t <target_address>\n"
			     "--lport | -l <local_port>\n"
			     "--chunk | -c <chunk_size>\n"
			     "--memory | -m <memory_size> (number of chunks)\n"
			     "--master | -1 start as a master\n");
			return 0;
		}
	}

	/* check if user supplied all needed infos */
	if ((master_flag == 0)
	    && ((target_addr == NULL) || (target_port == NULL))) {
		fprintf(stderr,
			"You have to specify target address and port\n");
		return 1;
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = IPPROTO_TCP;

	s = getaddrinfo(NULL, local_port, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "local getaddrinfo: %s\n", gai_strerror(s));
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
		close(sfd);
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
		/* connect to structure of node (I am just a client) */
		do {
			s = getaddrinfo(target_addr, target_port, &hints,
					&result);
		} while (s == EAI_AGAIN);
		if (s != 0) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
			close(sfd);
			exit(EXIT_FAILURE);
		}
		clientlist[clientcount].sd =
		    socket(result->ai_family, result->ai_socktype,
			   result->ai_protocol);
		clientlist[clientcount].addrlen = result->ai_addrlen;
		clientlist[clientcount].addr = result->ai_addr;
		clientcount++;

		if (connect
		    (clientlist[0].sd, result->ai_addr, result->ai_addrlen)
		    == -1) {
			freeaddrinfo(result);
			fprintf(stderr, "Could not connect\n");
			close(clientlist[0].sd);
			close(sfd);
			exit(EXIT_FAILURE);
		}
		fprintf(stderr, "master sd: %d\n", clientlist[0].sd);
		freeaddrinfo(result);

		/* TODO receive memory configuration and the list of nodes */

	} else {
		/* allocate given chunk of memory */
		shared_memory =
		    calloc(memory_size, chunk_size * sizeof(*shared_memory));
	}

	while (is_terminated == 0) {
		/* infinite main loop */
		FD_ZERO(&fdclientset);
		FD_SET(sfd, &fdclientset);
		int maxfd = sfd + 1;
		for (i = (clientcount - 1); i >= 0; --i) {
			if (clientlist[i].sd > maxfd) {
				maxfd = clientlist[i].sd + 1;
			}
			FD_SET(clientlist[i].sd, &fdclientset);
		}
		if (select(maxfd, &fdclientset, NULL, NULL, &timeout) == -1) {
			fprintf(stderr, "Error - select() failed\n");
		}

		if (FD_ISSET(sfd, &fdclientset)) {
			rv = accept(sfd, clientlist[clientcount].addr,
				    &clientlist[clientcount].addrlen);
			if (rv == -1) {
				fprintf(stderr, "Error - accept() failed\n");
			}
			fprintf(stderr, "Accepted client %i\n", rv);
			clientlist[clientcount].sd = rv;
			clientcount++;
		}

		for (i = (clientcount - 1); i >= 0; --i) {
			/* iterate over clients and wait for message */
			if (FD_ISSET(clientlist[i].sd, &fdclientset)) {
				rv = recv(clientlist[i].sd, buf, BUF_SIZE - 1,
					  0);
				buf[rv] = 0;
				switch (rv) {
				case 0:	//shutdown
					fprintf(stderr, "Got goodbye from %i\n",
						clientlist[i].sd);
					close(clientlist[i].sd);
					clientlist[i].sd =
					    clientlist[clientcount - 1].sd;
					clientlist[i].addr =
					    clientlist[clientcount - 1].addr;
					clientlist[i].addrlen =
					    clientlist[clientcount - 1].addrlen;
					memset((void *)
					       &clientlist[clientcount - 1], 0,
					       sizeof(*clientlist));
					clientcount--;
					break;
				case -1:
					fprintf(stderr,
						"Error - recv() client %d\n",
						clientlist[i].sd);
					close(clientlist[i].sd);
					clientlist[i].sd =
					    clientlist[clientcount - 1].sd;
					clientlist[i].addr =
					    clientlist[clientcount - 1].addr;
					clientlist[i].addrlen =
					    clientlist[clientcount - 1].addrlen;
					memset((void *)
					       &clientlist[clientcount - 1], 0,
					       sizeof(*clientlist));
					clientcount--;
					break;
				default:
					handle_message(i, buf, rv);
				}
			}
		}

		handle_send();

		sleep(100);
	}
	free(shared_memory);

	system("read");
	close(sfd);
}
