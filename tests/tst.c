#include <flexipoll.h>
#include <assert.h>
#include <stdio.h>

int main(int argc, const char* argv[])
{
  Flexipoll fp=flexipoll_new();
  if (!fp) {
    perror("flexipoll_new");
    return 1;
  }

  if (flexipoll_add_fd(fp,0,POLLIN)<0) {
    perror("flexipoll_add_fd");
    return 2;
  }

  int fds_with_events[10];
  const int max_fds=sizeof(fds_with_events)/sizeof(fds_with_events[0]);

  int N=flexipoll_poll(fp,fds_with_events,max_fds);
  if (N<0) {
    perror("flexipoll_poll");
    return 3;
  }

  assert(N==1);
  int events=flexipoll_events(fp,0);
  assert (events==POLLIN);

  {
    char buff[512];
    int nbytes=read(0,buff,sizeof(buff));
    if (nbytes<0) {
      perror("read");
      return 4;
    }

    int written=write(1,buff,nbytes);
    if (written<0) {
      perror("write");
      return 5;
    }
    if (written<nbytes) {
      fprintf(stderr,"Wrote only %d bytes out of %d\n",written,nbytes);
      return 6;
    }
  }

  return 0;
}
