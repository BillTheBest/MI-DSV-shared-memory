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

#define MAX_CLIENTS_NO 10
#define DEFAULT_MEMORY_SIZE 10
#define DEFAULT_CHUNK_SIZE 10
#define RETRY_SEND 1

/*| entry of list of clients */
typedef struct clientl {
	int sd;
	socklen_t addrlen;
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
	int index;
} __attribute__ ((__packed__));

struct address_book_s {
	char hostname[128];
	short port;
} __attribute__ ((__packed__));

static int master_flag = 0;
static char *local_port = NULL;
static char *target_addr = NULL;
static char *target_port = NULL;
static uint32_t memory_size = DEFAULT_MEMORY_SIZE;
static uint32_t chunk_size = DEFAULT_CHUNK_SIZE;
/*! list of file descriptors and addr */
static clientl_t clientlist[MAX_CLIENTS_NO];
/*! list of file descriptors of targets */
static clientl_t targetlist[MAX_CLIENTS_NO];
/*! list of node addressses, 0 is me */
static struct address_book_s address_book[MAX_CLIENTS_NO + 1];
/*! pair (file descriptor, index in address table) */
static struct fdidx {
	int fd;
	int id;
} tfdaddr[MAX_CLIENTS_NO];

static int addbookidx = 0;
static int tfdaddridx = 0;
static int clientcount = 0;
static int targetcount = 0;

static unsigned char *shared_memory = NULL;
static fd_set fdclientset;
static struct timeval timeout = { 0, 0 };

static time_t *timestamps;

#define ADDRBOOK_SIZE ((MAX_CLIENTS_NO+1) * sizeof(struct address_book_s) + 1)

#define DEF_BUF_SIZE 5000
#define BUF_SIZE (ADDRBOOK_SIZE>DEF_BUF_SIZE?ADDRBOOK_SIZE:DEF_BUF_SIZE)

static char buf[BUF_SIZE];

/*! sfd - local server socket descriptor */
int sfd = 0;

static struct addrinfo hints;
static struct addrinfo *result, *rp;

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

void insert_fd_to_addrbook(int fd, char *target_addr, char *target_port)
{
	fprintf(stderr, "Inserting into address book (%i)\n", addbookidx);

	strncpy(address_book[addbookidx].hostname, target_addr,
		sizeof(address_book[0].hostname));
	sscanf(target_port, "%i", &address_book[addbookidx].port);
	tfdaddr[tfdaddridx].fd = fd;
	tfdaddr[tfdaddridx].id = addbookidx;
	addbookidx++;
	tfdaddridx++;
}

int connect_2_master(char *hostname, char *target_port)
{
	int rv, i;

	fprintf(stderr, "Connecting to node %s:%s\n", hostname, target_port);
	do {
		rv = getaddrinfo(hostname, target_port, &hints, &result);
	} while (rv == EAI_AGAIN);
	if (rv != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return;
	}
	targetlist[targetcount].sd =
	    socket(result->ai_family, result->ai_socktype, result->ai_protocol);

	if (connect
	    (targetlist[targetcount].sd, result->ai_addr,
	     result->ai_addrlen) == -1) {
		freeaddrinfo(result);
		fprintf(stderr, "Could not connect\n");
		fprintf(stderr, "addressbook (%i):\n", addbookidx);
		for (i = 0; i < addbookidx; ++i) {
			fprintf(stderr, "%i: %s:%i\n", i,
				address_book[i].hostname, address_book[i].port);
		}

		//close(targetlist[targetcount].sd);
		//close(sfd);
		//exit(EXIT_FAILURE);
	}
	fprintf(stderr, "master sd: %d\n", targetlist[targetcount].sd);
	freeaddrinfo(result);
	targetcount++;
	return targetlist[targetcount - 1].sd;
}

void hostlist_insert(struct address_book_s it)
{
	int i;
	for (i = 0; i < (targetcount + 1); ++i) {
		if (strncmp
		    (it.hostname, address_book[i].hostname,
		     sizeof(address_book[i].hostname)) == 0) {
			if (it.port == address_book[i].port) {
				fprintf(stderr,
					"Already connected %s:%i %s:%i\n",
					address_book[i].hostname,
					address_book[i].port, it.hostname,
					it.port);
				return;
			}
		}
	}
	char sport[6];
	i = snprintf(sport, 6, "%i", it.port);
	sport[5] = 0;
	printf("%i %s", it.port, sport);
	int fd = connect_2_master(it.hostname, sport);
	insert_fd_to_addrbook(fd, it.hostname, sport);
}

