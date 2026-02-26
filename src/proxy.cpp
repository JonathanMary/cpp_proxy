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

#include <iostream>

namespace {
void print_address_info(const sockaddr_storage &addr) {
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
  /**
   * @todo Can abstract FD into its own class
   *       So we handle logic here, and RAII in FD class
   */
public:
  explicit Socket(const addrinfo *addr) {
    if (!addr) {
      std::cerr << "Error: addr is null." << std::endl;
    } else {
      socket_fd_ =
          socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
      if (socket_fd_ < 0) {
        std::cerr << "Error: socket creation failed." << std::endl;
      } else {
        int opt = 1;
        setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
      }
    }
  }

  ~Socket() noexcept {
    if (socket_fd_ >= 0) {
      close(socket_fd_);
    }
  }
  // No copy
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
  // Also no need for empty init and move--at least now
  Socket() = delete;
  Socket(Socket &&) = delete;
  Socket &operator=(Socket &&) = delete;

  int get() const noexcept { return socket_fd_; }

private:
  int socket_fd_{-1};
};

int main() {
  const char *PROXY_PORT = "8000";
  const char *PROXY_HOST = "0.0.0.0";
  const char *SERVER_PORT = "9000";
  const char *SERVER_HOST = "127.0.0.1";
  const int BUFFER_SIZE = 4096;

  std::array<char, BUFFER_SIZE> buffer;
  sockaddr_storage client_addr{};
  socklen_t client_addrlen;
  addrinfo hints{};
  addrinfo *endpoints;

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  int gai_res_endpoints =
      getaddrinfo(PROXY_HOST, PROXY_PORT, &hints, &endpoints);
  if (gai_res_endpoints != 0) {
    std::cerr << "error gai_res_endpoints: " << gai_strerror(gai_res_endpoints)
              << std::endl;
    return 1;
  }
  Socket endpoint(endpoints);
  if (endpoint.get() < 0) {
    return 1;
  }

  if (bind(endpoint.get(), endpoints->ai_addr, endpoints->ai_addrlen) != 0) {
    std::cerr << "Binding failed." << std::endl;
    return 1;
  }
  freeaddrinfo(endpoints);

  if (listen(endpoint.get(), SOMAXCONN) != 0) {
    std::cerr << " Listen failed." << std::endl;
    return 1;
  }
  std::cout << "Listening for connections on: " << PROXY_HOST << ", "
            << PROXY_PORT << std::endl;

  client_addrlen = sizeof(client_addr);

  for (;;) {
    // 1. accept client
    int client_sfd =
        accept(endpoint.get(), reinterpret_cast<sockaddr *>(&client_addr),
               &client_addrlen);
    if (client_sfd == -1) {
      std::cerr << "Accept client failed." << std::endl;
      return 1;
    }
    print_address_info(client_addr);

    addrinfo *server_addr;
    int gai_res_server =
        getaddrinfo(SERVER_HOST, SERVER_PORT, &hints, &server_addr);
    if (gai_res_server != 0) {
      std::cerr << "error gai_res_server 2: " << gai_strerror(gai_res_server)
                << std::endl;
      return 1;
    }

    Socket server(server_addr);
    if (server.get() < 0) {
      freeaddrinfo(server_addr);
      return 1;
    }

    // 2. connect upstream
    if (connect(server.get(), server_addr->ai_addr, server_addr->ai_addrlen) !=
        0) {
      std::cerr << "Connect server failed." << std::endl;
      freeaddrinfo(server_addr);
      return 1;
    }
    std::cout << "Connected to server." << std::endl;
    freeaddrinfo(server_addr);

    // 3. recv from client
    ssize_t client_msg_len =
        recv(client_sfd, buffer.data(), buffer.size() - 1, 0);
    if (client_msg_len < 0) {
      std::cerr << "recv client failed." << std::endl;
      return 1;
    }

    std::cout << "->*   " << client_msg_len << std::endl;

    // 4. send to server
    if (send(server.get(), buffer.data(), client_msg_len, 0) < 0) {
      std::cerr << "Send server failed." << std::endl;
      return 1;
    }
    std::cout << "  *-> " << client_msg_len << std::endl;

    ssize_t server_msg_len;
    while ((server_msg_len =
                recv(server.get(), buffer.data(), buffer.size() - 1, 0)) > 0) {
      // 5. recv until server closes
      std::cout << "  *<- " << server_msg_len << std::endl;

      // 6. forward to client
      if (send(client_sfd, buffer.data(), server_msg_len, 0) < 0) {
        std::cerr << "Send client failed." << std::endl;
        return 1;
      }
      std::cout << "<-*   " << server_msg_len << std::endl;
    }
    if (server_msg_len < 0) {
      std::cerr << "recv server failed." << std::endl;
      return 1;
    }
  }
  return 0;
}
