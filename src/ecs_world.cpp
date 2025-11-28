#include "ecs_world.hpp"
#include "lua_loader.hpp" // Doit contenir GameConfig et MovementPattern

#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cassert>

namespace ecs
{

// -----------------------------------------------------------------------------
// Petits helpers math
// -----------------------------------------------------------------------------

namespace
{
    inline float lengthSquared(float x, float y)
    {
        return x * x + y * y;
    }

    inline float length(float x, float y)
    {
        return std::sqrt(lengthSquared(x, y));
    }

    inline void normalize(float& x, float& y)
    {
        float len = length(x, y);
        if (len > 0.00001f)
        {
            x /= len;
            y /= len;
        }
        else
        {
            x = 1.0f;
            y = 0.0f;
        }
    }
}

// -----------------------------------------------------------------------------
// InternalConfig : runtime view de GameConfig
// -----------------------------------------------------------------------------

struct World::InternalConfig
{
    struct ArchetypeInfo
    {
        std::string name;

        bool respawnable{false};
        bool hasHealth{false};
        int  health{0};
        bool collision{false};

        float speed{0.f};
        float range{0.f};

        float lookX{1.f};
        float lookY{0.f};

        int weaponIndex{-1};

        // liste des noms de targets (pour debug / config)
        std::vector<std::string> targetNames;

        // pattern de mouvement éventuel
        MovementPattern movementPattern;
    };

    struct WeaponInfo
    {
        std::string name;

        int   projectileIndex{-1};
        float rate{1.f};
        float projectileSpeed{0.f};
        float projectileLifetime{0.f};
        int   damage{0};

        MovementPattern pattern;
    };

    struct ProjectileInfo
    {
        std::string name;

        bool  collision{false};
        bool  damage{false};
        float halfWidth{0.5f};
        float halfHeight{0.5f};
    };

    std::vector<ArchetypeInfo>  archetypes;
    std::vector<WeaponInfo>     weapons;
    std::vector<ProjectileInfo> projectiles;

    std::unordered_map<std::string, int> archetypeIndexByName;
    std::unordered_map<std::string, int> weaponIndexByName;
    std::unordered_map<std::string, int> projectileIndexByName;
};

// -----------------------------------------------------------------------------
// Impl : SoA et runtime state
// -----------------------------------------------------------------------------

struct World::Impl
{
    // Entités
    std::vector<std::uint32_t> generations;
    std::vector<bool>          alive;
    std::vector<bool>          frozen;
    std::vector<bool>          respawnable;

    // Position / vitesse
    std::vector<float> posX;
    std::vector<float> posY;
    std::vector<bool>  hasPosition;

    std::vector<float> velX;
    std::vector<float> velY;
    std::vector<bool>  hasVelocity;

    // Look direction
    std::vector<float> lookX;
    std::vector<float> lookY;
    std::vector<bool>  hasLook;

    // Health
    std::vector<int>  health;
    std::vector<bool> hasHealth;

    // Collision
    std::vector<bool> hasCollision;

    // Archetype (index dans InternalConfig::archetypes)
    std::vector<int>  archetypeIndex;
    std::vector<bool> hasArchetype;

    // Weapon
    std::vector<int>   weaponIndex;
    std::vector<bool>  hasWeapon;
    std::vector<float> fireCooldown; // temps restant avant le prochain tir

    // Lifetime
    std::vector<float> lifetime;
    std::vector<bool>  hasLifetime;

    // Projectile
    std::vector<bool>  isProjectile;
    std::vector<int>   projectileIndex; // index dans InternalConfig::projectiles
    std::vector<Entity> projectileOwner;
    std::vector<int>   projectileDamage;
    std::vector<float> projHalfWidth;
    std::vector<float> projHalfHeight;

    // Multi-target : pour chaque entité, liste des indices d'archetypes (par priorité)
    std::vector<std::vector<int>> targetPriorities;

    // Gestion du pool d’entités
    std::vector<std::uint32_t> freeIds;

