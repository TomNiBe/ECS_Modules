// Moteur de jeu léger bâti sur l’ECS et la couche réseau.  Le moteur possède
// un ecs::registry et fournit des méthodes pour charger la configuration,
// créer des entités depuis des archétypes et faire avancer la simulation.  Les
// paramètres proviennent d’un script Lua (resources.hpp).  Les systèmes sont
// enregistrés à la construction et exécutés à chaque appel de update().  Ce
// fichier déclare le moteur et les types de composants utilisés par défaut.

#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <vector>
#include <functional>

#include "ecs/ecs.hpp"
#include "engine/resources.hpp"
#include "ecs/zipper.hpp"

namespace engine {

// -----------------------------------------------------------------------------
// Définitions des composants : structures de données initialisées depuis Lua.
// -----------------------------------------------------------------------------

// Position 2D (centre de l’entité)
struct Position {
    float x = 0.f;
    float y = 0.f;
};

// Vitesse 2D (unités par seconde sur X et Y)
struct Velocity {
    float x = 0.f;
    float y = 0.f;
};

// Vitesse de déplacement (scalaire)
struct Speed {
    float value = 0.f;
};

// Direction de visée (non normalisée)
struct LookDirection {
    float x = 1.f;
    float y = 0.f;
};

// Points de vie
struct Health {
    int value = 0;
};

// Boîte de collision (demi‑dimensions et offset)
struct Hitbox {
    float halfWidth  = 0.5f;
    float halfHeight = 0.5f;
    // Offsets du centre de la hitbox
    float offsetX = 0.f;
    float offsetY = 0.f;

    Hitbox() = default;
    Hitbox(float hw, float hh, float offX = 0.f, float offY = 0.f)
        : halfWidth(hw), halfHeight(hh), offsetX(offX), offsetY(offY) {}
};

// Paramètres de collision : couche, masque, solidité, déclencheur et statique
struct Collider {
    std::uint32_t layer = 0;
    std::uint32_t mask  = 0;
    bool isSolid   = false;
    bool isTrigger = true;
    bool isStatic  = false;

    Collider() = default;
    Collider(std::uint32_t l, std::uint32_t m,
             bool solid = false, bool trigger = true, bool isStatic = false)
        : layer(l), mask(m), isSolid(solid), isTrigger(trigger), isStatic(isStatic) {}
};

// Identifiant de faction/équipe
struct Faction {
    int id = 0;

    Faction() = default;
    explicit Faction(int fid) : id(fid) {}
};

// Dégâts en attente (appliqués puis remis à zéro)
struct PendingDamage {
    int amount = 0;
    // identifiant de la source (facultatif)
    std::size_t source = static_cast<std::size_t>(-1);

    PendingDamage() = default;
    PendingDamage(int amt, std::size_t src = static_cast<std::size_t>(-1))
        : amount(amt), source(src) {}
};

// Capacité de perforation : nombre de cibles restantes et entités déjà touchées
struct Piercing {
    int remainingHits = 0;
    std::unordered_set<std::size_t> hitEntities;

    Piercing() = default;
    Piercing(int hits, std::unordered_set<std::size_t> set = {})
        : remainingHits(hits), hitEntities(std::move(set)) {}
};

// Épines : inflige des dégâts au contact lorsque activé
struct Thorns {
    int  damage  = 0;
    bool enabled = false;
    Thorns() = default;
    Thorns(int dmg, bool en) : damage(dmg), enabled(en) {}
};

// Référence vers la définition d’archétype utilisée pour l’entité
struct ArchetypeRef {
    const Archetype* def = nullptr;
    ArchetypeRef() = default;
    explicit ArchetypeRef(const Archetype* a) : def(a) {}
};

// Liste des noms d’archétypes cibles (IA)
struct TargetList {
    // Ordre de priorité des cibles (défini dans la configuration)
    std::vector<std::string> names;
    // Mode de sélection par catégorie (par exemple « closest_in_class »)
    std::unordered_map<std::string, std::string> modes;

    TargetList() = default;
    TargetList(const std::vector<std::string>& ns,
               const std::unordered_map<std::string, std::string>& md = {})
        : names(ns), modes(md) {}
};

// Portée d’attaque
struct Range {
    float value = 0.f;
};

// Indique si l’entité peut réapparaître (sinon transitoire)
struct Respawnable {
    bool value = false;
};

// Durée de vie restante ; l’entité est détruite lorsque le temps expire
struct Lifetime {
    float remaining = 0.f;
};

// Dégâts infligés
struct Damage {
    int value = 0;
};

// Pattern de mouvement : séquence d’offsets et index courant
struct MovementPatternComp {
    // Séquence d’offsets et index pour avancer dans la séquence
    std::vector<std::pair<float, float>> offsets;
    std::size_t index = 0;
};

// Position désirée après intégration ; ajustée par les collisions puis appliquée
struct DesiredPosition {
    float x = 0.f;
    float y = 0.f;
    DesiredPosition() = default;
    DesiredPosition(float ix, float iy) : x(ix), y(iy) {}
};

// État d’entrée : axes de mouvement [-1,1] et état du tir
struct InputState {
    float moveX = 0.f;
    float moveY = 0.f;
    bool  firePressed  = false;
    bool  fireHeld     = false;
    bool  fireReleased = false;
};

// Référence à une arme et état associé (cooldown, chargement, etc.)
struct WeaponRef {
    const WeaponDef* def = nullptr;
    float            cooldown = 0.f;
    float            timer    = 0.f;
    bool             isCharging    = false;
    float            chargeTimeAccum = 0.f;
    // Le niveau de charge est calculé lors du relâchement et réinitialisé après le tir
    std::size_t      chargeLevel    = 0;
};

// -----------------------------------------------------------------------------
// Classe Engine : encapsule le registry ECS et orchestre la simulation.
// -----------------------------------------------------------------------------
class Engine {
public:
    // Construit le moteur à partir de la configuration ; enregistre composants et systèmes.  Lève une exception si une définition manque.
    explicit Engine(const GameConfig& cfg)
        : m_config(cfg) {
        // Enregistre tous les types de composants dans le registry
        m_registry.register_component<Position>();
        m_registry.register_component<Velocity>();
        m_registry.register_component<Speed>();
        m_registry.register_component<LookDirection>();
        m_registry.register_component<Health>();
        m_registry.register_component<Hitbox>();
        m_registry.register_component<Collider>();
        m_registry.register_component<Faction>();
        m_registry.register_component<PendingDamage>();
        m_registry.register_component<Piercing>();
        m_registry.register_component<TargetList>();
        m_registry.register_component<Range>();
        m_registry.register_component<Respawnable>();
        m_registry.register_component<Lifetime>();
        m_registry.register_component<Damage>();
        m_registry.register_component<MovementPatternComp>();
        m_registry.register_component<InputState>();
        m_registry.register_component<WeaponRef>();
        m_registry.register_component<DesiredPosition>();
        // Composants supplémentaires
        m_registry.register_component<Thorns>();
        m_registry.register_component<ArchetypeRef>();
        // Enregistre les systèmes ; ils capturent m_dt par référence et cette valeur
        // sera mise à jour dans update() avant leur exécution.
        registerSystems();
    }

