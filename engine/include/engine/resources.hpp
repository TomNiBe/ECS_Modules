//===-------------------------------------------------------------------------------===
// resources.hpp
//
// Ce fichier définit les structures de données pour décrire les objets, armes
// et projectiles de manière pilotée par données. Il fournit aussi la fonction
// loadGameConfig() qui lit un script Lua et remplit une structure GameConfig.
// Le script Lua doit retourner des tables 'archetypes', 'weapons' et 'projectiles'.
// Aucune constante de gameplay n’est codée en dur : toutes les valeurs proviennent de la configuration.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <stdexcept>
#include <iostream>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace engine {

// -----------------------------------------------------------------------------
// Structures de base utilisées dans la configuration
// -----------------------------------------------------------------------------

// Vecteur 2D simple utilisé pour les positions et directions.
struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

// Définition d’un motif de déplacement : une liste d’offsets appliqués au fil du temps.
// Les offsets sont utilisés cycliquement.
struct MovementPattern {
    std::vector<std::pair<float, float>> offsets;
};

// Définition d’une hitbox en unités monde. Les dimensions sont converties en demi‑extensions lors de la création.
struct HitboxDef {
    float width  = 1.f;
    float height = 1.f;
    float offsetX = 0.f;
    float offsetY = 0.f;
};

// Définition d’un projectile : collision, dégâts et dimensions physiques.
struct ProjectileDef {
    bool collision = false;
    bool damage    = false;
    float width    = 1.f;
    float height   = 1.f;
};

// Définition d’une arme : cadence de tir, vitesse du projectile, durée de vie,
// dégâts infligés, nom de projectile associé et motif de déplacement facultatif.
struct WeaponDef {
    std::string name;
    float       rate     = 0.f;
    float       speed    = 0.f;
    float       lifetime = 0.f;
    int         damage   = 0;
    std::string projectileName;
    MovementPattern pattern;
    int         piercingHits = 0;
    // Spécification de charge : décrit l’effet de maintenir le tir sur le coup.
    struct ChargeLevel {
        float damageMul = 1.f;
        float speedMul  = 1.f;
        float sizeMul   = 1.f;
        int   piercingHits = 0;
    };
    struct ChargeSpec {
        float maxTime = 0.f;
        std::vector<float> thresholds;
        std::vector<ChargeLevel> levels;
    } charge;
};

// Définition d’un archétype : modèle pour générer des entités avec options (réapparition,
// santé initiale, collision, vitesse, direction par défaut, cibles, portée,
// arme et motif de mouvement).
struct Archetype {
    std::string name;
    bool        respawnable  = false;
    int         health       = 0;
    bool        collision    = false;
    HitboxDef   hitbox;
    float       speed        = 0.f;
    Vec2        lookDirection{};
    std::vector<std::string> target;
    float       range        = 0.f;
    std::string weaponName;
    MovementPattern pattern;
    int         faction      = 0;
    std::uint32_t colliderLayer = 0;
    std::uint32_t colliderMask  = 0;
    bool        colliderSolid  = false;
    bool        colliderTrigger = true;
    bool        colliderStatic  = false;

    // --- Extensions de ciblage ---
    // Les champs targetOrder et targetMode permettent de définir un ordre de priorité
    // et un mode de sélection par catégorie pour les cibles. S’ils ne sont pas renseignés,
    // la cible la plus proche est choisie.
    std::vector<std::string> targetOrder;
    std::unordered_map<std::string, std::string> targetMode;

    // --- Extensions épines ---
    // Active des dégâts de contact ; thornsDamage indique la valeur infligée.
    bool        thornsEnabled  = false;
    int         thornsDamage   = 0;
};

// Configuration de jeu agrégée depuis Lua : définitions des projectiles, des armes et des archétypes indexés par nom.
struct GameConfig {
    std::unordered_map<std::string, ProjectileDef> projectiles;
    std::unordered_map<std::string, WeaponDef>     weapons;
    std::unordered_map<std::string, Archetype>     archetypes;

