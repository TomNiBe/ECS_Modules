#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace net {

#ifdef _WIN32
    using socket_handle = SOCKET;
    using socklen_type  = int;
    using recv_len      = int;
    static constexpr socket_handle invalid_socket = INVALID_SOCKET;

    struct WSAInit {
        WSAInit() {
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
        }
        ~WSAInit() {
            WSACleanup();
        }
    };
#else
    using socket_handle = int;
    using socklen_type  = socklen_t;
    using recv_len      = ssize_t;
    static constexpr socket_handle invalid_socket = -1;
#endif

inline void socket_close(socket_handle s) {
#ifdef _WIN32
    if (s != invalid_socket) closesocket(s);
#else
    if (s != invalid_socket) ::close(s);
#endif
}

inline void socket_set_nonblocking(socket_handle s) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
    }
#endif
}

inline constexpr std::size_t MAX_DEFAULT_CLIENTS = 4;
inline constexpr std::uint32_t MAX_ENTITIES = 512;
inline constexpr std::uint16_t PROTOCOL_VERSION = 1;

inline constexpr std::uint32_t INPUT_MAGIC = 0x49505431u;
inline constexpr std::uint32_t SNAP_MAGIC  = 0x534E5031u;

#pragma pack(push, 1)

struct InputPacket {
    std::uint32_t magic{INPUT_MAGIC};
    std::uint16_t protocolVersion{PROTOCOL_VERSION};
    std::uint16_t _pad0{0};

    std::uint32_t inputSequence{0};
    std::uint32_t clientFrame{0};

    float moveX{0.f};
    float moveY{0.f};
    std::uint8_t firePressed{0};
    std::uint8_t fireHeld{0};
    std::uint8_t fireReleased{0};
    std::uint8_t _pad1{0};
};

struct SnapshotEntity {
    std::uint32_t id{0};
    std::uint16_t generation{0};

    std::uint8_t alive{1};
    std::uint8_t hasPosition{0};

    float x{0.f};
    float y{0.f};

    std::uint8_t hasVelocity{0};
    std::uint8_t hasHealth{0};
    std::uint8_t respawnable{0};
    std::uint8_t hasCollision{0};

    float vx{0.f};
    float vy{0.f};

    float health{0.f};

    float hitHalfWidth{0.f};
    float hitHalfHeight{0.f};

    std::uint16_t type{0};
    std::uint16_t flags{0};
};

struct SnapshotHeader {
    std::uint32_t magic{SNAP_MAGIC};
    std::uint16_t protocolVersion{PROTOCOL_VERSION};
    std::uint16_t _pad0{0};

    std::uint32_t sequence{0};
    std::uint32_t serverFrame{0};
    std::uint32_t controlledId{0};
    std::uint32_t playerCount{0};
    std::uint32_t entityCount{0};
};

#pragma pack(pop)

struct SnapshotPacket {
    SnapshotHeader header;
    std::vector<SnapshotEntity> entities;
};

class Server {
public:
    using NewClientCallback = std::function<void(std::size_t, const sockaddr_in&)>;
    using InputCallback     = std::function<void(std::size_t, const InputPacket&)>;

    struct ClientSlot {
        bool active{false};
        sockaddr_in addr{};
        std::uint32_t lastReceivedInput{0};
        std::uint32_t lastProcessedInput{0};
        std::uint32_t snapshotCounter{0};
    };