    // Fait apparaître une entité depuis un archétype ; position initiale facultative.
    // Lève invalid_argument si l’archétype ou l’arme associée est inconnu.
    ecs::entity_t spawn(const std::string& archetypeName, float x = 0.f, float y = 0.f) {
        auto it = m_config.archetypes.find(archetypeName);
        if (it == m_config.archetypes.end()) {
            throw std::invalid_argument("Unknown archetype: " + archetypeName);
        }
        const Archetype& arch = it->second;
        ecs::entity_t ent = m_registry.spawn_entity();
        // Position et vitesse initiales
        m_registry.emplace_component<Position>(ent, x, y);
        m_registry.emplace_component<Velocity>(ent, 0.f, 0.f);
        // Vitesse de déplacement
        m_registry.emplace_component<Speed>(ent, arch.speed);
        // Direction de visée
        m_registry.emplace_component<LookDirection>(ent, arch.lookDirection.x, arch.lookDirection.y);
        // Points de vie
        m_registry.emplace_component<Health>(ent, arch.health);
        // Boîte de collision (demi‑dimensions)
        // Inclut les décalages si présents
        m_registry.emplace_component<Hitbox>(ent, arch.hitbox.width * 0.5f, arch.hitbox.height * 0.5f,
                                             arch.hitbox.offsetX, arch.hitbox.offsetY);
        // Composant de collision
        m_registry.emplace_component<Collider>(ent, arch.colliderLayer, arch.colliderMask,
                                              arch.colliderSolid, arch.colliderTrigger, arch.colliderStatic);
        // Faction
        m_registry.emplace_component<Faction>(ent, arch.faction);
        // Liste de cibles : utilise l’ordre et les modes de la configuration ; si vide, l’IA vise le plus proche
        m_registry.emplace_component<TargetList>(ent, arch.targetOrder, arch.targetMode);
        // Portée d’attaque
        m_registry.emplace_component<Range>(ent, arch.range);
        // Réapparition possible
        m_registry.emplace_component<Respawnable>(ent, arch.respawnable);
        // Motif de déplacement
        m_registry.emplace_component<MovementPatternComp>(ent, arch.pattern.offsets, 0u);
        // Référence vers l’archétype pour l’IA
        m_registry.emplace_component<ArchetypeRef>(ent, &arch);
        // État d’entrée (initialement inactif)
        m_registry.emplace_component<InputState>(ent, 0.f, 0.f, false);
        // Arme
        if (!arch.weaponName.empty()) {
            auto wit = m_config.weapons.find(arch.weaponName);
            if (wit == m_config.weapons.end()) {
                throw std::invalid_argument("Archetype references unknown weapon: " + arch.weaponName);
            }
            const WeaponDef& wdef = wit->second;
            // Calcule le temps de recharge (inverse de la cadence de tir)
            float cooldown = (wdef.rate > 0.f) ? (1.f / wdef.rate) : std::numeric_limits<float>::infinity();
            m_registry.emplace_component<WeaponRef>(ent, &wdef, cooldown, 0.f);
        }
        // Composant épines
        if (arch.thornsEnabled || arch.thornsDamage > 0) {
            m_registry.emplace_component<Thorns>(ent, arch.thornsDamage, arch.thornsEnabled);
        }
        return ent;
    }

    // Renvoie une référence au registry sous‑jacent (ne pas la conserver au‑delà de la durée de vie du moteur).
    ecs::registry& getRegistry() { return m_registry; }

