/*
 * SOCK_STREAM echo ping. This illustrates the following effects:
 *
 * - Connectivity
 * - Latency
 *
 * Pending responses are stored in a simple linked list; these are removed
 * either when a response is received, or on timeout. A checksum is included
 * in the packet contents to detect corruption, and a sequence number is used
 * to identify the order of responses.
 *
 * SIGINFO causes current statistics to be written to stderr on the fly. The
 * total statistics are also printed to stderr when pinging is complete.
 */

/*
 * TODO: document with a diagram. Examples can be a new section for docs.bp.com
 * TODO: gethostbyname for argv[1]
 * TODO: any other syscalls for EINTR?
 * TODO: select can't predict the future. consider making everything non-blocking
 * TODO: i am ever suspicious about timing; confirm lengths are ok for select() loop.
 * TODO: add "don't fragment" option
 * TODO: add packet size option, filled with random data, for stress testing. checksum this, too.
 * TODO: option to dump packet contents, tcpdump style, for visualisation.
 * TODO: don't use stdint.h!
 * TODO: keep going if IP vanishes (e.g. by DHCP); i.e. send() fails
 * TODO: print the number which are pending in the stats
 */

#define _XOPEN_SOURCE 600

/* for SIGINFO */
#if defined(__APPLE__)
# define _DARWIN_C_SOURCE
#endif

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <unistd.h>
#include <float.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <math.h>
#include <signal.h>

#include "common.h"

/*
 * Linux defines SIGINFO as "A synonym for SIGPWR" according to signal(7), but
 * does not actually #define it in <signal.h>.
 */
#if defined(__linux__) && !defined(SIGINFO)
# define SIGINFO SIGPWR
#endif

/*
 * Opensolaris has no convention for SIGINFO so we're arbitrarily using SIGUSR1.
 */
#if defined(__sun)
# define SIGINFO SIGUSR1
#endif

/*
 * The time to timeout pending responses, and the interval between pings.
 * Both times are given in milliseconds. The cull factor (given as a multiple
 * of the timeout) is the length of time to wait for unanswered pings.
 */
double timeout    = 5.0 * 1000.0;
double interval   = 0.5 * 1000.0;
double cullfactor = 1.25;

/* Variables for logging statistics */
unsigned int stat_sent;
unsigned int stat_recieved;
unsigned int stat_timedout;
unsigned int stat_ignored;

double stat_timemax;
double stat_timemin = DBL_MAX;
double stat_timesum;
double stat_timesqr;

/* flags for signal handlers */
volatile sig_atomic_t shouldexit;
volatile sig_atomic_t shouldinfo;


/*
 * A linked-list of unanswered ping requests.
 */
struct pending {
	struct timeval t;
	uint16_t seq;

	struct pending *next;
};

static void
sighandler(int s)
{
	switch (s) {
#ifndef __EMSCRIPTEN__
	case SIGINFO:
		shouldinfo = 1;
		break;
#endif

	case SIGINT:
		shouldexit = 1;
		break;

	case SIGALRM: /* handled just for EINTR */
	default:
		return;
	}
}

/*
 * Calculate the difference a given time and the current time; a - b.
 */
static struct timeval
xtimersub(struct timeval *a, struct timeval *b)
{
	struct timeval t;

	t.tv_sec  = a->tv_sec  - b->tv_sec;
	t.tv_usec = a->tv_usec - b->tv_usec;

	if (t.tv_usec < 0) {
		t.tv_sec--;
		t.tv_usec += 1000 * 1000;
	}

	return t;
}

/*
 * Check validity of a timeval structure, adjusting it if necessary. This is
 * provided for convenience of inaccurate arithmetic around the limits of
 * floating point calculations, to permit ping internals at fractions of a
 * second without unnecessarily complex.error-checking around select().
 *
 * Since negative values in this program are only ever produced by floating
 * point inaccuracies, they will always be small (to the order of epsilon or
 * so), and hence are just set to 0, if present.
 *
 * See SUS3 <sys/types.h>'s specification for a discussion of valid values here.
 */
static void
xitimerfix(struct timeval *tv)
{
	assert(tv->tv_usec <= 1000000);

	if (tv->tv_sec < 0) {
		tv->tv_sec = 0;
	}

	if (tv->tv_usec < 0) {
		tv->tv_usec = 0;
	}
}

