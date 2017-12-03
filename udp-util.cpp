#include "udp-util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <random>

namespace udp_util {

bool randrop(double plp, double seed) {
    static std::mutex mtx;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    if (seed > 0.0) {
        gen.seed(seed);
    }
    static std::discrete_distribution<> d({1.0 - plp, plp});
    mtx.lock();
    int rand_idx = d(gen);
    mtx.unlock();
    return rand_idx;
}

udpsocket create_socket(const int port, const int toport, const int toip) {
    udpsocket s;
    if ((s.fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("cannot create socket");
        exit(1);
    }

    memset((char*) &s.toaddr, 0, sizeof(s.toaddr));
    s.toaddr.sin_family = AF_INET;
    s.toaddr.sin_port = htons(toport);
    s.toaddr.sin_addr.s_addr = htonl(toip);
    s.addr_len = sizeof(s.toaddr);

    sockaddr_in myaddr;
    socklen_t myaddr_len = sizeof(myaddr);
    memset((char*) &myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(port);
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr_len = sizeof(myaddr);

    /* bind to the address to which the service will be offered */
    if (bind(s.fd, (sockaddr *) &myaddr, sizeof(myaddr)) < 0) {
        perror("bind failed");
        exit(1);
    }

    return s;
}

void set_socket_timeout(const int sockfd, const long timeout) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeout;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("server: error to set timeout");
        exit(-1);
    }
}

void reset_socket_timeout(const int sockfd) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("server: error to reset timeout");
        exit(-1);
    }
}

int recvtimed(udpsocket* s, void* buf, const int bufsize, const long t) {
    set_socket_timeout(s->fd, t);
    int recved = recvfrom(s->fd, buf, bufsize, 0, (sockaddr*) &s->toaddr, &s->addr_len);
    reset_socket_timeout(s->fd);
    return recved;
}

int send(udpsocket* s, const void* buf, const int bufsize) {
    return sendto(s->fd, (void*) buf, bufsize, 0, (sockaddr*) &s->toaddr, s->addr_len);
}

} // socket_util