    // Commandes & events
    std::queue<Command> commandQueue;
    std::queue<Event>   eventQueue;
    std::vector<bool> inputFire;
};

// -----------------------------------------------------------------------------
// World : construction
// -----------------------------------------------------------------------------

World::World(const GameConfig& cfg, std::size_t maxEntities)
    : m_maxEntities(maxEntities)
    , m_config(new InternalConfig())
    , m_impl(new Impl())
{
    // ------------------------------
    // 1) Transformer GameConfig
    // ------------------------------
    InternalConfig& ic = *m_config;

    // 1.1) Projectiles
    {
        int idx = 0;
        ic.projectiles.reserve(cfg.projectiles.size());
        for (const auto& [name, pCfg] : cfg.projectiles)
        {
            InternalConfig::ProjectileInfo info;
            info.name = name;

            // TODO: adapter aux noms réels de ProjectileDef
            info.collision  = pCfg.collision;      // ex: bool collision;
            info.damage     = pCfg.damage;         // ex: bool damage;
            info.halfWidth  = pCfg.size.width * 0.5f;
            info.halfHeight = pCfg.size.height * 0.5f;

            ic.projectileIndexByName[name] = idx++;
            ic.projectiles.push_back(info);
        }
    }

    // 1.2) Weapons
    {
        int idx = 0;
        ic.weapons.reserve(cfg.weapons.size());
        for (const auto& [name, wCfg] : cfg.weapons)
        {
            InternalConfig::WeaponInfo info;
            info.name = name;

            // TODO: adapter aux noms réels de WeaponDef
            info.rate              = wCfg.rate;
            info.projectileSpeed   = wCfg.speed;
            info.projectileLifetime= wCfg.lifetime;
            info.damage            = wCfg.damage;
            info.pattern           = wCfg.pattern;

            auto itProj = ic.projectileIndexByName.find(wCfg.projectile);
            if (itProj != ic.projectileIndexByName.end())
                info.projectileIndex = itProj->second;
            else
                info.projectileIndex = -1; // pas de projectile valide

            ic.weaponIndexByName[name] = idx++;
            ic.weapons.push_back(info);
        }
    }

    // 1.3) Archetypes
    {
        int idx = 0;
        ic.archetypes.reserve(cfg.archetypes.size());
        for (const auto& [name, aCfg] : cfg.archetypes)
        {
            InternalConfig::ArchetypeInfo info;
            info.name = name;

            // TODO: adapter aux noms réels de ArchetypeDef
            info.respawnable = aCfg.respawnable;
            info.hasHealth   = (aCfg.Health > 0);
            info.health      = aCfg.Health;
            info.collision   = aCfg.Collision;
            info.speed       = aCfg.speed;
            info.range       = aCfg.range;
            info.lookX       = aCfg.lookDirection.x;
            info.lookY       = aCfg.lookDirection.y;
            info.movementPattern = aCfg.pattern;

            // Weapon
            info.weaponIndex = -1;
            auto itW = ic.weaponIndexByName.find(aCfg.Weapon);
            if (itW != ic.weaponIndexByName.end())
                info.weaponIndex = itW->second;

            // Multi-target : noms
            info.targetNames.clear();
            // ex: aCfg.target est std::vector<std::string>
            for (const auto& tName : aCfg.target)
            {
                info.targetNames.push_back(tName);
            }

            ic.archetypeIndexByName[name] = idx++;
            ic.archetypes.push_back(info);
        }
    }

    // ------------------------------
    // 2) Initialiser les pools SoA
    // ------------------------------
    Impl& impl = *m_impl;

    const std::size_t N = m_maxEntities;

    impl.generations.assign(N, 0);
    impl.alive.assign(N, false);
    impl.frozen.assign(N, false);
    impl.respawnable.assign(N, false);

    impl.posX.assign(N, 0.f);
    impl.posY.assign(N, 0.f);
    impl.hasPosition.assign(N, false);

    impl.velX.assign(N, 0.f);
    impl.velY.assign(N, 0.f);
    impl.hasVelocity.assign(N, false);

    impl.lookX.assign(N, 1.f);
    impl.lookY.assign(N, 0.f);
    impl.hasLook.assign(N, false);

    impl.health.assign(N, 0);
    impl.hasHealth.assign(N, false);

    impl.hasCollision.assign(N, false);

    impl.archetypeIndex.assign(N, -1);
    impl.hasArchetype.assign(N, false);

    impl.weaponIndex.assign(N, -1);
    impl.hasWeapon.assign(N, false);
    impl.fireCooldown.assign(N, 0.f);

    impl.lifetime.assign(N, 0.f);
    impl.hasLifetime.assign(N, false);

    impl.isProjectile.assign(N, false);
    impl.projectileIndex.assign(N, -1);
    impl.projectileOwner.assign(N, Entity{});
    impl.projectileDamage.assign(N, 0);
    impl.projHalfWidth.assign(N, 0.5f);
    impl.projHalfHeight.assign(N, 0.5f);

    impl.targetPriorities.assign(N, {});
    impl.inputFire.assign(N, false);


    impl.freeIds.clear();
    impl.freeIds.reserve(N);
    for (std::uint32_t id = 0; id < N; ++id)
        impl.freeIds.push_back(N - 1u - id); // LIFO -> meilleures localités mémoire
}

World::~World()
{
    delete m_impl;
    delete m_config;
}

// -----------------------------------------------------------------------------
// Helpers entités
// -----------------------------------------------------------------------------

Entity World::allocateEntity()
{
    Impl& impl = *m_impl;
    if (impl.freeIds.empty())
    {
        // pool plein -> entité invalide
        return Entity{};
    }

    std::uint32_t id = impl.freeIds.back();
    impl.freeIds.pop_back();

    impl.alive[id] = true;
    // génération inchangée; on l’incrémente à la destruction

    Entity e;
    e.id = id;
    e.generation = impl.generations[id];
    return e;
}

void World::releaseEntity(Entity e)
{
    Impl& impl = *m_impl;
    if (!validateEntity(e))
        return;

    const std::uint32_t id = e.id;

    impl.alive[id] = false;
    impl.frozen[id] = false;

    impl.hasPosition[id] = false;
    impl.hasVelocity[id] = false;
    impl.hasLook[id]     = false;

    impl.hasHealth[id]   = false;
    impl.hasCollision[id]= false;
    impl.hasArchetype[id]= false;

    impl.hasWeapon[id]   = false;
    impl.weaponIndex[id] = -1;
    impl.fireCooldown[id]= 0.f;

    impl.hasLifetime[id] = false;
    impl.isProjectile[id]= false;
    impl.projectileIndex[id] = -1;
    impl.projectileOwner[id] = Entity{};
    impl.projectileDamage[id]= 0;

    impl.targetPriorities[id].clear();
    impl.respawnable[id] = false;

    // Incrémenter la génération pour invalider les handles existants
    impl.generations[id]++;

    // Retour dans le pool d’IDs libres
    impl.freeIds.push_back(id);
}

bool World::validateEntity(Entity e) const noexcept
{
    const Impl& impl = *m_impl;
    if (!e.isValid())
        return false;
    if (e.id >= m_maxEntities)
        return false;
    if (impl.generations[e.id] != e.generation)
        return false;
    return true;
}

// -----------------------------------------------------------------------------
// Commandes & events : API publique
// -----------------------------------------------------------------------------

void World::enqueueCommand(const Command& cmd)
{
    m_impl->commandQueue.push(cmd);
}

void World::pushEvent(const Event& ev)
{
    m_impl->eventQueue.push(ev);
}

std::optional<Event> World::pollEvent()
{
    Impl& impl = *m_impl;
    if (impl.eventQueue.empty())
        return std::nullopt;

    Event ev = impl.eventQueue.front();
    impl.eventQueue.pop();
    return ev;
}

bool World::isAlive(Entity e) const noexcept
{
    if (!validateEntity(e))
        return false;
    return m_impl->alive[e.id];
}

// -----------------------------------------------------------------------------
// Helpers : events prédéfinis
// -----------------------------------------------------------------------------

void World::pushEntitySpawnedEvent(Entity e)
{
    Event ev;
    ev.type   = EventType::EntitySpawned;
    ev.entity = e;
    pushEvent(ev);
}

void World::pushEntityDestroyedEvent(Entity e)
{
    Event ev;
    ev.type   = EventType::EntityDestroyed;
    ev.entity = e;
    pushEvent(ev);
}

void World::pushProjectileFiredEvent(Entity projectile, Entity owner)
{
    Event ev;
    ev.type   = EventType::ProjectileFired;
    ev.entity = projectile;
    ev.other  = owner;
    pushEvent(ev);
}

void World::pushEntityDamagedEvent(Entity victim, Entity instigator, int damage)
{
    Event ev;
    ev.type   = EventType::EntityDamaged;
    ev.entity = victim;
    ev.other  = instigator;
    ev.damage = damage;
    pushEvent(ev);
}

void World::pushFrozenEvent(Entity e)
{
    Event ev;
    ev.type   = EventType::Frozen;
    ev.entity = e;
    pushEvent(ev);
}

void World::pushUnfrozenEvent(Entity e)
{
    Event ev;
    ev.type   = EventType::Unfrozen;
    ev.entity = e;
    pushEvent(ev);
}

// -----------------------------------------------------------------------------
// API de debug / query
// -----------------------------------------------------------------------------

std::optional<std::pair<float, float>> World::getPosition(Entity e) const
{
    if (!validateEntity(e))
        return std::nullopt;

    const Impl& impl = *m_impl;
    const std::uint32_t id = e.id;
    if (!impl.hasPosition[id])
        return std::nullopt;

    return std::make_pair(impl.posX[id], impl.posY[id]);
}

std::optional<int> World::getHealth(Entity e) const
{
    if (!validateEntity(e))
        return std::nullopt;

    const Impl& impl = *m_impl;
    const std::uint32_t id = e.id;
    if (!impl.hasHealth[id])
        return std::nullopt;

    return impl.health[id];
}

bool World::hasWeapon(Entity e) const
{
    if (!validateEntity(e))
        return false;

    const Impl& impl = *m_impl;
    return impl.hasWeapon[e.id];
}

void World::setEntityWeapon(Entity e, const std::string& weaponName)
{
    if (!validateEntity(e))
        return;

    Impl& impl = *m_impl;
    InternalConfig& ic = *m_config;

    auto it = ic.weaponIndexByName.find(weaponName);
    if (it == ic.weaponIndexByName.end())
        return;

    const std::uint32_t id = e.id;
    impl.hasWeapon[id]   = true;
    impl.weaponIndex[id] = it->second;
    impl.fireCooldown[id]= 0.f; // prêt à tirer immédiatement
}

void World::setFrozen(Entity e, bool frozen)
{
    if (!validateEntity(e))
        return;

    Impl& impl = *m_impl;
    const std::uint32_t id = e.id;
    if (impl.frozen[id] == frozen)
        return;

    impl.frozen[id] = frozen;
    if (frozen)
        pushFrozenEvent(e);
    else
        pushUnfrozenEvent(e);
}

// -----------------------------------------------------------------------------
// Spawns / destroy synchrones (optionnels)
// -----------------------------------------------------------------------------

Entity World::spawnImmediate(const std::string& archetypeName, float x, float y)
{
    InternalConfig& ic = *m_config;
    Impl& impl = *m_impl;

    auto it = ic.archetypeIndexByName.find(archetypeName);
    if (it == ic.archetypeIndexByName.end())
        return Entity{};

    int archIndex = it->second;
    const auto& arch = ic.archetypes[archIndex];

    Entity e = allocateEntity();
    if (!e.isValid())
        return e;

    const std::uint32_t id = e.id;

    // Archetype / flags de base
    impl.hasArchetype[id] = true;
    impl.archetypeIndex[id] = archIndex;
    impl.respawnable[id] = arch.respawnable;

    // Position
    impl.hasPosition[id] = true;
    impl.posX[id] = x;
    impl.posY[id] = y;

    // Look
    impl.hasLook[id] = true;
    impl.lookX[id] = arch.lookX;
    impl.lookY[id] = arch.lookY;
    normalize(impl.lookX[id], impl.lookY[id]);

    // Health
    impl.hasHealth[id] = arch.hasHealth;
    impl.health[id]    = arch.health;

    // Collision
    impl.hasCollision[id] = arch.collision;

    // Weapon
    if (arch.weaponIndex >= 0)
    {
        impl.hasWeapon[id]   = true;
        impl.weaponIndex[id] = arch.weaponIndex;
        impl.fireCooldown[id]= 0.f;
    }
    else
    {
        impl.hasWeapon[id]   = false;
        impl.weaponIndex[id] = -1;
        impl.fireCooldown[id]= 0.f;
    }

    // Mouvement de base : entité statique par défaut
    impl.hasVelocity[id] = false;
    impl.velX[id] = 0.f;
    impl.velY[id] = 0.f;

    // Lifetime, projectile, targetPriorities : par défaut
    impl.hasLifetime[id] = false;
    impl.isProjectile[id] = false;
    impl.projectileIndex[id] = -1;
    impl.projectileOwner[id] = Entity{};
    impl.projectileDamage[id]= 0;

    impl.targetPriorities[id].clear();
    // Construction de la liste d’indices d’archetypes cibles (multi-target)
    for (const auto& tName : arch.targetNames)
    {
        auto itT = ic.archetypeIndexByName.find(tName);
        if (itT != ic.archetypeIndexByName.end())
            impl.targetPriorities[id].push_back(itT->second);
    }

    // Event spawn
    pushEntitySpawnedEvent(e);

    return e;
}

void World::destroyImmediate(Entity e)
{
    if (!validateEntity(e))
        return;
    pushEntityDestroyedEvent(e);
    releaseEntity(e);
}

// -----------------------------------------------------------------------------
// Commandes côté ECS
// -----------------------------------------------------------------------------

void World::processSpawnCommand(const Command& cmd)
{
    spawnImmediate(cmd.archetype, cmd.x, cmd.y);
}

void World::processDestroyCommand(const Command& cmd)
{
    destroyImmediate(cmd.target);
}

void World::processDamageCommand(const Command& cmd)
{
    if (!validateEntity(cmd.target))
        return;

    Impl& impl = *m_impl;
    const std::uint32_t id = cmd.target.id;

    if (!impl.hasHealth[id])
        return;

    impl.health[id] -= cmd.damage;
    pushEntityDamagedEvent(cmd.target, Entity{}, cmd.damage);

    if (impl.health[id] <= 0)
    {
        // Mort -> destruction
        destroyImmediate(cmd.target);
    }
}

void World::processFreezeCommand(const Command& cmd, bool frozen)
{
    setFrozen(cmd.target, frozen);
}

void World::processCommands()
{
    Impl& impl = *m_impl;

    while (!impl.commandQueue.empty())
    {
        Command cmd = impl.commandQueue.front();
        impl.commandQueue.pop();

        switch (cmd.type)
        {
        case CommandType::SpawnEntity:
            processSpawnCommand(cmd);
            break;
        case CommandType::DestroyEntity:
            processDestroyCommand(cmd);
            break;
        case CommandType::DamageEntity:
            processDamageCommand(cmd);
            break;
        case CommandType::FreezeEntity:
            processFreezeCommand(cmd, true);
            break;
        case CommandType::UnfreezeEntity:
            processFreezeCommand(cmd, false);
            break;

        case CommandType::SetMoveInput:
        {
            if (!validateEntity(cmd.target))
                break;

            const std::uint32_t id = cmd.target.id;

            // si pas de position, on considère que ce n’est pas un perso mobile
            if (!impl.hasPosition[id])
                break;

            // vitesse de base via l’archetype (si dispo)
            float speed = 0.f;
            if (impl.hasArchetype[id])
            {
                InternalConfig& ic = *m_config;
                int archIndex = impl.archetypeIndex[id];
                if (archIndex >= 0 && archIndex < (int)ic.archetypes.size())
                    speed = ic.archetypes[archIndex].speed;
            }

            float mx = cmd.moveX;
            float my = cmd.moveY;
            if (mx == 0.f && my == 0.f)
            {
                impl.hasVelocity[id] = false;
                impl.velX[id] = 0.f;
                impl.velY[id] = 0.f;
            }
            else
            {
                normalize(mx, my);
                impl.hasVelocity[id] = true;
                impl.velX[id] = mx * speed;
                impl.velY[id] = my * speed;
            }
            break;
        }

        case CommandType::SetLookDirection:
        {
            if (!validateEntity(cmd.target))
                break;

            const std::uint32_t id = cmd.target.id;
            if (!impl.hasPosition[id])
                break;

            float lx = cmd.lookX;
            float ly = cmd.lookY;
            if (lx == 0.f && ly == 0.f)
                break; // on ignore, on garde l’ancienne direction

            normalize(lx, ly);
            impl.hasLook[id] = true;
            impl.lookX[id] = lx;
            impl.lookY[id] = ly;
            break;
        }

        case CommandType::SetFireInput:
        {
            if (!validateEntity(cmd.target))
                break;

            const std::uint32_t id = cmd.target.id;
            impl.inputFire[id] = cmd.firePressed;
            break;
        }
        }
    }
}

// -----------------------------------------------------------------------------
// Systems : update global
// -----------------------------------------------------------------------------

void World::update(float dt)
{
    // 1) traiter les commandes du thread principal
    processCommands();

    // 2) exécuter tous les systèmes
    runSystems(dt);
}

void World::runSystems(float dt)
{
    systemMovement(dt);
    systemAim(dt);
    systemWeapon(dt);
    systemProjectileDamage();
    systemLifetime(dt);
    systemHealth();
}

// -----------------------------------------------------------------------------
// System : Movement (position += velocity * dt)
// -----------------------------------------------------------------------------

void World::systemMovement(float dt)
{
    Impl& impl = *m_impl;
    const std::size_t N = m_maxEntities;

    for (std::size_t id = 0; id < N; ++id)
    {
        if (!impl.alive[id])
            continue;
        if (!impl.hasPosition[id])
            continue;
        if (!impl.hasVelocity[id])
            continue;
        if (impl.frozen[id])
            continue;

        impl.posX[id] += impl.velX[id] * dt;
        impl.posY[id] += impl.velY[id] * dt;
    }
}

// -----------------------------------------------------------------------------
// System : Aim (multi-target + fallback lookDirection)
// -----------------------------------------------------------------------------

void World::systemAim(float /*dt*/)
{
    Impl& impl = *m_impl;
    InternalConfig& ic = *m_config;
    const std::size_t N = m_maxEntities;

    for (std::size_t id = 0; id < N; ++id)
    {
        if (!impl.alive[id])
            continue;
        if (!impl.hasPosition[id])
            continue;
        if (!impl.hasArchetype[id])
            continue;
        if (!impl.hasWeapon[id])
            continue;
        if (impl.frozen[id])
            continue;

        const int archIndex = impl.archetypeIndex[id];
        if (archIndex < 0 || archIndex >= static_cast<int>(ic.archetypes.size()))
            continue;

        const auto& arch = ic.archetypes[archIndex];
        const auto& priorities = impl.targetPriorities[id];

        // Pas de targets configurés -> garder la direction actuelle
        if (priorities.empty())
            continue;
        if (arch.name == "player")
            continue;

        const float shooterX = impl.posX[id];
        const float shooterY = impl.posY[id];
        const float rangeSq  = arch.range * arch.range;

        // On choisit la cible :
        // - d'abord par priorité (index dans priorities)
        // - puis par distance la plus courte
        bool   found = false;
        float  bestDistSq = 0.f;
        int    bestTargetArchPriority = 0;
        std::uint32_t bestTargetId = 0;

        for (std::size_t targetId = 0; targetId < N; ++targetId)
        {
            if (!impl.alive[targetId])
                continue;
            if (!impl.hasPosition[targetId])
                continue;
            if (!impl.hasArchetype[targetId])
                continue;
            if (targetId == id)
                continue;

            const int targetArchIndex = impl.archetypeIndex[targetId];
            if (targetArchIndex < 0)
                continue;

            // Chercher la priorité de cet archetype dans la liste
            int priorityRank = -1;
            for (std::size_t pi = 0; pi < priorities.size(); ++pi)
            {
                if (priorities[pi] == targetArchIndex)
                {
                    priorityRank = static_cast<int>(pi);
                    break;
                }
            }

            if (priorityRank < 0)
                continue; // cet archetype n’est pas dans les targets de ce tireur

            const float dx = impl.posX[targetId] - shooterX;
            const float dy = impl.posY[targetId] - shooterY;
            const float distSq = lengthSquared(dx, dy);

            if (distSq > rangeSq)
                continue;

            if (!found)
            {
                found = true;
                bestTargetId = static_cast<std::uint32_t>(targetId);
                bestTargetArchPriority = priorityRank;
                bestDistSq = distSq;
            }
            else
            {
                // Priorité plus haute ? (index plus petit)
                if (priorityRank < bestTargetArchPriority)
                {
                    bestTargetArchPriority = priorityRank;
                    bestTargetId = static_cast<std::uint32_t>(targetId);
                    bestDistSq = distSq;
                }
                else if (priorityRank == bestTargetArchPriority && distSq < bestDistSq)
                {
                    // Même priorité mais plus proche
                    bestTargetId = static_cast<std::uint32_t>(targetId);
                    bestDistSq = distSq;
                }
            }
        }

        if (!found)
            continue;

        // Orienter le tireur vers la cible sélectionnée
        const float dx = impl.posX[bestTargetId] - shooterX;
        const float dy = impl.posY[bestTargetId] - shooterY;
        float lx = dx;
        float ly = dy;
        normalize(lx, ly);

        impl.hasLook[id] = true;
        impl.lookX[id] = lx;
        impl.lookY[id] = ly;
    }
}

// -----------------------------------------------------------------------------
// System : Weapon (gère les firerates + spawn projectiles)
// -----------------------------------------------------------------------------

void World::systemWeapon(float dt)
{
    Impl& impl = *m_impl;
    InternalConfig& ic = *m_config;
    const std::size_t N = m_maxEntities;

    for (std::size_t id = 0; id < N; ++id)
    {
        if (!impl.alive[id])
            continue;
        if (!impl.hasWeapon[id])
            continue;
        if (!impl.hasPosition[id])
            continue;
        if (!impl.hasLook[id])
            continue;
        if (impl.frozen[id])
            continue;

        const int weaponIdx = impl.weaponIndex[id];
        if (weaponIdx < 0 || weaponIdx >= static_cast<int>(ic.weapons.size()))
            continue;

        auto& weapon = ic.weapons[weaponIdx];
        if (weapon.projectileIndex < 0)
            continue;

        // Récupérer l’archetype pour savoir si c’est le player
        bool isPlayer = false;
        if (impl.hasArchetype[id])
        {
            int archIndex = impl.archetypeIndex[id];
            if (archIndex >= 0 && archIndex < (int)ic.archetypes.size())
            {
                const auto& arch = ic.archetypes[archIndex];
                if (arch.name == "player")
                    isPlayer = true;
            }
        }

        float& cd = impl.fireCooldown[id];
        cd -= dt;
        if (cd > 0.f)
            continue;

        // Décision de tirer :
        //  - player : seulement si inputFire == true
        //  - IA : auto-fire
        if (isPlayer && !impl.inputFire[id])
            continue;

        // Tirer un projectile
        Entity shooter;
        shooter.id = static_cast<std::uint32_t>(id);
        shooter.generation = impl.generations[id];

        const float sx = impl.posX[id];
        const float sy = impl.posY[id];

        float lx = impl.lookX[id];
        float ly = impl.lookY[id];
        normalize(lx, ly);

        Entity proj = allocateEntity();
        if (!proj.isValid())
        {
            // Pool saturé -> on décale juste le cooldown
            cd = (weapon.rate > 0.f) ? (1.f / weapon.rate) : 1.f;
            continue;
        }

        const std::uint32_t pid = proj.id;

        const auto& projInfo = ic.projectiles[weapon.projectileIndex];

        impl.alive[pid] = true;
        impl.hasPosition[pid] = true;
        impl.posX[pid] = sx;
        impl.posY[pid] = sy;

        impl.hasVelocity[pid] = true;
        impl.velX[pid] = lx * weapon.projectileSpeed;
        impl.velY[pid] = ly * weapon.projectileSpeed;

        impl.hasLook[pid] = true;
        impl.lookX[pid] = lx;
        impl.lookY[pid] = ly;

        impl.isProjectile[pid] = true;
        impl.projectileIndex[pid] = weapon.projectileIndex;
        impl.projectileOwner[pid] = shooter;
        impl.projectileDamage[pid] = weapon.damage;

        impl.hasLifetime[pid] = true;
        impl.lifetime[pid] = weapon.projectileLifetime;

        impl.hasCollision[pid] = projInfo.collision;
        impl.projHalfWidth[pid]  = projInfo.halfWidth;
        impl.projHalfHeight[pid] = projInfo.halfHeight;

        // Event projectile tiré
        pushProjectileFiredEvent(proj, shooter);

        // Reset du cooldown (tir régulier)
        cd = (weapon.rate > 0.f) ? (1.f / weapon.rate) : 1.f;
    }
}

// -----------------------------------------------------------------------------
// System : ProjectileDamage (collision simple AABB vs points)
// -----------------------------------------------------------------------------

void World::systemProjectileDamage()
{
    Impl& impl = *m_impl;
    const std::size_t N = m_maxEntities;

    // On parcourt tous les projectiles et on teste une collision "point vs AABB"
    // en considérant les autres entités comme des points.
    for (std::size_t pid = 0; pid < N; ++pid)
    {
        if (!impl.alive[pid])
            continue;
        if (!impl.isProjectile[pid])
            continue;
        if (!impl.hasPosition[pid])
            continue;
        if (!impl.hasCollision[pid])
            continue;
        if (impl.frozen[pid])
            continue;

        const float px = impl.posX[pid];
        const float py = impl.posY[pid];
        const float hw = impl.projHalfWidth[pid];
        const float hh = impl.projHalfHeight[pid];

        const Entity shooter = impl.projectileOwner[pid];
        const int damage = impl.projectileDamage[pid];

        bool hitSomething = false;
        Entity hitEntity{};

        for (std::size_t tid = 0; tid < N; ++tid)
        {
            if (!impl.alive[tid])
                continue;
            if (!impl.hasPosition[tid])
                continue;
            if (!impl.hasCollision[tid])
                continue;

            // Eviter de se hit soi-même
            if (impl.isProjectile[tid])
                continue;

            Entity target;
            target.id = static_cast<std::uint32_t>(tid);
            target.generation = impl.generations[tid];

            // Eviter de toucher le tireur (si toujours valide)
            if (validateEntity(shooter) && shooter.id == target.id)
                continue;

            const float tx = impl.posX[tid];
            const float ty = impl.posY[tid];

            // Test AABB (projectile) vs point (entité)
            if (tx >= px - hw && tx <= px + hw &&
                ty >= py - hh && ty <= py + hh)
            {
                hitSomething = true;
                hitEntity = target;
                break;
            }
        }

        if (hitSomething)
        {
            // Appliquer les dégâts
            if (validateEntity(hitEntity))
            {
                std::uint32_t tid = hitEntity.id;
                if (impl.hasHealth[tid])
                {
                    impl.health[tid] -= damage;
                    pushEntityDamagedEvent(hitEntity, shooter, damage);

                    if (impl.health[tid] <= 0)
                    {
                        destroyImmediate(hitEntity);
                    }
                }
            }

            // Détruire le projectile
            Entity proj;
            proj.id = static_cast<std::uint32_t>(pid);
            proj.generation = impl.generations[pid];
            destroyImmediate(proj);
        }
    }
}

// -----------------------------------------------------------------------------
// System : Lifetime
// -----------------------------------------------------------------------------

void World::systemLifetime(float dt)
{
    Impl& impl = *m_impl;
    const std::size_t N = m_maxEntities;

    for (std::size_t id = 0; id < N; ++id)
    {
        if (!impl.alive[id])
            continue;
        if (!impl.hasLifetime[id])
            continue;
        if (impl.frozen[id])
            continue;

        impl.lifetime[id] -= dt;
        if (impl.lifetime[id] <= 0.f)
        {
            Entity e;
            e.id = static_cast<std::uint32_t>(id);
            e.generation = impl.generations[id];
            destroyImmediate(e);
        }
    }
}

// -----------------------------------------------------------------------------
// System : Health (au cas où des HP seraient passés à <= 0 via d'autres moyens)
// -----------------------------------------------------------------------------

void World::systemHealth()
{
    Impl& impl = *m_impl;
    const std::size_t N = m_maxEntities;

    for (std::size_t id = 0; id < N; ++id)
    {
        if (!impl.alive[id])
            continue;
        if (!impl.hasHealth[id])
            continue;

        if (impl.health[id] <= 0)
        {
            Entity e;
            e.id = static_cast<std::uint32_t>(id);
            e.generation = impl.generations[id];
            destroyImmediate(e);
        }
    }
}

} // namespace ecs
