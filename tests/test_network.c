#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "network.h"

int main(void) {
    int sfd;
    int clientfd;
    unsigned char buffer[1024];

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

    while (1) {

        clientfd = accept_connection(sfd);

        if (clientfd == -1) {
            usleep(10000);
            continue;
        }

        printf("[SERVER] Client connected\n");

        while (1) {

            ssize_t n = recv_bytes(
                clientfd,
                buffer,
                sizeof(buffer)
            );

            if (n > 0) {

                printf(
                    "[CLIENT -> SERVER] Received %zd bytes: %.*s\n",
                    n,
                    (int)n,
                    buffer
                );

                send_bytes(
                    clientfd,
                    buffer,
                    n
                );

                printf(
                    "[SERVER -> CLIENT] Sent %zd bytes: %.*s\n",
                    n,
                    (int)n,
                    buffer
                );
            }

            /*
             * No data available yet.
             * The socket is non-blocking.
             */
            if (n == 0) {
                usleep(10000);
                continue;
            }

            if (n < 0) {
                perror("recv_bytes");
                break;
            }
        }

        close(clientfd);
    }

    close(sfd);

    return 0;
}