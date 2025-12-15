# Bibliothèque `net` – couche réseau UDP

La bibliothèque `net` fournit une couche réseau légère et sans blocage pour synchroniser un jeu multi‑joueur.  Elle repose sur les sockets UDP de l’OS, évite toute dépendance externe et utilise des structures C simples pour représenter les paquets.  Son but est de transporter les entrées du client vers le serveur et de renvoyer des instantanés d’état du serveur vers le client à cadence fixe.

## Principes de fonctionnement

* **UDP et pas de blocage** : la communication utilise le protocole UDP pour minimiser la latence.  Les sockets sont configurés en mode non bloquant (« pas de blocage »).  Les fonctions `pollInputs()` (côté serveur) et `pollSnapshot()` (côté client) doivent être appelées régulièrement pour vider la file de réception.
* **Cadence fixe** : le serveur envoie les instantanés à une cadence fixe (par exemple 60 fois par seconde).  De même, le client envoie ses entrées à chaque itération de sa boucle.  Il n’y a pas de retransmission automatique ; les données perdues sont compensées par la fréquence d’envoi.
* **Rôle du serveur** : le serveur écoute sur un port UDP, gère un tableau fixe de « emplacements » (slots) pour les clients, attribue un slot à chaque adresse inconnue et renvoie un instantané de l’état.  Il maintient pour chaque slot les derniers numéros de séquence reçus et traités ainsi qu’un compteur d’instantanés.
* **Rôle du client** : le client envoie périodiquement ses entrées au serveur et reçoit des instantanés d’état.  Il applique ces instantanés pour mettre à jour sa propre représentation du monde et ajuste ses prédictions.

## Gestion des emplacements (slots)

La structure interne `ClientSlot` contient :

- **`active`** : indique si le slot est utilisé.
- **`endpoint`** : adresse et port du client associé (type `sockaddr_in`).
- **`lastReceivedInput`** : plus grand numéro de séquence d’entrée reçu.
- **`lastProcessedInput`** : plus grand numéro de séquence d’entrée intégré dans la simulation.
- **`snapshotCounter`** : identifiant monotone des instantanés envoyés à ce client.

Lorsque le serveur reçoit un paquet d’entrée d’une adresse inconnue, il cherche un slot libre (`active == false`) et l’associe à cette adresse.  Si tous les slots sont occupés (par défaut `MAX_DEFAULT_CLIENTS = 4`), les nouveaux clients sont ignorés.  Les slots ne sont pas libérés automatiquement ; il est possible d’implémenter un délai d’inactivité dans le code appelant si nécessaire.

## Description des paquets

Le protocole définit plusieurs structures de paquets sérialisées en mémoire.  **Toutes les valeurs numériques sont stockées en ordre d’octets hôte** : il n’y a pas de conversion automatique en réseau.  Si la communication s’effectue entre machines à bouts différents (little endian/big endian), il est nécessaire de convertir manuellement les valeurs.

### Paquet d’entrée (`InputPacket`)

Le client envoie un `InputPacket` au serveur pour chaque image de jeu.  La structure est la suivante :

```
+----------------------+
| magic               |
| protocolVersion     |
| inputSequence       |
| clientFrame         |
| moveX               |
| moveY               |
| firePressed         |
| fireHeld            |
| fireReleased        |
| padding             |
+----------------------+
```

* **`magic`** : doit être égal à `INPUT_MAGIC` (constante `0x49505430u`, soit "IPT0").  Permet de valider le paquet.
* **`protocolVersion`** : version du protocole (actuellement 1).  Permet de détecter des incohérences lors d’une mise à jour.
* **`inputSequence`** : numéro de séquence monotone, incrémenté à chaque envoi.  Le serveur renvoie le dernier numéro traité dans l’instantané pour permettre au client d’éliminer les entrées déjà intégrées.
* **`clientFrame`** : compteur local de trame, optionnel (peut servir à des statistiques ou des prédictions).
* **`moveX` / `moveY`** : valeurs flottantes comprises entre −1 et 1 représentant les axes de déplacement.
* **`firePressed` / `fireHeld` / `fireReleased`** : indicateurs (`0` ou `1`) pour les actions de tir selon que le bouton vient d’être pressé, maintenu ou relâché au cours de cette trame.
* **`padding`** : octet réservé pour l’alignement ou de futurs champs.

Le client doit envoyer ce paquet en flux constant, même si l’état des entrées n’a pas changé, afin de maintenir la connexion active et de permettre au serveur de calculer les mouvements à partir des entrées les plus récentes.

### En‑tête d’instantané (`SnapshotHeader`)

Le serveur répond par un `SnapshotPacket` qui commence par un `SnapshotHeader` puis une liste de `SnapshotEntity`.  L’en‑tête est :

```
+----------------------+
| magic               |
| protocolVersion     |
| snapshotId          |
| serverFrame         |
| lastProcessedInput  |
| controlledId        |
| entityCount         |
| reserved            |
+----------------------+
```

