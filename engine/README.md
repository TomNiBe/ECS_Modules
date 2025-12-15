# Bibliothèque `engine` – façade de jeu

Le module `engine` propose une façade de haut niveau construite par‑dessus l’ECS.  Il charge des définitions depuis un script Lua, crée les entités en conséquence et orchestre l’ensemble des systèmes nécessaires (mouvements, tirs, collisions, dégâts, durée de vie, respawn…).  Son but est de fournir une base de jeu générique et déterministe sans imposer de logique spécifique au projet parent.

## Rôle et positionnement

* **Façade au‑dessus de l’ECS** : l’`Engine` encapsule un `ecs::registry` et enregistre tous les types de composants utiles.  Il expose des méthodes simples pour charger la configuration, créer des entités (archétypes) et faire avancer la simulation.
* **Chargement de configuration Lua** : les constantes de jeu (archétypes, armes, projectiles) sont décrites dans un fichier Lua qui retourne une table.  Le fichier `resources.hpp` définit les structures `GameConfig`, `Archetype`, `WeaponDef` et `ProjectileDef` et fournit une fonction `loadGameConfig()` qui lit le fichier et remplit ces structures.  Aucun paramètre de gameplay n’est codé en dur dans le moteur.
* **Boucle de simulation déterministe** : l’`Engine` intègre les mouvements, gère les collisions et applique les dégâts avec un pas de temps fixe `dt` fourni par l’utilisateur (serveur ou client).  Les systèmes sont enregistrés une fois lors de la construction et s’exécutent toujours dans le même ordre.

## Chargement de configuration Lua (`GameConfig`)

La configuration de jeu est structurée autour de trois types principaux :

### Projets (`ProjectileDef`)

La structure `ProjectileDef` définit le comportement de base d’un projectile :

- **`collision`** : booléen indiquant si le projectile doit détecter des collisions avec le monde ou les entités.
- **`damage`** : booléen indiquant si le projectile inflige des dégâts.
- **`width` / `height`** : dimensions du projectile en unités de monde.

### Armes (`WeaponDef`)

Une `WeaponDef` décrit une arme pouvant être équipée par un archétype :

- **`name`** : nom symbolique de l’arme.
- **`rate`** : cadence de tir en tirs par seconde.
- **`speed`** : vitesse des projectiles émis.
- **`lifetime`** : durée de vie des projectiles en secondes.
- **`damage`** : points de vie retirés par impact.
- **`projectileName`** : nom d’une définition de projectile.
- **`pattern`** : motif de déplacement optionnel appliqué aux projectiles (structure `MovementPattern`).
- **`piercingHits`** : nombre de cibles pouvant être traversées par un projectile avant de disparaître.
- **`charge`** : spécification de charge facultative avec `maxTime`, une liste de seuils de temps et des niveaux de charge (`damageMul`, `speedMul`, `sizeMul`, `piercingHits`).  Ces paramètres permettent de moduler l’effet du projectile en fonction de la durée d’appui.

### Archétypes (`Archetype`)

Les archétypes servent de modèles pour la création d’entités.  Chaque `Archetype` comprend :

- **`name`** : identifiant de l’archétype.
- **`respawnable`** : booléen indiquant si l’entité doit réapparaître après destruction.
- **`health`** : points de vie initiaux.
- **`collision`** : booléen pour activer ou non les collisions.
- **`hitbox`** (`HitboxDef`) : dimensions et décalage de la boîte de collision.
- **`speed`** : vitesse de mouvement par défaut.
- **`lookDirection`** (`Vec2`) : direction dans laquelle l’entité est orientée au départ.
- **`target`** : liste ou structure décrivant les classes d’ennemis à attaquer.  L’ordre spécifie la priorité ; chaque catégorie peut être associée à un mode de sélection (par ex. « plus proche de la classe »).  Ces informations sont converties en un composant `TargetList`.
- **`range`** : portée d’attaque en unités.
- **`weaponName`** : nom de l’arme équipée.
- **`pattern`** : motif de déplacement optionnel appliqué à l’entité.
- **`faction`** : identifiant d’équipe ou de camp pour éviter les tirs amis.
- **`colliderLayer` / `colliderMask` / `colliderSolid` / `colliderTrigger` / `colliderStatic`** : définition du comportement de collision (voir composant `Collider`).
- **`thornsEnabled`** / **`thornsDamage`** : activer des « épines » infligeant des dégâts aux entités qui entrent en contact.