void print_shared_memory()
{
	int m, c, i;
	if (shared_memory != NULL) {
		fprintf(stderr, "My memory:\n\t");
		for (m = 0; m < memory_size; ++m) {
			for (c = 0; c < chunk_size; ++c) {
				fprintf(stderr, "%02X",
					shared_memory[m * chunk_size + c]);
			}
			if ((m % 5) == 4) {
				fprintf(stderr, "\n\t");
			} else {
				fprintf(stderr, " ");
			}
		}
		fprintf(stderr, "\n");
	}
}

/*|
 * \brief Handle incomming message from buffer
 * \param[in] sd - socket descriptor of client
 * \param[in] bf - buffer with incomming message
 * \param[in] bs - size of incomming message
 */
void handle_message(int sd, char *bf, size_t bs)
{
	int i;
	void *p;
	struct memory_config *m;
	fprintf(stderr, "from %i got message len %i: %s\n", sd, bs, bf);
	if (strncmp(bf, "m", 1) == 0) {
		/* memory configuration */
		m = (struct memory_config *)bf;
		if (shared_memory == NULL) {
			memory_size = m->memory_size;
			chunk_size = m->chunk_size;
			allocate_shared_mem();
			int msize =
			    memory_size * chunk_size * sizeof(*shared_memory);
			memcpy(shared_memory, &bf[sizeof(*m)], msize);
			fprintf(stderr, "Set memory to: %i x %i\n", memory_size,
				chunk_size);
			print_shared_memory();
		} else {
			if (memory_size != m->memory_size) {
				fprintf(stderr,
					"ERROR - got wrong memory configuration - memory_size from %i\n",
					sd);
			}
			if (chunk_size != m->chunk_size) {
				fprintf(stderr,
					"ERROR - got wrong memory configuration - chunk_size from %i\n",
					sd);
			}
		}
	} else if (strncmp(bf, "h", 1) == 0) {
		/* host record */
		fprintf(stderr, "Received hostlist\n");
		struct address_book_s *items = (struct address_book_s *)&bf[1];
		int count = (bs - 1) / sizeof(struct address_book_s);
		for (i = 0; i < count; ++i) {
			fprintf(stderr, "h: %s:%i\n", items[i].hostname,
				items[i].port);
			hostlist_insert(items[i]);
		}
	} else if (strncmp(bf, "w", 1) == 0) {
		/* write */
		fprintf(stderr, "Received write\n");
		struct write_message *m = (struct write_message *)buf;
		int index = m->index;
		time_t timestamp = m->timestamp;

		if (timestamps[index] > m->timestamp) {
			fprintf(stderr, "I have better (newer) value!!!\n");
			return;
		}

		p = buf + sizeof(*m);

		memcpy(&shared_memory[chunk_size * index], p, chunk_size);
		fprintf(stderr, "%i\t", index);
		for (i = 0; i < chunk_size; ++i) {
			fprintf(stderr, "%02X",
				shared_memory[(chunk_size * index) + i]);
		}
		fprintf(stderr, "\n");
	}
}

int find_index_sd(clientl_t * list, int lcount, int sd)
{
	int i;

	for (i = 0; i < lcount; ++i) {
		if (list[i].sd == sd) {
			break;
		}
	}
}

void close_remove_id(clientl_t * list, int *lcount, int i)
{
	fprintf(stderr, "Closing connection with (%i)\n", list[i].sd);
	shutdown(list[i].sd, SHUT_RDWR);
	close(list[i].sd);
	list[i].sd = list[*lcount - 1].sd;
	list[i].addr = list[*lcount - 1].addr;
	list[i].addrlen = list[*lcount - 1].addrlen;
	memset((void *)&list[*lcount - 1], 0, sizeof(*list));
	*lcount = (*lcount) - 1;
}

void close_remove_sd(clientl_t * list, int *lcount, int sd)
{
	close_remove_id(list, lcount, find_index_sd(list, *lcount, sd));
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
	memcpy(buf, &m, sizeof(m));
	int shmsize = memory_size * chunk_size * sizeof(*shared_memory);

	if ((shmsize + sizeof(m)) > BUF_SIZE) {
		fprintf(stderr, "Epic fail - buffer is not big enought "
			"to store memory config and data\n"
			"Change BUF_SIZE or memory configuration\n");
		exit(2);
	}

	memcpy(&buf[sizeof(m)], shared_memory, shmsize);
	rv = send(sd, buf, sizeof(m) + shmsize, MSG_NOSIGNAL | MSG_WAITALL);
	if (rv == -1) {
		rv = errno;
		fprintf(stderr, "Error during send: %s\n", strerror(rv));
		close_remove_sd(clientlist, &clientcount, sd);
		return;
	}
}

