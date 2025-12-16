# `net` library – UDP network layer

The `net` library provides a lightweight, non‑blocking network layer to synchronise a multi‑player game. It relies on the OS UDP sockets, avoids any external dependency and uses simple C structures to represent packets. Its purpose is to carry client inputs to the server and return state snapshots from the server to the client at a fixed rate.

## Operating principles

* **UDP and non‑blocking**: communication uses the UDP protocol to minimise latency. Sockets are configured in non‑blocking mode. The functions `pollInputs()` (server side) and `pollSnapshot()` (client side) must be called regularly to drain the receive queue.

* **Fixed cadence**: the server sends snapshots at a fixed cadence (for example 60 times per second). Similarly, the client sends its inputs on every iteration of its loop. There is no automatic retransmission; lost data are compensated by the sending frequency.

* **Role of the server**: the server listens on a UDP port, manages a fixed array of “slots” for clients, assigns a slot to each unknown address and returns a state snapshot. It maintains for each slot the last input sequence numbers received and processed as well as a snapshot counter.

* **Role of the client**: the client periodically sends its inputs to the server and receives state snapshots. It applies these snapshots to update its own representation of the world and adjusts its predictions.

## Slot management

The internal structure `ClientSlot` contains:

- **`active`**: indicates whether the slot is in use.
- **`endpoint`**: client address and port (`sockaddr_in`).
- **`lastReceivedInput`**: highest input sequence number received.
- **`lastProcessedInput`**: highest input sequence number integrated into the simulation.
- **`snapshotCounter`**: monotonically increasing identifier of snapshots sent to this client.

When the server receives an input packet from an unknown address, it looks for a free slot (`active == false`) and associates it with that address. If all slots are occupied (default `MAX_DEFAULT_CLIENTS = 4`), new clients are ignored. Slots are not freed automatically; it is possible to implement an inactivity timeout in the calling code if necessary.

## Packet descriptions

The protocol defines several packet structures serialised in memory. **All numeric values are stored in host byte order**: there is no automatic network conversion. If communication occurs between machines with different endianness (little‑endian/big‑endian), it is necessary to manually convert the values.

### Input packet (`InputPacket`)

The client sends an `InputPacket` to the server for each game frame. The structure is:

```
+----------------------+
| magic               |
| protocolVersion     |
| inputSequence       |
| clientFrame         |
| moveX               |
| moveY               |
| firePressed         |
| fireHeld            |
| fireReleased        |
| padding             |
+----------------------+
```

* **`magic`**: must be equal to `INPUT_MAGIC` (constant `0x49505430u`, i.e. "IPT0"). Allows validating the packet.
* **`protocolVersion`**: protocol version (currently 1). Allows detecting inconsistencies during an update.
* **`inputSequence`**: monotonically increasing sequence number, incremented on each send. The server returns the last processed number in the snapshot to allow the client to discard inputs already integrated.
* **`clientFrame`**: local frame counter, optional (can be used for statistics or prediction).
* **`moveX` / `moveY`**: floating values between −1 and 1 representing movement axes.
* **`firePressed` / `fireHeld` / `fireReleased`**: indicators (`0` or `1`) for firing actions depending on whether the button was just pressed, held or released during this frame.
* **`padding`**: reserved byte for alignment or future fields.

The client must send this packet in a constant stream, even if the input state has not changed, to keep the connection active and allow the server to compute movements from the most recent inputs.

### Snapshot header (`SnapshotHeader`)

The server responds with a `SnapshotPacket` which begins with a `SnapshotHeader` and then a list of `SnapshotEntity`. The header is:

```
+----------------------+
| magic               |
| protocolVersion     |
| snapshotId          |
| serverFrame         |
| lastProcessedInput  |
| controlledId        |
| entityCount         |
| reserved            |
+----------------------+
```