    // ---------------------------------------------------------------------
    // Limites optionnelles du monde et de la zone jouable
    //
    // worldBounds définit les bornes de simulation ; les entités sortant de cette boîte sont supprimées.
    // playableBounds restreint le déplacement du joueur ; sa position désirée est clampée dans cette zone.
    // Ces limites sont désactivées par défaut et activées uniquement si définies dans la configuration.
    struct Bounds {
        float minX = 0.f;
        float minY = 0.f;
        float maxX = 0.f;
        float maxY = 0.f;
        bool enabled = false;
    };

    Bounds worldBounds;
    Bounds playableBounds;
};

// -----------------------------------------------------------------------------
// Fonctions utilitaires pour lire les tables Lua dans des structures C++
//
// Ces fonctions parcourent les tables Lua et remplissent les structures correspondantes.
// Elles opèrent sur un lua_State* fourni par loadGameConfig() et ne doivent pas être appelées directement.
// -----------------------------------------------------------------------------

namespace detail {

// Lit un motif de mouvement depuis une table Lua.
inline MovementPattern readMovementPattern(lua_State* L, int index) {
    MovementPattern pattern;
    if (!lua_istable(L, index)) {
        return pattern;
    }
    index = lua_absindex(L, index);
    lua_Unsigned len = lua_rawlen(L, index);
    for (lua_Unsigned i = 1; i <= len; ++i) {
        lua_rawgeti(L, index, i);
        if (lua_istable(L, -1)) {
            lua_rawgeti(L, -1, 1);
            float x = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
            lua_pop(L, 1);
            lua_rawgeti(L, -1, 2);
            float y = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
            lua_pop(L, 1);
            pattern.offsets.emplace_back(x, y);
        }
        lua_pop(L, 1);
    }
    return pattern;
}

// Lit les définitions de projectiles dans cfg.projectiles
inline void readProjectiles(lua_State* L, int index, GameConfig& cfg) {
    if (!lua_istable(L, index)) {
        return;
    }
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const char* name = lua_tostring(L, -2);
        if (name) {
            ProjectileDef def;
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "Collision");
                def.collision = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
                lua_getfield(L, -1, "Damage");
                def.damage = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
                lua_getfield(L, -1, "Size");
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "width");
                    if (lua_isnumber(L, -1)) {
                        def.width = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "height");
                    if (lua_isnumber(L, -1)) {
                        def.height = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
            cfg.projectiles[name] = def;
        }
        lua_pop(L, 1);
    }
}

