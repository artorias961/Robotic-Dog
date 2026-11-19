#pragma once
// Thin RAII UDP wrapper, Windows + POSIX. Blocking recv with a short timeout
// so the RX jthread can notice its stop_token.
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")  // MSVC auto-link; GCC links via -lws2_32
  #endif
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace quad::net {

#ifdef _WIN32
using SockHandle = SOCKET;
inline constexpr SockHandle kBadSock = INVALID_SOCKET;
#else
using SockHandle = int;
inline constexpr SockHandle kBadSock = -1;
#endif

struct Endpoint {
  sockaddr_in addr{};
  static Endpoint make(std::string_view ip, std::uint16_t port) {
    Endpoint e;
    e.addr.sin_family = AF_INET;
    e.addr.sin_port = htons(port);
    if (inet_pton(AF_INET, std::string(ip).c_str(), &e.addr.sin_addr) != 1)
      throw std::runtime_error("bad IP: " + std::string(ip));
    return e;
  }
  std::string ip() const {
    char buf[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, const_cast<in_addr*>(&addr.sin_addr), buf, sizeof(buf));
    return buf;
  }
};

class UdpSocket {
 public:
  explicit UdpSocket(std::uint16_t bind_port = 0) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw std::runtime_error("WSAStartup failed");
#endif
    sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == kBadSock) throw std::runtime_error("socket() failed");

    if (bind_port != 0) {
      sockaddr_in local{};
      local.sin_family = AF_INET;
      local.sin_addr.s_addr = INADDR_ANY;
      local.sin_port = htons(bind_port);
      if (::bind(sock_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
        throw std::runtime_error("bind() failed on port " + std::to_string(bind_port));
    }
    set_recv_timeout_ms(50);  // lets the RX thread poll its stop_token
  }

  ~UdpSocket() {
#ifdef _WIN32
    if (sock_ != kBadSock) ::closesocket(sock_);
    WSACleanup();
#else
    if (sock_ != kBadSock) ::close(sock_);
#endif
  }
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  void send_to(const Endpoint& to, std::span<const std::uint8_t> data) const {
    ::sendto(sock_, reinterpret_cast<const char*>(data.data()),
             static_cast<int>(data.size()), 0,
             reinterpret_cast<const sockaddr*>(&to.addr), sizeof(to.addr));
  }

  // One datagram, or nullopt on timeout. `from` reports the sender.
  std::optional<std::vector<std::uint8_t>> recv(Endpoint& from) const {
    std::uint8_t buf[512];
    socklen_t len = sizeof(from.addr);
    const auto n = ::recvfrom(sock_, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                              reinterpret_cast<sockaddr*>(&from.addr), &len);
    if (n <= 0) return std::nullopt;
    return std::vector<std::uint8_t>(buf, buf + n);
  }

 private:
  void set_recv_timeout_ms(int ms) const {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
  }

  SockHandle sock_{kBadSock};
};

}  // namespace quad::net
