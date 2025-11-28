#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace ecs {

// ------------------------------------------------------------
// Data structures lues depuis les fichiers Lua
// ------------------------------------------------------------

// Simple 2D size
struct Size2D {
    float width  = 0.f;
    float height = 0.f;
};

// Simple 2D vector
struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

// Pattern de mouvement : suite d’offsets (dx, dy)
struct MovementPattern {
    std::vector<std::pair<float, float>> offsets;
};

// Définitions de projectiles (pour la config)
struct ProjectileDef {
    bool collision = false;   // "Collision" dans Lua
    bool damage    = false;   // "Damage" dans Lua
    Size2D size    {};        // "Size = { width, height }"
};

// Définitions d’armes (pour la config)
struct WeaponDef {
    std::string name;         // nom de l’arme dans la table Lua
    float rate      = 0.f;    // "rate"
    float speed     = 0.f;    // "speed"
    float lifetime  = 0.f;    // "lifetime"
    int   damage    = 0;      // "damage"
    std::string projectile;   // nom du projectile ("projectile")
    MovementPattern pattern;  // "pattern" (offsets)
};

// Définitions d’archetypes (pour la config)
struct Archetype {
    std::string name;         // nom de l’archetype ("player", "archer", etc.)

    bool respawnable = false; // "respawnable"
    int  Health       = 0;    // "Health"
    bool Collision    = false;// "Collision"

    float speed       = 0.f;  // "speed"
    Vec2  lookDirection;      // "lookDirection = { x, y }"

    // Liste de types d’ennemis à cibler, dans l’ordre de priorité :
    // target = { "clown", "player" }
    std::vector<std::string> target;

    float range       = 0.f;  // "range"

    // Nom de l’arme associée à cet archetype ("Weapon")
    std::string Weapon;

    // Pattern de mouvement optionnel pour l’entité
    MovementPattern pattern;
};

// Config complète lue depuis Lua
struct GameConfig {
    std::unordered_map<std::string, ProjectileDef> projectiles;
    std::unordered_map<std::string, WeaponDef>     weapons;
    std::unordered_map<std::string, Archetype>     archetypes;
};

// ------------------------------------------------------------
// Fonction principale de chargement Lua
// ------------------------------------------------------------
GameConfig loadGameConfig(const std::string& filename);

} // namespace ecs
