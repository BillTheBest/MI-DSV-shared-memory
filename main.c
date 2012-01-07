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
#include <time.h>

#define BUF_SIZE 5000
#define MAX_CLIENTS_NO 10
#define DEFAULT_MEMORY_SIZE 10
#define DEFAULT_CHUNK_SIZE 10
#define RETRY_SEND 1

/*| entry of list of clients */
typedef struct clientl {
	int sd;
	size_t addrlen;
	struct sockaddr *addr;
} clientl_t;

struct memory_config {
	char msgtype;
	uint32_t memory_size;
	uint32_t chunk_size;
} __attribute__ ((__packed__));

struct write_message {
	char msgtype;
	time_t timestamp;
} __attribute__ ((__packed__));

static int master_flag = 0;
static char *local_port = NULL;
static char *target_addr = NULL;
static char *target_port = NULL;
static uint32_t memory_size = DEFAULT_MEMORY_SIZE;
static uint32_t chunk_size = DEFAULT_CHUNK_SIZE;
static clientl_t clientlist[MAX_CLIENTS_NO];
static int clientcount = 0;
static char *shared_memory = NULL;
static fd_set fdclientset;
static struct timeval timeout = { 0, 0 };

static time_t *timestamps;

static char buf[BUF_SIZE];

/*! sfd - local server socket descriptor */
int sfd = 0;

/*! msd - socket descriptor of client for communication with master */
int msd = 0;

/* TODO signal for ending */
static int is_terminated = 0;

void handle_signal(int sig)
{
	is_terminated = 1;
	fprintf(stderr, "set is_terminated\n");
}

void allocate_shared_mem()
{
	shared_memory =
	    calloc(memory_size, chunk_size * sizeof(*shared_memory));
	timestamps = calloc(memory_size, sizeof(time_t));
}

/*|
 * \brief Handle incomming message from buffer
 * \param[in] sd - socket descriptor of client
 * \param[in] bf - buffer with incomming message
 * \param[in] bs - size of incomming message
 */
void handle_message(int sd, char *bf, size_t bs)
{
	void *p;
	struct memory_config *m;
	fprintf(stderr, "from %i got message len %i: %s\n", sd, bs, bf);
	if (strncmp(bf, "m", 1) == 0) {
		/* memory configuration */
		m = (struct memory_config *)bf;
		memory_size = m->memory_size;
		chunk_size = m->chunk_size;
		allocate_shared_mem();
		fprintf(stderr, "Set memory to: %i x %i\n", memory_size,
			chunk_size);
	} else if (strncmp(bf, "h", 1) == 0) {
		/* host record */
		fprintf(stderr, "Received hostlist\n");
	} else if (strncmp(bf, "w", 1) == 0) {
		/* write */
		fprintf(stderr, "Received write\n");
	}
}

void close_remove_id(int i)
{
	shutdown(clientlist[i].sd, SHUT_RDWR);
	clientlist[i].sd = clientlist[clientcount - 1].sd;
	clientlist[i].addr = clientlist[clientcount - 1].addr;
	clientlist[i].addrlen = clientlist[clientcount - 1].addrlen;
	memset((void *)&clientlist[clientcount - 1], 0, sizeof(*clientlist));
	clientcount--;
}

void close_remove_sd(int sd)
{
	int i;

	for (i = 0; i < clientcount; ++i) {
		if (clientlist[i].sd == sd) {
			break;
		}
	}

	close_remove_id(i);
}

void send_memory_config(int sd)
{
	/* this function is called only when somebody connects to me */
	struct memory_config m;
	int rv;
	int i;
	m.msgtype = 'm';
	m.memory_size = memory_size;
	m.chunk_size = chunk_size;
	rv = send(sd, &m, sizeof(struct memory_config), MSG_NOSIGNAL);
	if (rv == -1) {
		rv = errno;
		fprintf(stderr, "Error during send: %s\n", strerror(rv));
		close_remove_sd(sd);
		return;
	}
}

void send_host_list(int sd)
{
	int rv;
	int i;

}

int generate_write_op()
{
	char chunk[chunk_size];
	/* TODO randomize index from 0 to memory_size-1 */
	int index = 1;
	int i;
	for (i = 0; i < chunk_size; ++i) {
		chunk[i] = i + 0x30;	/* TODO randomize data */
	}
	strncpy(&shared_memory[index * chunk_size], chunk, chunk_size);
	return index;
}

/*!
 * \brief randomly write or read from shared memory
 *
 * read is local, write is shared and causes replication
 */
