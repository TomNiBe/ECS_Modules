-- example_config.lua
-- Setup :
--   - Clown au centre, regarde vers le nord (0, 1)
--   - Player à l'ouest (-10, 0)
--   - Archer à l'est (10, 0), regarde vers l'est (1, 0)
--   - Archer n'a PAS de "target" -> il est passif (tire naïvement vers l'est)
--   - Clown a target = { "player", "archer" } -> priorité player puis archer

return {
    archetypes = {
        -- PLAYER : simple cible, pas d'arme
        player = {
            respawnable   = false,
            Health        = 100,
            Collision     = true,
            speed         = 1.0,
            -- Orientation peu importe ici (il ne tire pas)
            lookDirection = { x = -1.0, y = 0.0 },
            pattern       = {
                {0.0, 0.0},
            },
        },

        -- ARCHER : placé à l'est (ex: x = +10 dans ton main)
        --          regarde vers l'est (1, 0)
        --          AUCUNE target -> passif, il tire droit devant lui vers l'est.
        archer = {
            respawnable   = false,
            Health        = 20,
            Collision     = true,
            Weapon        = "Arc",
            -- pas de champ "target" ici -> il ne cherche personne, tire naïf
            range         = 2000.0,
            speed         = 0.0,
            lookDirection = { x = 1.0, y = 0.0 }, -- vers l'est
            pattern       = {
                {0.0, 0.0},
            },
        },

        -- CLOWN : placé au centre (0, 0) dans ton main
        --         regarde vers le nord (0, 1) par défaut
        --         a deux cibles possibles : player puis archer
        clown = {
            respawnable   = false,
            Health        = 100,
            Collision     = true,
            Weapon        = "Arc",
            -- Le clown priorise d'abord le "player", ensuite "archer"
            target        = { "player", "archer" },
            range         = 2000.0,
            speed         = 0.0,
            lookDirection = { x = 0.0, y = 1.0 }, -- vers le nord
            pattern       = {
                {0.0, 0.0},
            },
        },
    },

    weapons = {
        Arc = {
            rate       = 1.0,   -- un tir par seconde
            speed      = 1.0,   -- vitesse du projectile
            lifetime   = 30.0,  -- suffisant pour traverser la scène
            damage     = 10,
            projectile = "Arrow",
            -- pas de pattern spécial -> tir en ligne droite dans lookDirection
            pattern = {
                {0.0, 0.0},
            },
        },
    },

    projectiles = {
        Arrow = {
            Collision = true,
            Damage    = true,
            Size      = { width = 5, height = 2 },
        },
    },
}
