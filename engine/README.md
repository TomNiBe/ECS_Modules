# `engine` library – game façade

The `engine` module offers a high‑level façade built on top of the ECS. It loads definitions from a Lua script, creates the corresponding entities and orchestrates all the required systems (movement, shooting, collisions, damage, lifetime, respawn, etc.). Its purpose is to provide a generic and deterministic game foundation without imposing project‑specific logic.

## Role and positioning

* **Façade on top of the ECS**: the `Engine` encapsulates an `ecs::registry` and registers all useful component types. It exposes simple methods to load the configuration, create entities (archetypes) and advance the simulation.

* **Lua configuration loading**: game constants (archetypes, weapons, projectiles) are described in a Lua file that returns a table. The file `resources.hpp` defines the structures `GameConfig`, `Archetype`, `WeaponDef` and `ProjectileDef` and provides a function `loadGameConfig()` which reads the file and populates these structures. No gameplay parameter is hard‑coded in the engine.

* **Deterministic simulation loop**: the `Engine` integrates movement, handles collisions and applies damage with a fixed time step `dt` provided by the user (server or client). Systems are registered once during construction and always execute in the same order.

## Loading Lua configuration (`GameConfig`)

The game configuration is structured around three main types:

### Projectiles (`ProjectileDef`)

The `ProjectileDef` structure defines the basic behaviour of a projectile:

- **`collision`**: boolean indicating whether the projectile should detect collisions with the world or entities.
- **`damage`**: boolean indicating whether the projectile inflicts damage.
- **`width` / `height`**: dimensions of the projectile in world units.

### Weapons (`WeaponDef`)

A `WeaponDef` describes a weapon that can be equipped by an archetype:

- **`name`**: symbolic name of the weapon.
- **`rate`**: rate of fire in shots per second.
- **`speed`**: speed of emitted projectiles.
- **`lifetime`**: lifetime of the projectiles in seconds.
- **`damage`**: hit points removed per impact.
- **`projectileName`**: name of a projectile definition.
- **`pattern`**: optional movement pattern applied to the projectiles (`MovementPattern` structure).
- **`piercingHits`**: number of targets the projectile can penetrate before disappearing.
- **`charge`**: optional charge specification with `maxTime`, a list of time thresholds and charge levels (`damageMul`, `speedMul`, `sizeMul`, `piercingHits`). These parameters allow modulating the projectile’s effect according to how long the fire button is held.

### Archetypes (`Archetype`)

Archetypes serve as templates for entity creation. Each `Archetype` includes:

- **`name`**: identifier of the archetype.
- **`respawnable`**: boolean indicating whether the entity should reappear after destruction.
- **`health`**: initial hit points.
- **`collision`**: boolean to enable or disable collisions.
- **`hitbox`** (`HitboxDef`): dimensions and offset of the collision box.
- **`speed`**: default movement speed.
- **`lookDirection`** (`Vec2`): direction in which the entity is initially oriented.
- **`target`**: list or structure describing classes of enemies to attack. The order specifies the priority; each category can be associated with a selection mode (for example “closest in class”). These informations are converted into a `TargetList` component.
- **`range`**: attack range in units.
- **`weaponName`**: name of the equipped weapon.
- **`pattern`**: optional movement pattern applied to the entity.
- **`faction`**: team identifier to avoid friendly fire.
- **`colliderLayer` / `colliderMask` / `colliderSolid` / `colliderTrigger` / `colliderStatic`**: definition of collision behaviour (see the `Collider` component).
- **`thornsEnabled`** / **`thornsDamage`**: enable thorns that inflict damage to entities that come into contact.

### Global configuration (`GameConfig`)

An instance of `GameConfig` contains three associative arrays: `projectiles`, `weapons` and `archetypes` keyed by their name. The function `loadGameConfig(const std::string &path)` reads a Lua file and fills these arrays. If the format is incorrect, an exception is thrown. These structures persist throughout the life of the engine.

## Simulation cycle

The engine provides two main methods:

1. **`spawn(archetypeName, x, y)`**: creates an entity from an archetype, installs all the components and initialises its position. The component fields are built from the configuration (speed, hit points, collision box, faction, weapons, movement pattern, thorns, etc.).
2. **`update(dt)`**: performs a simulation step of duration `dt` (in seconds). The steps are:
   - Update the internal clock with `dt`.
   - Execute all registered systems via the registry (compute the new `DesiredPosition`, handle weapons, AI, etc.).
   - Resolve solid collisions and adjust desired positions.
   - Apply playable limits for the player (clamp within the allowed zone).
   - Copy desired positions into the `Position` component.
   - Remove entities that leave the world bounds.
   - Handle trigger collisions (projectiles, thorns), apply damage and remove dead entities.
   - Decrease lifetimes (`Lifetime`) and remove entities whose `remaining` is zero or negative.

All these operations are deterministic, free of dynamic allocations and rely on the ECS. The order of systems is determined during `Engine` construction.

## Provided components

