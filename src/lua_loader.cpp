#include "lua_loader.hpp"

#include <stdexcept>
#include <iostream>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace ecs {

// Forward declarations
static MovementPattern readMovementPattern(lua_State* L, int index);
static void readProjectiles(lua_State* L, int index, GameConfig& cfg);
static void readWeapons(lua_State* L, int index, GameConfig& cfg);
static void readArchetypes(lua_State* L, int index, GameConfig& cfg);

// ------------------------------------------------------------
// MAIN ENTRY POINT
// ------------------------------------------------------------
GameConfig loadGameConfig(const std::string& filename) {
    lua_State* L = luaL_newstate();
    if (!L)
        throw std::runtime_error("Failed to create Lua state");

    luaL_openlibs(L);

    // Load file
    if (luaL_loadfile(L, filename.c_str()) != LUA_OK) {
        std::string err = lua_tostring(L, -1);
        lua_close(L);
        throw std::runtime_error("Failed to load Lua file: " + err);
    }

    // Execute
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

    // ------------------ projectiles ---------------------
    lua_getfield(L, -1, "projectiles");
    if (lua_istable(L, -1))
        readProjectiles(L, lua_gettop(L), cfg);
    lua_pop(L, 1);

    // ------------------ weapons -------------------------
    lua_getfield(L, -1, "weapons");
    if (lua_istable(L, -1))
        readWeapons(L, lua_gettop(L), cfg);
    lua_pop(L, 1);

    // ------------------ archetypes ----------------------
    lua_getfield(L, -1, "archetypes");
    if (lua_istable(L, -1))
        readArchetypes(L, lua_gettop(L), cfg);
    lua_pop(L, 1);

    lua_pop(L, 1); // pop root table
    lua_close(L);

    return cfg;
}

// ------------------------------------------------------------
// READ MOVEMENT PATTERN
// pattern = { {dx1, dy1}, {dx2, dy2}, ... }
// ------------------------------------------------------------
static MovementPattern readMovementPattern(lua_State* L, int index) {
    MovementPattern pattern;

    if (!lua_istable(L, index))
        return pattern;

    index = lua_absindex(L, index);
    lua_Unsigned len = lua_rawlen(L, index);

    for (lua_Unsigned i = 1; i <= len; ++i) {
        lua_rawgeti(L, index, i); // push pattern[i]
        if (lua_istable(L, -1)) {
            lua_rawgeti(L, -1, 1); // x
            float x = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
            lua_pop(L, 1);

            lua_rawgeti(L, -1, 2); // y
            float y = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
            lua_pop(L, 1);

            pattern.offsets.emplace_back(x, y);
        }
        lua_pop(L, 1); // pop pattern[i]
    }

    return pattern;
}

// ------------------------------------------------------------
// PROJECTILES
// projectiles = {
//   Arrow = {
//     Collision = true,
//     Damage    = true,
//     Size      = { width = 5, height = 2 },
//   },
// }
// ------------------------------------------------------------
static void readProjectiles(lua_State* L, int index, GameConfig& cfg) {
    if (!lua_istable(L, index))
        return;

    index = lua_absindex(L, index);

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const char* name = lua_tostring(L, -2);
        if (!name) {
            lua_pop(L, 1);
            continue;
        }

        ProjectileDef def;

        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "Collision");
            def.collision = (lua_toboolean(L, -1) != 0);
            lua_pop(L, 1);

            lua_getfield(L, -1, "Damage");
            def.damage = (lua_toboolean(L, -1) != 0);
            lua_pop(L, 1);

            lua_getfield(L, -1, "Size");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "width");
                if (lua_isnumber(L, -1))
                    def.size.width = static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);

                lua_getfield(L, -1, "height");
                if (lua_isnumber(L, -1))
                    def.size.height = static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }

        cfg.projectiles[name] = def;

        lua_pop(L, 1); // pop value
    }
}