static struct pending **
findpending(uint16_t seq, struct pending **p)
{
	struct pending **curr;

	for (curr = p; *curr; curr = &(*curr)->next) {
		if ((*curr)->seq == seq) {
			return curr;
		}
	}

	return NULL;
}

/*
 * Convert a timeval struct to milliseconds.
 */
static double
tvtoms(struct timeval *tv)
{
	return tv->tv_usec / 1000.0 + tv->tv_sec * 1000.0;
}

static struct timeval
mstotv(double ms) {
	struct timeval tv;

	tv.tv_sec  = round(ms / 1000.0);
	tv.tv_usec = round(fmod(ms, 1000.0) * 1000.0);
	return tv;
}

static void
removepending(struct pending **p)
{
	struct pending *tmp;

	tmp = *p;
	*p = (*p)->next;
	free(tmp);
}

static int
sendecho(int s, struct pending **p, uint16_t seq)
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

	stat_sent++;

	/* Add this request to the list of pings pending responses */
	{
		struct pending *new;

		new = malloc(sizeof *new);
		if (NULL == new) {
			perror("malloc");
			exit(EXIT_FAILURE);
		}

		if (-1 == gettimeofday(&new->t, NULL)) {
			perror("gettimeofday");
			exit(EXIT_FAILURE);
		}

		new->next = *p;
		new->seq  = seq;

		*p = new;
	}

	return 0;
}

static int
recvecho(int s, struct pending **p, struct sockaddr_in *sin)
{
	static char buf[3 + 5 + 24 + 2];
	static size_t len = sizeof buf - 1;
	struct pending **curr;
	uint16_t seq;
	ssize_t r;

	assert(s != -1);
	assert(p != NULL);
	assert(sin != NULL);

	r = recv(s, buf + (sizeof buf - 1 - len), len, 0);
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
		errno = ECONNRESET;
		perror("recv");
		return -1;
	}

	assert(r >= 1);
	assert(r <= (ssize_t) len);

	len -= r;

	if (len > 0) {
		return 0;
	}

	len = sizeof buf - 1;

	buf[sizeof buf - 1] = '\0';

	stat_recieved++;

	if (1 != validate(buf, &seq)) {
		stat_ignored++;
		return 0;
	}

	curr = findpending(seq, p);
	if (curr == NULL) {
		fprintf(stderr, "disregarding: sequence %d not pending response\n", seq);
		stat_ignored++;
		return 0;
	}

	/* Calculate round-trip delta for this particular seq ID */
	{
		struct timeval now, dtv;
		double d;

		if (-1 == gettimeofday(&now, NULL)) {
			perror("gettimeofday");
			exit(EXIT_FAILURE);
		}

		dtv = xtimersub(&now, &(*curr)->t);
		d = tvtoms(&dtv);
		assert(d >= 0);

		printf("%d bytes from %s seq=%d time=%.3f ms\n",
			(int) strlen(buf), inet_ntoa(sin->sin_addr), (int) seq, d);

		stat_timesum += d;
		stat_timesqr += pow(d, 2);
		if (d < stat_timemin) {
			stat_timemin = d;
		}
		if (d > stat_timemax) {
			stat_timemax = d;
		}
	}

	removepending(curr);

	return 1;
}

/*
 * Cull pending packets older than timeout seconds.
 */
static void
culltimeouts(struct pending **p)
{
	struct pending **curr;
	struct pending **next;
	struct timeval now;

	if (-1 == gettimeofday(&now, NULL)) {
		perror("gettimeofday");
		exit(EXIT_FAILURE);
	}

	for (curr = p; *curr; curr = next) {
		struct timeval dtv;
		double d;

		dtv = xtimersub(&now, &(*curr)->t);
		d = tvtoms(&dtv);
		if (d > timeout) {
			stat_timedout++;
			printf("timeout: seq=%d time=%.3f ms\n", (*curr)->seq, d);
			removepending(curr);
			next = curr;
		} else {
			next = &(*curr)->next;
		}
	}
}