### Configuration globale (`GameConfig`)

Une instance de `GameConfig` contient trois tableaux associatifs : `projectiles`, `weapons` et `archetypes` clés par leur nom.  La fonction `loadGameConfig(const std::string &path)` lit un fichier Lua et remplit ces tableaux.  En cas d’erreur de format, une exception est levée.  Ces structures persistent durant toute la vie du moteur.

## Cycle de simulation

Le moteur fournit deux méthodes principales :

1. **`spawn(archetypeName, x, y)`** : crée une entité à partir d’un archétype, installe tous les composants et initialise sa position.  Les champs du composant sont construits à partir de la configuration (vitesse, points de vie, boîte de collision, faction, armes, motif de mouvement, épines, etc.).
2. **`update(dt)`** : exécute un pas de simulation de durée `dt` (en secondes).  Les étapes sont :
   - Mettre à jour l’horloge interne avec `dt`.
   - Exécuter tous les systèmes enregistrés via le registre (calcul de la nouvelle `DesiredPosition`, gestion des armes, IA, etc.).
   - Résoudre les collisions solides et ajuster les positions désirées.
   - Appliquer les limites jouables pour le joueur (clamp dans la zone autorisée).
   - Copier les positions désirées dans le composant `Position`.
   - Éliminer les entités qui sortent des limites du monde.
   - Gérer les collisions déclencheurs (projectiles, épines), appliquer les dégâts et supprimer les entités mortes.
   - Réduire les durées de vie (`Lifetime`) et supprimer les entités dont `remaining` est nul ou négatif.

Toutes ces opérations s’effectuent de manière déterministe, sans allocations dynamiques, et en s’appuyant sur l’ECS.  L’ordre des systèmes est déterminé lors de la construction de l’`Engine`.

## Composants fournis

Le moteur enregistre et utilise de nombreux composants par défaut.  Voici une liste succincte :

| Composant | Description |
|---|---|
| `Position` | Position en 2D (centre de l’entité). |
| `Velocity` | Vitesse en 2D (unités par seconde). |
| `Speed` | Vitesse scalaire de déplacement utilisée avec l’entrée utilisateur. |
| `LookDirection` | Direction de visée ou d’orientation. |
| `Health` | Points de vie restants. |
| `Hitbox` | Demi‑largeur/demi‑hauteur et éventuels décalages pour les collisions solides. |
| `Collider` | Couche et masque de collision, flags « solide », « déclencheur » ou « statique ». |
| `Faction` | Identifiant de camp pour gérer les attaques alliées/ennemies. |
| `PendingDamage` | Dégâts en attente d’application; remis à zéro après chaque mise à jour. |
| `Piercing` | Nombre de cibles qu’un projectile peut traverser et historique des entités déjà touchées. |
| `Thorns` | Épines infligeant des dégâts aux entités qui touchent celle‑ci. |
| `ArchetypeRef` | Pointeur vers la définition d’archétype associée pour accéder aux champs pré‑configurés. |
| `TargetList` | Liste prioritaire de cibles et modes de sélection par catégorie. |
| `Range` | Portée d’attaque pour les systèmes IA. |
| `Respawnable` | Indique si l’entité peut réapparaître après destruction. |
| `Lifetime` | Durée de vie restante d’un projectile ou d’un bonus. |
| `Damage` | Dégâts infligés par un projectile (valeur brute). |
| `MovementPatternComp` | Avancement dans un motif de déplacement (offsets, indice courant). |
| `InputState` | Entrées du joueur (axes, tir pressé/maintenu). |
| `WeaponRef` | Pointeur vers la définition d’arme équipée, cadence et charge courante. |
| `DesiredPosition` | Position calculée par les systèmes avant résolution des collisions. |

Cette liste n’est pas limitative ; le moteur peut enregistrer des composants supplémentaires selon les besoins de la configuration.

