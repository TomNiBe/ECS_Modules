# `ecs` library – entity–component system

This library implements a minimalist, data‑oriented entity–component system (ECS). It completely decouples logic (systems) from data (components) and allows efficient iteration over entities. Entities are represented by a simple, stable numeric handle and components are stored in contiguous arrays to permit fast, predictable traversal.

## General principles

### Why a data‑oriented ECS?

An ECS cleanly separates data from logic. Data are grouped into **components** (simple aggregate structures), whereas logic is organised into **systems** that operate on sets of entities possessing certain components. This approach:

- improves memory locality and thus performance,
- facilitates reuse and composition of behaviour,
- makes the simulation deterministic because the execution order of systems is explicitly controlled.

### Key concepts

* **Entity (`ecs::entity_t`)**: an opaque handle that encapsulates an integer index. An entity has no state of its own; it only serves as a key to access its components. A handle can be compared, copied and reused after destruction (via the free list). It does not contain a generation counter; therefore you should check that a handle is still alive before using it.

* **Component**: a data structure with no logic. For example, a `Position` with fields `x` and `y`. Each component type is registered exactly once with the registry. Components are stored in a `sparse_array` (a contiguous array of `std::optional<Component>`) indexed by the entity.

* **System**: a function or lambda registered with the registry that takes references to component arrays as parameters. Systems iterate over these arrays and update the components. They should not allocate memory or modify the component structure during iteration (except through registry methods designed for that purpose).

## The registry (`ecs::registry`)

The `registry` is the core of the ECS: it manages entities, stores components and executes systems.

### Creation and destruction of entities

* **`spawn_entity()`**: creates a new entity and returns its handle. If free slots are available, they are reused.
* **`kill_entity(ecs::entity_t)`**: destroys an entity. All its components are cleared and its index is returned to the free list. After destruction, any handle to that entity becomes invalid.

### Registering and accessing components

Before using a component type, you must register it:

```cpp
ecs::registry reg;
reg.register_component<Position>();
reg.register_component<Velocity>();
```

Registration creates an internal `sparse_array` for this type. To manipulate components:

* **Adding**: `reg.emplace_component<Position>(ent, x, y)` constructs a component in place for entity `ent`.
* **Removing**: `reg.remove_component<Position>(ent)` removes the component from the entity (via `erase`).
* **Access**: arrays returned by `get_components<T>()` are references to `sparse_array<T>`. Access a component using `array[ent]` which returns a `std::optional<T>`. If the option is empty, the entity does not have this component.

### Executing systems

Systems are registered via `add_system<Composant1, Composant2>(lambda)` where `lambda` receives a reference to the registry and references to the corresponding component arrays. Systems are stored in a list and **execute in the order of registration** when `run_systems()` is called. This order is fixed and must be chosen carefully to maintain determinism.

For example:

```cpp
reg.add_system<Position, Velocity>([](ecs::registry &r,
                                      auto &positions,
                                      auto &velocities) {
    for (std::size_t i = 0; i < positions.size(); ++i) {
        ecs::entity_t ent{i};
        auto &posOpt = positions[ent];
        auto &velOpt = velocities[ent];
        if (posOpt && velOpt) {
            posOpt->x += velOpt->x;
            posOpt->y += velOpt->y;
        }
    }
});
```

### `ecs::entity_t`: identity and good practices

The `entity_t` type encapsulates an index. A default handle is invalid and can be tested as a boolean. Identities are stable as long as the entity is alive. As there is no generation count by default, it is recommended to:

- not keep handles beyond the lifetime guaranteed by the game logic;
- check that an entity is alive before accessing its components;
- avoid storing raw indices in persistent containers.

## `sparse_array`: storage structure

Each component type is stored in an instance of `sparse_array<T>`. This structure:

- is a contiguous array of `std::optional<T>` indexed by `entity_t`;
- automatically grows when writing to a higher index;
- returns an empty option when reading out of bounds;
- does not guarantee pointer stability after a reallocation;
- permits the presence or absence of a component without per‑entity allocation.

Accessing an absent component is inexpensive: a static empty object is returned. When a component is added via `emplace_at` or `insert_at`, the option is filled and the entity is considered to have this component.

## Order of execution of systems and implications

Systems are executed in the order they are registered. This order must be chosen consistently with the game logic (for example, move entities before processing collisions). Modifying the order can change the final state and break determinism. Avoid registering systems in different parts of the code depending on circumstances; centralise registration during engine creation.

## Determinism and good practices

- **No dynamic memory inside the loop**: avoid allocations in systems to guarantee stable update times.
- **Use a fixed time step**: pass a constant interval (`dt`) to the simulation to prevent divergence between machines.
- **No dependence on undefined order**: do not use containers like `unordered_map` for logical iteration in a system (the order may vary between platforms). Prefer sorted vectors or compute a deterministic order.
- **Avoid side effects**: systems should only read and write the components concerned. Modify the structure of entities (adding or removing components) outside loops that iterate over those same components to avoid invalidating indices.

## Performance and advice

To achieve good performance with this model:

1. **Minimise dynamic memory**: register all component types at startup and avoid creating them dynamically during the game. `sparse_array` allocates contiguously.
2. **Contiguous traversal**: systems should iterate sequentially over indices to benefit from caches. Use the provided zip utilities (see `ecs/zipper.hpp`) to traverse multiple arrays in parallel without branching.
3. **Predictable access**: group components that are frequently used together in the same system to limit cache jumps. Avoid random access in the inner loop.
4. **No blocking**: leave network and engine libraries to handle potentially blocking calls. The registry does not execute anything outside your systems.

## Minimal usage example

```cpp
#include "ecs/ecs.hpp"

struct Position { float x = 0.f; float y = 0.f; };
struct Velocity { float x = 0.f; float y = 0.f; };

int main() {
    ecs::registry reg;
    // register components
    reg.register_component<Position>();
    reg.register_component<Velocity>();

    // position update system
    reg.add_system<Position, Velocity>([](ecs::registry &,
                                          auto &positions,
                                          auto &velocities) {
        for (std::size_t i = 0; i < positions.size(); ++i) {
            ecs::entity_t ent{i};
            auto &pos = positions[ent];
            auto &vel = velocities[ent];
            if (pos && vel) {
                pos->x += vel->x;
                pos->y += vel->y;
            }
        }
    });

    // create an entity
    ecs::entity_t e = reg.spawn_entity();
    reg.emplace_component<Position>(e, 0.f, 0.f);
    reg.emplace_component<Velocity>(e, 1.f, 0.f);

    // run a single update
    reg.run_systems();

    return 0;
}
```

In this example, an entity is created, assigned a position and a velocity, then a system runs that adds the velocity to the position. This model can be extended to many components and systems to build complex simulations while maintaining a clear and performant structure.