    // Avance la simulation de dt secondes et exécute les systèmes enregistrés.
    void update(float dt) {
        m_dt = dt;
        // Exécute tous les systèmes pour mettre à jour les composants
        m_registry.run_systems();
        // Résout les collisions solides sans mettre à jour immédiatement Position
        resolveSolidCollisions();
        // Applique les limites de la zone jouable pour le joueur
        enforcePlayableBoundsForPlayer();
        // Copie les positions désirées résolues dans Position
        commitPositions();
        // Supprime les entités hors des limites du monde
        cullOutsideWorldBounds();
        // Gère les collisions, projectiles et épines après mise à jour des positions
        handleCollisions();
        // Applique les dégâts accumulés et détruit les entités sans points de vie
        applyDamage();
        // Supprime les entités dont la durée de vie est expirée
        auto &lifetimes = m_registry.get_components<Lifetime>();
        for (std::size_t idx = 0; idx < lifetimes.size(); ++idx) {
            ecs::entity_t ent{idx};
            auto &opt = lifetimes[ent];
            if (opt && opt->remaining <= 0.f) {
                m_registry.kill_entity(ent);
            }
        }
    }

private:
    // Enregistre les systèmes internes.  Les lambdas capturent m_dt par référence pour utiliser la valeur mise à jour dans update().
    void registerSystems() {
        // Système d’entrée : convertit InputState et Speed en Velocity
        m_registry.template add_system<InputState, Velocity, Speed>([this](ecs::registry &,
                                                                           auto &inputs,
                                                                           auto &vels,
                                                                           auto &speeds) {
            for (auto [in, vel, spd] : ecs::zip(inputs, vels, speeds)) {
                // Axes de déplacement
                vel.x = in.moveX * spd.value;
                vel.y = in.moveY * spd.value;
            }
        });
        // Système de position désirée : intègre Velocity dans DesiredPosition avec dt
        m_registry.template add_system<Position, Velocity, DesiredPosition>([this](ecs::registry &r,
                                                                                  auto &positions,
                                                                                  auto &vels,
                                                                                  auto &desired) {
            for (std::size_t idx = 0; idx < positions.size(); ++idx) {
                ecs::entity_t ent{idx};
                auto &posOpt = positions[ent];
                auto &velOpt = vels[ent];
                if (!posOpt || !velOpt) {
                    continue;
                }
                    float newX = posOpt->x + velOpt->x * m_dt;
                    float newY = posOpt->y + velOpt->y * m_dt;
                    auto &desOpt = desired[ent];
                    if (desOpt) {
                        desOpt->x = newX;
                        desOpt->y = newY;
                    } else {
                        r.emplace_component<DesiredPosition>(ent, newX, newY);
                    }
            }
        });
        // Système d’armes : gère la charge et crée les projectiles selon le niveau de charge
        m_registry.template add_system<WeaponRef, InputState, Position, LookDirection>([this](ecs::registry &r,
                                                                                              auto &weapons,
                                                                                              auto &inputs,
                                                                                              auto &positions,
                                                                                              auto &looks) {
            for (std::size_t idx = 0; idx < weapons.size(); ++idx) {
                ecs::entity_t ent{idx};
                auto &wOpt = weapons[ent];
                auto &inOpt = inputs[ent];
                auto &posOpt = positions[ent];
                auto &lookOpt = looks[ent];
                if (!wOpt || !inOpt || !posOpt || !lookOpt) {
                    continue;
                }
                WeaponRef &w = *wOpt;
                InputState &in = *inOpt;
                Position &pos = *posOpt;
                LookDirection &look = *lookOpt;
                // Avance le compteur de rechargement
                if (w.timer > 0.f) {
                    w.timer -= m_dt;
                }
                // Gère la charge
                // Commence à charger lorsque le tir est pressé
                if (in.firePressed) {
                    w.isCharging = true;
                    w.chargeTimeAccum = 0.f;
                }
                // Accumule la charge tant que le tir est maintenu
                if (in.fireHeld && w.isCharging) {
                    float maxT = (w.def ? w.def->charge.maxTime : 0.f);
                    w.chargeTimeAccum += m_dt;
                    if (w.chargeTimeAccum > maxT) {
                        w.chargeTimeAccum = maxT;
                    }
                }
                // Relâchement : crée un projectile selon le niveau de charge
                if (in.fireReleased && w.isCharging && w.timer <= 0.f && w.def) {
                    // Calcule le niveau de charge selon les seuils
                    std::size_t level = 0;
                    const auto &spec = w.def->charge;
                    for (std::size_t i = 0; i < spec.thresholds.size(); ++i) {
                        if (w.chargeTimeAccum >= spec.thresholds[i]) {
                            level = i;
                        }
                    }
                    if (!spec.levels.empty() && level >= spec.levels.size()) {
                        level = spec.levels.size() - 1;
                    }
                    w.chargeLevel = level;
                    // Normalise la direction de visée
                    float dx = look.x;
                    float dy = look.y;
                    float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 0.f) {
                        dx /= len;
                        dy /= len;
                    } else {
                        dx = 1.f;
                        dy = 0.f;
                    }
                    // Crée l’entité projectile
                    const WeaponDef* wdef = w.def;
                    // Recherche la définition du projectile
                    auto pit = m_config.projectiles.find(wdef->projectileName);
                    if (pit != m_config.projectiles.end()) {
                        const ProjectileDef& pdef = pit->second;
                        ecs::entity_t proj = r.spawn_entity();
                        // Position
                        r.emplace_component<Position>(proj, pos.x, pos.y);
                        // Calcule la vitesse finale
                        float baseSpeed = wdef->speed;
                        float finalSpeed = baseSpeed;
                        int finalPierce = wdef->piercingHits;
                        int finalDamage = wdef->damage;
                        float sizeMul = 1.f;
                        if (level < spec.levels.size()) {
                            const auto &lev = spec.levels[level];
                            finalDamage = static_cast<int>(std::round(static_cast<float>(wdef->damage) * lev.damageMul));
                            finalSpeed  = baseSpeed * lev.speedMul;
                            sizeMul     = lev.sizeMul;
                            finalPierce = wdef->piercingHits + lev.piercingHits;
                        }
                        // Vitesse
                        r.emplace_component<Velocity>(proj, dx * finalSpeed, dy * finalSpeed);
                        // Durée de vie
                        r.emplace_component<Lifetime>(proj, wdef->lifetime);
                        // Dégâts
                        r.emplace_component<Damage>(proj, finalDamage);
                        // Hitbox mise à l’échelle
                        float hw = pdef.width * 0.5f * sizeMul;
                        float hh = pdef.height * 0.5f * sizeMul;
                        r.emplace_component<Hitbox>(proj, hw, hh);
                        // Les projectiles ne réapparaissent pas
                        r.emplace_component<Respawnable>(proj, false);
                        // Attribue la couche et le masque selon la faction du tireur
                        int factionId = 0;
                        auto &facOpt = r.get_components<Faction>()[ent];
                        if (facOpt) {
                            factionId = facOpt->id;
                        }
                        // Attribue des couches exemples : 0x4 pour les projectiles du joueur et 0x8 pour ceux de l’ennemi
                        std::uint32_t layer = (factionId == 0 ? 0x4u : 0x8u);
                        std::uint32_t mask  = (factionId == 0 ? 0x2u : 0x1u);
                        // Les projectiles sont des déclencheurs et ne bloquent pas
                        r.emplace_component<Collider>(proj, layer, mask, /*solid*/ false, /*trigger*/ true, /*static*/ false);
                        r.emplace_component<Faction>(proj, factionId);
                        // Capacité de perforation
                        r.emplace_component<Piercing>(proj, finalPierce, std::unordered_set<std::size_t>{});
                    }
                    // Réinitialise l’état de charge
                    w.isCharging = false;
                    w.chargeTimeAccum = 0.f;
                    // Applique le temps de recharge
                    w.timer = w.cooldown;
                }
                // Réinitialise les indicateurs de tir pour chaque frame
                in.firePressed  = false;
                in.fireReleased = false;
            }
        });
        // Système de durée de vie : diminue la durée restante à chaque frame
        m_registry.template add_system<Lifetime>([this](ecs::registry &,
                                                        auto &lifetimes) {
            for (std::size_t idx = 0; idx < lifetimes.size(); ++idx) {
                auto &opt = lifetimes[ecs::entity_t{idx}];
                if (opt) {
                    opt->remaining -= m_dt;
                }
            }
        });

