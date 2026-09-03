# MQTT Broker From Scratch

Simple MQTT broker written in **C**, following the MQTT 3.1.1 protocol.

## Part 1 — MQTT Protocol

The first part implements the basic MQTT protocol layer.

Currently implemented:

- MQTT packet structures and headers
- CONNECT, PUBLISH, SUBSCRIBE and UNSUBSCRIBE packets
- ACK packet structures
- MQTT Remaining Length encoding/decoding
- Packet serialization and deserialization helpers
- Basic MQTT packet parsing

### Project structure

```text
src/
├── mqtt.c
├── mqtt.h
├── pack.c
└── pack.h
```

## Part 2 — Networking

The second part implements the networking layer using non-blocking TCP sockets.

Currently implemented:

- TCP socket creation and binding
- Non-blocking sockets
- Client connections
- Send and receive helpers
- Event loop and callbacks
- Basic networking test

The original tutorial uses Linux-specific APIs such as `epoll`.

This project is adapted to run natively on **macOS**, using:

- `epoll` → `kqueue`
- `timerfd` → `EVFILT_TIMER`
- `MSG_NOSIGNAL` → `SO_NOSIGPIPE`

### Project structure

```text
src/
├── mqtt.c
├── mqtt.h
├── network.c
├── network.h
├── pack.c
└── pack.h
```
