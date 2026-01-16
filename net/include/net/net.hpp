//===-------------------------------------------------------------------------------===
// net.hpp
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>

  // MinGW: link with ws2_32 (CMake: target_link_libraries(... ws2_32))
  using socket_handle = SOCKET;
  static constexpr socket_handle invalid_socket = INVALID_SOCKET;

  inline void socket_close(socket_handle s) {
      if (s != invalid_socket) {
          ::closesocket(s);
      }
  }

  inline void socket_set_nonblocking(socket_handle s) {
      u_long mode = 1;
      if (::ioctlsocket(s, FIONBIO, &mode) != 0) {
          throw std::runtime_error("Failed to set socket non-blocking (ioctlsocket)");
      }
  }

  // Winsock recvfrom returns int
  using recv_len = int;
  using socklen_type = int;

  // One-time WSA init helper (safe to call multiple times if you keep one instance)
  struct WSAInit {
      WSAInit() {
          WSADATA wsa{};
          if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
              throw std::runtime_error("WSAStartup failed");
          }
      }
      ~WSAInit() { ::WSACleanup(); }
  };

#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>

  using socket_handle = int;
  static constexpr socket_handle invalid_socket = -1;

  inline void socket_close(socket_handle s) {
      if (s != invalid_socket) {
          ::close(s);
      }
  }

  inline void socket_set_nonblocking(socket_handle s) {
      int flags = ::fcntl(s, F_GETFL, 0);
      if (flags < 0) {
          throw std::runtime_error("fcntl(F_GETFL) failed");
      }
      if (::fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
          throw std::runtime_error("fcntl(F_SETFL) failed");
      }
  }

  using recv_len = ssize_t;
  using socklen_type = socklen_t;
#endif

namespace net {

// ... (tes constantes/structs inchangés)

// -----------------------------------------------------------------------------
// Classe Server
// -----------------------------------------------------------------------------
class Server {
public:
    using NewClientCallback = std::function<void(std::size_t, const sockaddr_in&)>;
    using InputCallback = std::function<void(std::size_t, const InputPacket&)>;