        // -----------------------------------------------------------------
        // Système de pattern de mouvement : applique les offsets de mouvement définis en Lua
        m_registry.template add_system<Position, MovementPatternComp, DesiredPosition>([this](ecs::registry &r,
                                                                                             auto &positions,
                                                                                             auto &patterns,
                                                                                             auto &desired) {
            std::size_t count = patterns.size();
            for (std::size_t idx = 0; idx < count; ++idx) {
                ecs::entity_t ent{idx};
                auto &patOpt = patterns[ent];
                auto &posOpt = positions[ent];
                if (!patOpt || !posOpt) {
                    continue;
                }
                auto &pat = *patOpt;
                if (pat.offsets.empty()) {
                    continue;
                }
                // Calcule le déplacement en fonction de dt
                const auto &off = pat.offsets[pat.index];
                float dx = off.first * m_dt;
                float dy = off.second * m_dt;
                auto &desOpt = desired[ent];
                if (desOpt) {
                    desOpt->x += dx;
                    desOpt->y += dy;
                } else {
                    // Crée une position désirée à partir de la position actuelle
                    const Position &pos = *posOpt;
                    r.emplace_component<DesiredPosition>(ent, pos.x + dx, pos.y + dy);
                }
                // Avance l’index du motif avec remise à zéro
                ++pat.index;
                if (pat.index >= pat.offsets.size()) {
                    pat.index = 0;
                }
            }
        });