void send_host_list(int sd)
{
	int rv;
	int i;
	i = sizeof(struct address_book_s) * addbookidx;
	buf[0] = 'h';
	memcpy(&buf[1], (void *)address_book, i);
	send(sd, buf, i + 1, MSG_NOSIGNAL | MSG_WAITALL);
	fprintf(stderr, "Sent hostlist\n");
}

int generate_write_op()
{
	unsigned char chunk[chunk_size];
	int index = (int)((double)((double)random() / RAND_MAX) * memory_size);
	timestamps[index] = time(NULL);
	int i;
	fprintf(stderr, "new chunk %i:\n", index);
	for (i = 0; i < chunk_size; ++i) {
		chunk[i] = (unsigned char)(random() & 0xFF);
		fprintf(stderr, "%02X", chunk[i]);
	}
	fprintf(stderr, "\n");
	memcpy(&shared_memory[index * chunk_size], chunk,
	       sizeof(chunk[0]) * (chunk_size - 1));
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
	double rate = (double)random() / RAND_MAX;

	if (rate < 0.3) {
		fprintf(stderr, "%f, index/size %i/%i\n", rate,
			(int)(rate * memory_size), memory_size);
		int index = generate_write_op();
		void *pi;
		m.msgtype = 'w';
		m.timestamp = timestamps[index];
		m.index = index;
		pi = memcpy(buf, (void *)&m, sizeof(m));
		pi = pi + sizeof(m);
		pi = memcpy(pi, &shared_memory[chunk_size * index], chunk_size);
		pi = pi + chunk_size;

		/* send only to targets (do not duplicate) */
		if (targetcount > 0) {
			for (i = 0; i < targetcount; ++i) {
				send(targetlist[i].sd, buf,
				     pi - (void *)&buf,
				     MSG_NOSIGNAL | MSG_WAITALL);
			}
		}

		fprintf(stderr, "sent %i:\t", index);
		for (i = 0; i < chunk_size; ++i) {
			fprintf(stderr, "%02X",
				shared_memory[(chunk_size * index) + i]);
		}
		fprintf(stderr, "\n");
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
		fprintf(stderr, "Accepted client %i ", rv);
		clientlist[clientcount].sd = rv;
		send_memory_config(clientlist[clientcount].sd);
		sleep(1);
		send_host_list(clientlist[clientcount].sd);
		clientcount++;
	}
}

void remove_fd_from_addrbook(int fd)
{
	int i, b;
	for (i = 0; i < tfdaddridx; ++i) {
		if (tfdaddr[i].fd == fd) {
			memcpy(address_book[tfdaddr[i].id].hostname,
			       address_book[addbookidx - 1].hostname,
			       sizeof(address_book[0].hostname));
			address_book[tfdaddr[i].id].port =
			    address_book[addbookidx - 1].port;
			tfdaddr[i].fd = tfdaddr[tfdaddridx - 1].fd;
			tfdaddr[i].id = tfdaddr[tfdaddridx - 1].id;
			tfdaddridx--;

			for (b = 0; i < tfdaddridx; ++i) {
				if (tfdaddr[b].id == (addbookidx - 1)) {
					tfdaddr[b].id = i;
				}
			}

			addbookidx--;
			return;
		}
	}
}

