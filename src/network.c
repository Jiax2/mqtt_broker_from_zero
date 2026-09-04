#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>

#include "network.h"

#define EVLOOP_INITIAL_SIZE 4

int set_nonblocking(int fd) {
  int flags;

  flags = fcntl(fd, F_GETFL, 0);

  if (flags == -1)
    return -1;

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    return -1;

  return 0;
}

/* Disable Nagle's algorithm */
int set_tcp_nodelay(int fd) {
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int));
}

/* macOS alternative to MSG_NOSIGNAL */
static int set_nosigpipe(int fd) {
  return setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &(int){1}, sizeof(int));
}

static int create_and_bind_unix(const char *path) {
  struct sockaddr_un addr;
  int fd;

  fd = socket(AF_UNIX, SOCK_STREAM, 0);

  if (fd == -1)
    return -1;

  memset(&addr, 0, sizeof(addr));

  addr.sun_family = AF_UNIX;

  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  unlink(path);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {

    close(fd);
    return -1;
  }

  return fd;
}

static int create_and_bind_tcp(const char *host, const char *port) {
  struct addrinfo hints;
  struct addrinfo *result;
  struct addrinfo *rp;

  int sfd;

  memset(&hints, 0, sizeof(hints));

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo(host, port, &hints, &result) != 0)
    return -1;

  for (rp = result; rp != NULL; rp = rp->ai_next) {

    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

    if (sfd == -1)
      continue;

    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {

      perror("SO_REUSEADDR");
    }

    if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {

      break;
    }

    close(sfd);
  }

  if (rp == NULL) {
    freeaddrinfo(result);
    return -1;
  }

  freeaddrinfo(result);

  return sfd;
}

int create_and_bind(const char *host, const char *port, int socket_family) {
  if (socket_family == UNIX)
    return create_and_bind_unix(host);

  if (socket_family == INET)
    return create_and_bind_tcp(host, port);

  return -1;
}

int make_listen(const char *host, const char *port, int socket_family) {
  int sfd;

  sfd = create_and_bind(host, port, socket_family);

  if (sfd == -1)
    return -1;

  if (set_nonblocking(sfd) == -1) {
    close(sfd);
    return -1;
  }

  if (socket_family == INET) {

    if (set_tcp_nodelay(sfd) == -1) {
      close(sfd);
      return -1;
    }

    if (set_nosigpipe(sfd) == -1) {
      close(sfd);
      return -1;
    }
  }

  /*
   * Temporary replacement for:
   *
   * conf->tcp_backlog
   */
  if (listen(sfd, SOMAXCONN) == -1) {
    close(sfd);
    return -1;
  }

  return sfd;
}

int accept_connection(int sfd) {
  int clientfd;

  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);

  clientfd = accept(sfd, (struct sockaddr *)&addr, &addrlen);

  if (clientfd < 0)
    return -1;

  if (set_nonblocking(clientfd) == -1) {
    close(clientfd);
    return -1;
  }

  if (set_tcp_nodelay(clientfd) == -1) {
    close(clientfd);
    return -1;
  }

  if (set_nosigpipe(clientfd) == -1) {
    close(clientfd);
    return -1;
  }

  char ip_buffer[INET_ADDRSTRLEN + 1];

  if (inet_ntop(AF_INET, &addr.sin_addr, ip_buffer, sizeof(ip_buffer)) ==
      NULL) {

    close(clientfd);
    return -1;
  }

  return clientfd;
}

ssize_t send_bytes(int fd, const unsigned char *buf, size_t len) {
  size_t total = 0;
  size_t bytesleft = len;

  ssize_t n = 0;

  while (total < len) {

    n = send(fd, buf + total, bytesleft, 0);

    if (n == -1) {

      if (errno == EAGAIN || errno == EWOULDBLOCK) {

        break;
      }

      goto err;
    }

    total += n;
    bytesleft -= n;
  }

  return total;

err:

  fprintf(stderr, "send(2) - error sending data: %s\n", strerror(errno));

  return -1;
}

ssize_t recv_bytes(int fd, unsigned char *buf, size_t bufsize) {
  ssize_t n = 0;
  ssize_t total = 0;

  while (total < (ssize_t)bufsize) {

    n = recv(fd, buf, bufsize - total, 0);

    if (n < 0) {

      if (errno == EAGAIN || errno == EWOULDBLOCK) {

        break;
      }

      goto err;
    }

    if (n == 0)
      return 0;

    buf += n;
    total += n;
  }

  return total;

err:

  fprintf(stderr, "recv(2) - error reading data: %s\n", strerror(errno));

  return -1;
}

/******************************
 *         KQUEUE API         *
 ******************************/

struct evloop *evloop_create(int max_events, int timeout) {
  struct evloop *loop;

  loop = malloc(sizeof(*loop));

  if (loop == NULL)
    return NULL;

  evloop_init(loop, max_events, timeout);

  return loop;
}

