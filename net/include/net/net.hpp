//===-------------------------------------------------------------------------------===
// net.hpp
//
// Couche réseau UDP minimaliste pour prototypes de jeu. Cette implémentation évite
// les dépendances externes et fournit des définitions de paquets simples ainsi que
// des classes client et serveur légères. Le client et le serveur fonctionnent en
// mode non bloquant ; pollInputs() ou pollSnapshot() doivent être appelées
// régulièrement dans la boucle principale pour vider la socket. Le serveur gère un
// tableau fixe de slots clients et assigne un slot libre à tout expéditeur inconnu.
// Les paquets utilisent des structures à mise en page fixe sans allocations dynamiques.
// Tous les entiers sont en ordre d’hôte ; adaptez si nécessaire pour des architectures différentes.

#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>


namespace net {

// -----------------------------------------------------------------------------
// Constantes de protocole et définitions de paquets
//
// Chaque paquet contient une valeur magique 32 bits permettant de rejeter rapidement les
// données inattendues. Une version de protocole est présente dans les en‑têtes
// d’entrée et d’instantané pour permettre l’évolution du format.
// -----------------------------------------------------------------------------

constexpr std::uint32_t PROTOCOL_VERSION = 1;
constexpr std::uint32_t INPUT_MAGIC      = 0x49505430u; // 'IPT0'
constexpr std::uint32_t SNAP_MAGIC       = 0x534E4150u; // 'SNAP'

// Nombre maximal de clients suivis par défaut ; les autres sont ignorés.
static constexpr std::size_t MAX_DEFAULT_CLIENTS = 4;
// Nombre maximal d’entités dans un instantané ; les instantanés plus grands seront tronqués.
static constexpr std::uint32_t MAX_ENTITIES = 4096;

#pragma pack(push, 1)

// Paquet envoyé du client au serveur contenant une frame d’entrée.
// Le client incrémente inputSequence à chaque envoi ; le serveur renvoie la séquence la plus récente traitée dans l’instantané.
struct InputPacket {
    std::uint32_t magic;           ///< doit valoir INPUT_MAGIC
    std::uint32_t protocolVersion; ///< doit valoir PROTOCOL_VERSION
    std::uint32_t inputSequence;   ///< numéro de séquence monotone
    std::uint32_t clientFrame;     ///< compteur d’images client (optionnel)
    float         moveX;           ///< entrée horizontale dans [-1,1]
    float         moveY;           ///< entrée verticale dans [-1,1]
    std::uint8_t  firePressed;     ///< 1 si le tir est pressé pendant cette frame
    std::uint8_t  fireHeld;        ///< 1 si le tir est maintenu
    std::uint8_t  fireReleased;    ///< 1 si le tir est relâché durant cette frame
    std::uint8_t  padding;         ///< réservé (alignement/usage futur)
};

// En‑tête précédant un instantané envoyé du serveur au client. Immédiatement après suivent entityCount enregistrements SnapshotEntity.
struct SnapshotHeader {
    std::uint32_t magic;           ///< doit valoir SNAP_MAGIC
    std::uint32_t protocolVersion; ///< doit valoir PROTOCOL_VERSION
    std::uint32_t snapshotId;      ///< identifiant d’instantané monotone par client
    std::uint32_t serverFrame;     ///< compteur de tick du serveur
    std::uint32_t lastProcessedInput; ///< dernière inputSequence appliquée pour ce client
    std::uint32_t controlledId;    ///< identifiant de l’entité contrôlée par ce client, ou 0xffffffff
    std::uint32_t entityCount;     ///< nombre d’entités suivantes
    std::uint32_t reserved;        ///< réservé pour des indicateurs ou usage futur
};

/// État sérialisé d’une entité dans un instantané.  Des indicateurs
/// précisent quels champs sont valides.
struct SnapshotEntity {
    std::uint32_t id;         ///< identifiant d’entité
    std::uint32_t generation; ///< génération/version de l’entité (non utilisé ici)
    std::uint8_t  alive;      ///< 1 si l’entité est vivante
    std::uint8_t  hasPosition;
    float         x;
    float         y;
    std::uint8_t  hasVelocity;
    float         vx;
    float         vy;
    std::uint8_t  hasHealth;
    std::int32_t  health;
    std::uint8_t  respawnable;
    std::uint8_t  hasCollision;
    float         hitHalfWidth;
    float         hitHalfHeight;
    std::uint8_t  padding[3]; ///< réservé pour que sizeof(SnapshotEntity) soit divisible par 4
};

#pragma pack(pop)

/// Conteneur pratique pour un instantané désérialisé.  Utilisé côté client.
struct SnapshotPacket {
    SnapshotHeader              header;
    std::vector<SnapshotEntity> entities;
};

// -----------------------------------------------------------------------------
// Types internes
//
// Le serveur conserve un petit nombre de clients grâce à ClientSlot. Chaque
// slot stocke l’adresse distante (sockaddr_in) et des données de suivi : la
// dernière séquence d’entrée reçue et la dernière séquence traitée.
// -----------------------------------------------------------------------------
struct ClientSlot {
    bool          active      = false;    ///< vrai si ce slot est utilisé
    sockaddr_in   endpoint{};            ///< adresse/port distant
    std::uint32_t lastReceivedInput = 0; ///< plus haute séquence reçue
    std::uint32_t lastProcessedInput = 0;///< plus haute séquence traitée
    std::uint32_t snapshotCounter   = 0; ///< compteur d’identifiants d’instantané
};

// -----------------------------------------------------------------------------
// Classe Server
//
// Serveur UDP non bloquant qui écoute des structures InputPacket et émet des
// instantanés. Le serveur maintient un tableau fixe de ClientSlot. Les
// expéditeurs inconnus se voient attribuer un slot libre jusqu’à atteindre
// le nombre maximal de clients. L’utilisateur fournit des callbacks pour les
// nouveaux clients et pour les paquets d’entrée. Toutes les méthodes
// publiques ne sont pas thread‑safe et doivent être appelées depuis un seul
// thread.
// -----------------------------------------------------------------------------
class Server {
public:
    /// Type de callback appelée lorsqu’un nouveau client est détecté.  Fournit
    /// l’indice du slot et l’adresse distante.
    using NewClientCallback = std::function<void(std::size_t, const sockaddr_in&)>;
    /// Type de callback appelée lorsqu’un paquet d’entrée est reçu.  Fournit
    /// l’indice du slot et le paquet.  La callback ne doit pas conserver
    /// de pointeurs vers le paquet.
    using InputCallback = std::function<void(std::size_t, const InputPacket&)>;