The engine registers and uses numerous components by default. Here is a concise list:

| Component | Description |
|---|---|
| `Position` | 2D position (centre of the entity). |
| `Velocity` | 2D velocity (units per second). |
| `Speed` | Scalar movement speed used with player input. |
| `LookDirection` | Aiming or facing direction. |
| `Health` | Remaining hit points. |
| `Hitbox` | Half‑width/half‑height and optional offsets for solid collisions. |
| `Collider` | Collision layer and mask; flags for “solid”, “trigger” or “static”. |
| `Faction` | Team identifier to handle allied/enemy attacks. |
| `PendingDamage` | Damage pending application; reset to zero after each update. |
| `Piercing` | Number of targets a projectile can pass through and the history of already hit entities. |
| `Thorns` | Thorns that deal damage to entities that touch this one. |
| `ArchetypeRef` | Pointer to the associated archetype definition to access pre‑configured fields. |
| `TargetList` | Priority list of targets and selection modes per category. |
| `Range` | Attack range for AI systems. |
| `Respawnable` | Indicates whether the entity can respawn after destruction. |
| `Lifetime` | Remaining lifetime of a projectile or bonus. |
| `Damage` | Damage inflicted by a projectile (raw value). |
| `MovementPatternComp` | Progression in a movement pattern (offsets, current index). |
| `InputState` | Player inputs (axes, fire pressed/held/released). |
| `WeaponRef` | Pointer to the equipped weapon definition, cooldown and current charge. |
| `DesiredPosition` | Position computed by systems before collision resolution. |

This list is not exhaustive; the engine may register additional components depending on the configuration.

## ASCII diagram

The execution flow can be represented as follows:

```
    +--------------+
    |  Lua script  |
    +------+-------+
           |
           v
    +------+-------+
    | Configuration|  (GameConfig)
    +------+-------+
           |
           v
    +------+-------+
    | Engine       |
    +------+-------+
           |
           v
    +------+-------+
    | ECS Registry |
    +------+-------+
           |
           v
    +------+-------+
    | Game state   |
    +------+-------+
           |
           v
    +------+-------+
    | Network      | (state snapshots)
    +--------------+
```

This diagram shows how the Lua script is loaded into a configuration used by the engine to initialise entities in the ECS registry. The simulation produces a state that is transmitted to the network module to synchronise clients.

## Responsibilities

- **Engine responsibilities**:
  - Load the configuration and create entities from archetypes.
  - Register components and systems required for the simulation.
  - Handle collisions, apply damage, remove expired entities and update position.
  - Offer a minimal API (`spawn`, `update`, `getRegistry`) to the game code.

- **Calling code responsibilities (game, server or client)**:
  - Call `loadGameConfig()` to load the Lua configuration at startup.
  - Instantiate the `Engine` with this configuration.
  - Create the initial entities (player, enemies, projectiles) via `spawn`.
  - On each tick, retrieve player or network inputs, update the relevant components (e.g. `InputState`) then call `update(dt)`.
  - Serialise the state or apply it depending on whether one is the server or the client.
  - React to game events (end of game, respawn) by creating or deleting entities.

## Integration examples

### Server‑side example

```cpp
// Load configuration and create the engine
engine::GameConfig cfg = engine::loadGameConfig("config/game.lua");
engine::Engine eng(cfg);

// Create initial entities
eng.spawn("player", 0.f, 0.f);
eng.spawn("enemy", 10.f, 0.f);

// Game loop
while (game_running) {
    // 1. Retrieve inputs from the network (InputPacket) and update InputState
    net::InputPacket input;
    // ... network read ...
    // eng.getRegistry().get_components<engine::InputState>()[player_entity]->moveX = input.moveX;
    // 2. Advance simulation with a fixed time step
    eng.update(1.f / 60.f);
    // 3. Serialise state and send it to clients via net::SnapshotPacket
    // ... serialisation ...
}
```

### Client‑side example

```cpp
engine::GameConfig cfg = engine::loadGameConfig("config/game.lua");
engine::Engine eng(cfg);

// Render loop
while (displaying) {
    // 1. Collect keyboard/mouse inputs and send an InputPacket to the server
    net::InputPacket pkt;
    pkt.moveX = readHorizontalAxis();
    pkt.moveY = readVerticalAxis();
    pkt.firePressed  = fireButtonJustPressed();
    pkt.fireHeld     = fireButtonHeld();
    pkt.fireReleased = fireButtonJustReleased();
    // ... network send ...
    // 2. Receive a SnapshotPacket and apply the data to local entities
    net::SnapshotPacket snap;
    // ... network read ...
    // update the position and state of controlled entities according to snap
    // 3. Advance the local simulation by a small amount for interpolation
    eng.update(1.f / 60.f);
    // 4. Render the scene based on the ECS registry
}
```

These examples illustrate how the engine is used together with the network module to build a deterministic multi‑player game. The comments and high‑level logic remain in French in the original sources, while the names of types and functions from the API remain in English to match the C++ code.