// Lit les définitions d’armes dans cfg.weapons
inline void readWeapons(lua_State* L, int index, GameConfig& cfg) {
    if (!lua_istable(L, index)) {
        return;
    }
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const char* name = lua_tostring(L, -2);
        if (name) {
            WeaponDef def;
            def.name = name;
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "rate");
                if (lua_isnumber(L, -1)) {
                    def.rate = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "speed");
                if (lua_isnumber(L, -1)) {
                    def.speed = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "lifetime");
                if (lua_isnumber(L, -1)) {
                    def.lifetime = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "damage");
                if (lua_isnumber(L, -1)) {
                    def.damage = static_cast<int>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "projectile");
                if (lua_isstring(L, -1)) {
                    def.projectileName = lua_tostring(L, -1);
                    if (cfg.projectiles.find(def.projectileName) == cfg.projectiles.end()) {
                        std::cerr << "[LuaLoader] Warning: weapon '" << name
                                  << "' references unknown projectile '"
                                  << def.projectileName << "'\n";
                    }
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "pattern");
                if (lua_istable(L, -1)) {
                    def.pattern = readMovementPattern(L, lua_gettop(L));
                }
                lua_pop(L, 1);
                // piercingHits : nombre de cibles que le projectile peut transpercer
                lua_getfield(L, -1, "piercingHits");
                if (lua_isnumber(L, -1)) {
                    def.piercingHits = static_cast<int>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                // Spécification de charge
                lua_getfield(L, -1, "charge");
                if (lua_istable(L, -1)) {
                    // Temps de charge maximal
                    lua_getfield(L, -1, "maxTime");
                    if (lua_isnumber(L, -1)) {
                        def.charge.maxTime = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                    // Tableau des seuils
                    lua_getfield(L, -1, "thresholds");
                    if (lua_istable(L, -1)) {
                        lua_Unsigned n = lua_rawlen(L, -1);
                        def.charge.thresholds.resize(static_cast<std::size_t>(n));
                        for (lua_Unsigned i = 1; i <= n; ++i) {
                            lua_rawgeti(L, -1, i);
                            if (lua_isnumber(L, -1)) {
                                def.charge.thresholds[static_cast<std::size_t>(i - 1)] = static_cast<float>(lua_tonumber(L, -1));
                            }
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);
                    // Tableau des niveaux
                    lua_getfield(L, -1, "levels");
                    if (lua_istable(L, -1)) {
                        lua_Unsigned n2 = lua_rawlen(L, -1);
                        def.charge.levels.resize(static_cast<std::size_t>(n2));
                        for (lua_Unsigned i = 1; i <= n2; ++i) {
                            lua_rawgeti(L, -1, i);
                            if (lua_istable(L, -1)) {
                                WeaponDef::ChargeLevel level;
                                lua_getfield(L, -1, "damageMul");
                                if (lua_isnumber(L, -1)) {
                                    level.damageMul = static_cast<float>(lua_tonumber(L, -1));
                                }
                                lua_pop(L, 1);
                                lua_getfield(L, -1, "speedMul");
                                if (lua_isnumber(L, -1)) {
                                    level.speedMul = static_cast<float>(lua_tonumber(L, -1));
                                }
                                lua_pop(L, 1);
                                lua_getfield(L, -1, "sizeMul");
                                if (lua_isnumber(L, -1)) {
                                    level.sizeMul = static_cast<float>(lua_tonumber(L, -1));
                                }
                                lua_pop(L, 1);
                                lua_getfield(L, -1, "piercingHits");
                                if (lua_isnumber(L, -1)) {
                                    level.piercingHits = static_cast<int>(lua_tonumber(L, -1));
                                }
                                lua_pop(L, 1);
                                def.charge.levels[static_cast<std::size_t>(i - 1)] = level;
                            }
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
            cfg.weapons[def.name] = def;
        }
        lua_pop(L, 1);
    }
}

// Lit les définitions d’archétypes dans cfg.archetypes
inline void readArchetypes(lua_State* L, int index, GameConfig& cfg) {
    if (!lua_istable(L, index)) {
        return;
    }
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const char* name = lua_tostring(L, -2);
        if (name) {
            Archetype def;
            def.name = name;
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "respawnable");
                def.respawnable = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
                lua_getfield(L, -1, "Health");
                if (lua_isnumber(L, -1)) {
                    def.health = static_cast<int>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "Collision");
                def.collision = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
                lua_getfield(L, -1, "hitbox");
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "width");
                    if (lua_isnumber(L, -1)) {
                        def.hitbox.width = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "height");
                    if (lua_isnumber(L, -1)) {
                        def.hitbox.height = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "offsetX");
                    if (lua_isnumber(L, -1)) {
                        def.hitbox.offsetX = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "offsetY");
                    if (lua_isnumber(L, -1)) {
                        def.hitbox.offsetY = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "speed");
                if (lua_isnumber(L, -1)) {
                    def.speed = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "lookDirection");
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "x");
                    if (lua_isnumber(L, -1)) {
                        def.lookDirection.x = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "y");
                    if (lua_isnumber(L, -1)) {
                        def.lookDirection.y = static_cast<float>(lua_tonumber(L, -1));
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
                // Analyse le champ 'target' : tableau d’ordre et de modes ou tableau simple.
                lua_getfield(L, -1, "target");
                if (lua_istable(L, -1)) {
                    // Vérifie la présence du champ 'order'
                    bool hasOrderField = false;
                    lua_getfield(L, -1, "order");
                    if (lua_istable(L, -1)) {
                        hasOrderField = true;
                    }
                    lua_pop(L, 1);
                    if (hasOrderField) {
                        // Lit le tableau d’ordre
                        lua_getfield(L, -1, "order");
                        lua_Unsigned len = lua_rawlen(L, -1);
                        for (lua_Unsigned i = 1; i <= len; ++i) {
                            lua_rawgeti(L, -1, i);
                            if (lua_isstring(L, -1)) {
                                def.targetOrder.emplace_back(lua_tostring(L, -1));
                            }
                            lua_pop(L, 1);
                        }
                        lua_pop(L, 1);
                        // Lit la table des modes si elle existe
                        lua_getfield(L, -1, "mode");
                        if (lua_istable(L, -1)) {
                            lua_pushnil(L);
                            while (lua_next(L, -2) != 0) {
                                // clé à -2, valeur à -1
                                if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                                    std::string key = lua_tostring(L, -2);
                                    std::string val = lua_tostring(L, -1);
                                    def.targetMode[key] = val;
                                }
                                lua_pop(L, 1);
                            }
                        }
                        lua_pop(L, 1);
                    } else {
                        // Format hérité : interprète la table comme un tableau de chaînes
                        lua_Unsigned len = lua_rawlen(L, -1);
                        for (lua_Unsigned i = 1; i <= len; ++i) {
                            lua_rawgeti(L, -1, i);
                            if (lua_isstring(L, -1)) {
                                std::string nm = lua_tostring(L, -1);
                                def.targetOrder.emplace_back(nm);
                            }
                            lua_pop(L, 1);
                        }
                    }
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "range");
                if (lua_isnumber(L, -1)) {
                    def.range = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "Weapon");
                if (lua_isstring(L, -1)) {
                    def.weaponName = lua_tostring(L, -1);
                    if (cfg.weapons.find(def.weaponName) == cfg.weapons.end()) {
                        std::cerr << "[LuaLoader] Warning: archetype '" << name
                                  << "' references unknown weapon '" << def.weaponName
                                  << "'\n";
                    }
                }
                lua_pop(L, 1);
                lua_getfield(L, -1, "pattern");
                if (lua_istable(L, -1)) {
                    def.pattern = readMovementPattern(L, lua_gettop(L));
                }
                lua_pop(L, 1);
                // Identifiant de faction
                lua_getfield(L, -1, "faction");
                if (lua_isnumber(L, -1)) {
                    def.faction = static_cast<int>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                // Couche du collider
                lua_getfield(L, -1, "colliderLayer");
                if (lua_isnumber(L, -1)) {
                    def.colliderLayer = static_cast<std::uint32_t>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                // Masque du collider
                lua_getfield(L, -1, "colliderMask");
                if (lua_isnumber(L, -1)) {
                    def.colliderMask = static_cast<std::uint32_t>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                // Collider solide
                lua_getfield(L, -1, "colliderSolid");
                if (lua_isboolean(L, -1)) {
                    def.colliderSolid = lua_toboolean(L, -1) != 0;
                }
                lua_pop(L, 1);
                // Collider déclencheur
                lua_getfield(L, -1, "colliderTrigger");
                if (lua_isboolean(L, -1)) {
                    def.colliderTrigger = lua_toboolean(L, -1) != 0;
                }
                lua_pop(L, 1);
                // Collider statique
                lua_getfield(L, -1, "colliderStatic");
                if (lua_isboolean(L, -1)) {
                    def.colliderStatic = lua_toboolean(L, -1) != 0;
                }
                lua_pop(L, 1);

                // thorns : active les dégâts de contact si présent et vrai (sinon faux)
                lua_getfield(L, -1, "thorns");
                if (lua_isboolean(L, -1)) {
                    def.thornsEnabled = lua_toboolean(L, -1) != 0;
                }
                lua_pop(L, 1);
                // thornsDamage : dégâts infligés au contact (0 pour désactiver)
                lua_getfield(L, -1, "thornsDamage");
                if (lua_isnumber(L, -1)) {
                    def.thornsDamage = static_cast<int>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
            }
            cfg.archetypes[def.name] = def;
        }
        lua_pop(L, 1);
    }
}

} // namespace detail

// -----------------------------------------------------------------------------
// loadGameConfig
//
// Charge une configuration depuis un fichier Lua. Le script doit retourner une table
// contenant les sous‑tables 'projectiles', 'weapons' et 'archetypes'.
// Lève std::runtime_error en cas d’erreur. Utilise directement l’API C de Lua.
// -----------------------------------------------------------------------------
inline GameConfig loadGameConfig(const std::string& filename) {
    lua_State* L = luaL_newstate();
    if (!L) {
        throw std::runtime_error("Failed to create Lua state");
    }
    luaL_openlibs(L);
    if (luaL_loadfile(L, filename.c_str()) != LUA_OK) {
        std::string err = lua_tostring(L, -1);
        lua_close(L);
        throw std::runtime_error("Failed to load Lua file: " + err);
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        std::string err = lua_tostring(L, -1);
        lua_close(L);
        throw std::runtime_error("Lua error: " + err);
    }
    if (!lua_istable(L, -1)) {
        lua_close(L);
        throw std::runtime_error("Lua script must return a table");
    }
    GameConfig cfg;
    // projectiles
    lua_getfield(L, -1, "projectiles");
    if (lua_istable(L, -1)) {
        detail::readProjectiles(L, lua_gettop(L), cfg);
    }
    lua_pop(L, 1);
    // armes
    lua_getfield(L, -1, "weapons");
    if (lua_istable(L, -1)) {
        detail::readWeapons(L, lua_gettop(L), cfg);
    }
    lua_pop(L, 1);
    // archétypes
    lua_getfield(L, -1, "archetypes");
    if (lua_istable(L, -1)) {
        detail::readArchetypes(L, lua_gettop(L), cfg);
    }
    lua_pop(L, 1);
    // header (facultatif) : peut contenir worldBounds et playableBounds ; sinon les limites restent désactivées.
    lua_getfield(L, -1, "header");
    if (lua_istable(L, -1)) {
        // worldBounds : { minX, minY, maxX, maxY }
        lua_getfield(L, -1, "worldBounds");
        if (lua_istable(L, -1)) {
            bool ok = true;
            // Lit chaque composant et vérifie que les quatre valeurs sont présentes
            lua_getfield(L, -1, "minX");
            if (lua_isnumber(L, -1)) cfg.worldBounds.minX = static_cast<float>(lua_tonumber(L, -1)); else ok = false;
            lua_pop(L, 1);
            lua_getfield(L, -1, "minY");
            if (lua_isnumber(L, -1)) cfg.worldBounds.minY = static_cast<float>(lua_tonumber(L, -1)); else ok = false;
            lua_pop(L, 1);
            lua_getfield(L, -1, "maxX");
            if (lua_isnumber(L, -1)) cfg.worldBounds.maxX = static_cast<float>(lua_tonumber(L, -1)); else ok = false;
            lua_pop(L, 1);
            lua_getfield(L, -1, "maxY");
            if (lua_isnumber(L, -1)) cfg.worldBounds.maxY = static_cast<float>(lua_tonumber(L, -1)); else ok = false;
            lua_pop(L, 1);
            cfg.worldBounds.enabled = ok;
        }
        lua_pop(L, 1);
        // playableBounds : { minX, minY, maxX, maxY }
        lua_getfield(L, -1, "playableBounds");
        if (lua_istable(L, -1)) {
            bool ok = true;
            lua_getfield(L, -1, "minX");
            if (lua_isnumber(L, -1)) cfg.playableBounds.minX = static_cast<float>(lua_tonumber(L, -1)); else ok = false;
            lua_pop(L, 1);
            lua_getfield(L, -1, "minY");
            if (lua_isnumber(L, -1)) cfg.playableBounds.minY = static_cast<float>(lua_tonumber(L, -1)); else ok = false;
            lua_pop(L, 1);
            lua_getfield(L, -1, "maxX");
            if (lua_isnumber(L, -1)) cfg.playableBounds.maxX = static_cast<float>(lua_tonumber(L, -1)); else ok = false;
            lua_pop(L, 1);
            lua_getfield(L, -1, "maxY");
            if (lua_isnumber(L, -1)) cfg.playableBounds.maxY = static_cast<float>(lua_tonumber(L, -1)); else ok = false;
            lua_pop(L, 1);
            cfg.playableBounds.enabled = ok;
        }
        lua_pop(L, 1);
    } else {
        // Si aucun header n’est fourni, les limites restent désactivées
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_close(L);
    return cfg;
}

} // namespace engine