static void
printstats(FILE *f, int multiline)
{
	double avg;
	double variance;

	assert(f != NULL);

	fprintf(f, multiline ? "%u transmitted, "
	                       "%u received, "
	                       "%u timed out, "
	                       "%u disregarded, "
	                       "%.1f%% packet loss"
	                     : "%u/%u packets, "
	                       "%u timed out, "
	                       "%u disregarded, "
	                       "%.1f%% loss",
		stat_sent, stat_recieved, stat_timedout, stat_ignored,
		(stat_sent - stat_recieved) * 100.0 / stat_sent);

	if (stat_recieved == 0 || stat_sent - stat_timedout == 0) {
		fprintf(f, "\n");
		return;
	}

	fprintf(f, multiline ? "\n"
	                       "round-trip "
	                     : ", ");

	/* Calculate statistics */
	avg = stat_timesum / stat_recieved;

	if (stat_recieved == 1) {
		fprintf(f, "min/avg/max = "
			   "%.3f/%.3f/%.3f\n",
			stat_timemin, avg, stat_timemax);
	} else {
		variance = (stat_timesqr - stat_recieved * pow(avg, 2))
			/ (stat_recieved - 1);

		fprintf(f, "min/avg/max/stddev = "
			   "%.3f/%.3f/%.3f/%.3f ms\n",
			stat_timemin, avg, stat_timemax, sqrt(variance));
	}
}

static void
usage(void) {
	fprintf(stderr, "usage: stping [ -i <interval> ] [ -t <timeout> ] [ -u <cullfactor> ]\n"
		"\t[ -c <count> ] <address> <port>\n");
}

