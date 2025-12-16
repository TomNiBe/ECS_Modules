# Common Project Libraries

The `libs/` directory groups the fundamental building blocks used for simulation, game logic, and network communication. Each sub‑module is designed to be autonomous and reusable. They are published as Git submodules and can be used independently from the rest of the project.

## General Philosophy

These libraries follow several core principles:

1. **Separation of responsibilities**: each sub‑module covers a clearly defined domain.  
   - `ecs` handles entity storage and iteration over components,  
   - `engine` provides a high‑level façade to load Lua‑defined data and orchestrate simulation systems,  
   - `net` manages communication between a client and a server over UDP.  
   No module depends on game‑specific code.

2. **Data‑oriented design**: structures are designed around data rather than object hierarchies. Components are plain data structures with no embedded logic, and simulation is performed by external functions (systems). All gameplay constants come from Lua configuration files rather than C++ code.

3. **Determinism**: to allow faithful reproduction of a match and synchronization across machines, systems execute in a deterministic order with a fixed time step. Network calls are non‑blocking, and updates are sent at a fixed rate.

4. **Performance**: internal containers (notably in `ecs`) rely on array‑based layouts to ensure memory contiguity and predictable access patterns. Dynamic allocations are avoided during simulation, and hot loops iterate over contiguous ranges.

## Sub‑module Overview

- **`ecs/`**: implements a minimalist entity–component system inspired by the R‑Type project. It provides a `registry` to create and destroy entities, register component types, and execute systems. Components are stored in sparse arrays, allowing the absence of a component without per‑entity allocation.

- **`engine/`**: acts as a game façade built on top of the ECS. This module loads projectile, weapon, and archetype definitions from a Lua script (`GameConfig`), automatically creates the required components, and runs the simulation frame by frame. It manages entity creation (`spawn`), simulation updates (`update`), collisions, damage application, lifetimes, and respawning.

- **`net/`**: provides a minimal, non‑blocking UDP network layer to synchronize the simulation between a client and a server. Exchanged packets are simple C structures with no dynamic allocation: `InputPacket` for client inputs, and `SnapshotHeader` / `SnapshotEntity` for state snapshots sent by the server. The module automatically assigns client slots and exposes callbacks for packet handling.

## Global Architecture (ASCII diagram)

```
                 +------------------------------+
                 |       Player Inputs         |
                 +--------------+---------------+
                                |
                                v
                      +---------+---------+
                      |   Game Engine     |
                      |   (ECS + Engine)  |
                      +---------+---------+
                                |
                                v
                 +--------------+---------------+
                 |   State Snapshots sent via   |
                 |       the network layer      |
                 +------------------------------+
```

This diagram summarizes the main flow: the network module receives player inputs and forwards them to the game engine, which updates the state through the ECS and sends state snapshots back to clients over the network.

## General Flow

1. Clients send their inputs (movement, firing, etc.) to the server using `InputPacket`.
2. The server collects these inputs and feeds them into the simulation systems (`ecs` and `engine`) using a fixed time step. The logic is fully deterministic and data‑oriented.
3. After the update, the server serializes the relevant state into a `SnapshotPacket` containing a `SnapshotHeader` and a list of `SnapshotEntity`. This packet is sent to clients at a fixed rate.
4. Clients apply received snapshots to update their local world representation and interpolate or predict between snapshots to achieve smooth rendering.

## Navigation

Below are links to the detailed documentation for each sub‑module:

| Sub‑module | Description | Link |
|---|---|---|
| `ecs` | Minimalist entity–component system | [`ecs` documentation](ecs/README.md) |
| `engine` | Game façade built on top of the ECS | [`engine` documentation](engine/README.md) |
| `net` | Non‑blocking UDP network layer | [`net` documentation](net/README.md) |

Each README is self‑contained and explains in detail the principles, types, and conventions required to use the corresponding module.
