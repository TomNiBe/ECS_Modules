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

inline constexpr std::uint32_t MAGIC_JOIN = 0x4A4F494Eu;
inline constexpr std::uint32_t MAGIC_INPUT = 0x49505431u;
inline constexpr std::uint32_t MAGIC_SNAP  = 0x534E5031u;

#pragma pack(push, 1)

struct JoinLobbyPacket {
    std::uint32_t magic{MAGIC_JOIN};
    char lobbyCode[8]{0};
};

struct JoinResponsePacket {
    std::uint32_t magic{MAGIC_JOIN};
    std::uint8_t accepted{0};
    std::uint8_t assignedSlot{0};
    std::uint16_t _pad{0};
};

struct InputPacket {
    std::uint32_t magic{MAGIC_INPUT};
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
    std::uint32_t magic{MAGIC_SNAP};
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

struct RawPacket {
    sockaddr_in sender;
    std::vector<char> data;
};

class Server {
public:
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
            socket_close(_socket);
            throw std::runtime_error("Failed to bind server socket");
        }
    }

    ~Server() {
        socket_close(_socket);
    }

    std::vector<RawPacket> receiveAll() {
        std::vector<RawPacket> packets;
        while (true) {
            RawPacket pkt;
            pkt.data.resize(2048);
            sockaddr_in sender{};
            socklen_type senderLen = sizeof(sender);

            recv_len n = ::recvfrom(
                _socket,
                pkt.data.data(),
                static_cast<int>(pkt.data.size()),
                0,
                reinterpret_cast<sockaddr*>(&sender),
                &senderLen
            );

            if (n <= 0) break;

            pkt.data.resize(n);
            pkt.sender = sender;
            packets.push_back(std::move(pkt));
        }
        return packets;
    }

    void sendTo(const sockaddr_in& target, const void* data, std::size_t size) {
        ::sendto(_socket,
                 static_cast<const char*>(data),
                 static_cast<int>(size),
                 0,
                 reinterpret_cast<const sockaddr*>(&target),
                 sizeof(target));
    }

private:
    socket_handle _socket{invalid_socket};
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

    void sendJoin(const std::string& lobbyCode) {
        JoinLobbyPacket pkt{};
        std::strncpy(pkt.lobbyCode, lobbyCode.c_str(), 7);
        sendRaw(&pkt, sizeof(pkt));
    }

    void sendInput(const InputPacket& pkt) {
        sendRaw(&pkt, sizeof(pkt));
    }

    std::optional<SnapshotPacket> pollSnapshot() {
        constexpr std::size_t bufSize = sizeof(SnapshotHeader) + MAX_ENTITIES * sizeof(SnapshotEntity) + 128;
        std::array<char, bufSize> buffer{};

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

            if (hdr.magic != MAGIC_SNAP) continue;
            if (hdr.protocolVersion != PROTOCOL_VERSION) continue;
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

    std::optional<JoinResponsePacket> pollJoinResponse() {
        JoinResponsePacket resp{};
        std::array<char, 128> buffer{};
        sockaddr_in sender{};
        socklen_type slen = sizeof(sender);

        recv_len n = ::recvfrom(_socket, buffer.data(), (int)buffer.size(), 0, (sockaddr*)&sender, &slen);
        if (n > 0) {
            if (n >= sizeof(JoinResponsePacket)) {
                 std::memcpy(&resp, buffer.data(), sizeof(resp));
                 if (resp.magic == MAGIC_JOIN) return resp;
            }
        }
        return std::nullopt;
    }

private:
    void sendRaw(const void* data, std::size_t size) {
        ::sendto(_socket,
                 static_cast<const char*>(data),
                 static_cast<int>(size),
                 0,
                 reinterpret_cast<sockaddr*>(&_serverAddr),
                 sizeof(_serverAddr));
    }

    socket_handle _socket{invalid_socket};
    sockaddr_in   _serverAddr{};
};

}
