/*
 * SOCK_STREAM echo ping daemon.
 *
 * Ping repsonses are sent back to the source port of the ping client.
 */

#define _XOPEN_SOURCE 600

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

/*
 * Workaround for inline assembly in glibc confusing MSan
 */
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#undef FD_ZERO
#define FD_ZERO(p) memset((p), 0, sizeof *(p))
#endif
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))

/*
 * A linked-list of inbound ping requests.
 */
struct connection {
	struct sockaddr_storage ss;
	int socket;

	char addr[sizeof "255.255.255.255:65535"];

	char buf[3 + 5 + 24 + 2];
	size_t len;

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
newcon(struct connection **head, int s, struct sockaddr *sa, socklen_t sz)
{
	struct connection *new;

	assert(head != NULL);
	assert(s != -1);
	assert(sa != NULL);
	assert(sz > 0);

	new = malloc(sizeof *new);
	if (new == NULL) {
		perror("malloc");
		return NULL;
	}

	{
		const struct sockaddr_in *sin;
		char addr[sizeof "255.255.255.255"];
		unsigned port;

		sin = (const void *) sa;

		if (NULL == inet_ntop(sa->sa_family, &sin->sin_addr, addr, sizeof addr)) {
			perror("inet_ntop");
		}

		port = (unsigned) ntohs(sin->sin_port);

		snprintf(new->addr, sizeof new->addr, "%s:%u", addr, port);
	}

	new->socket = s;
	new->len = sizeof new->buf - 1;

	memcpy(&new->ss, sa, sz);

	printf("connection from %s\n", new->addr);

	new->next = *head;
	*head = new;

	return new;
}

static struct connection *
findcon(struct connection **head, int s)
{
	struct connection **current;

	assert(head != NULL);
	assert(s != -1);

	for (current = head; *current != NULL; current = &(*current)->next) {
		if ((*current)->socket == s) {
			return *current;
		}
	}

	return NULL;
}

static void
removecon(struct connection **head, int s)
{
	struct connection *tmp;
	struct connection **current;

	assert(head != NULL);
	assert(s != -1);

	for (current = head; *current != NULL; current = &(*current)->next) {
		if ((*current)->socket == s) {
			tmp = *current;

			printf("disconnection from %s\n", tmp->addr);

			*current = (*current)->next;
			free(tmp);
			return;
		}
	}
}

static int
recvecho(struct connection **head, int s, uint16_t *seq, struct sockaddr_in *sin)
{
	struct connection *conn;
	ssize_t r;

	(void) sin;

	assert(head != NULL);

	conn = findcon(head, s);

	assert(conn != NULL);

	r = recv(s, conn->buf + (sizeof conn->buf - 1 - conn->len), conn->len, 0);
	if (r == -1) {
		switch (errno) {
		case EINTR:
			return 0;

		default:
			perror("recv");
			return -1;
		}
	}

	if (r == 0) {
		return -1;
	}

	assert(r >= 1);
	assert(r <= (ssize_t) conn->len);

	conn->len -= r;

	if (conn->len > 0) {
		return 0;
	}

	conn->len = sizeof conn->buf - 1;

	conn->buf[sizeof conn->buf - 1] = '\0';

	if (1 != validate(conn->buf, seq)) {
		return 0;
	}

	printf("%u bytes from %s seq=%d\n",
		(unsigned) strlen(conn->buf), conn->addr, (int) *seq);

	return 1;
}

static int
sendecho(int s, uint16_t seq)
{
	const char *buf;
	size_t len;

	buf = mkping(seq);
	len = strlen(buf);

	while (len > 0) {
		ssize_t r;

		r = send(s, buf, len, 0);
		if (r == -1) {
			switch (errno) {
			case ENOBUFS:
			case EINTR:
				continue;

			default:
				perror("send");
				return -1;
			}
		}

		assert(r >= 0);
		assert(r <= (ssize_t) len);

		len -= r;
		buf += r;
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	int s;
	struct sockaddr_in sin;
	struct connection *head;

	head = NULL;

	if (3 != argc) {
		fprintf(stderr, "usage: stpingd <address> <port>\n");
		return EXIT_FAILURE;
	}

	s = getaddr(argv[1], argv[2], &sin, SOCK_STREAM, IPPROTO_TCP);
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

	{
		fd_set master;
		int maxfd;

		FD_ZERO(&master);
		FD_SET(s, &master);	

		maxfd = s;

		for (;;) {
			fd_set curr;
			int i;

			curr = master;

			/* select on our server socket and all our clients */
			if (-1 == select(maxfd + 1, &curr, NULL, NULL, NULL)) {
				perror("select");
				return EXIT_FAILURE;
			} 

			if (FD_ISSET(s, &curr)) {
				struct sockaddr_storage ss;
				struct connection *new;
				socklen_t size;
				int peer; 

				size = sizeof ss;

				peer = accept(s, (struct sockaddr *) &ss, &size);
				if (peer < 0) {
					perror("accept"); 
					return EXIT_FAILURE;
				}

				assert(size <= sizeof ss);

				if (peer > FD_SETSIZE) {
					printf("too many peers; rejecting new connection from %d\n", peer);
					close(peer);
				}

				new = newcon(&head, peer, (struct sockaddr *) &ss, size);
				if (new == NULL) {
					return EXIT_FAILURE;
				}

				FD_SET(peer, &master); 
				FD_CLR(peer, &curr); /* just to ensure the bit is present */
				maxfd = MAX(maxfd, peer);
			}

			for (i = 0; i <= maxfd; ++i) {
				uint16_t seq;
				int r;

				if (i == s || !FD_ISSET(i, &curr)) {
					continue;
				}

				r = recvecho(&head, i, &seq, &sin);
				if (r == -1) {
					FD_CLR(i, &master);
					removecon(&head, i);					
					close(i);
					continue;
				}

				if (r == 0) {
					continue;
				}

				sendecho(i, seq);
			}
		}
	}

	/* NOTREACHED */

	return EXIT_SUCCESS;
}

