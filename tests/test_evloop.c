#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "network.h"


/*
 * Called by kqueue when the client socket
 * has data available.
 */
void client_callback(struct evloop *loop, void *args) {
    struct closure *cb = args;

    unsigned char buffer[1024];

    ssize_t n = recv_bytes(
        cb->fd,
        buffer,
        sizeof(buffer)
    );

    if (n > 0) {
        printf(
            "[KQUEUE] Event detected\n"
            "[CLIENT -> SERVER] Received %zd bytes: %.*s\n",
            n,
            (int)n,
            buffer
        );
    }

    /*
     * kqueue uses EV_DISPATCH, so the event
     * must be manually rearmed.
     */
    evloop_rearm_callback_read(
        loop,
        cb
    );
}


int main(void) {
    int sfd;
    int clientfd;

    struct evloop *loop;
    struct closure client_cb;

    sfd = make_listen(
        "127.0.0.1",
        "1883",
        INET
    );

    if (sfd == -1) {
        perror("make_listen");
        return 1;
    }

    printf("[SERVER] Listening on 127.0.0.1:1883\n");

    /*
     * Wait for a client.
     *
     * The socket is non-blocking, so accept()
     * may return -1 until somebody connects.
     */
    while (1) {

        clientfd = accept_connection(sfd);

        if (clientfd != -1)
            break;

        usleep(10000);
    }

    printf("[SERVER] Client connected\n");

    /*
     * Create kqueue event loop.
     */
    loop = evloop_create(
        10,
        -1
    );

    if (loop == NULL) {
        fprintf(stderr, "Could not create event loop\n");

        close(clientfd);
        close(sfd);

        return 1;
    }

    /*
     * Create the closure associated with
     * the client socket.
     */
    client_cb.fd = clientfd;
    client_cb.obj = NULL;
    client_cb.args = &client_cb;
    client_cb.call = client_callback;

    /*
     * Register client socket in kqueue.
     */
    evloop_add_callback(
        loop,
        &client_cb
    );

    printf("[KQUEUE] Waiting for events...\n");

    /*
     * Start kqueue event loop.
     */
    evloop_wait(loop);

    evloop_free(loop);

    close(clientfd);
    close(sfd);

    return 0;
}
