/* $Id$ */

/*
 * SOCK_DGRAN echo ping daemon.
 *
 * Ping repsonses are sent back to the source port of the ping client.
 */

#define _XOPEN_SOURCE 600

#ifdef USE_CTHRU
#include <cthru/socket.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "common.h"

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

	return s;
}

static int
recvecho(int s, uint16_t *seq, struct sockaddr_in *sin, socklen_t sinsz)
{
	char buf[1024];
	ssize_t r;

	r = recvfrom(s, buf, sizeof buf, 0, (void *) sin, &sinsz);
	if (-1 == r) {
		perror("recvfrom");
		return 0;
	}

	if (1 != validate(buf, seq)) {
		return 0;
	}

	printf("%d bytes from %s seq=%d\n", (int) strlen(buf) + 1, inet_ntoa(sin->sin_addr), *seq);
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
	int s;
	struct sockaddr_in sin;

	if (3 != argc) {
		fprintf(stderr, "usage: dgpingd <address> <port>\n");
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

	for (;;) {
		uint16_t seq;
		struct sockaddr_in sin;	/* TODO: scope: rename */

		if (1 == recvecho(s, &seq, &sin, sizeof sin)) {
			sendecho(s, seq, &sin);
		}
	}

	/* NOTREACHED */

	return EXIT_SUCCESS;
}

