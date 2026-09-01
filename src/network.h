#ifndef NETWORK_H
#define NETWORK_H

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/event.h>

// Socket families
#define UNIX 0
#define INET 1

/* Set non-blocking socket */
int set_nonblocking(int);

/* Disable Nagle's algorithm */
int set_tcp_nodelay(int);

/* Create and bind socket */
int create_and_bind(const char *, const char *, int);

/* Create listening socket */
int make_listen(const char *, const char *, int);

/* Accept connection */
int accept_connection(int);

/* Send all available bytes */
ssize_t send_bytes(int, const unsigned char *, size_t);

/* Receive bytes */
ssize_t recv_bytes(int, unsigned char *, size_t);


/*
 * Event loop wrapper for macOS kqueue.
 */
struct evloop {
    int kqfd;
    int max_events;
    int timeout;
    int status;

    struct kevent *events;

    int periodic_maxsize;
    int periodic_nr;

    struct {
        int timer_id;
        struct closure *closure;
    } **periodic_tasks;
};


/* Callback function type */
typedef void callback(struct evloop *, void *);


/* Callback object */
struct closure {
    int fd;
    void *obj;
    void *args;

    callback *call;
};


struct evloop *evloop_create(int, int);

void evloop_init(struct evloop *, int, int);

void evloop_free(struct evloop *);

int evloop_wait(struct evloop *);

void evloop_add_callback(
    struct evloop *,
    struct closure *
);

void evloop_add_periodic_task(
    struct evloop *,
    int,
    unsigned long long,
    struct closure *
);

int evloop_del_callback(
    struct evloop *,
    struct closure *
);

int evloop_rearm_callback_read(
    struct evloop *,
    struct closure *
);

int evloop_rearm_callback_write(
    struct evloop *,
    struct closure *
);

#endif