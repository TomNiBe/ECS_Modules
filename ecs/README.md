# Bibliothèque `ecs` – système entité‑composants

Cette bibliothèque implémente un système entité‑composants (ECS) minimaliste et orienté données.  Elle découple complètement la logique (systèmes) des données (composants) et permet d’itérer efficacement sur des entités.  Les entités sont représentées par un simple identifiant numérique stable et les composants sont stockés dans des tableaux contigus, permettant des parcours rapides et prédictibles.

## Principes généraux

### Pourquoi un ECS orienté données ?

Un ECS permet de séparer clairement les données de la logique.  Les données sont regroupées en **composants** (simples structures agrégées), tandis que la logique est organisée en **systèmes** qui opèrent sur des ensembles d’entités possédant certains composants.  Cette approche :

- améliore la localité mémoire et donc les performances,
- facilite la réutilisation et la composition des comportements,
- rend la simulation déterministe car l’ordre d’exécution des systèmes est explicitement contrôlé.

### Concepts clés

* **Entité (`ecs::entity_t`)** : handle opaque qui encapsule un index entier.  Une entité n’a pas d’état propre, elle sert uniquement de clé pour accéder à ses composants.  Un handle peut être comparé, copié et réutilisé après destruction (via la liste libre).  Il ne contient pas de génération ; il est donc recommandé de vérifier qu’un handle est toujours vivant avant de l’utiliser.

* **Composant** : structure de données sans logique.  Par exemple, un `Position` avec des champs `x` et `y`.  Chaque type de composant est enregistré une seule fois auprès du registre.  Les composants sont stockés dans un `sparse_array` contigu, un tableau d’`std::optional<Component>` indexé par l’entité.

* **Système** : fonction ou lambda appelée par le registre qui prend en paramètre des références vers les tableaux de composants.  Les systèmes itèrent sur ces tableaux et mettent à jour les composants.  Ils ne doivent pas allouer de mémoire ni modifier la structure des composants pendant l’itération (hormis via les méthodes du registre prévues à cet effet).

## Le registre (`ecs::registry`)

Le `registry` est le cœur de l’ECS : il gère les entités, stocke les composants et exécute les systèmes.

### Création et destruction d’entités

* **`spawn_entity()`** : crée une nouvelle entité et renvoie son handle.  Si des emplacements libres sont disponibles, ils sont réutilisés.
* **`kill_entity(ecs::entity_t)`** : détruit une entité.  Tous ses composants sont effacés et son index est remis dans la liste libre.  Après destruction, tout handle vers cette entité devient invalide.

### Enregistrement et accès aux composants

Avant d’utiliser un type de composant, il faut l’enregistrer :

```cpp
ecs::registry reg;
reg.register_component<Position>();
reg.register_component<Velocity>();
```

L’enregistrement crée un `sparse_array` interne pour ce type.  Pour manipuler les composants :

* **Ajout** : `reg.emplace_component<Position>(ent, x, y)` construit un composant en place pour l’entité `ent`.
* **Suppression** : `reg.remove_component<Position>(ent)` supprime le composant de l’entité (via `erase`).
* **Accès** : les tableaux retournés par `get_components<T>()` sont des références à des `sparse_array<T>`.  On accède à un composant avec `array[ent]` qui retourne un `std::optional<T>`.  Si l’option est vide, l’entité ne possède pas ce composant.

### Exécution des systèmes

Des systèmes sont enregistrés via `add_system<Composant1, Composant2>(lambda)` où `lambda` reçoit une référence vers le registre et des références vers les tableaux de composants correspondants.  Les systèmes sont stockés dans une liste et **s’exécutent dans l’ordre d’enregistrement** lorsque `run_systems()` est appelé.  Cet ordre est fixe et doit être choisi soigneusement pour maintenir le déterminisme.

Par exemple :

```cpp
reg.add_system<Position, Velocity>([](ecs::registry &r,
                                      auto &positions,
                                      auto &velocities) {
    for (std::size_t i = 0; i < positions.size(); ++i) {
        ecs::entity_t ent{i};
        auto &posOpt = positions[ent];
        auto &velOpt = velocities[ent];
        if (posOpt && velOpt) {
            posOpt->x += velOpt->x;
            posOpt->y += velOpt->y;
        }
    }
});
```

### `ecs::entity_t` : identité et bonnes pratiques

Le type `entity_t` encapsule un index.  Un handle par défaut est invalide et peut être testé comme un booléen.  Les identités sont stables tant que l’entité est vivante.  Étant donné que les générations ne sont pas gérées par défaut, il est conseillé de :

