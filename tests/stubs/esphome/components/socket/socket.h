#pragma once
#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#endif
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <memory>
#include <deque>
#include <vector>
namespace fake {
extern int allocations, alive, attempts, connect_result, socket_error, ready, write_limit;
extern bool eof, write_blocked;
extern std::deque<std::vector<uint8_t>> reads;
extern std::vector<uint8_t> writes;
inline int select_socket(int, fd_set *, fd_set *, fd_set *, timeval *) { return ready; }
}
#define select fake::select_socket
namespace esphome { namespace socket {
class Socket {
 public:
  Socket() { ++fake::allocations; ++fake::alive; }
  ~Socket() { --fake::alive; }
  int setblocking(bool) { return 0; }
  int connect(const sockaddr *, socklen_t) {
    ++fake::attempts;
    errno = fake::connect_result == 1 ? EINPROGRESS : ECONNREFUSED;
    return fake::connect_result == 0 ? 0 : -1;
  }
  int get_fd() { return 1; }
  int getsockopt(int, int, void *out, socklen_t *) { *static_cast<int *>(out) = fake::socket_error; return 0; }
  int write(const void *data, size_t size) {
    if (fake::write_blocked) { errno = EAGAIN; return -1; }
    const size_t n = std::min(size, size_t(fake::write_limit));
    auto p = static_cast<const uint8_t *>(data);
    fake::writes.insert(fake::writes.end(), p, p+n);
    return n;
  }
  int read(void *data, size_t size) {
    if (fake::reads.empty()) { errno = EAGAIN; return fake::eof ? 0 : -1; }
    auto &chunk = fake::reads.front();
    const size_t n = std::min(size, chunk.size());
    std::memcpy(data, chunk.data(), n);
    chunk.erase(chunk.begin(), chunk.begin()+n);
    if (chunk.empty()) fake::reads.pop_front();
    return n;
  }
};
inline std::unique_ptr<Socket> socket(int, int, int) { return std::unique_ptr<Socket>(new Socket); }
inline socklen_t set_sockaddr(sockaddr *, size_t, const char *, uint16_t) { return sizeof(sockaddr); }
} }
