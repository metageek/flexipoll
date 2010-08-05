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

  struct epoll_event* epvs; /* preallocated array of length num_fds */

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

  res->epvs=(struct epoll_event*)(malloc(sizeof(struct epoll_event)
                                         *(res->num_fds)));
  if (!res->pollfds) {
    int tmp=errno;
    free(res->pollfds);
    free(res->fd_to_entry);
    free(res);
    errno=tmp;
    return 0;
  }

  res->epoll_fd=epoll_create1(EPOLL_CLOEXEC);
  if ((res->epoll_fd)<0) {
    int tmp=errno;
    free(res->epvs);
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

  if (fp->epvs)
    free(fp->epvs);
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

  if ((fd<0) || (fd>=fp->num_fds)) {
    errno=EBADF;
    return -1;
  }

  if (events & (~all_events)) {
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
      struct epoll_event epv;
      epv.events=events;
      epv.data.ptr=entry;

      if (epoll_ctl(fp->epoll_fd,EPOLL_CTL_MOD,fd,&epv)<0) {
        int tmp=errno;
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

  if ((fd<0) || (fd>=fp->num_fds)) {
    errno=EBADF;
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

int flexipoll_poll(Flexipoll fp)
{
  if (!fp) {
    errno=EFAULT;
    return -1;
  }

  fp->pollfds[0].fd=fp->epoll_fd;
  fp->pollfds[0].events=POLLIN;
  fp->pollfds[0].revents=0;

  {
    int i=1;
    FlexipollEntry* entry=fp->poll.entries;
    while (entry) {
      fp->pollfds[i].fd=fp->epoll_fd;
      fp->pollfds[i].events=entry->events;
      fp->pollfds[i].revents=0;

      entry=entry->next_in_chain;
      i+=1;
    }
  }

  if (poll(fp->pollfds,fp->poll.count+1,-1)<0) {
    int tmp=errno;
    perror("poll");
    errno=tmp;
    return -1;
  }

  {
    int i=1;
    FlexipollEntry* entry=fp->poll.entries;
    while (entry) {
      entry->revents=fp->pollfds[i].revents;

      entry=entry->next_in_chain;
      i+=1;
    }
  }

  if (fp->pollfds[0].revents & POLLIN) {
    int num_events=epoll_wait(fp->epoll_fd,fp->epvs,fp->num_fds,0);
    if (num_events<0) {
      int tmp=errno;
      perror("epoll");
      errno=tmp;
      return -1;
    }

    int i;
    for (i=0; i<num_events; i++) {
      FlexipollEntry* entry=(FlexipollEntry*)(fp->epvs[i].data.ptr);
      entry->revents=fp->epvs[i].events;
    }
  }

  return 0;
}

int flexipoll_events(Flexipoll fp, int fd)
{
  if (!fp) {
    errno=EFAULT;
    return -1;
  }

  if ((fd<0) || (fd>=fp->num_fds)) {
    errno=EBADF;
    return -1;
  }

  FlexipollEntry* entry=fp->fd_to_entry+fd;
  if (entry->fd<0) {
    errno=EINVAL;
    return -1;
  }

  return entry->revents;
}
