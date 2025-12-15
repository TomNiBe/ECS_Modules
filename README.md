# Bibliothèques communes du projet

Le dossier `libs/` regroupe les briques fondamentales utilisées pour la simulation, la logique de jeu et la communication réseau.  Chaque sous‑module est conçu pour être autonome et réutilisable.  Ils sont publiés sous forme de sous‑module Git et peuvent être utilisés indépendamment du reste du projet.

## Philosophie générale

Ces bibliothèques suivent plusieurs principes :

1. **Séparation des responsabilités** : chaque sous‑module couvre un domaine précis : `ecs` gère le stockage et l’itération des entités et de leurs composants, `engine` fournit une façade de haut niveau pour charger des définitions depuis Lua et orchestrer les systèmes de simulation, et `net` assure les échanges entre un client et un serveur via UDP.  Aucun module ne dépend du code spécifique au jeu.
2. **Orientation données** : les structures sont conçues autour des données plutôt que des hiérarchies d’objets.  Les composants sont de simples structures sans logique intégrée et la simulation est réalisée par des fonctions extérieures (systèmes).  Toutes les constantes de jeu proviennent de fichiers de configuration Lua et non du code C++.
3. **Déterminisme** : pour permettre la reproduction fidèle d’une partie et la synchronisation entre machines, les systèmes s’exécutent dans un ordre déterministe, avec un pas de temps fixe.  Les appels réseau sont non bloquants et les mises à jour sont envoyées à cadence régulière.
4. **Performance** : les conteneurs internes (notamment dans `ecs`) utilisent des structures de tableaux pour assurer la contiguïté en mémoire et des accès prédictibles.  Les allocations dynamiques sont évitées pendant la simulation et les boucles se déroulent sur des plages contiguës.

## Vue d’ensemble des sous‑modules

- **`ecs/`** : implémente un système entité‑composants minimaliste inspiré des travaux du projet R‑Type.  Il fournit un `registry` pour créer et détruire des entités, enregistrer des types de composants et exécuter des systèmes.  Les composants sont stockés dans des tableaux clairsemés permettant une absence de composant sans allocation.
- **`engine/`** : constitue une façade de jeu bâtie sur l’ECS.  Ce module charge des définitions de projectile, d’arme et d’archétype depuis un script Lua (`GameConfig`), crée automatiquement les composants nécessaires et exécute la simulation image par image.  Il gère la création (`spawn`), l’intégration (`update`), les collisions, l’application de dégâts, la durée de vie et le respawn des entités.
- **`net/`** : propose une couche réseau UDP minimale et sans blocage pour synchroniser la simulation entre un client et un serveur.  Les paquets échangés sont de simples structures C sans allocation dynamique : `InputPacket` pour les entrées du client, `SnapshotHeader` et `SnapshotEntity` pour les instantanés d’état envoyés par le serveur.  Le module assigne automatiquement des emplacements aux clients et fournit des callbacks pour traiter les paquets.

## Architecture globale (schéma ASCII)

```
                 +------------------------------+
                 |  Entrées du joueur          |
                 +--------------+---------------+
                                |
                                v
                      +---------+---------+
                      |  Moteur de jeu   |
                      |  (ECS + Engine)  |
                      +---------+---------+
                                |
                                v
                 +--------------+---------------+
                 |  Instantanés d’état envoyés  |
                 |  via la couche réseau        |
                 +------------------------------+
```

Ce schéma résume le flux principal : le module réseau reçoit les entrées du joueur et les transmet au moteur de jeu, lequel met à jour l’état via l’ECS et renvoie des instantanés d’état vers les clients via le réseau.

## Flux général

1. Les clients envoient leurs entrées (déplacements, tirs, etc.) au serveur via `InputPacket`.
2. Le serveur collecte ces entrées et les transmet aux systèmes de simulation (`ecs` et `engine`) en fixant un pas de temps constant.  La logique est entièrement déterministe et orientée données.
3. Après la mise à jour, le serveur sérialise l’état pertinent dans un `SnapshotPacket` qui contient un `SnapshotHeader` et une liste de `SnapshotEntity`.  Ce paquet est envoyé aux clients à cadence régulière.
4. Les clients appliquent les instantanés reçus pour mettre à jour leur propre représentation locale de l’état et interpolent/anticipent entre deux instantanés pour un rendu fluide.

## Navigation

Vous trouverez ci‑dessous des liens vers la documentation détaillée de chaque sous‑module :

| Sous‑module | Description | Lien |
|---|---|---|
| `ecs` | Système entité‑composants minimaliste | [Documentation `ecs`](ecs/README.md) |
| `engine` | Façade de jeu au‑dessus de l’ECS | [Documentation `engine`](engine/README.md) |
| `net` | Couche réseau UDP non bloquante | [Documentation `net`](net/README.md) |

Chaque README est autonome et explique en détail les principes, les types et les conventions nécessaires à l’utilisation du module correspondant.