## Diagramme ASCII

Le flux d’exécution peut se représenter ainsi :

```
    +--------------+
    | Script Lua   |
    +------+-------+
           |
           v
    +------+-------+
    | Chargement   |  (GameConfig)
    +------+-------+
           |
           v
    +------+-------+
    | Moteur       |
    | (Engine)     |
    +------+-------+
           |
           v
    +------+-------+
    | Registre ECS |
    +------+-------+
           |
           v
    +------+-------+
    | État du jeu  |
    +------+-------+
           |
           v
    +------+-------+
    | Réseau       | (envoi d’instantanés)
    +--------------+
```

Ce schéma montre comment le script Lua est chargé en une configuration, utilisée par le moteur pour initialiser les entités dans le registre ECS.  La simulation produit un état qui est transmis au module réseau pour synchroniser les clients.

## Responsabilités

- **Responsabilités de l’`Engine`** :
  - Charger la configuration et créer les entités d’après les archétypes.
  - Enregistrer les composants et les systèmes nécessaires à la simulation.
  - Gérer les collisions, appliquer les dégâts, supprimer les entités expirées et mettre à jour la position.
  - Offrir une API minimaliste (`spawn`, `update`, `getRegistry`) au code du jeu.

- **Responsabilités du code appelant (jeu, serveur ou client)** :
  - Appeler `loadGameConfig()` pour charger la configuration Lua au démarrage.
  - Instancier l’`Engine` avec cette configuration.
  - Créer les entités initiales (joueur, ennemis, projectiles) via `spawn`.
  - À chaque tic, récupérer les entrées du joueur ou du réseau, mettre à jour les composants concernés (par ex. `InputState`), puis appeler `update(dt)`.
  - Sérialiser l’état ou l’appliquer selon qu’on est serveur ou client.
  - Réagir aux événements du jeu (fin de partie, respawn) en créant ou supprimant des entités.

## Exemples d’intégration

### Exemple côté serveur

```cpp
// Chargement de la configuration et création de l’engine
engine::GameConfig cfg = engine::loadGameConfig("config/game.lua");
engine::Engine eng(cfg);

// Création des entités initiales
eng.spawn("player", 0.f, 0.f);
eng.spawn("enemy", 10.f, 0.f);

// Boucle de jeu
while (tour_en_cours) {
    // 1. Récupérer les entrées depuis le réseau (InputPacket) et mettre à jour les InputState
    net::InputPacket input;
    // ... lecture réseau ...
    // eng.getRegistry().get_components<engine::InputState>()[entité_joueur]->moveX = input.moveX;
    // 2. Appeler la simulation avec un pas de temps fixe
    eng.update(1.f / 60.f);
    // 3. Sérialiser l’état et l’envoyer aux clients via net::SnapshotPacket
    // ... sérialisation ...
}
```

### Exemple côté client

```cpp
engine::GameConfig cfg = engine::loadGameConfig("config/game.lua");
engine::Engine eng(cfg);

// Boucle de rendu
while (affichage) {
    // 1. Collecter les entrées clavier/souris et envoyer un InputPacket au serveur
    net::InputPacket pkt;
    pkt.moveX = lireAxeHorizontal();
    pkt.moveY = lireAxeVertical();
    pkt.firePressed = boutonTirJusteAppuyé();
    pkt.fireHeld    = boutonTirMaintenu();
    pkt.fireReleased = boutonTirJusteRelâché();
    // ... envoi réseau ...
    // 2. Recevoir un SnapshotPacket et appliquer les données aux entités locales
    net::SnapshotPacket snap;
    // ... lecture réseau ...
    // mettre à jour la position et l’état des entités contrôlées selon snap
    // 3. Faire avancer la simulation locale d’une petite quantité pour l’interpolation
    eng.update(1.f / 60.f);
    // 4. Rendre la scène en se basant sur le registre ECS
}
```

Ces exemples illustrent comment le moteur est utilisé conjointement au module réseau pour construire un jeu multi‑joueur déterministe.  Le code reste en français pour les commentaires et la logique de plus haut niveau, tandis que les noms des types et fonctions issus de l’API restent en anglais pour correspondre au code C++.