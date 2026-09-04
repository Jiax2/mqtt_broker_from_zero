#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "mqtt.h"
#include "network.h"
#include "server.h"

/*
 * Temporary maximum MQTT packet payload size.
 *
 * The tutorial uses:
 *     conf->max_request_size
 *
 * We don't have the configuration module yet,
 * so we use a constant for now.
 */
#define MAX_REQUEST_SIZE (2 * 1024 * 1024)

/*
 * MQTT fixed header:
 *
 * 1 byte  -> packet type + flags
 * 1-4     -> Remaining Length
 *
 * Maximum header size = 5 bytes.
 */
#define MQTT_HEADER_MAX_SIZE 5

/*
 * Skipped SOL_SECONDS easter egg from the tutorial.
 */

/*
 * Prototype for MQTT command handlers.
 */
typedef int handler(struct closure *, union mqtt_packet *);

/*
 * MQTT command handlers.
 *
 * These will be implemented later in Part 3.
 */
static int connect_handler(struct closure *, union mqtt_packet *);

static int disconnect_handler(struct closure *, union mqtt_packet *);

static int subscribe_handler(struct closure *, union mqtt_packet *);

static int unsubscribe_handler(struct closure *, union mqtt_packet *);

static int publish_handler(struct closure *, union mqtt_packet *);

static int puback_handler(struct closure *, union mqtt_packet *);

static int pubrec_handler(struct closure *, union mqtt_packet *);

static int pubrel_handler(struct closure *, union mqtt_packet *);

static int pubcomp_handler(struct closure *, union mqtt_packet *);

static int pingreq_handler(struct closure *, union mqtt_packet *);

/*
 * MQTT packet type -> handler
 */
static handler *handlers[15] = {
    NULL,                // 0
    connect_handler,     // 1  CONNECT
    NULL,                // 2  CONNACK
    publish_handler,     // 3  PUBLISH
    puback_handler,      // 4  PUBACK
    pubrec_handler,      // 5  PUBREC
    pubrel_handler,      // 6  PUBREL
    pubcomp_handler,     // 7  PUBCOMP
    subscribe_handler,   // 8  SUBSCRIBE
    NULL,                // 9  SUBACK
    unsubscribe_handler, // 10 UNSUBSCRIBE
    NULL,                // 11 UNSUBACK
    pingreq_handler,     // 12 PINGREQ
    NULL,                // 13 PINGRESP
    disconnect_handler   // 14 DISCONNECT
};

/*
 * Temporary information about a new connection.
 */
struct connection {
  char ip[INET_ADDRSTRLEN + 1];
  int fd;
};

/*
 * Main server I/O callbacks.
 */
static void on_read(struct evloop *, void *);

static void on_write(struct evloop *, void *);

static void on_accept(struct evloop *, void *);

/*
 * Periodic callback for broker statistics.
 *
 * It will be implemented later.
 */
static void publish_stats(struct evloop *, void *);

/*
 * Accept a new TCP client and store basic
 * information about the connection.
 */
static int accept_new_client(int serverfd, struct connection *conn) {
  if (conn == NULL)
    return -1;

  /*
   * Accept connection.
   */
  int clientfd = accept_connection(serverfd);

  if (clientfd == -1)
    return -1;

  /*
   * Get remote client's address.
   */
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);

  if (getpeername(clientfd, (struct sockaddr *)&addr, &addrlen) < 0) {

    close(clientfd);
    return -1;
  }

  /*
   * Convert client IP from binary representation
   * to a readable string such as "192.168.1.20".
   */
  if (inet_ntop(AF_INET, &addr.sin_addr, conn->ip, sizeof(conn->ip)) == NULL) {

    close(clientfd);
    return -1;
  }

  conn->fd = clientfd;

  return 0;
}

/*
 * Called when the listening socket detects
 * a new incoming connection.
 */
static void on_accept(struct evloop *loop, void *arg) {
  struct closure *server = arg;

  struct connection conn;

  /*
   * Accept new client.
   */
  if (accept_new_client(server->fd, &conn) == -1) {

    /*
     * The server socket uses EV_DISPATCH,
     * so we must rearm it.
     */
    evloop_rearm_callback_read(loop, server);

    return;
  }

  /*
   * Create a closure for the new client.
   */
  struct closure *client_closure = malloc(sizeof(*client_closure));

  if (client_closure == NULL) {

    close(conn.fd);

    evloop_rearm_callback_read(loop, server);

    return;
  }

  /*
   * Populate client closure.
   *
   * payload and closure_id will be added
   * later when bytestring and UUID support
   * are introduced.
   */
  client_closure->fd = conn.fd;
  client_closure->obj = NULL;
  client_closure->args = client_closure;
  client_closure->call = on_read;

  /*
   * Register the new client in the event loop.
   */
  evloop_add_callback(loop, client_closure);

  /*
   * Rearm listening socket so another
   * client can connect.
   */
  evloop_rearm_callback_read(loop, server);

  printf("[SERVER] New connection from %s (fd=%d)\n", conn.ip, conn.fd);
}

/*
 * Receive one complete MQTT packet.
 *
 * MQTT packet:
 *
 * +----------------------+
 * | Type + Flags         | 1 byte
 * +----------------------+
 * | Remaining Length     | 1-4 bytes
 * +----------------------+
 * | Remaining packet     | N bytes
 * +----------------------+
 */