        // -----------------------------------------------------------------
        // Système d’IA ennemie : vise et tire vers des cibles selon la priorité et la portée
        m_registry.template add_system<WeaponRef, InputState, Position, LookDirection, Faction, Range, TargetList, ArchetypeRef>([this](ecs::registry &r,
                                                                                                                                     auto &weapons,
                                                                                                                                     auto &inputs,
                                                                                                                                     auto &positions,
                                                                                                                                     auto &lookdirs,
                                                                                                                                     auto &factions,
                                                                                                                                     auto &ranges,
                                                                                                                                     auto &targets,
                                                                                                                                     auto &archRefs) {
            std::size_t total = positions.size();
            // Parcourt toutes les entités avec une arme
            for (std::size_t idx = 0; idx < weapons.size(); ++idx) {
                ecs::entity_t ent{idx};
                auto &wOpt     = weapons[ent];
                auto &inOpt    = inputs[ent];
                auto &posOpt   = positions[ent];
                auto &lookOpt  = lookdirs[ent];
                auto &facOpt   = factions[ent];
                auto &rangeOpt = ranges[ent];
                auto &targOpt  = targets[ent];
                if (!wOpt || !inOpt || !posOpt || !lookOpt || !facOpt || !rangeOpt || !targOpt) {
                    continue;
                }
                // Ignore la faction du joueur (id 0)
                if (facOpt->id == 0) {
                    continue;
                }
                WeaponRef &w = *wOpt;
                // Tire seulement si l’arme est prête (timer ≤ 0)
                if (w.timer > 0.f) {
                    continue;
                }
                const Position &myPos = *posOpt;
                int myFaction = facOpt->id;
                float maxDistSq = rangeOpt->value * rangeOpt->value;
                const auto &order = targOpt->names;
                const auto &modeMap = targOpt->modes;
                std::size_t chosenIdx = static_cast<std::size_t>(-1);
                float chosenDistSq = 0.f;
                // Traite les catégories de priorité
                for (const std::string &cat : order) {
                    bool useClosest = false;
                    auto mit = modeMap.find(cat);
                    if (mit != modeMap.end()) {
                        std::string mode = mit->second;
                        if (mode == "closest_in_class" || mode == "closest") {
                            useClosest = true;
                        }
                    }
                    std::size_t bestCandidate = static_cast<std::size_t>(-1);
                    float bestDistSq = std::numeric_limits<float>::max();
                    for (std::size_t j = 0; j < total; ++j) {
                        if (j == idx) continue;
                        ecs::entity_t other{j};
                        auto &oPosOpt = positions[other];
                        auto &oFacOpt = factions[other];
                        auto &oArchOpt = archRefs[other];
                        if (!oPosOpt || !oFacOpt || !oArchOpt) continue;
                        // Ignore la même faction
                        if (oFacOpt->id == myFaction) continue;
                        const Archetype *adef = oArchOpt->def;
                        if (!adef) continue;
                        if (adef->name != cat) continue;
                        // Vérifie la portée
                        const Position &oPos = *oPosOpt;
                        float dx = oPos.x - myPos.x;
                        float dy = oPos.y - myPos.y;
                        float distSq = dx * dx + dy * dy;
                        if (distSq > maxDistSq) continue;
                        if (useClosest) {
                            if (distSq < bestDistSq || (distSq == bestDistSq && j < bestCandidate)) {
                                bestDistSq = distSq;
                                bestCandidate = j;
                            }
                        } else {
                            bestCandidate = j;
                            bestDistSq = distSq;
                            break;
                        }
                    }
                    if (bestCandidate != static_cast<std::size_t>(-1)) {
                        chosenIdx = bestCandidate;
                        chosenDistSq = bestDistSq;
                        break;
                    }
                }
                // Si aucune catégorie n’est spécifiée, vise l’ennemi le plus proche
                if (chosenIdx == static_cast<std::size_t>(-1) && order.empty()) {
                    float bestDistSq = std::numeric_limits<float>::max();
                    std::size_t bestIdx = static_cast<std::size_t>(-1);
                    for (std::size_t j = 0; j < total; ++j) {
                        if (j == idx) continue;
                        ecs::entity_t other{j};
                        auto &oPosOpt = positions[other];
                        auto &oFacOpt = factions[other];
                        if (!oPosOpt || !oFacOpt) continue;
                        if (oFacOpt->id == myFaction) continue;
                        const Position &oPos = *oPosOpt;
                        float dx = oPos.x - myPos.x;
                        float dy = oPos.y - myPos.y;
                        float distSq = dx * dx + dy * dy;
                        if (distSq > maxDistSq) continue;
                        if (distSq < bestDistSq || (distSq == bestDistSq && j < bestIdx)) {
                            bestDistSq = distSq;
                            bestIdx = j;
                        }
                    }
                    if (bestIdx != static_cast<std::size_t>(-1)) {
                        chosenIdx = bestIdx;
                        chosenDistSq = bestDistSq;
                    }
                }
                // Vise et tire si une cible est trouvée
                if (chosenIdx != static_cast<std::size_t>(-1)) {
                    ecs::entity_t tEnt{chosenIdx};
                    auto &tPosOpt = positions[tEnt];
                    if (tPosOpt) {
                        const Position &tPos = *tPosOpt;
                        float dx = tPos.x - myPos.x;
                        float dy = tPos.y - myPos.y;
                        float len = std::sqrt(dx * dx + dy * dy);
                        if (len > 0.f) {
                            dx /= len;
                            dy /= len;
                        } else {
                            dx = 1.f;
                            dy = 0.f;
                        }
                        lookOpt->x = dx;
                        lookOpt->y = dy;
                        InputState &in = *inOpt;
                        in.firePressed  = true;
                        in.fireReleased = true;
                        in.fireHeld     = false;
                    }
                }
            }
        });

        // D’autres systèmes peuvent être enregistrés ; les collisions et dégâts sont gérés dans update()
    }

    GameConfig m_config;
    ecs::registry m_registry;
    float m_dt = 0.f;