void evloop_init(struct evloop *loop, int max_events, int timeout) {
  loop->max_events = max_events;

  loop->events = malloc(sizeof(struct kevent) * max_events);

  loop->kqfd = kqueue();

  loop->timeout = timeout;

  loop->periodic_maxsize = EVLOOP_INITIAL_SIZE;

  loop->periodic_nr = 0;

  loop->periodic_tasks =
      malloc(EVLOOP_INITIAL_SIZE * sizeof(*loop->periodic_tasks));

  loop->status = 0;
}

void evloop_free(struct evloop *loop) {

  free(loop->events);

  for (int i = 0; i < loop->periodic_nr; i++) {
    free(loop->periodic_tasks[i]);
  }

  free(loop->periodic_tasks);

  close(loop->kqfd);

  free(loop);
}

static int kqueue_add(int kqfd, int fd, int16_t filter, void *data) {
  struct kevent event;

  EV_SET(&event, fd, filter, EV_ADD | EV_CLEAR | EV_DISPATCH, 0, 0, data);

  return kevent(kqfd, &event, 1, NULL, 0, NULL);
}

static int kqueue_mod(int kqfd, int fd, int16_t filter, void *data) {
  struct kevent event;

  EV_SET(&event, fd, filter, EV_ADD | EV_ENABLE | EV_CLEAR | EV_DISPATCH, 0, 0,
         data);

  return kevent(kqfd, &event, 1, NULL, 0, NULL);
}

static int kqueue_del(int kqfd, int fd) {
  struct kevent event;

  EV_SET(&event, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);

  kevent(kqfd, &event, 1, NULL, 0, NULL);

  EV_SET(&event, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

  kevent(kqfd, &event, 1, NULL, 0, NULL);

  return 0;
}

void evloop_add_callback(struct evloop *loop, struct closure *cb) {
  if (kqueue_add(loop->kqfd, cb->fd, EVFILT_READ, cb) < 0) {

    perror("kqueue register callback");
  }
}

void evloop_add_periodic_task(struct evloop *loop, int seconds,
                              unsigned long long ns, struct closure *cb) {
  struct kevent event;

  int timer_id = loop->periodic_nr + 1;

  unsigned long long microseconds =
      ((unsigned long long)seconds * 1000000ULL) + (ns / 1000ULL);

  if (microseconds == 0)
    microseconds = 1;

  EV_SET(&event, timer_id, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_USECONDS,
         microseconds, NULL);

  if (kevent(loop->kqfd, &event, 1, NULL, 0, NULL) < 0) {

    perror("kevent EVFILT_TIMER");
    return;
  }

  if (loop->periodic_nr + 1 > loop->periodic_maxsize) {
    loop->periodic_maxsize *= 2;

    loop->periodic_tasks =
        realloc(loop->periodic_tasks,
                loop->periodic_maxsize * sizeof(*loop->periodic_tasks));
  }

  loop->periodic_tasks[loop->periodic_nr] =
      malloc(sizeof(*loop->periodic_tasks[loop->periodic_nr]));

  loop->periodic_tasks[loop->periodic_nr]->timer_id = timer_id;

  loop->periodic_tasks[loop->periodic_nr]->closure = cb;

  loop->periodic_nr++;
}

int evloop_wait(struct evloop *loop) {

  int rc = 0;
  int events = 0;

  while (1) {

    struct timespec timeout;
    struct timespec *timeout_ptr = NULL;

    if (loop->timeout >= 0) {

      timeout.tv_sec = loop->timeout / 1000;

      timeout.tv_nsec = (loop->timeout % 1000) * 1000000L;

      timeout_ptr = &timeout;
    }

    events = kevent(loop->kqfd, NULL, 0, loop->events, loop->max_events,
                    timeout_ptr);

    if (events < 0) {

      if (errno == EINTR)
        continue;

      rc = -1;
      loop->status = errno;

      break;
    }

    for (int i = 0; i < events; i++) {

      struct kevent *event = &loop->events[i];

      if (event->flags & EV_ERROR) {

        loop->status = (int)event->data;

        continue;
      }

      if (event->filter == EVFILT_TIMER) {

        for (int j = 0; j < loop->periodic_nr; j++) {

          if ((int)event->ident == loop->periodic_tasks[j]->timer_id) {

            struct closure *cb = loop->periodic_tasks[j]->closure;

            cb->call(loop, cb->args);

            break;
          }
        }

        continue;
      }

      struct closure *cb = (struct closure *)event->udata;

      if (cb != NULL) {

        cb->call(loop, cb->args);
      }
    }
  }

  return rc;
}

int evloop_rearm_callback_read(struct evloop *loop, struct closure *cb) {
  return kqueue_mod(loop->kqfd, cb->fd, EVFILT_READ, cb);
}

int evloop_rearm_callback_write(struct evloop *loop, struct closure *cb) {
  return kqueue_mod(loop->kqfd, cb->fd, EVFILT_WRITE, cb);
}

int evloop_del_callback(struct evloop *loop, struct closure *cb) {
  return kqueue_del(loop->kqfd, cb->fd);
}