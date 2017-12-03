#ifndef UDP_UTIL_H
#define UDP_UTIL_H

#include <arpa/inet.h>

namespace udp_util {

struct udpsocket {
    int fd;
    sockaddr_in toaddr;
    socklen_t addr_len = sizeof(toaddr);
};

bool randrop(double plp = 0.0, double seed = -1.0);

udpsocket create_socket(const int port, const int toport=0, const int toip=INADDR_ANY);

udpsocket create_socket(const sockaddr_in toaddr, const socklen_t addr_len);

void set_socket_timeout(const int sockfd, const long timeout);

void reset_socket_timeout(const int sockfd);

int recvtimed(udpsocket* s, void* buf, const int bufsize, const long t);

int send(udpsocket* s, const void* buf, const int bufsize);

} // socket_util

#endif // UDP_UTIL_H