    explicit Server(std::uint16_t port) {
#ifdef _WIN32
        // Ensure WSA initialized before creating sockets
        static WSAInit _wsa_once{};
#endif
        _socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (_socket == invalid_socket) {
            throw std::runtime_error("Failed to create server socket");
        }

        // non-blocking
        socket_set_nonblocking(_socket);

        // bind
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (::bind(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            socket_close(_socket);
            _socket = invalid_socket;
            throw std::runtime_error("Failed to bind server socket");
        }
    }

    ~Server() {
        socket_close(_socket);
        _socket = invalid_socket;
    }

    void setCallbacks(NewClientCallback onNewClient, InputCallback onInput) {
        _onNewClient = std::move(onNewClient);
        _onInput     = std::move(onInput);
    }

    void pollInputs() {
        std::array<char, 256> buffer{};
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

            if (n < 0) {
#ifdef _WIN32
                // WSAEWOULDBLOCK => no more data
                const int err = ::WSAGetLastError();
                if (err == WSAEWOULDBLOCK) break;
#else
                // EWOULDBLOCK/EAGAIN => no more data
                break;
#endif
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

            std::size_t idx = findClient(sender);
            if (idx == MAX_DEFAULT_CLIENTS) {
                idx = findFreeSlot();
                if (idx == MAX_DEFAULT_CLIENTS) {
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

            _clients[idx].lastReceivedInput = pkt.inputSequence;
            if (_onInput) {
                _onInput(idx, pkt);
            }
            _clients[idx].lastProcessedInput = pkt.inputSequence;
        }
    }

    void sendSnapshot(std::size_t slotIndex,
                      std::uint32_t serverFrame,
                      std::uint32_t controlledId,
                      const std::vector<SnapshotEntity>& entities) {
        if (slotIndex >= MAX_DEFAULT_CLIENTS) return;
        if (!_clients[slotIndex].active) return;

        std::uint32_t count = static_cast<std::uint32_t>(entities.size());
        if (count > MAX_ENTITIES) count = MAX_ENTITIES;

        SnapshotHeader hdr{};
        hdr.magic              = SNAP_MAGIC;
        hdr.protocolVersion    = PROTOCOL_VERSION;
        hdr.snapshotId         = _clients[slotIndex].snapshotCounter++;
        hdr.serverFrame        = serverFrame;
        hdr.lastProcessedInput = _clients[slotIndex].lastProcessedInput;
        hdr.controlledId       = controlledId;
        hdr.entityCount        = count;
        hdr.reserved           = 0;

        std::size_t totalSize = sizeof(SnapshotHeader) + count * sizeof(SnapshotEntity);
        std::vector<char> buffer(totalSize);
        std::memcpy(buffer.data(), &hdr, sizeof(SnapshotHeader));
        if (count > 0) {
            std::memcpy(buffer.data() + sizeof(SnapshotHeader), entities.data(),
                        count * sizeof(SnapshotEntity));
        }

        ::sendto(_socket,
                 buffer.data(),
                 static_cast<int>(buffer.size()),
                 0,
                 reinterpret_cast<sockaddr*>(&_clients[slotIndex].endpoint),
                 sizeof(sockaddr_in));
    }

    void setLastProcessedInput(std::size_t slotIndex, std::uint32_t seq) {
        if (slotIndex < MAX_DEFAULT_CLIENTS && _clients[slotIndex].active) {
            _clients[slotIndex].lastProcessedInput = seq;
        }
    }

private:
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

    std::size_t findFreeSlot() const {
        for (std::size_t i = 0; i < MAX_DEFAULT_CLIENTS; ++i) {
            if (!_clients[i].active) {
                return i;
            }
        }
        return MAX_DEFAULT_CLIENTS;
    }

    socket_handle _socket{invalid_socket};
    std::array<ClientSlot, MAX_DEFAULT_CLIENTS> _clients{};
    NewClientCallback _onNewClient;
    InputCallback _onInput;
};

// -----------------------------------------------------------------------------
// Classe Client
// -----------------------------------------------------------------------------
class Client {
public:
    Client(const std::string& host, std::uint16_t port) {
#ifdef _WIN32
        static WSAInit _wsa_once{};
#endif
        _socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (_socket == invalid_socket) {
            throw std::runtime_error("Failed to create client socket");
        }

        socket_set_nonblocking(_socket);

        std::memset(&_server, 0, sizeof(_server));
        _server.sin_family = AF_INET;
        _server.sin_port   = htons(port);
        if (::inet_pton(AF_INET, host.c_str(), &_server.sin_addr) <= 0) {
            socket_close(_socket);
            _socket = invalid_socket;
            throw std::runtime_error("Invalid server address");
        }
    }

    ~Client() {
        socket_close(_socket);
        _socket = invalid_socket;
    }

    void sendInput(const InputPacket& pkt) {
        InputPacket tmp = pkt;
        tmp.magic = INPUT_MAGIC;
        tmp.protocolVersion = PROTOCOL_VERSION;
        ::sendto(_socket,
                 reinterpret_cast<const char*>(&tmp),
                 static_cast<int>(sizeof(tmp)),
                 0,
                 reinterpret_cast<sockaddr*>(&_server),
                 sizeof(_server));
    }

    std::optional<SnapshotPacket> pollSnapshot() {
        constexpr std::size_t maxSize = sizeof(SnapshotHeader) + MAX_ENTITIES * sizeof(SnapshotEntity);
        std::array<char, maxSize> buffer{};

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

        if (n <= 0) {
#ifdef _WIN32
            const int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK) return std::nullopt;
#else
            return std::nullopt;
#endif
            return std::nullopt;
        }

        if (static_cast<std::size_t>(n) < sizeof(SnapshotHeader)) {
            return std::nullopt;
        }

        SnapshotHeader hdr{};
        std::memcpy(&hdr, buffer.data(), sizeof(SnapshotHeader));
        if (hdr.magic != SNAP_MAGIC || hdr.protocolVersion != PROTOCOL_VERSION) {
            return std::nullopt;
        }

        std::size_t expectedSize = sizeof(SnapshotHeader) + hdr.entityCount * sizeof(SnapshotEntity);
        if (static_cast<std::size_t>(n) < expectedSize) {
            return std::nullopt;
        }

        SnapshotPacket pkt{};
        pkt.header = hdr;
        pkt.entities.resize(hdr.entityCount);
        if (hdr.entityCount > 0) {
            std::memcpy(pkt.entities.data(),
                        buffer.data() + sizeof(SnapshotHeader),
                        hdr.entityCount * sizeof(SnapshotEntity));
        }
        return pkt;
    }

private:
    socket_handle _socket{invalid_socket};
    sockaddr_in   _server{};
};

} // namespace net
