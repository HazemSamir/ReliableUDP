#ifndef UDP-UTIL_H
#define UDP-UTIL_H

#include <arpa/inet.h>

namespace udp_util {

struct addr {
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
};

struct udpsocket {
    int fd;
    addr myaddr;
};

bool randrop(double plp = 0.0, double seed = -1.0);

udpsocket create_socket(const int port);

void set_socket_timeout(const int sockfd, const long timeout);

void reset_socket_timeout(const int sockfd);

int recvtimed(int fd, void* buf, const int bufsize, sockaddr_in* addr, socklen_t* addr_len, const long t);

int recvtimed(udpsocket* s, void* buf, const int bufsize, const long t);

int send(udpsocket* s, const void* buf, const int bufsize);

} // socket_util

#endif // UDP-UTIL_H