void handle_send()
{
	if (shared_memory == NULL) {
		return;		/* memory is still not configured */
	}

	int i;
	//fprintf(stderr, "send\n");
//      if (((double) random()/RAND_MAX) >= 0.5) {
//              /* write */
//      } else {
//              /* read */
//      }
	struct write_message m;
	long int r = random() % 100000000;
	double rate = (double)r / 100000000;
	if (rate < 0.2) {
		int index = generate_write_op();
		void *pi;
		int *pint;
		m.msgtype = 'w';
		m.timestamp = time(NULL);
		buf[0] = 'w';
		pint = (int *)&buf[1];
		*pint = index;
		pi = (void *)pint;
		pi = pi + sizeof(index);
		pi = strncpy(pi,
			     &shared_memory[chunk_size * index], chunk_size);
		pi = pi + chunk_size;
		if (clientcount > 0) {
			for (i = 0; i < clientcount; ++i) {
				send(clientlist[i].sd, buf,
				     pi - (void *)&buf, MSG_NOSIGNAL);
			}
		}
		if (master_flag == 0) {
			send(msd, buf, pi - (void *)&buf, MSG_NOSIGNAL);
		}
		fprintf(stderr, "sent write operation\n");
	} else {
		fprintf(stderr, " ");
	}
}

void accept_new_client()
{
	int rv = accept(sfd, clientlist[clientcount].addr,
			&clientlist[clientcount].addrlen);
	if (rv == -1) {
		fprintf(stderr, "Error - accept() failed\n");
	} else {
		fprintf(stderr, "Accepted client %i\n", rv);
		clientlist[clientcount].sd = rv;
		send_memory_config(clientlist[clientcount].sd);
		clientcount++;
	}
}

int main(int argc, char *argv[])
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int s;
	struct sockaddr_storage peer_addr;
	socklen_t peer_addr_len;
	ssize_t nread;
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

	srandom(time(NULL));
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
		msd = socket(result->ai_family, result->ai_socktype,
			     result->ai_protocol);

		if (connect(msd, result->ai_addr, result->ai_addrlen) == -1) {
			freeaddrinfo(result);
			fprintf(stderr, "Could not connect\n");
			close(clientlist[0].sd);
			close(sfd);
			exit(EXIT_FAILURE);
		}
		fprintf(stderr, "master sd: %d\n", clientlist[0].sd);
		freeaddrinfo(result);

		/* TODO receive memory configuration
		   and the list of nodes */

	} else {
		/* allocate given chunk of memory */
		allocate_shared_mem();
		accept_new_client();
	}

	fprintf(stderr, "before mainloop:\n"
		"sfd: %i\n"
		"master_flag: %i\n" "msd: %i\n", sfd, master_flag, msd);

	signal(SIGINT, handle_signal);
	while (is_terminated == 0) {
		/* infinite main loop */
		FD_ZERO(&fdclientset);
		if (sfd > 0) {
			FD_SET(sfd, &fdclientset);
		}
		if (master_flag == 0 && msd > 0) {
			FD_SET(msd, &fdclientset);
		}

		int maxfd = (sfd > msd ? sfd : msd) + 1;

		for (i = (clientcount - 1); i >= 0; --i) {
			if (clientlist[i].sd > 0) {
				if (clientlist[i].sd > maxfd) {
					maxfd = clientlist[i].sd + 1;
				}
				FD_SET(clientlist[i].sd, &fdclientset);
			}
		}
		if (select(maxfd, &fdclientset, NULL, NULL, &timeout) == -1) {

			rv == errno;
			fprintf(stderr,
				"Error - select() failed maxfd %i: %s\n", maxfd,
				strerror(rv));

		}

		if (FD_ISSET(sfd, &fdclientset)) {
			accept_new_client();
		}

		if (master_flag == 0) {
			if (FD_ISSET(msd, &fdclientset)) {
				rv = recv(msd, buf, BUF_SIZE - 1, 0);
				buf[rv] = 0;
				switch (rv) {
				case 0:
				case -1:
					fprintf(stderr,
						"Unexpected end of master\n");
					shutdown(msd, SHUT_RDWR);
					msd = 0;
					break;
				default:
					handle_message(msd, buf, rv);
				}
			}
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
					close_remove_id(i);
					break;
				case -1:

					rv = errno;
					fprintf(stderr,
						"Error - recv() client %d, %s\n",
						clientlist[i].sd, strerror(rv));
					close_remove_id(i);
					break;
				default:
					handle_message(clientlist[i].sd, buf,
						       rv);
				}
			}
		}

		if ((master_flag == 0 && msd > 0) || clientcount > 0) {
			handle_send();
		}

		sleep(1);
	}
	free(shared_memory);
	free(timestamps);
	for (i = 0; i < clientcount; ++i) {
		shutdown(clientlist[i].sd, SHUT_RDWR);
	}
	shutdown(sfd, SHUT_RDWR);
	shutdown(msd, SHUT_RDWR);
	puts("Died...");

	return 0;
}
