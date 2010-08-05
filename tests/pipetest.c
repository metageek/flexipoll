/* pipetest.c
 *	Scalability test of async poll functionality on pipes.
 *	Copyright 2002, 2006 Red Hat, Inc.
 *	Portions Copyright 2001 Davide Libenzi <davidel@xmailserver.org>.
 *	<include GPL>
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/resource.h>


#undef DEBUG 

#if !defined(DEBUG)
#define dprintf(x...)
#define d2printf(x...)
#else
#define dprintf(x...)	printf(x)
#define d2printf(x...)	printf(x)
#endif

enum {
	MODE_POLL,
	MODE_SYS_EPOLL,
	MODE_KEVENT_POLL,
} mode = MODE_POLL;

const char *modes[] = {
	"poll",
	"sys-epoll",
};

int gnuplot = 0;

int epoll_fd = -1;
int done;

#define FD_SLOP	10
#define MAX_FDS 64*1024	

#define READ	0
#define WRITE	1

struct {
	int fds[2];
} pipefds[MAX_FDS/2];

int nr_pipes;

long	nr_token_passes;
long	max_generation;
int	max_threads = 1;
int	threads_complete = 0;

struct token {
	long	generation;
	int	tofd;
	int	thread;
};

int BUFSIZE = sizeof(struct token);

struct fdinfo {
	int		zero;
	//struct iocb	iocb2;
	int		one;
	int		fd;
	int		pipe_idx;
	int		active:1;
	int		rw:1;
	char		*buf;
	//char		buf2[BUFSIZE];
} fdinfo[MAX_FDS];

#define queue_iocb(iocb)	(pending_iocbs[nr_pending_iocbs++] = (iocb))

struct pollfd	pollfds[MAX_FDS];
int nr_pollfds;

struct iocb *pending_iocbs[MAX_FDS*3];
int nr_pending_iocbs;

struct token pending_tokens[MAX_FDS];
int nr_pending_tokens;

void pexit(char *msg)
{
	perror(msg);
	exit(1);
}

void really_send_toke(struct token *toke)
{
	struct fdinfo *inf;
	int *fds;
	int fd;
	int pipe_idx = nr_pipes - toke->thread - 1;

	fds = pipefds[pipe_idx].fds;
	fd = fds[WRITE];
	inf = &fdinfo[fd];

	dprintf("sending on pipe index %u, fd %d\n", pipe_idx, fd);

    int res = write(fds[WRITE], toke, BUFSIZE);
    d2printf("write[%ld %d %d]\n", toke->generation,
        toke->tofd, toke->thread);
    if (res != BUFSIZE) {
        printf("write = %d (%s)", res, strerror(errno));
        exit(1);
    }
}

void send_pending_tokes(void)
{
	int i;
	for (i=0; i<nr_pending_tokens; i++)
		really_send_toke(&pending_tokens[i]);
	nr_pending_tokens = 0;
}

void send_toke(struct token *toke)
{
	unsigned pipe_idx;
	int *fds;

#if 0
	/* what the heck is this?  -- JEM */
	pipe_idx = toke->generation * 17 + toke->thread;
	pipe_idx += toke->thread * (nr_pipes / max_threads);
	pipe_idx %= nr_pipes;
#endif
	pipe_idx = nr_pipes - toke->thread - 1;

	fds = pipefds[pipe_idx].fds;

	toke->tofd = fds[READ];
#if 1
	pending_tokens[nr_pending_tokens++] = *toke;
#else
	really_send_toke(toke);
#endif
}

void process_token(int fd, struct token *toke, int nr)
{
	if (nr != BUFSIZE)
		fprintf(stderr, "process_token: nr == %d (vs %d)\n",
			nr, BUFSIZE);
	assert(nr == BUFSIZE);
	assert(toke->tofd == fd);
	nr_token_passes++;
	toke->generation++;

	dprintf("passed %ld\n", nr_token_passes);

	if (toke->generation < max_generation)
		send_toke(toke);
	else {
		dprintf("thread %d complete, %ld passes\n", toke->thread, nr_token_passes);
		threads_complete++;
		if (threads_complete >= max_threads)
			done = 1;
	}
}

void read_and_process_token(int fd)
{
	//char buf[BUFSIZE];
	struct token *toke;
	struct fdinfo *inf = &fdinfo[fd];
	char *buf = inf->buf;
	int nr;
	nr = read(fd, buf, BUFSIZE);
	if (-1 == nr)
		pexit("read");
	/* kludge: works around epoll edge notification bug */
	toke = (struct token *)buf;
	while (nr >= BUFSIZE) {
		process_token(fd, toke, BUFSIZE);
		toke++;
		nr -= BUFSIZE;
	}
	assert(nr == 0);
}


void makeapipe(int idx)
{
	int *fds = pipefds[idx].fds;
	struct fdinfo *inf;
	int i;

	if (pipe(fds))
		pexit("pipe");

	for (i=0; i<2; i++) {
		int fl = fcntl(fds[i], F_GETFL);
		fl |= O_NONBLOCK;
		fcntl(fds[i], F_SETFL, fl);
	}

	inf = &fdinfo[fds[READ]];
	inf->buf = calloc(1, BUFSIZE);
	assert(inf->buf != NULL);
	assert(inf->active == 0);
	inf->active = 1;
	inf->rw = READ;
	inf->fd = fds[READ];
	inf->pipe_idx = idx;

	if (mode == MODE_POLL) {
		pollfds[nr_pollfds].fd = fds[READ];
		pollfds[nr_pollfds].events = POLLIN;
		pollfds[nr_pollfds].revents = 0;
		nr_pollfds++;
	}

	if (mode == MODE_SYS_EPOLL) {
		struct epoll_event event;

		memset(&event, 0, sizeof(event));
		event.data.fd = fds[READ];
		event.events = EPOLLIN;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[READ], &event) < 0)
			pexit("epoll_ctl");
	}

	inf = &fdinfo[fds[WRITE]];
	assert(inf->active == 0);
	inf->active = 1;
	inf->rw = WRITE;
	inf->fd = fds[WRITE];
	inf->pipe_idx = idx;
}

