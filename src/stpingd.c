/*
 * SOCK_STREAM echo ping daemon.
 *
 * Ping repsonses are sent back to the source port of the ping client.
 */

#define _XOPEN_SOURCE 600

#ifdef USE_CTHRU
#include <cthru/socket.h>
#endif

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "common.h"

#define PING_LENGTH (1024)

/*
 * A linked-list of inbound ping requests.
 */
struct connection {
	int socket;
	char buf[PING_LENGTH];
	int p;

	struct connection *next;
};


static int
bindon(int s, struct sockaddr_in *sin)
{
	const int ov = 1;

	if (-1 == setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &ov, sizeof ov)) {
		perror("setsockopt");
		close(s);
		return -1;
	}

	if (-1 == bind(s, (void *) sin, sizeof *sin)) {
		perror("bind");
		close(s);
		return -1;
	}

	if (-1 == listen(s, 1)) {
		perror("listen");
		close(s);
		return -1;
	}

	return s;
}

static struct connection *
findcon(int s, struct connection **head)
{
	struct connection **current;

	assert(s != -1);
	assert(head != NULL);

    for (current = head; *current != NULL; current = &(*current)->next) {
        if ((*current)->socket == s) {
            return *current;
        }
    }

	{
		*current = malloc(sizeof **current);
		if (*current == NULL) {
    		return NULL;
		}
	}

    return *current;
}

static void
removecon(int s, struct connection **head)
{
    struct connection *tmp;
	struct connection **current;

	assert(s != -1);
	assert(head != NULL);

	for (current = head; *current != NULL; current = &(*current)->next) {
		if((*current)->socket == s) {
			tmp = *current;
	 		*current = (*current)->next;
			free(tmp);
		}
	}

}


/*
 * Handle an inbound echo. 
 * Read as much as possible and validate.
 */
static int
recvecho(struct connection **head, int s, uint16_t *seq, struct sockaddr_in *sin, socklen_t sinsz)
{

	char buf[1024];
	struct connection *new;
	ssize_t r;

	assert(head != NULL);

	new = findcon(s, head);

	if (NULL == new) {
		return EOF;
	}

	r = recv(s, buf, sizeof buf, 0);
	if (-1 == r) {
		perror("recvfrom");
		return 0;
	}

	if (r == 0) {
		return EOF;
	}

	sinsz = sizeof (*sin);
	if (getpeername(s, (struct sockaddr *)sin, &sinsz)) {
		perror("getpeername");
		return 0;
	}

	memcpy(new->buf + new->p, buf, r);
	new->p += r;
	
	if (1 != validate(new->buf, seq)) {
		return 0;
	}

	/* 
	 * We've validated the ping at this point so reset the pointer
     * into the buffer.
	 */	
	new->p = 0;

	printf("%d bytes from %s seq=%d\n", (int) strlen(new->buf) + 1, inet_ntoa(sin->sin_addr), *seq);

	return 1;
}

static void
sendecho(int s, uint16_t seq, struct sockaddr_in *sin)
{
	const char *buf;

	buf = mkping(seq);

	if (-1 == sendto(s, buf, strlen(buf) + 1, 0, (void *) sin, sizeof *sin)) {
		perror("sendto");
	}
}

int
main(int argc, char *argv[])
{
	int i;
	int s;
	struct sockaddr_in sin;
	struct connection *head;

	fd_set active;
	fd_set read;

	socklen_t size;

	head = NULL;

	if (3 != argc) {
		fprintf(stderr, "usage: stpingd <address> <port>\n");
		return EXIT_FAILURE;
	}

	s = getaddr(argv[1], argv[2], &sin);
	if (-1 == s) {
		return EXIT_FAILURE;
	}

	/* TODO bind on INADDR_ANY instead? We could broadcast pings by default. */
	s = bindon(s, &sin);
	if (-1 == s) {
		fprintf(stderr, "unable to listen\n");
		return EXIT_FAILURE;
	}

	if (0 != setvbuf(stdout, NULL, _IOLBF, 0)) {
		perror("setvbuf");
		return EXIT_FAILURE;
	}

	/* TODO find "TCP" automatically */
	printf("listening on %s:%s %s\n", argv[1], argv[2], "TCP/IP");

	FD_ZERO (&active);
	FD_SET (s, &active);	

	for (;;) {

		read = active;
		/* select on our server socket and all our clients */
		if (select(FD_SETSIZE, &read, NULL, NULL, NULL) < 0) {
			perror ("select");
			return EXIT_FAILURE;
		} 

		for (i = 0; i < FD_SETSIZE; ++i)
             if (FD_ISSET (i, &read)) {
                 if (i == s) { 
					/* accept new socket connection */ 
					int c; 

					printf("New client added.\n");	
					size = sizeof (sin);
					c = accept (s, (struct sockaddr *) &sin, &size);

					if (c < 0) { 
						perror ("accept"); 
						return EXIT_FAILURE;
					} 

					FD_SET (c, &active); 
				} else {
					/* ping! */ 
					int r;
					uint16_t seq;
					struct sockaddr_in sin;
					printf("Client needs reading.\n"); 
					r = recvecho(&head, i, &seq, &sin, sizeof sin);

					if (1 == r) { 
						sendecho(i, seq, &sin);
					} else if (EOF == r) {
						printf("Dropping client.\n");
						FD_CLR (i, &active);
						removecon(i, &head);					
						close(i);
					}
				}
		 } 

	}

	/* NOTREACHED */

	return EXIT_SUCCESS;
}

