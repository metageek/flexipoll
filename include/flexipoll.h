#ifndef _FLEXIPOLL_H_
#define _FLEXIPOLL_H_

/* Opaque handle to a set of fds to poll. */
typedef struct Flexipoll* Flexipoll;

/* Constructor.  On error, returns NULL.
 */
Flexipoll flexipoll_new(void);
/* Destructor. */
void flexipoll_delete(Flexipoll fp);

/* Register a file descriptor to be polled.  events is a bitmap, the
 *  same as in pollfd.events (see poll(2)).  If the given fd is
 *  already being polled, the current bitmap is replaced with events.
 *
 * Returns 0 on success, or <0 on error.
 */
int flexipoll_add_fd(Flexipoll fp, int fd, short events);

/* Unregister a file descriptor.
 *
 * Returns 0 on success, or <0 on error.  It is not an error to unregister
 *  a file descriptor which is not currently registered.
 */
int flexipoll_remove_fd(Flexipoll fp, int fd);

/* Block for fds with events to report. */
int flexipoll_poll(Flexipoll fp);

/* Get the events bitmap for this fd as of the last call to
 *  flexipoll_poll().  Returns <0 on error.
 */
int flexipoll_events(Flexipoll fp, int fd);

#endif /*_FLEXIPOLL_H_*/