void makepipes(int nr)
{
	int i;
	for (i=0; i<nr; i++)
		makeapipe(i);
	nr_pipes = nr;
}


void poll_main_loop(void)
{
	int res;
	while (!done) {
		int i, j;

		dprintf("poll_main_loop: poll(%d)\n", nr_pollfds);
		send_pending_tokes();
		res = poll(pollfds, nr_pollfds, -1);
		if (res <= 0)
			pexit("poll");

		dprintf("got %d\n", res);

		for (i=0,j=0; i<res; j++) {
			if (pollfds[j].revents & POLLIN) {
				read_and_process_token(pollfds[j].fd);
				i++;
			}
		}		
	}
}

void sys_epoll_setup(void)
{
	epoll_fd = epoll_create(MAX_FDS);
	if (-1 == epoll_fd)
		pexit("epoll_create");
}

void sys_epoll_main_loop(void)
{
	struct epoll_event *event,
		     *epevents = malloc(sizeof(struct epoll_event) * MAX_FDS);
	int i, nfds;

	while (!done) {
		send_pending_tokes();

		nfds = epoll_wait(epoll_fd, epevents, MAX_FDS, 100);
        dprintf("got %d\n", nfds);

		if (nfds < 0)
			pexit("epoll_wait");
		for (i = 0, event = epevents; i < nfds; i++, event++) {
			if (event->events & EPOLLIN)
				read_and_process_token(event->data.fd);
		}
	}
}

void seedthreads(int nr)
{
	struct token toke;
	int i;

	for (i=0; i<nr; i++) {
		toke.generation = 0;
		toke.thread = i;
		toke.tofd = -1;
		send_toke(&toke);
	}
}

int main(int argc, char *argv[])
{
	struct timeval stv, etv;
	int nr;
	long long usecs, passes_per_sec;

	while (argc > 1) {
		if (0 == strcmp(argv[1], "--poll")) {
			mode = MODE_POLL;
		} else if (0 == strcmp(argv[1], "--sys-epoll")) {
			mode = MODE_SYS_EPOLL;
		} else if (0 == strcmp(argv[1], "--bufsize")) {
			argv++,argc--;
			BUFSIZE = atoi(argv[1]);
			assert(BUFSIZE > (int)sizeof(struct token));
		} else if (0 == strcmp(argv[1], "--gnuplot")) {
			gnuplot = 1;
		} else
			break;
		argv++,argc--;
	}

	if (argc != 4) {
		fprintf(stderr, "usage: pipetest [--poll | --sys-epoll]\n"
            "\t[--bufsize] <num pipes> <message threads> <max generation>\n");
		return 2;
	}

	nr = atoi(argv[1]);
	max_threads = atoi(argv[2]);
	max_generation = atol(argv[3]);

    struct rlimit fdlimit = {.rlim_cur=100000, .rlim_max=100000};
    setrlimit(RLIMIT_NOFILE, &fdlimit);

	if (gnuplot)
		printf("%d %d %d ", nr, max_threads, BUFSIZE);
	else
		printf("using %d pipe pairs, %d message threads, %ld generations, %d bufsize\n",
		       nr, max_threads, max_generation, BUFSIZE);

	if (nr < 2) {
		printf("uhm, please specify at least 2 pipe pairs\n");
		exit(1);
	}

	if (nr >= (MAX_FDS/2 - FD_SLOP)) {
		printf("%d exceeds limit of %d pipe pairs.\n", nr,
			(MAX_FDS/2-FD_SLOP));
		exit(1);
	}

	if (mode == MODE_SYS_EPOLL)
		sys_epoll_setup();

	makepipes(nr);

	/* epoll and poll both have their startup overhead in 
	 * makepipes().  By submitting the initial read/poll 
	 * requests for aio now, we avoid measuring the static 
	 * overhead of aio as the number of file descriptors 
	 * increases.
	 */
	send_pending_tokes();

	gettimeofday(&stv, NULL);

	seedthreads(max_threads);

	if (mode == MODE_POLL)
		poll_main_loop();

	if (mode == MODE_SYS_EPOLL)
		sys_epoll_main_loop();

	gettimeofday(&etv, NULL);

	etv.tv_sec -= stv.tv_sec;
	etv.tv_usec -= stv.tv_usec;
	if (etv.tv_usec < 0) {
		etv.tv_usec += 1000000;
		etv.tv_sec -= 1;
	}

	if (!gnuplot)
		printf("Ok! Mode %s: %ld passes in %ld.%06ld seconds\n",
		       modes[mode],
		       nr_token_passes,
		       etv.tv_sec, etv.tv_usec);

	usecs = etv.tv_usec + etv.tv_sec * 1000000LL;
	passes_per_sec = nr_token_passes * 1000000LL * 100;
	passes_per_sec /= usecs;

	if (gnuplot)
		printf("%Ld.%02Ld\n",
		       passes_per_sec / 100, passes_per_sec % 100);
	else
		printf("passes_per_sec: %Ld.%02Ld\n",
		       passes_per_sec / 100,
		       passes_per_sec % 100);

	return 0;
}
