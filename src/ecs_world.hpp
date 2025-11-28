#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <queue>
#include <optional>
#include <limits>

namespace ecs
{

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------

// Défini dans lua_loader.hpp
struct GameConfig;

// -----------------------------------------------------------------------------
// Entity handle
// -----------------------------------------------------------------------------

struct Entity
{
    std::uint32_t id{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{0};

    bool isValid() const noexcept
    {
        return id != std::numeric_limits<std::uint32_t>::max();
    }

    friend bool operator==(const Entity& a, const Entity& b) noexcept
    {
        return a.id == b.id && a.generation == b.generation;
    }

    friend bool operator!=(const Entity& a, const Entity& b) noexcept
    {
        return !(a == b);
    }
};

// -----------------------------------------------------------------------------
// Events : ce que l’ECS remonte vers le reste du moteur
// -----------------------------------------------------------------------------

enum class EventType
{
    EntitySpawned,
    EntityDestroyed,
    ProjectileFired,
    EntityDamaged,
    Frozen,
    Unfrozen
};

struct Event
{
    EventType type{EventType::EntitySpawned};

    /// Entité principale concernée par l’event
    Entity entity{};

    /// Entité “autre” (ex : source du projectile, instigateur des dégâts, etc.)
    Entity other{};

    /// Pour EntityDamaged : valeur des dégâts appliqués
    int damage{0};
};

// -----------------------------------------------------------------------------
// Commands : ce que le moteur / thread principal envoie à l’ECS
// -----------------------------------------------------------------------------

enum class CommandType
{
    SpawnEntity,
    DestroyEntity,
    DamageEntity,
    FreezeEntity,
    UnfreezeEntity,
    SetMoveInput,
    SetLookDirection,
    SetFireInput   // nouveau : input de tir (player ou IA contrôlée hors ECS)
};

struct Command
{
    CommandType type{CommandType::SpawnEntity};

    // Pour SpawnEntity
    std::string archetype;  // nom de l’archetype dans GameConfig
    float x{0.f};
    float y{0.f};

    // Pour DestroyEntity / DamageEntity / FreezeEntity / UnfreezeEntity / inputs
    Entity target{};

    // Pour DamageEntity
    int damage{0};

    // Pour SetMoveInput : vecteur de déplacement normalisé (-1..1)
    float moveX{0.f};
    float moveY{0.f};

    // Pour SetLookDirection : direction de visée (non forcément normalisée)
    float lookX{0.f};
    float lookY{0.f};

    // Pour SetFireInput : bouton de tir (pressed/held)
    bool firePressed{false};
};

// -----------------------------------------------------------------------------
// World : cœur de l’ECS
// -----------------------------------------------------------------------------

class World
{
public:
    /// Nombre max d’entités par défaut (peut être override dans le ctor).
    static constexpr std::size_t DEFAULT_MAX_ENTITIES = 4096;

    /// Construit le monde à partir de la config Lua (déjà chargée).
    explicit World(const GameConfig& config,
                   std::size_t maxEntities = DEFAULT_MAX_ENTITIES);

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    World(World&&) = delete;
    World& operator=(World&&) = delete;

    ~World();

    // -------------------------------------------------------------------------
    // API côté moteur / thread principal
    // -------------------------------------------------------------------------

    /// Enqueue une commande à traiter lors du prochain update().
    void enqueueCommand(const Command& cmd);

    /// Met à jour la simulation d’un pas de temps dt (en secondes).
    void update(float dt);

    /// Récupère le prochain event de la frame courante (si disponible).
    /// Retourne std::nullopt quand la queue est vide.
    std::optional<Event> pollEvent();

    /// Indique si une entité est encore vivante (handle valide + non détruite).
    bool isAlive(Entity e) const noexcept;

    // -------------------------------------------------------------------------
    // Helpers de debug / queries simples (thread principal)
    // -------------------------------------------------------------------------

    /// Retourne la position de l’entité (si elle existe et possède une Position).
    std::optional<std::pair<float, float>> getPosition(Entity e) const;

    /// Retourne les PV actuels de l’entité (si elle possède un composant Health).
    std::optional<int> getHealth(Entity e) const;

    /// Retourne true si l’entité possède un composant Weapon.
    bool hasWeapon(Entity e) const;

    /// Modifie l’arme d’une entité (si elle a un slot d’arme et que le nom existe dans GameConfig).
    /// Ne génère pas d’event ; la logique de gameplay côté moteur peut en rajouter si besoin.
    void setEntityWeapon(Entity e, const std::string& weaponName);

    /// Fige / défige une entité (via flag interne).  
    /// Cette API est synchrone; pour une utilisation cross-thread, préfère les Commands FreezeEntity/UnfreezeEntity.
    void setFrozen(Entity e, bool frozen);

    // -------------------------------------------------------------------------
    // API “haut niveau” : spawn/destroy synchrone (optionnelle)
    // -------------------------------------------------------------------------

    /// Spawn immédiat depuis un archetype.  
    /// NOTE : en prod multi-thread, préférer les `Command::SpawnEntity`.
    Entity spawnImmediate(const std::string& archetypeName,
                          float x,
                          float y);

    /// Destruction immédiate d’une entité.  
    /// NOTE : en prod multi-thread, préférer les `Command::DestroyEntity`.
    void destroyImmediate(Entity e);

private:
    // -------------------------------------------------------------------------
    // Types internes
    // -------------------------------------------------------------------------

    struct InternalConfig;
    struct ArchetypeCache;

    // Impl SoA cachée (positions, vitesses, health, inputs, etc.).
    struct Impl;

    // -------------------------------------------------------------------------
    // Gestion des entités
    // -------------------------------------------------------------------------

    Entity allocateEntity();
    void   releaseEntity(Entity e);

    bool   validateEntity(Entity e) const noexcept;

    // -------------------------------------------------------------------------
    // Commandes
    // -------------------------------------------------------------------------

    void processCommands();

    void processSpawnCommand(const Command& cmd);
    void processDestroyCommand(const Command& cmd);
    void processDamageCommand(const Command& cmd);
    void processFreezeCommand(const Command& cmd, bool frozen);
    void processMoveInputCommand(const Command& cmd);
    void processLookInputCommand(const Command& cmd);
    void processFireInputCommand(const Command& cmd);

    // -------------------------------------------------------------------------
    // Systems
    // -------------------------------------------------------------------------

    void runSystems(float dt);

    void systemMovement(float dt);
    void systemAim(float dt);
    void systemWeapon(float dt);
    void systemProjectileDamage();
    void systemLifetime(float dt);
    void systemHealth();

    // -------------------------------------------------------------------------
    // Events
    // -------------------------------------------------------------------------

    void pushEvent(const Event& ev);

    // Helpers pour générer des events typiques
    void pushEntitySpawnedEvent(Entity e);
    void pushEntityDestroyedEvent(Entity e);
    void pushProjectileFiredEvent(Entity projectile, Entity owner);
    void pushEntityDamagedEvent(Entity victim, Entity instigator, int damage);
    void pushFrozenEvent(Entity e);
    void pushUnfrozenEvent(Entity e);

    // -------------------------------------------------------------------------
    // Données internes (SoA / config)
    // -------------------------------------------------------------------------

    std::size_t m_maxEntities;

    // Config transformée pour usage runtime (pointeurs vers defs, ids, etc.).
    InternalConfig* m_config;

    // Implémentation SoA cachée
    Impl* m_impl;
};

// -----------------------------------------------------------------------------
// Fin namespace ecs
// -----------------------------------------------------------------------------

} // namespace ecs