static ssize_t recv_packet(int clientfd, unsigned char *buf, char *command) {
  ssize_t nbytes = 0;
  int n = 0;

  /*
   * Read first MQTT byte:
   *
   * upper 4 bits -> packet type
   * lower 4 bits -> flags
   */
  nbytes = recv_bytes(clientfd, buf, 1);

  if (nbytes <= 0)
    return -ERRCLIENTDC;

  unsigned char byte = *buf;

  /*
   * Extract and validate packet type.
   */
  union mqtt_header header = {.byte = byte};

  if (header.bits.type < CONNECT || header.bits.type > DISCONNECT) {
    return -ERRPACKETERR;
  }

  /*
   * Move past the first MQTT byte.
   *
   * buf now points to the Remaining Length field.
   */
  buf++;

  /*
   * Remaining Length can use between
   * 1 and 4 bytes.
   */
  unsigned char length_buffer[4];

  int count = 0;

  do {

    /*
     * MQTT Remaining Length cannot
     * exceed 4 encoded bytes.
     */
    if (count >= 4)
      return -ERRPACKETERR;

    n = recv_bytes(clientfd, buf + count, 1);

    if (n <= 0)
      return -ERRCLIENTDC;

    length_buffer[count] = buf[count];

    nbytes += n;

  } while (length_buffer[count++] & 0x80);

  /*
   * Decode MQTT Remaining Length.
   */
  const unsigned char *length_ptr = length_buffer;

  unsigned long long remaining_length = mqtt_decode_length(&length_ptr);

  /*
   * Prevent excessively large packets.
   *
   * Later this will become:
   *
   * conf->max_request_size
   */
  if (remaining_length > MAX_REQUEST_SIZE) {
    return -ERRMAXREQSIZE;
  }

  /*
   * Read the remaining MQTT packet.
   *
   * Remember:
   *
   * buf points after the first byte.
   *
   * count tells us how many bytes were used
   * by Remaining Length.
   */
  if (remaining_length > 0) {

    n = recv_bytes(clientfd, buf + count, remaining_length);

    if (n <= 0)
      return -ERRCLIENTDC;

    nbytes += n;
  }

  /*
   * Save original MQTT first byte so on_read()
   * can determine which handler must be executed.
   */
  *command = byte;

  return nbytes;
}

/*
 * Called when a connected client has data
 * available to read.
 */
static void on_read(struct evloop *loop, void *arg) {
  struct closure *cb = arg;

  /*
   * Allocate enough memory for:
   *
   * maximum packet data
   * +
   * maximum MQTT header size.
   */
  unsigned char *buffer = malloc(MAX_REQUEST_SIZE + MQTT_HEADER_MAX_SIZE);

  if (buffer == NULL)
    return;

  char command = 0;

  /*
   * Receive complete MQTT packet.
   */
  ssize_t bytes = recv_packet(cb->fd, buffer, &command);

  /*
   * Client disconnected.
   */
  if (bytes == -ERRCLIENTDC)
    goto disconnect;

  /*
   * Packet exceeds maximum allowed size.
   */
  if (bytes == -ERRMAXREQSIZE)
    goto disconnect;

  /*
   * Invalid MQTT packet.
   */
  if (bytes == -ERRPACKETERR)
    goto disconnect;

  /*
   * Deserialize raw MQTT bytes into
   * our mqtt_packet structure.
   */
  union mqtt_packet packet;

  unpack_mqtt_packet(buffer, &packet);

  /*
   * Recover packet type from the first byte.
   */
  union mqtt_header header = {.byte = command};

  /*
   * Find handler associated with this
   * MQTT packet type.
   */
  handler *packet_handler = handlers[header.bits.type];

  /*
   * Some packet types do not have an
   * incoming handler.
   */
  if (packet_handler == NULL)
    goto disconnect;

  /*
   * Execute MQTT command handler.
   */
  int rc = packet_handler(cb, &packet);

  /*
   * Handler generated a response.
   *
   * Wait until socket can be written.
   */
  if (rc == REARM_W) {

    cb->call = on_write;

    evloop_rearm_callback_write(loop, cb);
  }

  /*
   * Nothing to send.
   *
   * Continue waiting for another MQTT packet.
   */
  else if (rc == REARM_R) {

    cb->call = on_read;

    evloop_rearm_callback_read(loop, cb);
  }

  free(buffer);

  return;

/*
 * Disconnect client and clean its resources.
 */
disconnect:

  free(buffer);

  printf("[SERVER] Client disconnected (fd=%d)\n", cb->fd);

  /*
   * Remove socket from kqueue.
   */
  evloop_del_callback(loop, cb);

  /*
   * Close TCP connection.
   */
  shutdown(cb->fd, SHUT_RDWR);

  close(cb->fd);

  /*
   * Free closure created in on_accept().
   */
  free(cb);
}

/*
 * Called when a client socket is ready
 * for writing.
 *
 * The complete implementation requires:
 *
 *     cb->payload
 *     struct bytestring
 *     bytestring_release()
 *
 * which are introduced later in Part 3.
 */
static void on_write(struct evloop *loop, void *arg) {
  struct closure *cb = arg;

  /*
   * TODO:
   *
   * send_bytes(
   *     cb->fd,
   *     cb->payload->data,
   *     cb->payload->size
   * );
   *
   * This will be added after implementing
   * struct bytestring.
   */

  /*
   * After sending a response, return
   * to read mode.
   */
  cb->call = on_read;

  evloop_rearm_callback_read(loop, cb);
}