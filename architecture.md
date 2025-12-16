# Code architecture overview

This document describes the high‑level architecture of the provided code base and how the different modules fit together. The project is organised into three independent libraries:

- `ecs`: a minimalist entity–component system.
- `engine`: a deterministic game engine built on top of the ECS.
- `net`: a lightweight UDP networking layer.

Each module exposes its own public headers under `include/` and is documented in its own README. The following sections summarise their responsibilities and interactions.

## Module dependencies

The overall dependency graph is simple: the `engine` sits on top of the `ecs` and uses it to store game state and run systems; the `net` module is independent from the ECS but is used alongside the engine to synchronise clients and servers. None of the modules depends on game‑specific code; all behaviour comes from data loaded from Lua.

```
                        +------------------+
                        |      net         |
                        | (UDP sockets)    |
                        +---------+--------+
                                  ^
                                  |
                   snapshots      |     input packets
                                  |
                     +------------+-----------+
                     |       engine           |
                     |  (simulation loop)     |
                     +------------+-----------+
                                  ^
                                  |
                   uses registry  |     runs systems
                                  |
                 +---------------+--------------+
                 |               ecs            |
                 |  (entity, components,        |
                 |   registry and zippers)      |
                 +------------------------------+
```

In this diagram:

- The `ecs` module defines the core data structures (`entity_t`, `sparse_array`, `registry`) and utilities (`zipper`, `indexed_zipper`) used to manage entities and components. It has no knowledge of the network or game rules.

- The `engine` module owns an `ecs::registry`, loads configuration data from Lua (`GameConfig` defined in `resources.hpp`), registers the required component types and systems, and exposes simple functions to spawn entities and advance the simulation. It integrates movement, AI, collisions and lifetime management in a deterministic manner.

- The `net` module provides `Server` and `Client` classes to exchange `InputPacket` and `SnapshotPacket` structures over UDP. It does not depend on the ECS but is typically used by the game code to send player inputs and receive state snapshots.

## File organisation

```
.
├── README.md            <- Project overview
├── CMakeLists.txt       <- Build configuration
├── ecs/                 <- Entity–component system
│   ├── README.md        <- Detailed ECS documentation
│   └── include/ecs/     <- Public ECS headers
│       ├── ecs.hpp      <- Definition of entity_t, sparse_array and registry
│       └── zipper.hpp   <- Utilities to iterate over multiple sparse arrays
├── engine/              <- Game façade built on the ECS
│   ├── README.md        <- Detailed engine documentation
│   └── include/engine/  <- Public engine headers
│       ├── engine.hpp   <- Engine class and default components
│       └── resources.hpp<- Structures and functions to load Lua config
└── net/                 <- UDP networking layer
    ├── README.md        <- Detailed network documentation
    └── include/net/     <- Public network headers
        └── net.hpp      <- Packet definitions and client/server classes
```

- **`ecs.hpp`** contains the definitions of `entity_t`, `sparse_array` and `registry`. It implements entity creation/destruction, component registration/storage and system management. Sparse arrays provide O(1) access to components using an entity index and grow automatically when needed.

- **`zipper.hpp`** provides the `zipper` and `indexed_zipper` templates. These iterate over multiple `sparse_array`s in lockstep, skipping indices where any array lacks a component. `indexed_zipper` additionally yields the entity index, allowing systems to obtain the entity handle while iterating.

- **`engine.hpp`** declares all default components used by the engine (position, velocity, health, collider, etc.), the `Engine` class which encapsulates the `ecs::registry` and coordinates the simulation, and the `WeaponRef`/`MovementPatternComp` helpers.

- **`resources.hpp`** defines simple POD structures (`ProjectileDef`, `WeaponDef`, `Archetype`, `GameConfig`) that mirror the Lua configuration file. It also exposes `loadGameConfig()` to parse the Lua script and fill a `GameConfig` structure. The underlying implementation uses the Lua C API to traverse tables and extract fields.

- **`net.hpp`** defines the protocol constants, packet structures (`InputPacket`, `SnapshotHeader`, `SnapshotEntity`, `SnapshotPacket`), the internal `ClientSlot` structure and the `Server` and `Client` classes. The server maintains a fixed array of slots, assigns new clients to free slots and dispatches input packets to user‑defined callbacks. The client sends input packets at a fixed rate and deserialises state snapshots.

## Interaction at runtime

A typical runtime usage pattern looks like this:

```
                                  Client                         Server
                                   |                                |
                                   |     InputPacket (player input) |
                                   |------------------------------->|
                                   |                                |
    local prediction               |    run simulation (ecs+engine) |
    advance engine locally         |         for dt seconds         |
         for dt seconds            |                                |
                                   |         SnapshotPacket         |
                                   |<-------------------------------|
                                   |  apply snapshot, interpolate   |
                                   |  and render to the player      |
```

1. The client collects player inputs and packages them into an `InputPacket` which it sends to the server over UDP at a fixed cadence.

2. The server receives input packets from all connected clients. At the end of each simulation tick, it calls into the `Engine` to update the ECS. The server then serialises the relevant game state into a `SnapshotPacket` and sends it back to each client.

3. Upon receiving a snapshot, the client updates its local state, optionally interpolating between snapshots for smooth rendering, and continues to advance its own engine instance to predict motion while waiting for the next snapshot.

This design decouples data storage (`ecs`), simulation logic (`engine`) and networking (`net`), making each component reusable and testable on its own.
