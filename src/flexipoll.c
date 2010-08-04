#include <flexipoll.h>

#include <poll.h>
#include <sys/epoll.h>

typedef struct FlexipollEntry {
  int fd;
  short events;
  
  struct FlexipollEntry *next_overall, *next_in_chain;
} FlexipollEntry;

struct Flexipoll {
  struct {
    FlexipollEntry *entries;
    int count;
  } all, poll, epoll;

  struct pollfd* pollfds;
};