int
main(int argc, char **argv)
{
	int s;
	int count;
	struct pending *p;
	struct sockaddr_in sin;
	struct sigaction sigact;
	sigset_t set;
	int status;

	sigemptyset(&set);
	(void) sigaddset(&set, SIGINT);
	(void) sigaddset(&set, SIGALRM);
#ifndef __EMSCRIPTEN__
	(void) sigaddset(&set, SIGINFO);
#endif

	sigact.sa_handler = sighandler;
	sigact.sa_mask    = set;
	sigact.sa_flags   = 0;

	/* Handle CLI options */
	count = 0;
	{
		int c;

		while ((c = getopt(argc, argv, "hc:i:t:u:")) != -1) {
			switch (c) {
			case 'c':
				count = atoi(optarg);
				if (count <= 0 || optarg[strspn(optarg, "0123456789")]) {
					fprintf(stderr, "Invalid ping count\n");
					return EXIT_FAILURE;
				}
				break;

			case 'i':
				interval = atof(optarg) * 1000.0;
				if (interval < DBL_EPSILON || interval < 0.001) {
					fprintf(stderr, "Invalid ping interval\n");
					return EXIT_FAILURE;
				}
				break;

			case 't':
				timeout = atof(optarg) * 1000.0;
				if (timeout <= DBL_EPSILON && optarg[strspn(optarg, "0.")]) {
					fprintf(stderr, "Invalid ping timeout\n");
					return EXIT_FAILURE;
				}
				break;

			case 'u':
				cullfactor = atof(optarg);
				if (cullfactor <= DBL_EPSILON && optarg[strspn(optarg, "0.")]) {
					fprintf(stderr, "Invalid cullfactor\n");
					return EXIT_FAILURE;
				}
				break;

			case '?':
			case 'h':
			default:
				usage();
				return EXIT_FAILURE;
			}
		}
		argc -= optind;
		argv += optind;
	}

	if (2 != argc) {
		usage();
		return EXIT_FAILURE;
	}

	s = getaddr(argv[0], argv[1], &sin, SOCK_STREAM, IPPROTO_TCP);
	if (-1 == s) {
		return EXIT_FAILURE;
	}

	if (-1 == connect(s, (void *) &sin, sizeof sin)) {
		perror("connect");
		return EXIT_FAILURE;
	}

	if (0 != setvbuf(stdout, NULL, _IOLBF, 0)) {
		perror("setvbuf");
		return EXIT_FAILURE;
	}

	if (0 != setvbuf(stderr, NULL, _IOLBF, 0)) {
		perror("setvbuf");
		return EXIT_FAILURE;
	}

#ifndef __EMSCRIPTEN__
	if (-1 == sigaction(SIGINFO, &sigact, NULL)) {
		perror("sigaction");
		return EXIT_FAILURE;
	}
#endif

	if (-1 == sigaction(SIGINT, &sigact, NULL)) {
		perror("sigaction");
		return EXIT_FAILURE;
	}

	/*
	 * This loop is responsible for two things: delaying for 'interval', whilst
	 * dealing with any incoming responses as and when they appear. The latter
	 * must be as timely as possible, so it may interrupt the interval delay.
	 *
	 * Once the delay is complete, a new ping is sent.
	 *
	 * On cleanup, the program enters a "culling" state, wherein it will
	 * continue waiting for any pending responses, until either they arrive or
	 * timeout. In either case, the pending queue becomes empty.
	 *
	 * If no pending responses arrive, the alarm() call provides a cut-off.
	 */

	{
		struct timeval before, after;
		struct timeval remaining;
		uint16_t seq;

		enum {
			STATE_SEND, STATE_RECV, STATE_SELECT, STATE_CULL
		} state;

		/* TODO: fold together, possibly merge into 'state' */
		int culling;	/* "not sending" */
		int recvfailed;	/* "not receiving" */

		p = NULL;
		culling = 0;
		recvfailed = 0;
		state = STATE_SEND;
		seq = 0;
		status = EXIT_SUCCESS;	/* TODO: calculate from culling/recvfailed flags */

		before = mstotv(interval);

		while (!culling || p != NULL) {
			fd_set rfds;

			culltimeouts(&p);

			switch (state) {
			case STATE_SELECT:
				FD_ZERO(&rfds);

				if (!recvfailed) {
					FD_SET(s, &rfds);
				}

				if (-1 == gettimeofday(&after, NULL)) {
					perror("gettimeofday");
					exit(EXIT_FAILURE);
				}

				/* calculate remaining interval */
				{
					struct timeval elapsed;

					elapsed = xtimersub(&after, &before);
					remaining = mstotv(interval);
					remaining = xtimersub(&remaining, &elapsed);
					xitimerfix(&remaining);
				}

				if (-1 == gettimeofday(&before, NULL)) {
					perror("gettimeofday");
					exit(EXIT_FAILURE);
				}

				switch (select(s + 1, &rfds, NULL, NULL, &remaining)) {
				case -1:
					if (errno == EINTR) {
						break;
					}

					perror("select");
					exit(EXIT_FAILURE);
				
				case 0:
					/* timeout */
					state = culling ? STATE_SELECT : STATE_SEND;
					continue;

				case 1:
					assert(FD_ISSET(s, &rfds));

					/* TODO: buffer partial sends, and re-enter STATE_SEND */
					state = STATE_RECV;
					continue;
				}

				break;

			case STATE_RECV:
				switch (recvecho(s, &p, &sin)) {
				case -1:
					if (errno == EINTR) {
						break;
					}

					recvfailed = 1;

					status = EXIT_FAILURE;
					state = STATE_CULL;
					continue;

				case 0:
					/* partial read */
					state = STATE_SELECT;
					continue;

				case 1:
					state = STATE_SELECT;
					continue;
				}

				break;

			case STATE_SEND:
				switch (sendecho(s, &p, seq)) {
				case -1:
					if (errno == EINTR) {
						break;
					}

					status = EXIT_FAILURE;
					state = STATE_CULL;
					continue;

				case 0:
					seq++;

					if (count != 0 && seq >= count) {
						state = STATE_CULL;
						continue;
					}

					/* reset interval, for the next ping */
					{
						remaining = mstotv(interval);
						xitimerfix(&remaining);
					}

					state = STATE_SELECT;
					continue;
				}

				break;

			case STATE_CULL:
				culling = 1;

				if (-1 == sigaction(SIGALRM, &sigact, NULL)) {
					perror("sigaction");
					return EXIT_FAILURE;
				}

				if (timeout <= DBL_EPSILON || cullfactor <= DBL_EPSILON) {
					shouldexit = 1;
					break;
				}

				if (-1 == (int) alarm(timeout / 1000.0 * cullfactor)) {
					perror("alarm");
					return EXIT_FAILURE;
				}

				state = STATE_SELECT;
				continue;
			}


			/* dispatch interrupts */
			{
				if (shouldinfo) {
					shouldinfo = 0;

					printstats(stderr, 0);
				}

				if (shouldexit && culling) {
					break;
				}

				if (shouldexit) {
					shouldexit = 0;

					state = STATE_CULL;
				}
			}
		}
	}

	close(s);

	fprintf(stdout, "\n- STREAM Ping Statistics -\n");
	printstats(stdout, 1);

	if (count > 0 && stat_recieved != count) {
		exit(EXIT_FAILURE);
	}

	if (status == EXIT_FAILURE) {
		exit(EXIT_FAILURE);
	}

	return stat_timedout;
}

