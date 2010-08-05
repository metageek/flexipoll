#include <flexipoll.h>

#include <poll.h>
#include <sys/epoll.h>

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

static const short all_events=(POLLIN
                               |POLLOUT
#ifdef _GNU_SOURCE
                               |POLLRDHUP
#endif
                               |POLLPRI
                               |POLLERR
                               |POLLHUP);

typedef struct FlexipollEntry {
  int fd;
  short events,revents;

  int active,total,in_epoll;

  struct epoll_event epv;
  
  struct FlexipollEntry *next_overall, *next_in_chain,
    *prev_overall, *prev_in_chain;
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

  int epoll_fd;
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

  res->epoll_fd=epoll_create1(EPOLL_CLOEXEC);
  if ((res->epoll_fd)<0) {
    int tmp=errno;
    free(res->pollfds);
    free(res->fd_to_entry);
    free(res);
    errno=tmp;
    return 0;
  }


  {
    int i;
    for (i=0; i<res->num_fds; i++)
      res->fd_to_entry[i].fd=-1;
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
  if (fp->epoll_fd>=0)
    close(fp->epoll_fd);
  free(fp);
}

int flexipoll_add_fd(Flexipoll fp, int fd, short events)
{
  if (!fp) {
    errno=EFAULT;
    return -1;
  }

  if ((fd<0)
      || (fd>=fp->num_fds)
      || (events & (~all_events))
      ) {
    errno=EINVAL;
    return -1;
  }

  FlexipollEntry* entry=fp->fd_to_entry+fd;
  if (entry->fd<0) {
    entry->fd=fd;
    entry->active=entry->total=0;
    entry->in_epoll=0;
    entry->revents=0;

    entry->next_overall=fp->all.entries;
    entry->prev_overall=0;
    fp->all.entries=entry;
    fp->all.count++;

    entry->next_in_chain=fp->poll.entries;
    entry->prev_in_chain=0;
    fp->poll.entries=entry;
    fp->poll.count++;
  } else {
    if (entry->in_epoll) {
      short old_events=entry->epv.events;
      entry->epv.events=events;
      if (epoll_ctl(fp->epoll_fd,EPOLL_CTL_MOD,fd,&(entry->epv))<0) {
        int tmp=errno;
        entry->epv.events=old_events;
        perror("epoll_ctl");
        errno=tmp;
        return -1;
      }
    }
  }

  entry->events=events;
  return 0;
}

int flexipoll_remove_fd(Flexipoll fp, int fd)
{
  if (!fp) {
    errno=EFAULT;
    return -1;
  }

  if ((fd<0)
      || (fd>=fp->num_fds)
      ) {
    errno=EINVAL;
    return -1;
  }

  FlexipollEntry* entry=fp->fd_to_entry+fd;
  if (entry->fd<0)
    return 0;

  if (entry->in_epoll) {
    if (epoll_ctl(fp->epoll_fd,EPOLL_CTL_DEL,fd,0)<0) {
      int tmp=errno;
      perror("epoll_ctl");
      errno=tmp;
      return -1;
    }
  }

  entry->fd=0;
}
