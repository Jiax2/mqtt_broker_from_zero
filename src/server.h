#ifndef SERVER_H
#define SERVER_H

/*
 * Event loop settings.
 * -1 means no timeout, so the loop blocks indefinitely.
 */
#define EVLOOP_MAX_EVENTS 256
#define EVLOOP_TIMEOUT    -1

/*
 * Packet reception errors.
 */
#define ERRCLIENTDC    1
#define ERRPACKETERR   2
#define ERRMAXREQSIZE  3

/*
 * Handler return codes.
 * Decide whether the client socket must
 * be rearmed for reading or writing.
 */
#define REARM_R 0
#define REARM_W 1

int start_server(
    const char *,
    const char *
);

#endif

int prueba(){
  hoaf; s
}