* **`magic`** : doit valoir `SNAP_MAGIC` (constante `0x534E4150u`, soit "SNAP").
* **`protocolVersion`** : version du protocole (actuellement 1).
* **`snapshotId`** : identifiant monotone d’instantané pour ce client.  Permet de détecter les paquets perdus ou retardés.
* **`serverFrame`** : compteur de trame côté serveur (par exemple nombre de mises à jour effectuées).
* **`lastProcessedInput`** : plus grand numéro de séquence d’entrée appliqué dans cette trame pour ce client.  Le client peut supprimer de sa file les entrées dont le numéro est inférieur ou égal.
* **`controlledId`** : identifiant de l’entité contrôlée par ce client, ou `0xffffffff` si aucune entité n’est associée.
* **`entityCount`** : nombre d’entrées `SnapshotEntity` qui suivent immédiatement l’en‑tête.  Si ce nombre dépasse `MAX_ENTITIES`, le serveur tronque la liste pour rester sous la taille maximale d’un datagramme UDP.
* **`reserved`** : champ réservé pour de futurs drapeaux.

### Entité d’instantané (`SnapshotEntity`)

Chaque entité contenue dans un instantané est sérialisée par un `SnapshotEntity` :

```
+----------------------+  <-- début de chaque entité
| id                  |
| generation          |
| alive               |
| hasPosition         |
| x                   |
| y                   |
| hasVelocity         |
| vx                  |
| vy                  |
| hasHealth           |
| health              |
| respawnable         |
| hasCollision        |
| hitHalfWidth        |
| hitHalfHeight       |
| padding[3]          |
+----------------------+  <-- fin de l’entité
```

* **`id`** : identifiant de l’entité dans l’ECS.
* **`generation`** : numéro de génération (non utilisé ici, toujours zéro).
* **`alive`** : `1` si l’entité est vivante, `0` sinon.
* **`hasPosition`** et **`hasVelocity`** : indicateurs permettant de savoir si les champs de position ou de vitesse sont valides.  Les valeurs `x`, `y`, `vx` et `vy` ne doivent être lues que si les indicateurs correspondants sont à `1`.
* **`hasHealth`** : indique si la valeur de points de vie suit.
* **`respawnable`** : réplique le composant `Respawnable` pour savoir si l’entité doit réapparaître.
* **`hasCollision`** : indique si les données de collision sont valides et si l’entité doit être considérée comme collidable.
* **`hitHalfWidth` / `hitHalfHeight`** : demi‑largeur et demi‑hauteur de la boîte de collision.  Ces valeurs sont présentes uniquement si `hasCollision` vaut `1`.
* **`padding[3]`** : octets réservés pour aligner la taille de la structure sur un multiple de 4 octets.

### Paquet d’instantané (`SnapshotPacket`)

Enfin, le `SnapshotPacket` côté client regroupe un `SnapshotHeader` et un tableau dynamique d’`SnapshotEntity`.  La structure est :

```
struct SnapshotPacket {
    SnapshotHeader              header;
    std::vector<SnapshotEntity> entities;
};
```

Le client désérialise l’en‑tête, réserve un tableau de `entityCount` éléments, puis lit chaque `SnapshotEntity`.  Les entités non présentes dans la liste doivent être conservées telles quelles ou supprimées selon la logique du jeu.

## Conseils et bonnes pratiques

* **Ordre d’octets hôte** : comme mentionné, toutes les valeurs sont envoyées telles quelles (endianness hôte).  Si les clients et le serveur ne partagent pas le même ordre d’octets, utilisez `htonl()`, `ntohl()` et leurs variantes pour convertir les entiers.  Les nombres à virgule flottante nécessitent une conversion manuelle.
* **Taille des paquets** : les datagrammes UDP ont une taille maximale (en pratique ~512 octets est sûr sur Internet).  Le champ `MAX_ENTITIES` (4096 par défaut) limite le nombre d’entités dans un instantané pour éviter de dépasser cette limite.  Adaptez cette constante à votre jeu.
* **Perte et réordonancement** : UDP ne garantit ni la livraison ni l’ordre des paquets.  Le client doit conserver les entrées envoyées tant que le serveur n’a pas accusé réception via `lastProcessedInput`.  Le serveur utilise `snapshotId` pour détecter les instantanés obsolètes ou en double.
* **Relecture d’instantanés** : le client peut appliquer directement le dernier instantané reçu (en écrasant son état local), ou interpoler entre plusieurs instantanés pour obtenir un rendu fluide.  Lorsque des paquets sont perdus, l’interpolation permet de masquer les sauts.
* **Assignation et libération de slots** : pour gérer de nouveaux joueurs, surveillez l’inactivité d’un slot et libérez‑le si aucun paquet n’a été reçu pendant un certain temps.  La bibliothèque ne fournit pas cette logique ; c’est au code du serveur d’implémenter cette politique.

En appliquant ces conseils, vous obtiendrez une communication réseau simple, robuste et déterministe, adaptée aux jeux d’action nécessitant peu de latence.