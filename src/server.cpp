#include <arpa/inet.h>
#include <netdb.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/event.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {
constexpr char kServerHost[] = "0.0.0.0";
constexpr char kServerPort[] = "9000";
constexpr int kMaxEvents = 64;
constexpr std::size_t kBufferSize = 4096;

class Socket {
public:
  Socket() noexcept = default;

  explicit Socket(int fd) noexcept : fd_(fd) {}

  ~Socket() noexcept {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  Socket(Socket &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

  Socket &operator=(Socket &&other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) {
        close(fd_);
      }
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;

  int fd() const noexcept { return fd_; }

private:
  int fd_{-1};
};

void PrintPeer(const sockaddr_storage &peer) {
  const auto *addr = reinterpret_cast<const sockaddr_in *>(&peer);
  char ip[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
  std::cout << "Accepted client: " << ip << ":" << ntohs(addr->sin_port)
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

int main() {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  addrinfo *raw = nullptr;

  const int gai_res = getaddrinfo(kServerHost, kServerPort, &hints, &raw);
  if (gai_res != 0) {
    std::cerr << "getaddrinfo failed: " << gai_strerror(gai_res) << std::endl;
    return -1;
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> endpoint(raw,
                                                              freeaddrinfo);

  Socket listen_socket(socket(endpoint->ai_family, endpoint->ai_socktype,
                              endpoint->ai_protocol));
  if (listen_socket.fd() < 0) {
    std::cerr << "socket failed: " << std::strerror(errno) << std::endl;
    return -1;
  }

  int opt = 1;
  if (setsockopt(listen_socket.fd(), SOL_SOCKET, SO_REUSEADDR, &opt,
                 sizeof(opt)) != 0) {
    std::cerr << "setsockopt failed: " << std::strerror(errno) << std::endl;
    return -1;
  }

  safe_setblocking_false(listen_socket.fd());

  if (bind(listen_socket.fd(), endpoint->ai_addr, endpoint->ai_addrlen) != 0) {
    std::cerr << "bind failed: " << std::strerror(errno) << std::endl;
    return -1;
  }

  if (listen(listen_socket.fd(), SOMAXCONN) != 0) {
    std::cerr << "listen failed: " << std::strerror(errno) << std::endl;
    return -1;
  }

  std::cout << "Simple server listening on " << kServerHost << ":"
            << kServerPort << std::endl;

  int kq = kqueue();
  if (kq == -1) {
    std::cerr << "error: invalid fd in queue" << std::endl;
    return 1;
  }

  // struct kevent event;
  struct kevent events[kMaxEvents];
  EV_SET(events, listen_socket.fd(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
  if (kevent(kq, events, 1, nullptr, 0, nullptr) == -1) {
    std::cerr << "error: ret initialization" << std::endl;
    return 1;
  }

  std::array<char, kBufferSize> buffer{};
  std::unordered_map<int, Socket> clients;

  for (;;) {
    int ret = kevent(kq, nullptr, 0, events, kMaxEvents, nullptr);
    if (ret < 0) {
      std::cerr << "error" << errno << std::endl;
      continue;
    }

    for (int i = 0; i < ret; i++) {
      if (events[i].flags & EV_ERROR) {
        std::cerr << events[i].data << std::endl;
      }
      // Readable
      if (events[i].filter == EVFILT_READ) {
        // do read
        if (events[i].ident == listen_socket.fd()) {
          sockaddr_storage client_addr{};
          socklen_t client_len = sizeof(client_addr);
          int acc_res =
              accept(events[i].ident,
                     reinterpret_cast<sockaddr *>(&client_addr), &client_len);
          if (acc_res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              continue;
            }
            return -1;
          }
          safe_setblocking_false(acc_res);
          clients.emplace(acc_res, Socket(acc_res));

          PrintPeer(client_addr);

          struct kevent change;
          EV_SET(&change, acc_res, EVFILT_READ, EV_ADD, 0, 0, nullptr);
          kevent(kq, &change, 1, nullptr, 0, 0);
        } else {
          const ssize_t n =
              recv(events[i].ident, buffer.data(), buffer.size(), 0);
          if (n <= 0) {
            std::cerr << "recv failed or client closed: " << n << std::endl;

            struct kevent del;
            EV_SET(&del, events[i].ident, EVFILT_READ, EV_DELETE, 0, 0,
                   nullptr);
            kevent(kq, &del, 1, nullptr, 0, nullptr);
            clients.erase(events[i].ident);
            continue;
          }

          struct kevent change;
          EV_SET(&change, events[i].ident, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
          kevent(kq, &change, 1, nullptr, 0, 0);
        } // end read
      } else if (events[i].filter == EVFILT_WRITE) {
        // do write
        static constexpr std::string_view kBody = "Hello from simple server\n";
        const std::string response = "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: text/plain\r\n"
                                     "Content-Length: " +
                                     std::to_string(kBody.size()) +
                                     "\r\n"
                                     "Connection: close\r\n"
                                     "\r\n" +
                                     std::string(kBody);

        if (send(events[i].ident, response.data(), response.size(), 0) < 0) {
          std::cerr << "send failed: " << std::strerror(errno) << std::endl;
        }
        struct kevent del;
        EV_SET(&del, events[i].ident, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(kq, &del, 1, nullptr, 0, nullptr);
        clients.erase(events[i].ident);
      }
    }
  }
}
