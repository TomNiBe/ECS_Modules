#include "ecs_world.hpp"
#include "lua_loader.hpp"

#include <ncurses.h>
#include <chrono>
#include <thread>
#include <string>
#include <optional>
#include <cmath>

using namespace ecs;

int main(int argc, char** argv)
{
    std::string configFile = (argc > 1)
        ? argv[1]
        : std::string("../config/example_config.lua");

    try {
        // Charger la config Lua
        GameConfig config = loadGameConfig(configFile);

        // --- Création du monde ECS ---
        World world(config);

        // --- Spawns de base ---
        // Player au centre (0,0)
        Command spawnPlayer{};
        spawnPlayer.type      = CommandType::SpawnEntity;
        spawnPlayer.archetype = "player";
        spawnPlayer.x         = 0.f;
        spawnPlayer.y         = 0.f;
        world.enqueueCommand(spawnPlayer);

        // Archer à droite
        Command spawnArcher{};
        spawnArcher.type      = CommandType::SpawnEntity;
        spawnArcher.archetype = "archer";
        spawnArcher.x         = 10.f;
        spawnArcher.y         = 0.f;
        world.enqueueCommand(spawnArcher);

        // Clown à gauche
        Command spawnClown{};
        spawnClown.type       = CommandType::SpawnEntity;
        spawnClown.archetype  = "clown";
        spawnClown.x          = -10.f;
        spawnClown.y          = 0.f;
        world.enqueueCommand(spawnClown);

        // Première update pour consommer les spawns
        world.update(0.0f);

        // Récupérer les Entity via les events
        Entity player{};
        Entity archer{};
        Entity clown{};
        bool playerKnown = false;
        bool archerKnown = false;
        bool clownKnown  = false;

        while (true) {
            std::optional<Event> evOpt = world.pollEvent();
            if (!evOpt)
                break;
            const Event& ev = *evOpt;
            if (ev.type == EventType::EntitySpawned) {
                if (!playerKnown) {
                    player = ev.entity;
                    playerKnown = true;
                } else if (!archerKnown) {
                    archer = ev.entity;
                    archerKnown = true;
                } else if (!clownKnown) {
                    clown = ev.entity;
                    clownKnown = true;
                }
            }
        }

        if (!playerKnown) {
            fprintf(stderr, "ERREUR: Player non spawne.\n");
            return 1;
        }

        // Donner (au cas où) une arme au player si besoin
        if (!world.hasWeapon(player)) {
            world.setEntityWeapon(player, "Arc"); // si existe dans le Lua
        }

        // =======================
        //        NCURSES
        // =======================
        initscr();
        cbreak();
        noecho();
        curs_set(0);
        keypad(stdscr, TRUE);
        nodelay(stdscr, TRUE); // getch() non bloquant

        // --- Quelques paramètres d'affichage ---
        int screenW, screenH;
        getmaxyx(stdscr, screenH, screenW);
        int originX = screenW / 2;
        int originY = screenH / 2;

        // Vitesse d'update (ex: ~30 FPS)
        const float dt = 0.1f;
        const auto frameDuration = std::chrono::milliseconds(100);

        bool running = true;

        // direction courante du player (persistante)
        float moveX = 0.f;
        float moveY = 0.f;
        bool firePressed = false; // tir du player

        while (running) {
            // -------- INPUT --------
            int ch = getch();
            while (ch != ERR) {
                if (ch == 'x') {
                    running = false;
                }
                // Mapping AZERTY que tu as demandé :
                // e = haut, s = gauche, d = bas, f = droite
                else if (ch == 'e') { // haut
                    moveX = 0.f;
                    moveY = -1.f;
                }
                else if (ch == 'd') { // bas
                    moveX = 0.f;
                    moveY = 1.f;
                }
                else if (ch == 's') { // gauche
                    moveX = -1.f;
                    moveY = 0.f;
                }
                else if (ch == 'f') { // droite
                    moveX = 1.f;
                    moveY = 0.f;
                }
                else if (ch == 'c') { // stop
                    moveX = 0.f;
                    moveY = 0.f;
                }
                else if (ch == ' ') { // espace: toggle fire
                    firePressed = !firePressed;
                }

                ch = getch();
            }

            // Envoi systématique des inputs au player à chaque frame
            if (world.isAlive(player)) {
                Command moveCmd{};
                moveCmd.type   = CommandType::SetMoveInput;
                moveCmd.target = player;
                moveCmd.moveX  = moveX;
                moveCmd.moveY  = moveY;
                world.enqueueCommand(moveCmd);

                Command fireCmd{};
                fireCmd.type        = CommandType::SetFireInput;
                fireCmd.target      = player;
                fireCmd.firePressed = firePressed;
                world.enqueueCommand(fireCmd);
            }

            // -------- UPDATE ECS --------
            world.update(dt);

            // on consomme les events juste pour l'info (on pourrait log dans un fichier)
            while (true) {
                std::optional<Event> evOpt = world.pollEvent();
                if (!evOpt)
                    break;
                const Event& ev = *evOpt;
                // tu peux ajouter des logs si tu veux debug
                (void)ev;
            }

            // -------- RENDER --------
            erase();

            // Récupérer la position du player pour le centrer éventuellement
            float centerX = 0.f;
            float centerY = 0.f;
            if (world.isAlive(player)) {
                auto pPosOpt = world.getPosition(player);
                if (pPosOpt) {
                    centerX = pPosOpt->first;
                    centerY = pPosOpt->second;
                }
            }

            // Affichage de la grille : on scanne toutes les entités
            for (std::uint32_t id = 0; id < 512; ++id) { // limite soft pour la démo
                Entity e;
                e.id = id;
                // on ne sait pas la génération exacte, on tente toutes les gen possibles ?
                // plus simple : on check via isAlive avec gen=impl.gen[id], mais on n'y a pas accès ici.
                // Donc pour la démo, on va juste récupérer la position via getPosition avec un handle "bricolé":
                // -> ce n'est pas parfait, mais suffisant pour visualiser.
                // En pratique tu aurais une map d'Entity que tu veux dessiner.

                // On va plutôt faire simple : on ne dessine que
                // - player
                // - archer
                // - clown
                // - projectiles (entities dont isAlive && !hasArchetype && isProjectile)
            }

            // Player
            if (world.isAlive(player)) {
                auto posOpt = world.getPosition(player);
                if (posOpt) {
                    float wx = posOpt->first - centerX;
                    float wy = posOpt->second - centerY;
                    int sx = originX + static_cast<int>(std::round(wx));
                    int sy = originY - static_cast<int>(std::round(wy));
                    if (sy >= 0 && sy < screenH && sx >= 0 && sx < screenW) {
                        mvaddch(sy, sx, '@'); // player
                    }
                }
            }

            // Archer
            if (archerKnown && world.isAlive(archer)) {
                auto posOpt = world.getPosition(archer);
                if (posOpt) {
                    float wx = posOpt->first - centerX;
                    float wy = posOpt->second - centerY;
                    int sx = originX + static_cast<int>(std::round(wx));
                    int sy = originY - static_cast<int>(std::round(wy));
                    if (sy >= 0 && sy < screenH && sx >= 0 && sx < screenW) {
                        mvaddch(sy, sx, 'A');
                    }
                }
            }

            // Clown
            if (clownKnown && world.isAlive(clown)) {
                auto posOpt = world.getPosition(clown);
                if (posOpt) {
                    float wx = posOpt->first - centerX;
                    float wy = posOpt->second - centerY;
                    int sx = originX + static_cast<int>(std::round(wx));
                    int sy = originY - static_cast<int>(std::round(wy));
                    if (sy >= 0 && sy < screenH && sx >= 0 && sx < screenW) {
                        mvaddch(sy, sx, 'C');
                    }
                }
            }

            // Projectiles : on les affiche avec '*'
            // (hack simple : on balaie un certain nombre d'ids et on se base sur getPosition)
            for (std::uint32_t rawId = 0; rawId < 512; ++rawId) {
                Entity e;
                e.id        = rawId;
                e.generation = 0; // ce n'est pas parfait, mais on ne peut pas connaitre la gen ici.
                // On triche: si getPosition renvoie quelque chose mais que ce n'est PAS
                // player/archer/clown, on l'affiche comme projectile.
                if (!world.isAlive(e))
                    continue;

                // Filtrer ceux qu'on connaît déjà
                if (e.id == player.id || e.id == archer.id || e.id == clown.id)
                    continue;

                auto posOpt = world.getPosition(e);
                if (!posOpt)
                    continue;

                float wx = posOpt->first - centerX;
                float wy = posOpt->second - centerY;
                int sx = originX + static_cast<int>(std::round(wx));
                int sy = originY - static_cast<int>(std::round(wy));
                if (sy >= 0 && sy < screenH && sx >= 0 && sx < screenW) {
                    mvaddch(sy, sx, '*'); // projectile
                }
            }

            // HUD simple
            mvprintw(0, 0, "ECS NCURSES DEMO  |  e=haut, s=gauche, d=bas, f=droite, c=stop, SPACE=fire(toggle), x=quit");

            refresh();

            std::this_thread::sleep_for(frameDuration);
        }

        endwin();
        return 0;

    } catch (const std::exception& ex) {
        endwin();
        fprintf(stderr, "Error: %s\n", ex.what());
        return 1;
    }
}