    // ---------------------------------------------------------------------
    // Gestion des collisions et des dégâts entre entités
    void handleCollisions() {
        // Récupère les tableaux de composants
        auto &positions = m_registry.get_components<Position>();
        auto &hitboxes  = m_registry.get_components<Hitbox>();
        auto &colliders = m_registry.get_components<Collider>();
        auto &factions  = m_registry.get_components<Faction>();
        auto &damages   = m_registry.get_components<Damage>();
        auto &piercings = m_registry.get_components<Piercing>();
        auto &thorns    = m_registry.get_components<Thorns>();
        // Collecte les entités qui possèdent position, hitbox et collider
        std::vector<std::size_t> ents;
        std::size_t maxCount = positions.size();
        ents.reserve(maxCount);
        for (std::size_t idx = 0; idx < maxCount; ++idx) {
            ecs::entity_t ent{idx};
            if (positions[ent] && hitboxes[ent] && colliders[ent]) {
                ents.push_back(idx);
            }
        }
        std::vector<ecs::entity_t> toKill;
        // Vérifie chaque paire une fois
        for (std::size_t a = 0; a < ents.size(); ++a) {
            for (std::size_t b = a + 1; b < ents.size(); ++b) {
                std::size_t ia = ents[a];
                std::size_t ib = ents[b];
                ecs::entity_t entA{ia};
                ecs::entity_t entB{ib};
                auto &posAOpt = positions[entA];
                auto &posBOpt = positions[entB];
                if (!posAOpt || !posBOpt) {
                    continue;
                }
                auto &hbAOpt = hitboxes[entA];
                auto &hbBOpt = hitboxes[entB];
                auto &colAOpt = colliders[entA];
                auto &colBOpt = colliders[entB];
                if (!hbAOpt || !hbBOpt || !colAOpt || !colBOpt) {
                    continue;
                }
                Collider &colA = *colAOpt;
                Collider &colB = *colBOpt;
                // Calcule les hitbox en coordonnées monde
                Position &posA = *posAOpt;
                Hitbox &hbA  = *hbAOpt;
                Position &posB = *posBOpt;
                Hitbox &hbB  = *hbBOpt;
                float leftA   = posA.x + hbA.offsetX - hbA.halfWidth;
                float rightA  = posA.x + hbA.offsetX + hbA.halfWidth;
                float topA    = posA.y + hbA.offsetY - hbA.halfHeight;
                float bottomA = posA.y + hbA.offsetY + hbA.halfHeight;
                float leftB   = posB.x + hbB.offsetX - hbB.halfWidth;
                float rightB  = posB.x + hbB.offsetX + hbB.halfWidth;
                float topB    = posB.y + hbB.offsetY - hbB.halfHeight;
                float bottomB = posB.y + hbB.offsetY + hbB.halfHeight;
                bool intersects = !(leftA > rightB || rightA < leftB || topA > bottomB || bottomA < topB);
                if (!intersects) {
                    continue;
                }
                // Vérifie les masques de collision
                if (((colA.mask & colB.layer) == 0) && ((colB.mask & colA.layer) == 0)) {
                    continue;
                }
                // Applique les dégâts d’épines indépendamment du statut de déclencheur
                auto &thAOpt = thorns[entA];
                if (thAOpt && thAOpt->enabled && thAOpt->damage > 0) {
                    int tdmg = thAOpt->damage;
                    auto &pdOptB = m_registry.get_components<PendingDamage>()[entB];
                    if (!pdOptB) {
                        m_registry.emplace_component<PendingDamage>(entB, tdmg, entA.value());
                    } else {
                        pdOptB->amount += tdmg;
                    }
                }
                auto &thBOpt = thorns[entB];
                if (thBOpt && thBOpt->enabled && thBOpt->damage > 0) {
                    int tdmg = thBOpt->damage;
                    auto &pdOptA = m_registry.get_components<PendingDamage>()[entA];
                    if (!pdOptA) {
                        m_registry.emplace_component<PendingDamage>(entA, tdmg, entB.value());
                    } else {
                        pdOptA->amount += tdmg;
                    }
                }
                // Ignore le reste si les deux colliders sont solides (résolution déjà faite)
                if (!colA.isTrigger && !colB.isTrigger) {
                    continue;
                }
                // Identifie si les entités sont des projectiles
                bool aProjectile = damages[entA].has_value();
                bool bProjectile = damages[entB].has_value();
                // Si une seule entité est un projectile, applique ses dégâts
                if (aProjectile != bProjectile) {
                    ecs::entity_t proj  = aProjectile ? entA : entB;
                    ecs::entity_t target = aProjectile ? entB : entA;
                    // Ignore les tirs alliés lors du calcul des dégâts
                    auto &facProj = factions[proj];
                    auto &facTarget = factions[target];
                    if (!facProj || !facTarget || facProj->id != facTarget->id) {
                        // Évite plusieurs impacts sur la même cible pour les projectiles perforants
                        auto &pOpt = piercings[proj];
                        if (!pOpt || pOpt->hitEntities.insert(target.value()).second) {
                            // Accumule les dégâts
                            int dmg = damages[proj]->value;
                            auto &pdOpt = m_registry.get_components<PendingDamage>()[target];
                            if (!pdOpt) {
                                m_registry.emplace_component<PendingDamage>(target, dmg, proj.value());
                            } else {
                                pdOpt->amount += dmg;
                            }
                            // Diminue les perforations restantes et marque le projectile à supprimer
                            if (pOpt) {
                                if (--pOpt->remainingHits <= 0) {
                                    toKill.push_back(proj);
                                }
                            } else {
                                toKill.push_back(proj);
                            }
                        }
                    }
                }
            }
        }
        for (auto ent : toKill) {
            m_registry.kill_entity(ent);
        }
    }