int main(int argc, char *argv[])
{
	int s;
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
	hints.ai_family = AF_UNSPEC;	//AF_INET;
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

		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {

			gethostname(address_book[addbookidx].hostname,
				    sizeof(address_book[0].hostname));
			short *pport = (short *)rp->ai_addr->sa_data;
			address_book[addbookidx].port =
			    ntohs(((struct sockaddr_in *)rp->
				   ai_addr)->sin_port);
			fprintf(stderr, "I am: %s:%i \n",
				address_book[addbookidx].hostname,
				address_book[addbookidx].port);
			addbookidx++;
			break;	/* Success */
		}

		close(sfd);
	}

	freeaddrinfo(result);	/* No longer needed */

	if (rp == NULL) {	/* No address succeeded */
		fprintf(stderr, "Could not bind\n");
		close(sfd);
		exit(EXIT_FAILURE);
	}

	if (listen(sfd, MAX_CLIENTS_NO) != 0) {
		close(sfd);
		fprintf(stderr, "Cannot listen\n");
		return 2;
	}

	/* if I am a master - initial node, enter mainloop
	 * otherwise connect to node structure */

	srandom(time(NULL));
	if (master_flag == 0) {
		/* connect to structure of node (I am just a client) */
		strncpy(address_book[addbookidx].hostname, target_addr,
			sizeof(address_book[0].hostname));
		sscanf(target_port, "%i", &address_book[addbookidx++].port);
		int fd = connect_2_master(target_addr, target_port);
		insert_fd_to_addrbook(fd, target_addr, target_port);
		/* send own address and port to structure */
		send_host_list(targetlist[0].sd);	/* send to target */
	} else {
		/* allocate given chunk of memory */
		allocate_shared_mem();
		/* wait for the first client */
		accept_new_client();
	}

	fprintf(stderr, "before mainloop:\n"
		"\tsfd: %i\n\tmaster_flag: %i\n", sfd, master_flag);

	print_shared_memory();

	signal(SIGINT, handle_signal);

	while (is_terminated == 0) {
		/* infinite main loop */
		FD_ZERO(&fdclientset);
		FD_SET(sfd, &fdclientset);
		int maxfd = sfd + 1;

		/* my clients */
		if (clientcount > 0) {
			for (i = (clientcount - 1); i >= 0; --i) {
				if (clientlist[i].sd > 0) {
					if (clientlist[i].sd >= maxfd) {
						maxfd = clientlist[i].sd + 1;
					}
					FD_SET(clientlist[i].sd, &fdclientset);
				}
			}
		}

		/* my targets */
		if (targetcount > 0) {
			for (i = (targetcount - 1); i >= 0; --i) {
				if (targetlist[i].sd > 0) {
					if (targetlist[i].sd >= maxfd) {
						maxfd = targetlist[i].sd + 1;
					}
					FD_SET(targetlist[i].sd, &fdclientset);
				}
			}
		}

		if (select(maxfd, &fdclientset, NULL, NULL, &timeout) == -1) {
			rv == errno;
			fprintf(stderr,
				"Error - select() failed maxfd %i: %s\n", maxfd,
				strerror(rv));
		}

		/* test all fd's after select */
		if (FD_ISSET(sfd, &fdclientset)) {
			accept_new_client();
		}

		/* my clients */
		if (clientcount > 0) {
			for (i = (clientcount - 1); i >= 0; --i) {
				/* iterate over clients and wait for message */
				if (FD_ISSET(clientlist[i].sd, &fdclientset)) {
					rv = recv(clientlist[i].sd, buf,
						  BUF_SIZE - 1, 0);
					buf[rv] = 0;
					switch (rv) {
					case 0:	/* shutdown */
					case -1:	/* error */
						if (rv == 0) {
							fprintf(stderr,
								"Got goodbye from %i\n",
								clientlist
								[i].sd);
						} else {
							rv = errno;
							fprintf(stderr,
								"Error - recv() client %d, %s\n",
								clientlist
								[i].sd,
								strerror(rv));
						}
						close_remove_id(clientlist,
								&clientcount,
								i);
						break;
					default:
						handle_message(clientlist[i].sd,
							       buf, rv);
					}
				}
			}
		}

		/* my targets */
		if (targetcount > 0) {
			for (i = (targetcount - 1); i >= 0; --i) {
				/* iterate over targets and wait for message */
				if (FD_ISSET(targetlist[i].sd, &fdclientset)) {
					rv = recv(targetlist[i].sd, buf,
						  BUF_SIZE - 1, 0);
					buf[rv] = 0;
					switch (rv) {
					case 0:	/* shutdown */
					case -1:	/* error */
						if (rv == -1) {
							rv = errno;
							fprintf(stderr,
								"Error - recv() target %d, %s\n",
								targetlist[i].
								sd,
								strerror(rv));
						} else {
							fprintf(stderr,
								"Got goodbye from target %i\n",
								targetlist
								[i].sd);
							fprintf(stderr,
								"Clientlist %i, Targetlist %i, address_book %i, tfdaddridx %i\n",
								clientcount,
								targetcount,
								addbookidx,
								tfdaddridx);
						}
						int disc_node =
						    targetlist[i].sd;
						close_remove_id(targetlist,
								&targetcount,
								i);
						remove_fd_from_addrbook
						    (disc_node);

						break;
					default:
						handle_message(targetlist[i].sd,
							       buf, rv);
					}
				}
			}
		}

		if ((targetcount > 0) || (clientcount > 0)) {
			handle_send();
		}
		print_shared_memory();

		sleep(1);
	}
	print_shared_memory();
	free(shared_memory);
	free(timestamps);
	for (i = 0; i < clientcount; ++i) {
		shutdown(clientlist[i].sd, SHUT_RDWR);
		close(clientlist[i].sd);
	}
	for (i = 0; i < targetcount; ++i) {
		shutdown(targetlist[i].sd, SHUT_RDWR);
		close(targetlist[i].sd);
	}
	shutdown(sfd, SHUT_RDWR);
	close(sfd);
	puts("Died...");

	return 0;
}
