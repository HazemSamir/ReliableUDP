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
#include <atomic>
#include <mutex>
#include <fstream>
#include <random>
#include <thread>

#define BUFFER_SIZE 200
#define PCKT_HEADER_SIZE 8

#define STOP_AND_WAIT false

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

long find_file_size(FILE* fd) {
    fseek(fd, 0L, SEEK_END);
	long size = ftell(fd);
	fseek(fd, 0L, SEEK_SET);
	return size;
}

namespace stop_and_wait {

const long TIME_OUT = 100000; // 0.1 sec

bool recv_ack(const int seqno, udp_util::udpsocket* sock, const long time_out=TIME_OUT) {
    ack_packet ack;
    int recv_bytes = 0;
    if ((recv_bytes = udp_util::recvtimed(sock, &ack, sizeof(ack), time_out)) != sizeof(ack)) {
        perror("server: recvfrom - ack failed");
        return false;
    }
    return ack.ackno == seqno;
}

int sw_sendto(udp_util::udpsocket* sock, const packet* pckt) {
    int sent;
    int pckt_size = PCKT_HEADER_SIZE + pckt->len;

    do {
        if (udp_util::randrop()) {
            cout << pckt->seqno << "- dropped" << endl;
        } else {
            if ((sent = udp_util::send(sock, pckt, pckt_size)) == -1) {
                perror("server: error sending pckt!");
                exit(-1);
            }
            cout << pckt->seqno << "- sent " << sent << " bytes" << endl;
        }
    } while(!recv_ack(pckt->seqno, sock));
    return sent;
}

int send_file(udp_util::udpsocket* sock, FILE* fd) {
    long file_size = find_file_size(fd);
    packet curr_pckt;
    curr_pckt.seqno = 0;

    cout << "file_size: " << file_size << " bytes" << endl;

    int sent = 0;
    for (long sent_bytes = 0; sent_bytes < file_size; sent_bytes+=curr_pckt.len, curr_pckt.seqno++) {
        curr_pckt.cksum = 1;
		curr_pckt.len = fread(curr_pckt.data, 1, BUFFER_SIZE, fd);
        if ((sent = sw_sendto(sock, &curr_pckt) < 0)) {
            return sent;
        }
    }

    curr_pckt.len = 0;
    if ((sent = sw_sendto(sock, &curr_pckt) < 0)) {
        return sent;
    }

    return file_size;
}

} // namespace stop_and_wait

namespace selective_repeat {

const long TIME_OUT = 500000; // 0.5 sec

enum PacketStatus {
    NOT_ACKED,
    ACKED,
    THREAD_FINISHED
};

mutex cout_lock;
mutex status_lock;
vector<packet> pckts;
vector<PacketStatus> status;
/// TODO test with window = 1
const int window_size = 20;
// atomic<int> g_seqno;
atomic<bool> g_finished;

void pckt_thread_handle(udp_util::udpsocket* sock, const int id, const long time_out) {
    int sent;
    int pckt_size = PCKT_HEADER_SIZE + pckts[id].len;

    while(true) {
        if (udp_util::randrop()) {
            cout_lock.lock();
            cout << pckts[id].seqno << "/" << id << " is dropped" << endl;
            cout_lock.unlock();
        } else {
            if ((sent = udp_util::send(sock, &pckts[id], pckt_size)) == -1) {
                perror("server: error sending pckt!");
                exit(-1);
            }
            cout_lock.lock();
            cout << pckts[id].seqno << "/" << id << " is sent with " << sent << " bytes" << endl;
            cout_lock.unlock();
        }

        usleep(time_out);

        status_lock.lock();
        if (status[id] == ACKED) {
            status[id] = THREAD_FINISHED;
            status_lock.unlock();
            break;
        }
        status_lock.unlock();
    }
}

void ack_listener_thread(udp_util::udpsocket* sock, const long time_out) {
    while(!g_finished) {
        ack_packet ack;
        if (udp_util::recvtimed(sock, &ack, sizeof(ack), time_out) == sizeof(ack)) {
            int pckt_id = ack.ackno;
            if (pckt_id >= 0) {
                status_lock.lock();
                status[pckt_id] = ACKED;
                status_lock.unlock();

                cout_lock.lock();
                cout << "packet " << ack.ackno << "/" << pckt_id << " acked!" << endl;
                cout_lock.unlock();
            }
        }
    }
}

int send_file(udp_util::udpsocket* sock, FILE* fd) {
    long file_size = find_file_size(fd);
    long read_data = 0;
    cout << "file_size: " << file_size << " bytes" << endl;

    {
        /* Launch a listener thread for ACKs */
        thread ack_listener(ack_listener_thread, sock, TIME_OUT);
        ack_listener.detach();
    }

    int g_seqno = 0;
    int base = 0;
    bool finished_file = false;
    while(!g_finished) {
        /// TODO use circular queue
        // advance window base to next unACKed seq#
        for (; base < pckts.size(); ++base) {
            status_lock.lock();
            if (status[base] != THREAD_FINISHED) {
                status_lock.unlock();
                break;
            }
            status_lock.unlock();
        }

        for (; pckts.size() - base < window_size && !finished_file;) {
            status_lock.lock();
            status.push_back(NOT_ACKED);
            status_lock.unlock();

            pckts.emplace_back();
            int id = pckts.size() - 1;

            pckts[id].cksum = 1;
            pckts[id].len = 0;
            finished_file = true;
            if (read_data < file_size) {
                pckts[id].len = fread(pckts[id].data, 1, BUFFER_SIZE, fd);
                finished_file = false;
            }
            pckts[id].seqno = g_seqno++;

            thread th(pckt_thread_handle, sock, id, 100000);
            th.detach();

            read_data += pckts[id].len;
        }
        g_finished = (base == pckts.size());
    }
    return file_size;
}

} // namespace selective_repeat

/// Return number of bytes sent or -1 if error happened
int send_file(const char* file_name, udp_util::udpsocket* sock) {
    FILE* fd = fopen(file_name, "r");
    if (fd == NULL) {
        perror("server: File NOT FOUND 404");
		return -1;
    }

    cout << "server: opened file: \"" << file_name << "\"" << endl;

    int sent = -1;
    if (STOP_AND_WAIT) {
        sent = stop_and_wait::send_file(sock, fd);
    } else {
        sent = selective_repeat::send_file(sock, fd);
    }
    fclose(fd);
    return sent;
}

void send_first_ack(udp_util::udpsocket* sock) {
    ack_packet ack;
    ack.ackno = -1;

    if (udp_util::send(sock, &ack, sizeof(ack)) == -1) {
        perror("server: error sending first ACK pckt!");
        exit(-1);
    }
}

int main()
{
    udp_util::udpsocket sock = udp_util::create_socket(8080);
    /* set PLP and random seed */
    udp_util::randrop(0.0);

    while(true) {
		/* Block until receiving a request from a client */
		cout << "Server is waiting to receive..." << endl;
		char buf[BUFFER_SIZE];
		int recv_bytes = 0;
		if ((recv_bytes = recvfrom(sock.fd, buf, BUFFER_SIZE - 1, 0, (sockaddr*) &sock.client_addr,
				&sock.client_addr_len)) < 0) {
			perror("server: main recvfrom failed");
			exit(-1);
        }
		buf[recv_bytes] = '\0';
		cout << "server: received buffer: " << buf << endl;

		send_first_ack(&sock);
		send_file(buf, &sock);
    }
    return 0;
}