    /// Construit un serveur lié au port UDP fourni.  Lève une std::runtime_error
    /// si la socket ne peut être créée ou liée.
    explicit Server(std::uint16_t port) {
        _socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (_socket < 0) {
            throw std::runtime_error("Failed to create server socket");
        }
        // configure la socket en non bloquant
        int flags = fcntl(_socket, F_GETFL, 0);
        fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
        // lie la socket au port
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (::bind(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(_socket);
            throw std::runtime_error("Failed to bind server socket");
        }
    }

    /// Le destructeur ferme la socket sous‑jacente.
    ~Server() {
        if (_socket >= 0) {
            ::close(_socket);
        }
    }

    /// Définit les callbacks pour les nouvelles connexions et les paquets d’entrée.
    /// Chaque callback peut être laissée non définie.  Les callbacks sont
    /// invoquées depuis pollInputs(), évitez donc un traitement lourd à cet endroit.
    void setCallbacks(NewClientCallback onNewClient, InputCallback onInput) {
        _onNewClient = std::move(onNewClient);
        _onInput     = std::move(onInput);
    }

    /// Interroge la socket UDP pour les paquets d’entrée.  Pour chaque
    /// InputPacket valide reçu, la callback correspondante est appelée.  Les
    /// expéditeurs inconnus se voient automatiquement attribuer un slot libre.
    void pollInputs() {
        std::array<char, 256> buffer{};
        while (true) {
            sockaddr_in sender{};
            socklen_t   senderLen = sizeof(sender);
            ssize_t n = ::recvfrom(_socket, buffer.data(), buffer.size(), 0,
                                   reinterpret_cast<sockaddr*>(&sender), &senderLen);
            if (n < 0) {
            // plus de données
                break;
            }
            if (static_cast<std::size_t>(n) < sizeof(InputPacket)) {
                continue;
            }
            InputPacket pkt{};
            std::memcpy(&pkt, buffer.data(), sizeof(InputPacket));
            if (pkt.magic != INPUT_MAGIC || pkt.protocolVersion != PROTOCOL_VERSION) {
                continue;
            }
            // recherche ou allocation d’un slot client
            std::size_t idx = findClient(sender);
            if (idx == MAX_DEFAULT_CLIENTS) {
                idx = findFreeSlot();
                if (idx == MAX_DEFAULT_CLIENTS) {
                    // aucun slot libre
                    continue;
                }
                _clients[idx].active = true;
                _clients[idx].endpoint = sender;
                _clients[idx].lastReceivedInput = 0;
                _clients[idx].lastProcessedInput = 0;
                _clients[idx].snapshotCounter = 0;
                if (_onNewClient) {
                    _onNewClient(idx, sender);
                }
            }
            // met à jour la dernière entrée reçue
            _clients[idx].lastReceivedInput = pkt.inputSequence;
            if (_onInput) {
                _onInput(idx, pkt);
            }
            // marque comme traité par défaut
            _clients[idx].lastProcessedInput = pkt.inputSequence;
        }
    }

    /// Envoie un instantané au slot client spécifié.  Les champs snapshotId,
    /// serverFrame, lastProcessedInput et controlledId sont remplis
    /// automatiquement.  Les entités fournies sont tronquées à MAX_ENTITIES si
    /// nécessaire.  Si le slot est inactif, l’appel ne fait rien.
    void sendSnapshot(std::size_t slotIndex,
                      std::uint32_t serverFrame,
                      std::uint32_t controlledId,
                      const std::vector<SnapshotEntity>& entities) {
        if (slotIndex >= MAX_DEFAULT_CLIENTS) {
            return;
        }
        if (!_clients[slotIndex].active) {
            return;
        }
        std::uint32_t count = static_cast<std::uint32_t>(entities.size());
        if (count > MAX_ENTITIES) {
            count = MAX_ENTITIES;
        }
        // construit l’en‑tête
        SnapshotHeader hdr{};
        hdr.magic            = SNAP_MAGIC;
        hdr.protocolVersion  = PROTOCOL_VERSION;
        hdr.snapshotId       = _clients[slotIndex].snapshotCounter++;
        hdr.serverFrame      = serverFrame;
        hdr.lastProcessedInput = _clients[slotIndex].lastProcessedInput;
        hdr.controlledId     = controlledId;
        hdr.entityCount      = count;
        hdr.reserved         = 0;
        // calcule la taille totale
        std::size_t totalSize = sizeof(SnapshotHeader) + count * sizeof(SnapshotEntity);
        std::vector<char> buffer(totalSize);
        std::memcpy(buffer.data(), &hdr, sizeof(SnapshotHeader));
        if (count > 0) {
            std::memcpy(buffer.data() + sizeof(SnapshotHeader), entities.data(),
                        count * sizeof(SnapshotEntity));
        }
        // envoie
        ::sendto(_socket, buffer.data(), buffer.size(), 0,
                 reinterpret_cast<sockaddr*>(&_clients[slotIndex].endpoint),
                 sizeof(sockaddr_in));
    }

    /// Permet au serveur de mettre à jour la dernière séquence d’entrée traitée
    /// pour un slot client.  Cette valeur sera renvoyée dans les futurs
    /// instantanés.  Utilisez ceci lorsque vous appliquez des entrées hors ordre.
    void setLastProcessedInput(std::size_t slotIndex, std::uint32_t seq) {
        if (slotIndex < MAX_DEFAULT_CLIENTS && _clients[slotIndex].active) {
            _clients[slotIndex].lastProcessedInput = seq;
        }
    }

private:
    /// Renvoie l’indice du slot correspondant à l’expéditeur ou
    /// MAX_DEFAULT_CLIENTS s’il n’est pas trouvé.
    std::size_t findClient(const sockaddr_in& sender) const {
        for (std::size_t i = 0; i < MAX_DEFAULT_CLIENTS; ++i) {
            if (_clients[i].active &&
                _clients[i].endpoint.sin_addr.s_addr == sender.sin_addr.s_addr &&
                _clients[i].endpoint.sin_port == sender.sin_port) {
                return i;
            }
        }
        return MAX_DEFAULT_CLIENTS;
    }

    /// Renvoie l’indice d’un slot libre ou MAX_DEFAULT_CLIENTS s’il n’y en a pas.
    std::size_t findFreeSlot() const {
        for (std::size_t i = 0; i < MAX_DEFAULT_CLIENTS; ++i) {
            if (!_clients[i].active) {
                return i;
            }
        }
        return MAX_DEFAULT_CLIENTS;
    }

    int                        _socket{-1};
    std::array<ClientSlot, MAX_DEFAULT_CLIENTS> _clients{};
    NewClientCallback          _onNewClient;
    InputCallback              _onInput;
};

// -----------------------------------------------------------------------------
// Classe Client
//
// Client UDP non bloquant qui envoie des paquets d’entrée et reçoit des
// instantanés. Le client stocke l’adresse du serveur et utilise sendto() pour
// l’envoi. Les instantanés reçus sont convertis en structure SnapshotPacket.
// Les méthodes ne sont pas thread‑safe et doivent être appelées depuis un seul
// thread.
// -----------------------------------------------------------------------------
class Client {
public:
    /// Construit un client visant l’hôte et le port spécifiés.  L’hôte doit être
    /// une adresse IPv4 en notation décimale (par ex. « 127.0.0.1 »).  Lève
    /// std::runtime_error si la socket ne peut être créée ou si l’hôte est
    /// invalide.
    Client(const std::string& host, std::uint16_t port) {
        _socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (_socket < 0) {
            throw std::runtime_error("Failed to create client socket");
        }
        // configure la socket en non bloquant
        int flags = fcntl(_socket, F_GETFL, 0);
        fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
        // prépare l’adresse du serveur
        std::memset(&_server, 0, sizeof(_server));
        _server.sin_family = AF_INET;
        _server.sin_port   = htons(port);
        if (::inet_pton(AF_INET, host.c_str(), &_server.sin_addr) <= 0) {
            ::close(_socket);
            throw std::runtime_error("Invalid server address");
        }
    }

