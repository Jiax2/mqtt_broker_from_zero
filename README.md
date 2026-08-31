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

The next step is **Part 2 — Networking**, where the TCP communication layer will be implemented.