* **`magic`**: must equal `SNAP_MAGIC` (constant `0x534E4150u`, i.e. "SNAP").
* **`protocolVersion`**: protocol version (currently 1).
* **`snapshotId`**: snapshot identifier that increases monotonically for this client. Allows detecting lost or delayed packets.
* **`serverFrame`**: server‑side frame counter (for example number of updates performed).
* **`lastProcessedInput`**: largest input sequence number applied in this frame for this client. The client can remove from its queue the inputs whose number is less than or equal to this value.
* **`controlledId`**: identifier of the entity controlled by this client, or `0xffffffff` if no entity is associated.
* **`entityCount`**: number of `SnapshotEntity` entries that immediately follow the header. If this number exceeds `MAX_ENTITIES`, the server truncates the list to remain under the maximum UDP datagram size.
* **`reserved`**: reserved field for future flags.

### Snapshot entity (`SnapshotEntity`)

Each entity contained in a snapshot is serialised by a `SnapshotEntity`:

```
+----------------------+  <-- start of each entity
| id                  |
| generation          |
| alive               |
| hasPosition         |
| x                   |
| y                   |
| hasVelocity         |
| vx                  |
| vy                  |
| hasHealth           |
| health              |
| respawnable         |
| hasCollision        |
| hitHalfWidth        |
| hitHalfHeight       |
| padding[3]          |
+----------------------+  <-- end of the entity
```

* **`id`**: entity identifier in the ECS.
* **`generation`**: generation/version number of the entity (unused here, always zero).
* **`alive`**: `1` if the entity is alive, `0` otherwise.
* **`hasPosition`** and **`hasVelocity`**: flags indicating whether the position or velocity fields are valid. The values `x`, `y`, `vx` and `vy` should only be read if the corresponding indicators are `1`.
* **`hasHealth`**: indicates whether the hit points field follows.
* **`respawnable`**: replicates the `Respawnable` component to know whether the entity should respawn.
* **`hasCollision`**: indicates whether collision data are valid and whether the entity should be considered collidable.
* **`hitHalfWidth` / `hitHalfHeight`**: half‑width and half‑height of the collision box. These values are present only if `hasCollision` is `1`.
* **`padding[3]`**: reserved bytes to align the size of the structure to a multiple of 4 bytes.

### Snapshot packet (`SnapshotPacket`)

Finally, the client‑side `SnapshotPacket` contains a `SnapshotHeader` and a dynamic array of `SnapshotEntity`. The structure is:

```cpp
struct SnapshotPacket {
    SnapshotHeader              header;
    std::vector<SnapshotEntity> entities;
};
```

The client deserialises the header, reserves an array of `entityCount` elements, then reads each `SnapshotEntity`. Entities not present in the list should be kept as they are or removed according to the game logic.

## Tips and best practices

* **Host byte order**: as mentioned, all values are sent as‑is (host endianness). If clients and server do not share the same endianness, use `htonl()`, `ntohl()` and their variants to convert integers. Floating‑point numbers require manual conversion.

* **Packet size**: UDP datagrams have a maximum size (in practice ~512 bytes is safe on the Internet). The `MAX_ENTITIES` field (4096 by default) limits the number of entities in a snapshot to avoid exceeding this limit. Adapt this constant to your game.

* **Loss and reordering**: UDP does not guarantee delivery or ordering of packets. The client must keep the inputs sent as long as the server has not acknowledged them via `lastProcessedInput`. The server uses `snapshotId` to detect obsolete or duplicate snapshots.

* **Snapshot reapplication**: the client may apply directly the last snapshot received (overwriting its local state), or interpolate between several snapshots for smooth rendering. When packets are lost, interpolation helps mask jumps.

* **Slot assignment and release**: to handle new players, monitor slot inactivity and free it if no packet has been received for a certain time. The library does not provide this logic; it is up to the server code to implement such a policy.

By following these guidelines, you will obtain a simple, robust and deterministic network communication suitable for action games requiring low latency.