- ne pas conserver de handles au‑delà de la durée de vie garantie par la logique du jeu ;
- vérifier qu’une entité est vivante avant d’accéder à ses composants ;
- éviter de stocker les indices bruts dans des conteneurs persistants.

## `sparse_array` : structure de stockage

Chaque type de composant est stocké dans une instance de `sparse_array<T>`.  Cette structure :

- est un tableau contigu de `std::optional<T>` indexé par le `entity_t` ;
- s’agrandit automatiquement lors de l’accès en écriture à un index supérieur ;
- retourne une option vide lors d’un accès en lecture hors bornes ;
- ne garantit pas la stabilité des pointeurs lors d’une réallocation ;
- permet la présence ou l’absence d’un composant sans allocation par entité.

Accéder à un composant absent est peu coûteux : un objet statique vide est renvoyé.  Lorsqu’un composant est ajouté via `emplace_at` ou `insert_at`, l’option est remplie et l’entité est considérée comme possédant ce composant.

## Ordre d’exécution des systèmes et implications

Les systèmes sont exécutés dans l’ordre où ils sont enregistrés.  Cet ordre doit être choisi de manière cohérente avec la logique de jeu (par exemple, déplacer les entités avant de traiter les collisions).  Modifier l’ordre peut changer l’état final et briser le déterminisme.  Évitez d’enregistrer des systèmes à des endroits différents du code selon les circonstances ; centralisez l’enregistrement lors de la création du moteur.

## Déterminisme et bonnes pratiques

- **Pas de mémoire dynamique pendant la boucle** : éviter les allocations dans les systèmes pour garantir des temps de mise à jour stables.
- **Utiliser un pas de temps fixe** : passez un intervalle constant (`dt`) à la simulation pour éviter des divergences entre machines.
- **Pas de dépendance à l’ordre non défini** : n’utilisez pas des conteneurs comme `unordered_map` pour l’itération logique dans un système (l’ordre peut varier entre plateformes).  Préférez des vecteurs triés ou calculez un ordre déterministe.
- **Évitez les effets de bord** : les systèmes doivent uniquement lire et écrire les composants concernés.  Modifiez la structure des entités (ajout ou suppression de composants) en dehors des boucles qui itèrent sur ces mêmes composants pour ne pas invalider les indices.

## Performances et conseils

Pour obtenir de bonnes performances avec ce modèle :

1. **Minimiser la mémoire dynamique** : enregistrez tous les types de composants au lancement et évitez d’en créer dynamiquement en cours de jeu.  Les `sparse_array` allouent de manière contiguë.
2. **Parcours contigus** : les systèmes doivent itérer séquentiellement sur les indices pour profiter des caches.  Utilisez les utilitaires de zip fournis (voir `ecs/zipper.hpp`) pour parcourir plusieurs tableaux en parallèle sans branchement.
3. **Accès prévisible** : regroupez les composants fréquemment utilisés dans un même système afin de limiter les sauts de cache.  Évitez les accès aléatoires dans la boucle interne.
4. **Pas de blocage** : laissez aux bibliothèques réseau et moteur la gestion des appels potentiellement bloquants.  Le registre n’exécute rien en dehors de vos systèmes.

## Exemple d’utilisation minimal

```cpp
#include "ecs/ecs.hpp"

struct Position { float x = 0.f; float y = 0.f; };
struct Velocity { float x = 0.f; float y = 0.f; };

int main() {
    ecs::registry reg;
    // enregistrement des composants
    reg.register_component<Position>();
    reg.register_component<Velocity>();

    // système de mise à jour des positions
    reg.add_system<Position, Velocity>([](ecs::registry &,
                                          auto &positions,
                                          auto &velocities) {
        for (std::size_t i = 0; i < positions.size(); ++i) {
            ecs::entity_t ent{i};
            auto &pos = positions[ent];
            auto &vel = velocities[ent];
            if (pos && vel) {
                pos->x += vel->x;
                pos->y += vel->y;
            }
        }
    });

    // création d’une entité
    ecs::entity_t e = reg.spawn_entity();
    reg.emplace_component<Position>(e, 0.f, 0.f);
    reg.emplace_component<Velocity>(e, 1.f, 0.f);

    // exécution d’une mise à jour
    reg.run_systems();

    return 0;
}
```

Dans cet exemple, une entité est créée, on lui assigne une position et une vitesse, puis on exécute un système qui additionne la vitesse à la position.  Ce modèle peut être étendu à de nombreux composants et systèmes pour construire des simulations complexes tout en conservant une structure claire et performante.