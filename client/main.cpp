#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <vector>
#include <sys/time.h>
#include <fstream>
#include <mutex>
#include <random>

#define BUFFER_SIZE 250
#define MAX_RETRY 100

using namespace std;

/* Data-only packets */
struct packet {
    /* Header */
    uint16_t cksum;
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data[BUFFER_SIZE];
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
    uint16_t cksum;
    uint16_t len;
    uint32_t ackno;
};

namespace udp_util {

struct udpsocket {
    int fd;
    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
};

bool randrop(double plp=0.0, double seed=-1.0) {
    static mutex mtx;
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

udpsocket create_socket(const int port) {
    udpsocket s;
    if ((s.fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("cannot create socket");
		exit(1);
	}

    memset((char*) &s.client_addr, 0, sizeof(s.client_addr));
	s.client_addr.sin_family = AF_INET;
	s.client_addr.sin_port = htons(port);
	s.client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* bind to the address to which the service will be offered */
	if (bind(s.fd, (sockaddr *) &s.client_addr, sizeof(s.client_addr)) < 0) {
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

int recvtimed(int fd, void* buf, const int bufsize, sockaddr_in* addr, socklen_t* addr_len, const long t) {
    set_socket_timeout(fd, t);
    int recved = recvfrom(fd, buf, bufsize, 0, (sockaddr*) addr, addr_len);
    reset_socket_timeout(fd);
    return recved;
}

int recvtimed(udpsocket* s, void* buf, const int bufsize, const long t) {
    set_socket_timeout(s->fd, t);
    int recved = recvfrom(s->fd, buf, bufsize, 0, (sockaddr*) &s->client_addr, &s->client_addr_len);
    reset_socket_timeout(s->fd);
    return recved;
}

int send(udpsocket* s, const void* buf, const int bufsize) {
    return sendto(s->fd, (void*) buf, bufsize, 0, (sockaddr*) &s->client_addr, sizeof(s->client_addr));
}

} // socket_util

int send_ack(const int ackno, const int len, const int sockfd, struct sockaddr_in *server_addr) {
    ack_packet ack;
    ack.ackno = ackno;
    ack.len = len;
    cout << "acked " << ackno << "+" << len << endl;
    return sendto(sockfd, &ack, sizeof ack, 0,
			(struct sockaddr *) server_addr, sizeof(*server_addr));
}

int main()
{
    udp_util::udpsocket sock = udp_util::create_socket(5555);

    struct sockaddr_in server_addr;
    socklen_t server_addr_len;
	memset((char*) &server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(55555);

	char *filename = "test.jpg";
    int filesize = 0;
    const long TIME_OUT = 999999;

	for(int i = 0; i < MAX_RETRY; ++i) {
        if (sendto(sock.fd, filename, strlen(filename), 0,
                (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
            perror("client: error sending pckt!");
            exit(-1);
        }

        char buf[BUFFER_SIZE];
        int received = udp_util::recvtimed(sock.fd, buf, BUFFER_SIZE, &server_addr, &server_addr_len, TIME_OUT);
        if (received < 0) {
            perror("client: timeout to receive filesize: ");
            continue;
        } else if (received != sizeof(ack_packet)) {
            cerr << "Didn't receive right ACK - received " << received << " bytes instead" << endl;
            continue;
        }
        filesize = ((ack_packet*) buf)->ackno;
        cout << "client: received ACK from server - filesize=" << filesize << endl;
        break;
	}
/*
	// Stop and wait
    packet curr_pckt;
    ofstream of;
    of.open(filename);
    int curr_pckt_no = 0;

    while(curr_pckt_no < filesize) {
        // Block until receiving packet from the server
        cout << "client: waiting to receive..." << endl;
        char buf[BUFFER_SIZE];
        int recv_bytes = 0;
        if ((recv_bytes = udp_util::recvtimed(sock.fd, buf, BUFFER_SIZE - 1, &server_addr, &server_addr_len, TIME_OUT)) < 0) {
            perror("client: recvfrom failed");
            break;
        }
        curr_pckt = *(packet*) buf;
        cout << "expected packet: " << curr_pckt_no << endl;
        cout << "received packet: " << curr_pckt.seqno << endl;
        cout << "client: received " << recv_bytes << " bytes" << endl;
        cout << "client: data.len = " << curr_pckt.len << " bytes" << endl;

        if (curr_pckt.seqno == curr_pckt_no) {
            of.write(curr_pckt.data, recv_bytes-8);
            curr_pckt_no += recv_bytes-8;
        }
        if (curr_pckt.seqno <= curr_pckt_no) {
            send_ack(curr_pckt.seqno, recv_bytes-8, sock.fd, &server_addr);
            if (curr_pckt.len == 0) {
                break;
            }
        }
    }
    of.close();
*/

    // Selective repeat
    FILE* fd = fopen(filename, "w");

    fseek(fd, filesize - 1, SEEK_SET);
    fputc('\0', fd);

    int window_size = 20*BUFFER_SIZE;
    packet curr_pckt;
    int curr_pckt_no = 0;
    bool receive_status[filesize];
    unsigned long bytes_count = 0;

    while(bytes_count < filesize) {
        // Block until receiving packet from the server
        cout << "client: waiting to receive..." << endl;
        char buf[BUFFER_SIZE];
        int recv_bytes = 0;
        if ((recv_bytes = udp_util::recvtimed(sock.fd, buf, BUFFER_SIZE - 1, &server_addr, &server_addr_len, TIME_OUT)) < 0) {
            perror("client: recvfrom failed");
            break;
        }
        curr_pckt = *(packet*) buf;
        cout << "expected packet: " << curr_pckt_no << endl;
        cout << "received packet: " << curr_pckt.seqno << endl;
        cout << "client: received " << recv_bytes << " bytes" << endl;
        cout << "client: data.len = " << curr_pckt.len << " bytes" << endl;

        if (curr_pckt.seqno >= curr_pckt_no && curr_pckt.seqno <= curr_pckt_no + window_size) {
            fseek(fd, curr_pckt.seqno, SEEK_SET);
            fwrite(curr_pckt.data, recv_bytes-8, 1, fd);

            for (int i = curr_pckt.seqno; i < (curr_pckt.seqno + curr_pckt.len); i++) {
                receive_status[i] = true;
                bytes_count++;
            }

            if (curr_pckt.seqno == curr_pckt_no) {
                while(receive_status[curr_pckt_no]) {
                    curr_pckt_no++;
                }
            }

            send_ack(curr_pckt.seqno, recv_bytes-8, sock.fd, &server_addr);
        }
        else if (curr_pckt.seqno < curr_pckt_no) {
            send_ack(curr_pckt.seqno, recv_bytes-8, sock.fd, &server_addr);
        }
    }

    int i = 0;
    for (int j = 0; j < sizeof(receive_status); j++) {
        if (!receive_status[j]) i++;
    }
    cout << "Falses: " << i << endl;
    cout << "Bytes count: " << bytes_count << endl;
    cout << "File size: " << filesize << endl;

    fclose(fd);

    return 0;
}