    explicit Server(std::uint16_t port) {
#ifdef _WIN32
        static WSAInit _wsa_once{};
#endif
        _socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (_socket == invalid_socket) {
            throw std::runtime_error("Failed to create server socket");
        }

        socket_set_nonblocking(_socket);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (::bind(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw std::runtime_error("Failed to bind server socket");
        }
    }

    ~Server() {
        socket_close(_socket);
    }

    void setCallbacks(NewClientCallback onNewClient, InputCallback onInput) {
        _onNewClient = std::move(onNewClient);
        _onInput     = std::move(onInput);
    }

    void pollInputs() {
        while (true) {
            std::array<char, 1024> buffer{};
            sockaddr_in sender{};
            socklen_type senderLen = sizeof(sender);

            recv_len n = ::recvfrom(
                _socket,
                buffer.data(),
                static_cast<int>(buffer.size()),
                0,
                reinterpret_cast<sockaddr*>(&sender),
                &senderLen
            );

            if (n <= 0) break;

            if (static_cast<std::size_t>(n) < sizeof(InputPacket)) continue;

            InputPacket pkt{};
            std::memcpy(&pkt, buffer.data(), sizeof(InputPacket));
            
            if (pkt.magic != INPUT_MAGIC || pkt.protocolVersion != PROTOCOL_VERSION) continue;

            std::size_t idx = findClient(sender);
            if (idx == MAX_DEFAULT_CLIENTS) {
                idx = findFreeSlot();
                if (idx == MAX_DEFAULT_CLIENTS) continue;
                
                _clients[idx].active = true;
                _clients[idx].addr = sender;
                _clients[idx].lastReceivedInput = 0;
                _clients[idx].lastProcessedInput = 0;
                _clients[idx].snapshotCounter = 0;
                if (_onNewClient) _onNewClient(idx, sender);
            }
            
            _clients[idx].lastReceivedInput = pkt.inputSequence;
            if (_onInput) _onInput(idx, pkt);
        }
    }

    void sendSnapshot(std::size_t slotIndex, std::uint32_t packedFrameData, std::uint32_t controlledId, const std::vector<SnapshotEntity>& ents) {
        if (slotIndex >= MAX_DEFAULT_CLIENTS || !_clients[slotIndex].active) return;

        SnapshotHeader hdr{};
        hdr.magic = SNAP_MAGIC;
        hdr.protocolVersion = PROTOCOL_VERSION;
        hdr.serverFrame = packedFrameData; 
        hdr.controlledId = controlledId;
        hdr.entityCount = static_cast<std::uint32_t>(ents.size());

        sendInternal(slotIndex, hdr, ents);
    }

    void setLastProcessedInput(std::size_t slotIndex, std::uint32_t seq) {
        if (slotIndex < MAX_DEFAULT_CLIENTS) _clients[slotIndex].lastProcessedInput = seq;
    }

    std::uint32_t getLastProcessedInput(std::size_t slotIndex) const {
        if (slotIndex < MAX_DEFAULT_CLIENTS && _clients[slotIndex].active) {
            return _clients[slotIndex].lastProcessedInput;
        }
        return 0;
    }

private:
    void sendInternal(std::size_t slotIndex, SnapshotHeader hdr, const std::vector<SnapshotEntity>& ents) {
        hdr.sequence = _clients[slotIndex].snapshotCounter++;

        std::vector<char> buffer;
        buffer.reserve(sizeof(SnapshotHeader) + ents.size() * sizeof(SnapshotEntity));

        const char* hPtr = reinterpret_cast<const char*>(&hdr);
        buffer.insert(buffer.end(), hPtr, hPtr + sizeof(SnapshotHeader));

        if (!ents.empty()) {
            const char* ePtr = reinterpret_cast<const char*>(ents.data());
            buffer.insert(buffer.end(), ePtr, ePtr + ents.size() * sizeof(SnapshotEntity));
        }

        ::sendto(_socket, buffer.data(), static_cast<int>(buffer.size()), 0,
                 reinterpret_cast<sockaddr*>(&_clients[slotIndex].addr), sizeof(_clients[slotIndex].addr));
    }

    std::size_t findClient(const sockaddr_in& addr) const {
        for (std::size_t i = 0; i < MAX_DEFAULT_CLIENTS; ++i) {
            if (_clients[i].active &&
                _clients[i].addr.sin_addr.s_addr == addr.sin_addr.s_addr &&
                _clients[i].addr.sin_port == addr.sin_port) {
                return i;
            }
        }
        return MAX_DEFAULT_CLIENTS;
    }

    std::size_t findFreeSlot() const {
        for (std::size_t i = 0; i < MAX_DEFAULT_CLIENTS; ++i) {
            if (!_clients[i].active) return i;
        }
        return MAX_DEFAULT_CLIENTS;
    }

    socket_handle    _socket{invalid_socket};
    NewClientCallback _onNewClient{};
    InputCallback     _onInput{};
    std::array<ClientSlot, MAX_DEFAULT_CLIENTS> _clients{};
};

class Client {
public:
    Client(const std::string& host, std::uint16_t port) {
#ifdef _WIN32
        static WSAInit _wsa_once{};
#endif
        _socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (_socket == invalid_socket) throw std::runtime_error("Failed to create client socket");

        socket_set_nonblocking(_socket);

        _serverAddr.sin_family = AF_INET;
        _serverAddr.sin_port   = htons(port);
        inet_pton(AF_INET, host.c_str(), &_serverAddr.sin_addr);
    }

    ~Client() {
        socket_close(_socket);
    }

    void sendInput(const InputPacket& pkt) {
        ::sendto(_socket, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
                 reinterpret_cast<sockaddr*>(&_serverAddr), sizeof(_serverAddr));
    }

    std::optional<SnapshotPacket> pollSnapshot() {
        constexpr std::size_t maxSize = sizeof(SnapshotHeader) + MAX_ENTITIES * sizeof(SnapshotEntity);
        std::array<char, maxSize> buffer{};
        
        std::optional<SnapshotPacket> latestSnapshot = std::nullopt;

        while (true) {
            sockaddr_in sender{};
            socklen_type senderLen = sizeof(sender);

            recv_len n = ::recvfrom(
                _socket,
                buffer.data(),
                static_cast<int>(buffer.size()),
                0,
                reinterpret_cast<sockaddr*>(&sender),
                &senderLen
            );

            if (n <= 0) break;

            if (sender.sin_addr.s_addr != _serverAddr.sin_addr.s_addr ||
                sender.sin_port != _serverAddr.sin_port) continue;

            if (static_cast<std::size_t>(n) < sizeof(SnapshotHeader)) continue;

            SnapshotHeader hdr{};
            std::memcpy(&hdr, buffer.data(), sizeof(SnapshotHeader));

            if (hdr.magic != SNAP_MAGIC || hdr.protocolVersion != PROTOCOL_VERSION) continue;
            if (hdr.entityCount > MAX_ENTITIES) continue;

            const std::size_t expectedSize = sizeof(SnapshotHeader) + hdr.entityCount * sizeof(SnapshotEntity);
            if (static_cast<std::size_t>(n) < expectedSize) continue;

            SnapshotPacket pkt{};
            pkt.header = hdr;
            pkt.entities.resize(hdr.entityCount);
            if (hdr.entityCount > 0) {
                std::memcpy(pkt.entities.data(),
                            buffer.data() + sizeof(SnapshotHeader),
                            hdr.entityCount * sizeof(SnapshotEntity));
            }

            latestSnapshot = std::move(pkt);
        }

        return latestSnapshot;
    }

private:
    socket_handle _socket{invalid_socket};
    sockaddr_in   _serverAddr{};
};

} // namespace net
