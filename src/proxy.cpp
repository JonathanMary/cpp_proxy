/**
 * HTTP Specs:
 *   - https://datatracker.ietf.org/doc/html/rfc1945 [RFC 1945]
 *   - https://datatracker.ietf.org/doc/html/rfc2616 [RFC 2616]
 * @todo Abstract most steps out of main
 */

#include "parser.h"

#include <arpa/inet.h>
#include <cstdint>
#include <netdb.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <iostream>
#include <string_view>
#include <unordered_map>

namespace {
const char *kProxyPort = "8000";
const char *kProxyHost = "0.0.0.0";
const char *kServerPort = "9000";
const char *kServerHost = "127.0.0.1";
const int kMaxEvents = 64;

const int kBufferSize = 4096;
std::array<char, kBufferSize> buffer;

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

void safe_setblocking_false(int socket_descriptor) {
  int flags = fcntl(socket_descriptor, F_GETFL, 0);
  if (flags == -1) {
    std::cerr << "Error getting listen socket flags" << std::endl;
  }
  if (fcntl(socket_descriptor, F_SETFL, flags | O_NONBLOCK) == -1) {
    std::cerr << "Error setting listen socket flags" << std::endl;
    return;
  }
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

class ConnectionContext {
public:
  bool keep_alive;
  Socket server_socket;
};


int main() {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;

  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> endpoints(nullptr,
                                                               freeaddrinfo);

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
  safe_setblocking_false(endpoint_socket.fd());

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

  int kq = kqueue();
  if (kq == -1) {
    std::cerr << "error: invalid fd in queue" << std::endl;
    return 1;
  }

  struct kevent event_store[kMaxEvents];
  // listen to endpoint_socket_descriptor
  EV_SET(event_store, endpoint_socket.fd(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
  if (kevent(kq, event_store, 1, nullptr, 0, nullptr) == -1) {
    std::cerr << "error: ret initialization" << std::endl;
    return 1;
  }
  std::unordered_map<int, Socket> client_list;
  std::unordered_map<int, ConnectionContext> server_list;

  for (;;) {
    int nevents = kevent(kq, nullptr, 0, event_store, kMaxEvents, nullptr);
    if (nevents < 0) {
      std::cerr << "Error getting events: " << errno << std::endl;
      continue;
    }

    for (int i = 0; i < nevents; i++) {
      if (event_store[i].flags & EV_ERROR) {
        std::cerr << event_store[i].data << std::endl;
        continue;
      }
      if (event_store[i].filter == EVFILT_READ) { // Reads
        if (event_store[i].ident == endpoint_socket.fd()) {
          // 1. accept client
          sockaddr_storage client_addr{};
          socklen_t client_addrlen = sizeof(client_addr);
          int fd = accept(event_store[i].ident, reinterpret_cast<sockaddr *>(&client_addr), &client_addrlen);
          if (fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { continue ;}
            std::cerr << "Accept client failed." << std::endl;
            continue;
          }
          assert(client_addr.ss_family == AF_INET);

          safe_setblocking_false(fd);
          client_list.emplace(fd, Socket(fd));

          PrintAddressInfo(client_addr);

          struct kevent change;
          EV_SET(&change, fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
          kevent(kq, &change, 1, nullptr, 0, nullptr);
        } else {
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

          int fd = socket(server_addr->ai_family,
                                      server_addr->ai_socktype,
                                      server_addr->ai_protocol);
          if (fd < 0) {
            continue;
          }

          if (connect(fd, server_addr->ai_addr,
                      server_addr->ai_addrlen) != 0) {
            std::cerr << "Connect server_socket failed." << std::endl;
            continue;
          }
          std::cout << "Connected to server_socket." << std::endl;

          Http req;
          bool client_closed = false;
          // 3. recv from client
          while (req.GetState() != ParseState::kEnd) {
            ssize_t client_msg_len =
                recv(event_store[i].ident, buffer.data(), buffer.size(), 0);
            if (client_msg_len < 0) {
              std::cerr << "recv client failed." << std::endl;
              break;
            }
            std::cout << "->*   " << client_msg_len << std::endl;

            if (client_msg_len == 0) {
              client_closed = true;
              break;
            }
            req.Parser(std::string_view(buffer.data(), client_msg_len));

            // 4. send to server_socket
            if (send(fd, req.Response().data(), req.Response().size(),
                    0) < 0) {
              std::cerr << "Send server_socket failed." << std::endl;
              continue;
            }
            std::cout << "  *-> " << req.Response().size() << std::endl;
            req.ClearMessage();
          }

          if (client_closed) {
            close(fd);

            struct kevent change;
            EV_SET(&change, event_store[i].ident, EVFILT_READ, EV_DELETE, 0, 0,
                   nullptr);
            kevent(kq, &change, 1, nullptr, 0, nullptr);

            client_list.erase(event_store[i].ident);
            server_list.erase(event_store[i].ident);
            continue;
          }

          if (server_list.find(event_store[i].ident) != server_list.end()) {
            server_list.erase(event_store[i].ident);
          }
          server_list.emplace(event_store[i].ident, ConnectionContext{ req.KeepAlive(), Socket(fd) });
          
          struct kevent change;
          EV_SET(&change, event_store[i].ident, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
          kevent(kq, &change, 1, nullptr, 0, nullptr);
        }
      } else if (event_store[i].filter == EVFILT_WRITE) {// Writes
        auto client_socket_descriptor = event_store[i].ident;

        ssize_t server_msg_len;
        Http res;
        // 5. recv from server until full response is parsed
        for (;;) {
          server_msg_len =
              recv(server_list[event_store[i].ident].server_socket.fd(), buffer.data(), buffer.size(), 0);
          std::cout << "  *<- " << server_msg_len << std::endl;
          if (server_msg_len < 1) {
            break;
          }
          res.Parser(std::string_view(buffer.data(), server_msg_len));

          // 6. forward parsed chunk to client
          if (send(client_socket_descriptor, res.Response().data(), res.Response().size(),
                  0) < 0) {
            std::cerr << "Send client failed." << std::endl;
            break;
          }
          std::cout << "<-*   " << res.Response().size() << std::endl;
          res.ClearMessage();

          struct kevent del;
          EV_SET(&del, client_socket_descriptor, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
          kevent(kq, &del, 1, nullptr, 0, nullptr);
        }

        struct kevent change;
        if (!server_list[event_store[i].ident].keep_alive) {
          EV_SET(&change, client_socket_descriptor, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
          kevent(kq, &change, 1, nullptr, 0, nullptr);
          client_list.erase(client_socket_descriptor);
          server_list.erase(client_socket_descriptor);
        } else {
          EV_SET(&change, client_socket_descriptor, EVFILT_READ, EV_ADD, 0, 0, nullptr);
          kevent(kq, &change, 1, nullptr, 0, nullptr);
        }
      }
    }
  }
  return 0;
}
