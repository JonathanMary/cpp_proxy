/**
 * HTTP Specs:
 *   - https://datatracker.ietf.org/doc/html/rfc1945 [RFC 1945]
 *   - https://datatracker.ietf.org/doc/html/rfc2616 [RFC 2616]
 * @todo Handle partial send/recv
 * @todo Abstract most steps out of main
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <iostream>

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

  int Get() const noexcept { return socket_fd_; }

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
  Socket endpoint_fd(socket(endpoints->ai_family, endpoints->ai_socktype,
                            endpoints->ai_protocol));
  if (endpoint_fd.Get() < 0) {
    std::cerr << "Error: socket creation failed." << std::endl;
    return 1;
  }
  int opt = 1;
  if (setsockopt(endpoint_fd.Get(), SOL_SOCKET, SO_REUSEADDR, &opt,
                 sizeof(opt)) < 0) {
    std::cerr << "Set socket option failed. " << errno << std::endl;
    return 1;
  }

  if (bind(endpoint_fd.Get(), endpoints->ai_addr, endpoints->ai_addrlen) != 0) {
    std::cerr << "Binding failed." << std::endl;
    return 1;
  }

  if (listen(endpoint_fd.Get(), SOMAXCONN) != 0) {
    std::cerr << " Listen failed." << std::endl;
    return 1;
  }
  std::cout << "Listening for connections on: " << kProxyHost << ", "
            << kProxyPort << std::endl;

  for (;;) {
    socklen_t client_addrlen = sizeof(client_addr);
    // 1. accept client
    Socket client_sfd(accept(endpoint_fd.Get(),
                             reinterpret_cast<sockaddr *>(&client_addr),
                             &client_addrlen));
    if (client_sfd.Get() == -1) {
      std::cerr << "Accept client failed." << std::endl;
      continue;
    }
    assert(client_addr.ss_family == AF_INET);

    PrintAddressInfo(client_addr);

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

    Socket server(socket(server_addr->ai_family, server_addr->ai_socktype,
                         server_addr->ai_protocol));
    if (server.Get() < 0) {
      continue;
    }

    // 2. connect upstream
    if (connect(server.Get(), server_addr->ai_addr, server_addr->ai_addrlen) !=
        0) {
      std::cerr << "Connect server failed." << std::endl;
      continue;
    }
    std::cout << "Connected to server." << std::endl;

    // 3. recv from client
    ssize_t client_msg_len =
        recv(client_sfd.Get(), buffer.data(), buffer.size() - 1, 0);
    if (client_msg_len < 0) {
      std::cerr << "recv client failed." << std::endl;
      continue;
    }
    std::cout << "->*   " << client_msg_len << std::endl;

    // 4. send to server
    if (send(server.Get(), buffer.data(), client_msg_len, 0) < 0) {
      std::cerr << "Send server failed." << std::endl;
      continue;
    }
    std::cout << "  *-> " << client_msg_len << std::endl;

    ssize_t server_msg_len;
    while ((server_msg_len =
                recv(server.Get(), buffer.data(), buffer.size() - 1, 0)) > 0) {
      // 5. recv until server closes
      std::cout << buffer.data() << std::endl;
      std::cout << "  *<- " << server_msg_len << std::endl;

      // 6. forward to client
      if (send(client_sfd.Get(), buffer.data(), server_msg_len, 0) < 0) {
        std::cerr << "Send client failed." << std::endl;
        continue;
      }
      std::cout << "<-*   " << server_msg_len << std::endl;
    }
    if (server_msg_len < 0) {
      std::cerr << "recv server failed." << std::endl;
      continue;
    }
  }
  return 0;
}
