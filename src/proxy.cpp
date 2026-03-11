/**
 * HTTP Specs:
 *   - https://datatracker.ietf.org/doc/html/rfc1945 [RFC 1945]
 *   - https://datatracker.ietf.org/doc/html/rfc2616 [RFC 2616]
 * @todo Abstract most steps out of main
 */

#include "parser.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <iostream>
#include <string_view>

namespace {
void PrintAddressInfo(const sockaddr_storage &addr) {
  char client_ip[INET_ADDRSTRLEN];

  inet_ntop(addr.ss_family,
            &(reinterpret_cast<const sockaddr_in *>(&addr))->sin_addr,
            client_ip, sizeof(client_ip));
  uint16_t port =
      ntohs((reinterpret_cast<const sockaddr_in *>(&addr))->sin_port);

  std::cout << "New connection from: " << client_ip << ", " << port
            << std::endl;
}
} // namespace

class Socket {
public:
  Socket() noexcept = default;

  explicit Socket(int fd) noexcept : socket_fd_(fd) {}

  ~Socket() noexcept {
    if (socket_fd_ >= 0) {
      close(socket_fd_);
    }
  }

  Socket(Socket &&other) noexcept
      : socket_fd_(std::exchange(other.socket_fd_, -1)) {}

  Socket &operator=(Socket &&other) noexcept {
    if (this != &other) {
      if (socket_fd_ >= 0) {
        close(socket_fd_);
      }
      socket_fd_ = std::exchange(other.socket_fd_, -1);
    }
    return *this;
  }

  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;

  int fd() const noexcept { return socket_fd_; }

private:
  int socket_fd_{-1};
};

int main() {
  const char *kProxyPort = "8000";
  const char *kProxyHost = "0.0.0.0";
  const char *kServerPort = "9000";
  const char *kServerHost = "127.0.0.1";
  const int kBufferSize = 4096;

  std::array<char, kBufferSize> buffer;
  sockaddr_storage client_addr{};
  addrinfo hints{};
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> endpoints(nullptr,
                                                               freeaddrinfo);

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  addrinfo *raw = nullptr;
  int gai_res_endpoints = getaddrinfo(kProxyHost, kProxyPort, &hints, &raw);
  if (gai_res_endpoints != 0) {
    std::cerr << "error gai_res_endpoints: " << gai_strerror(gai_res_endpoints)
              << std::endl;
    return 1;
  }
  endpoints.reset(raw);
  Socket endpoint_socket(socket(endpoints->ai_family, endpoints->ai_socktype,
                                endpoints->ai_protocol));
  if (endpoint_socket.fd() < 0) {
    std::cerr << "Error: socket creation failed." << std::endl;
    return 1;
  }
  int opt = 1;
  if (setsockopt(endpoint_socket.fd(), SOL_SOCKET, SO_REUSEADDR, &opt,
                 sizeof(opt)) < 0) {
    std::cerr << "Set socket option failed. " << errno << std::endl;
    return 1;
  }

  if (bind(endpoint_socket.fd(), endpoints->ai_addr, endpoints->ai_addrlen) !=
      0) {
    std::cerr << "Binding failed." << std::endl;
    return 1;
  }

  if (listen(endpoint_socket.fd(), SOMAXCONN) != 0) {
    std::cerr << " Listen failed." << std::endl;
    return 1;
  }
  std::cout << "Listening for connections on: " << kProxyHost << ", "
            << kProxyPort << std::endl;

  for (;;) {
    socklen_t client_addrlen = sizeof(client_addr);
    // 1. accept client
    Socket client_socket(accept(endpoint_socket.fd(),
                                reinterpret_cast<sockaddr *>(&client_addr),
                                &client_addrlen));
    if (client_socket.fd() == -1) {
      std::cerr << "Accept client failed." << std::endl;
      continue;
    }
    assert(client_addr.ss_family == AF_INET);

    PrintAddressInfo(client_addr);

    // 2. connect upstream
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> server_addr(
        nullptr, freeaddrinfo);
    addrinfo *raw;

    int gai_res_server = getaddrinfo(kServerHost, kServerPort, &hints, &raw);
    if (gai_res_server != 0) {
      std::cerr << "error gai_res_server 2: " << gai_strerror(gai_res_server)
                << std::endl;
      continue;
    }
    server_addr.reset(raw);

    Socket server_socket(socket(server_addr->ai_family,
                                server_addr->ai_socktype,
                                server_addr->ai_protocol));
    if (server_socket.fd() < 0) {
      continue;
    }

    if (connect(server_socket.fd(), server_addr->ai_addr,
                server_addr->ai_addrlen) != 0) {
      std::cerr << "Connect server_socket failed." << std::endl;
      continue;
    }
    std::cout << "Connected to server_socket." << std::endl;

    // 3. recv from client
    ssize_t client_msg_len =
        recv(client_socket.fd(), buffer.data(), buffer.size() - 1, 0);
    if (client_msg_len < 0) {
      std::cerr << "recv client failed." << std::endl;
      continue;
    }
    std::cout << "->*   " << client_msg_len << std::endl;

    // 4. send to server_socket
    if (send(server_socket.fd(), buffer.data(), client_msg_len, 0) < 0) {
      std::cerr << "Send server_socket failed." << std::endl;
      continue;
    }
    std::cout << "  *-> " << client_msg_len << std::endl;

    ssize_t server_msg_len;
    HttpRes res;
    while ((server_msg_len = recv(server_socket.fd(), buffer.data(),
                                  buffer.size() - 1, 0)) > 0) {
      if (server_msg_len < 0) {
        std::cerr << "recv server_socket failed." << std::endl;
        continue;
      }
      // 5. recv until server_socket closes
      std::cout << "  *<- " << server_msg_len << std::endl;
      res.Parser(std::string_view(buffer.data(), server_msg_len));
      if (res.StatusCode() == "200") {
        auto persistent_res = res.Response();

        // 6. forward to client
        if (send(client_socket.fd(), persistent_res.data(), persistent_res.size(), 0) < 0) {
          std::cerr << "Send client failed." << std::endl;
          continue;
        }
      } else {
        if (send(client_socket.fd(), buffer.data(), server_msg_len, 0) < 0) {
          std::cerr << "Send client failed." << std::endl;
          continue;
        }
      }
      std::cout << "<-*   " << server_msg_len << std::endl;
      res.Clear();
    }
  }
  return 0;
}
