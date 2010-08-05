#include <flexipoll.h>

#include <poll.h>
#include <sys/epoll.h>

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

typedef struct FlexipollEntry {
  int fd;
  short events;
  
  struct FlexipollEntry *next_overall, *next_in_chain;
} FlexipollEntry;

struct Flexipoll {
  int num_fds; /* Initialized to syconf(OPEN_MAX). */
  FlexipollEntry* fd_to_entry; /* array of length num_fds */

  struct pollfd* pollfds; /* array of length num_fds,
                           *  preallocated so we don't have to malloc()
                           *  it on each call to poll().
                           */

  struct {
    FlexipollEntry *entries;
    int count;
  } all, poll, epoll;
};

Flexipoll flexipoll_new(void)
{
  Flexipoll res=(Flexipoll)(malloc(sizeof(struct Flexipoll)));
  if (!res)
    return 0;

  res->num_fds=sysconf(_SC_OPEN_MAX);
  res->fd_to_entry=(FlexipollEntry*)(malloc(sizeof(struct FlexipollEntry)
                                            *(res->num_fds)));
  if (!res->fd_to_entry) {
    int tmp=errno;
    free(res);
    errno=tmp;
    return 0;
  }

  res->pollfds=(struct pollfd*)(malloc(sizeof(struct pollfd)
                                       *(res->num_fds)));
  if (!res->pollfds) {
    int tmp=errno;
    free(res->fd_to_entry);
    free(res);
    errno=tmp;
    return 0;
  }

  res->all.entries=res->poll.entries=res->epoll.entries=0;
  res->all.count=res->poll.count=res->epoll.count=0;

  return res;
}

void flexipoll_delete(Flexipoll fp)
{
  if (!fp)
    return;

  if (fp->pollfds)
    free(fp->pollfds);
  if (fp->fd_to_entry)
    free(fp->fd_to_entry);
  free(fp);
}