    // ---------------------------------------------------------------------
    // Résolution des collisions solides : ajuste DesiredPosition pour éviter les pénétrations
    void resolveSolidCollisions() {
        auto &positions = m_registry.get_components<Position>();
        auto &desired   = m_registry.get_components<DesiredPosition>();
        auto &vels      = m_registry.get_components<Velocity>();
        auto &hitboxes  = m_registry.get_components<Hitbox>();
        auto &colliders = m_registry.get_components<Collider>();
        std::size_t count = positions.size();
        // Premier passage : ajuste la composante X de DesiredPosition contre les autres solides
        for (std::size_t i = 0; i < count; ++i) {
            ecs::entity_t ent{i};
            auto &posOpt = positions[ent];
            auto &desOpt = desired[ent];
            auto &hbOpt  = hitboxes[ent];
            auto &colOpt = colliders[ent];
            auto &velOpt = vels[ent];
            if (!posOpt || !desOpt || !hbOpt || !colOpt || !colOpt->isSolid) {
                continue;
            }
            // Positions actuelle et désirée
            float oldX = posOpt->x;
            float oldY = posOpt->y;
            float candX = desOpt->x;
            float candY = desOpt->y;
            Hitbox &hbA = *hbOpt;
            Collider &colA = *colOpt;
            // Résolution sur l’axe X
            float resolvedX = candX;
            for (std::size_t j = 0; j < count; ++j) {
                if (i == j) continue;
                ecs::entity_t other{j};
                auto &posBO = positions[other];
                auto &hbBO  = hitboxes[other];
                auto &colBO = colliders[other];
                if (!posBO || !hbBO || !colBO) continue;
                if (!colBO->isSolid) continue;
                if (((colA.mask & colBO->layer) == 0) && ((colBO->mask & colA.layer) == 0)) continue;
                bool otherIsStatic = colBO->isStatic || i < j;
                if (!otherIsStatic) continue;
                Position &posB = *posBO;
                Hitbox &hbB = *hbBO;
                // Vérifie si le déplacement en X entraîne un croisement
                float oldRightA = oldX + hbA.offsetX + hbA.halfWidth;
                float oldLeftA  = oldX + hbA.offsetX - hbA.halfWidth;
                float newRightA = resolvedX + hbA.offsetX + hbA.halfWidth;
                float newLeftA  = resolvedX + hbA.offsetX - hbA.halfWidth;
                float topA      = oldY + hbA.offsetY - hbA.halfHeight;
                float bottomA   = oldY + hbA.offsetY + hbA.halfHeight;
                float leftB     = posB.x + hbB.offsetX - hbB.halfWidth;
                float rightB    = posB.x + hbB.offsetX + hbB.halfWidth;
                float topB      = posB.y + hbB.offsetY - hbB.halfHeight;
                float bottomB   = posB.y + hbB.offsetY + hbB.halfHeight;
                bool overlapY = !(topA > bottomB || bottomA < topB);
                if (!overlapY) continue;
                // Déplacement vers la droite
                if (resolvedX > oldX) {
                    // Vérifie le croisement du côté gauche de B
                    if (newRightA > leftB && oldRightA <= leftB) {
                        resolvedX = leftB - hbA.offsetX - hbA.halfWidth;
                        if (velOpt) velOpt->x = 0.f;
                    }
                }
                // Déplacement vers la gauche
                if (resolvedX < oldX) {
                    if (newLeftA < rightB && oldLeftA >= rightB) {
                        resolvedX = rightB - hbA.offsetX + hbA.halfWidth;
                        if (velOpt) velOpt->x = 0.f;
                    }
                }
            }
            desOpt->x = resolvedX;
        }
        // Second passage : résolution sur l’axe Y
        for (std::size_t i = 0; i < count; ++i) {
            ecs::entity_t ent{i};
            auto &posOpt = positions[ent];
            auto &desOpt = desired[ent];
            auto &hbOpt  = hitboxes[ent];
            auto &colOpt = colliders[ent];
            auto &velOpt = vels[ent];
            if (!posOpt || !desOpt || !hbOpt || !colOpt || !colOpt->isSolid) continue;
            float oldX = posOpt->x;
            float oldY = posOpt->y;
            float finalX = desOpt->x;
            float candY = desOpt->y;
            Hitbox &hbA = *hbOpt;
            Collider &colA = *colOpt;
            float resolvedY = candY;
            for (std::size_t j = 0; j < count; ++j) {
                if (i == j) continue;
                ecs::entity_t other{j};
                auto &posBO = positions[other];
                auto &hbBO  = hitboxes[other];
                auto &colBO = colliders[other];
                if (!posBO || !hbBO || !colBO) continue;
                if (!colBO->isSolid) continue;
                if (((colA.mask & colBO->layer) == 0) && ((colBO->mask & colA.layer) == 0)) continue;
                bool otherIsStatic = colBO->isStatic || i < j;
                if (!otherIsStatic) continue;
                Position &posB = *posBO;
                Hitbox &hbB = *hbBO;
                // Calcule les boîtes englobantes pour détecter un croisement
                float oldBottomA = oldY + hbA.offsetY + hbA.halfHeight;
                float oldTopA    = oldY + hbA.offsetY - hbA.halfHeight;
                float newBottomA = resolvedY + hbA.offsetY + hbA.halfHeight;
                float newTopA    = resolvedY + hbA.offsetY - hbA.halfHeight;
                // Utilise finalX pour les bornes horizontales
                float leftA  = finalX + hbA.offsetX - hbA.halfWidth;
                float rightA = finalX + hbA.offsetX + hbA.halfWidth;
                float leftB  = posB.x + hbB.offsetX - hbB.halfWidth;
                float rightB = posB.x + hbB.offsetX + hbB.halfWidth;
                float topB   = posB.y + hbB.offsetY - hbB.halfHeight;
                float bottomB= posB.y + hbB.offsetY + hbB.halfHeight;
                bool overlapX = !(leftA > rightB || rightA < leftB);
                if (!overlapX) continue;
                // Déplacement vers le bas
                if (resolvedY > oldY) {
                    if (newBottomA > topB && oldBottomA <= topB) {
                        resolvedY = topB - hbA.offsetY - hbA.halfHeight;
                        if (velOpt) velOpt->y = 0.f;
                    }
                }
                // Déplacement vers le haut
                if (resolvedY < oldY) {
                    if (newTopA < bottomB && oldTopA >= bottomB) {
                        resolvedY = bottomB - hbA.offsetY + hbA.halfHeight;
                        if (velOpt) velOpt->y = 0.f;
                    }
                }
            }
            desOpt->y = resolvedY;
        }
    }

    // Copie les positions désirées résolues dans les composants Position
    void commitPositions() {
        auto &positions = m_registry.get_components<Position>();
        auto &desired   = m_registry.get_components<DesiredPosition>();
        for (std::size_t idx = 0; idx < positions.size(); ++idx) {
            ecs::entity_t ent{idx};
            auto &posOpt = positions[ent];
            auto &desOpt = desired[ent];
            if (posOpt && desOpt) {
                posOpt->x = desOpt->x;
                posOpt->y = desOpt->y;
            }
        }
    }