// ------------------------------------------------------------
// WEAPONS
// weapons = {
//   Arc = {
//     rate      = 1.0,
//     speed     = 1.0,
//     lifetime  = 20.0,
//     damage    = 10,
//     projectile= "Arrow",
//     pattern   = { {0,0}, {1,1}, {2,0} },
//   },
// }
// ------------------------------------------------------------
static void readWeapons(lua_State* L, int index, GameConfig& cfg) {
    if (!lua_istable(L, index))
        return;

    index = lua_absindex(L, index);

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const char* name = lua_tostring(L, -2);
        if (!name) {
            lua_pop(L, 1);
            continue;
        }

        WeaponDef def;
        def.name = name;

        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "rate");
            if (lua_isnumber(L, -1))
                def.rate = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "speed");
            if (lua_isnumber(L, -1))
                def.speed = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "lifetime");
            if (lua_isnumber(L, -1))
                def.lifetime = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "damage");
            if (lua_isnumber(L, -1))
                def.damage = static_cast<int>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "projectile");
            if (lua_isstring(L, -1)) {
                // On stocke seulement le nom ; le mapping vers l’index
                // se fait dans l’ECS (InternalConfig).
                def.projectile = lua_tostring(L, -1);

                // Optionnel : warning si le projectile n’existe pas dans cfg
                if (cfg.projectiles.find(def.projectile) == cfg.projectiles.end()) {
                    std::cerr << "[LuaLoader] Warning: weapon '" << name
                              << "' references unknown projectile '" << def.projectile << "'\n";
                }
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, "pattern");
            if (lua_istable(L, -1))
                def.pattern = readMovementPattern(L, lua_gettop(L));
            lua_pop(L, 1);
        }

        cfg.weapons[name] = def;

        lua_pop(L, 1); // pop value
    }
}

// ------------------------------------------------------------
// ARCHETYPES
// archetypes = {
//   player = {
//     respawnable = true,
//     Health      = 100,
//     Collision   = true,
//     speed       = 0.0,
//     lookDirection = { x = 1.0, y = 0.0 },
//     target      = { "clown", "archer" },
//     range       = 2000.0,
//     Weapon      = "Arc",
//     pattern     = { ... },
//   },
// }
// ------------------------------------------------------------
static void readArchetypes(lua_State* L, int index, GameConfig& cfg) {
    if (!lua_istable(L, index))
        return;

    index = lua_absindex(L, index);

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {

        const char* name = lua_tostring(L, -2);
        if (!name) {
            lua_pop(L, 1);
            continue;
        }

        Archetype def;
        def.name = name;

        if (lua_istable(L, -1)) {

            lua_getfield(L, -1, "respawnable");
            def.respawnable = (lua_toboolean(L, -1) != 0);
            lua_pop(L, 1);

            lua_getfield(L, -1, "Health");
            if (lua_isnumber(L, -1))
                def.Health = static_cast<int>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "Collision");
            def.Collision = (lua_toboolean(L, -1) != 0);
            lua_pop(L, 1);

            lua_getfield(L, -1, "speed");
            if (lua_isnumber(L, -1))
                def.speed = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "lookDirection");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "x");
                if (lua_isnumber(L, -1))
                    def.lookDirection.x = static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);

                lua_getfield(L, -1, "y");
                if (lua_isnumber(L, -1))
                    def.lookDirection.y = static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
            lua_pop(L, 1);

            // Multi-target : la bonne pratique côté Lua est :
            // target = { "clown", "player" } et PAS "clown, player" dans une seule string
            lua_getfield(L, -1, "target");
            if (lua_istable(L, -1)) {
                lua_Unsigned len = lua_rawlen(L, -1);
                for (lua_Unsigned i = 1; i <= len; ++i) {
                    lua_rawgeti(L, -1, i);
                    if (lua_isstring(L, -1)) {
                        def.target.emplace_back(lua_tostring(L, -1));
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, "range");
            if (lua_isnumber(L, -1))
                def.range = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "Weapon");
            if (lua_isstring(L, -1)) {
                def.Weapon = lua_tostring(L, -1);

                // Optionnel : warning si l’arme n’existe pas dans cfg
                if (cfg.weapons.find(def.Weapon) == cfg.weapons.end()) {
                    std::cerr << "[LuaLoader] Warning: archetype '" << name
                              << "' references unknown weapon '" << def.Weapon << "'\n";
                }
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, "pattern");
            if (lua_istable(L, -1))
                def.pattern = readMovementPattern(L, lua_gettop(L));
            lua_pop(L, 1);
        }

        cfg.archetypes[name] = def;

        lua_pop(L, 1); // pop value
    }
}

} // namespace ecs