    /// Le destructeur ferme la socket.
    ~Client() {
        if (_socket >= 0) {
            ::close(_socket);
        }
    }

    /// Envoie un paquet d’entrée au serveur.  Remplit automatiquement les champs
    /// magic et protocolVersion.  L’appelant doit initialiser les autres
    /// champs (inputSequence, clientFrame, moveX, moveY et firePressed/
    /// fireHeld/fireReleased).
    void sendInput(const InputPacket& pkt) {
        InputPacket tmp = pkt;
        tmp.magic = INPUT_MAGIC;
        tmp.protocolVersion = PROTOCOL_VERSION;
        ::sendto(_socket, &tmp, sizeof(tmp), 0,
                 reinterpret_cast<sockaddr*>(&_server), sizeof(_server));
    }

    /// Tente de recevoir un paquet d’instantané.  Si un instantané valide est
    /// disponible, il est retourné dans un std::optional ; sinon std::nullopt
    /// est renvoyé.  Les paquets invalides sont silencieusement ignorés.
    std::optional<SnapshotPacket> pollSnapshot() {
        // taille maximale attendue : en‑tête + MAX_ENTITIES * SnapshotEntity
        constexpr std::size_t maxSize = sizeof(SnapshotHeader) + MAX_ENTITIES * sizeof(SnapshotEntity);
        std::array<char, maxSize> buffer{};
        sockaddr_in sender{};
        socklen_t   senderLen = sizeof(sender);
        ssize_t n = ::recvfrom(_socket, buffer.data(), buffer.size(), 0,
                               reinterpret_cast<sockaddr*>(&sender), &senderLen);
        if (n <= 0) {
            return std::nullopt;
        }
        // s’assure qu’au moins l’en‑tête est présent
        if (static_cast<std::size_t>(n) < sizeof(SnapshotHeader)) {
            return std::nullopt;
        }
        SnapshotHeader hdr{};
        std::memcpy(&hdr, buffer.data(), sizeof(SnapshotHeader));
        if (hdr.magic != SNAP_MAGIC || hdr.protocolVersion != PROTOCOL_VERSION) {
            return std::nullopt;
        }
        // calcule le nombre d’entités réellement présentes
        std::size_t expectedSize = sizeof(SnapshotHeader) + hdr.entityCount * sizeof(SnapshotEntity);
        if (static_cast<std::size_t>(n) < expectedSize) {
            // paquet incomplet
            return std::nullopt;
        }
        SnapshotPacket pkt{};
        pkt.header = hdr;
        pkt.entities.resize(hdr.entityCount);
        if (hdr.entityCount > 0) {
            std::memcpy(pkt.entities.data(), buffer.data() + sizeof(SnapshotHeader),
                        hdr.entityCount * sizeof(SnapshotEntity));
        }
        return pkt;
    }

private:
    int          _socket{-1};
    sockaddr_in  _server{};
};

} // namespace net