    // ---------------------------------------------------------------------
    // Application des dégâts : soustrait les dégâts accumulés et détruit les entités à 0 PV
    void applyDamage() {
        auto &pendings = m_registry.get_components<PendingDamage>();
        auto &healths  = m_registry.get_components<Health>();
        std::vector<ecs::entity_t> toKill;
        for (std::size_t idx = 0; idx < pendings.size(); ++idx) {
            ecs::entity_t ent{idx};
            auto &pdOpt = pendings[ent];
            if (!pdOpt) {
                continue;
            }
            int amount = pdOpt->amount;
            auto &hOpt = healths[ent];
            if (hOpt) {
                hOpt->value -= amount;
                if (hOpt->value <= 0) {
                    toKill.push_back(ent);
                }
            }
            // Supprime le composant PendingDamage
            pendings.erase(ent);
        }
        for (auto ent : toKill) {
            m_registry.kill_entity(ent);
        }
    }

    // ---------------------------------------------------------------------
    // Limites jouables pour le joueur
    //
    // Serre la DesiredPosition du joueur dans la zone jouable définie dans la
    // configuration.  Le serrage est appliqué indépendamment sur chaque axe et
    // annule la composante de vitesse correspondante lorsque l’entité touche
    // la frontière.  N’a aucun effet si la zone jouable est désactivée ou
    // si aucune entité joueur n’existe (identifiée par faction 0 et nom
    // d’archétype « player »).
    void enforcePlayableBoundsForPlayer() {
        const auto &bounds = m_config.playableBounds;
        if (!bounds.enabled) {
            return;
        }
        auto &positions = m_registry.get_components<Position>();
        auto &desired   = m_registry.get_components<DesiredPosition>();
        auto &vels      = m_registry.get_components<Velocity>();
        auto &hitboxes  = m_registry.get_components<Hitbox>();
        auto &factions  = m_registry.get_components<Faction>();
        auto &archRefs  = m_registry.get_components<ArchetypeRef>();
        std::size_t count = positions.size();
        for (std::size_t idx = 0; idx < count; ++idx) {
            ecs::entity_t ent{idx};
            auto &posOpt = positions[ent];
            auto &desOpt = desired[ent];
            auto &facOpt = factions[ent];
            auto &archOpt= archRefs[ent];
            if (!posOpt || !desOpt || !facOpt || !archOpt) {
                continue;
            }
            // Vérifie si cette entité est le joueur : identifiant de faction 0 et nom d’archétype « player »
            if (facOpt->id != 0) {
                continue;
            }
            const Archetype* def = archOpt->def;
            if (!def || def->name != "player") {
                continue;
            }
            // Joueur trouvé : serre sa position désirée
            // Récupère les demi‑dimensions et décalages de la hitbox si disponible
            float halfW = 0.f;
            float halfH = 0.f;
            float offX  = 0.f;
            float offY  = 0.f;
            auto &hbOpt = hitboxes[ent];
            if (hbOpt) {
                halfW = hbOpt->halfWidth;
                halfH = hbOpt->halfHeight;
                offX  = hbOpt->offsetX;
                offY  = hbOpt->offsetY;
            }
            // Calcule les bornes autorisées pour le centre de l’entité
            float minX = bounds.minX + halfW - offX;
            float maxX = bounds.maxX - halfW - offX;
            float minY = bounds.minY + halfH - offY;
            float maxY = bounds.maxY - halfH - offY;
            // Applique le serrage et ajuste la vitesse si nécessaire
            float newX = desOpt->x;
            float newY = desOpt->y;
            bool clampedX = false;
            bool clampedY = false;
            if (newX < minX) {
                newX = minX;
                clampedX = true;
            } else if (newX > maxX) {
                newX = maxX;
                clampedX = true;
            }
            if (newY < minY) {
                newY = minY;
                clampedY = true;
            } else if (newY > maxY) {
                newY = maxY;
                clampedY = true;
            }
            if (clampedX) {
                desOpt->x = newX;
                auto &velOpt = vels[ent];
                if (velOpt) {
                    velOpt->x = 0.f;
                }
            }
            if (clampedY) {
                desOpt->y = newY;
                auto &velOpt = vels[ent];
                if (velOpt) {
                    velOpt->y = 0.f;
                }
            }
            // Un seul joueur est attendu ; arrêt après traitement
            break;
        }
    }

    // ---------------------------------------------------------------------
    // Suppression des entités hors des limites du monde
    //
    // Élimine les entités dont la hitbox dépasse les limites configurées (ou,
    // sans hitbox, dont le centre sort de la zone).  Aucun traitement si les
    // limites sont désactivées.  Les suppressions sont différées après
    // l’itération pour éviter d’invalider les indices.
    void cullOutsideWorldBounds() {
        const auto &bounds = m_config.worldBounds;
        if (!bounds.enabled) {
            return;
        }
        auto &positions = m_registry.get_components<Position>();
        auto &hitboxes  = m_registry.get_components<Hitbox>();
        std::vector<ecs::entity_t> toKill;
        std::size_t count = positions.size();
        for (std::size_t idx = 0; idx < count; ++idx) {
            ecs::entity_t ent{idx};
            auto &posOpt = positions[ent];
            if (!posOpt) {
                continue;
            }
            float left, right, top, bottom;
            auto &hbOpt = hitboxes[ent];
            if (hbOpt) {
                left   = posOpt->x + hbOpt->offsetX - hbOpt->halfWidth;
                right  = posOpt->x + hbOpt->offsetX + hbOpt->halfWidth;
                top    = posOpt->y + hbOpt->offsetY - hbOpt->halfHeight;
                bottom = posOpt->y + hbOpt->offsetY + hbOpt->halfHeight;
            } else {
                // Considère l’entité comme un point
                left = right = posOpt->x;
                top  = bottom = posOpt->y;
            }
            if (left < bounds.minX || right > bounds.maxX || top < bounds.minY || bottom > bounds.maxY) {
                toKill.push_back(ent);
            }
        }
        for (auto ent : toKill) {
            m_registry.kill_entity(ent);
        }
    }
};

} // namespace engine