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
#define ACK_SIZE 8

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

int create_socket(int port) {
    int sockfd;
    if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("cannot create socket");
		exit(1);
	}

	struct sockaddr_in addr;
    memset((char*) &addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* bind to the address to which the service will be offered */
	if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind failed");
		exit(1);
	}

	return sockfd;
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

long find_file_size(FILE* fd) {
    fseek(fd, 0L, SEEK_END);
	long size = ftell(fd);
	fseek(fd, 0L, SEEK_SET);
	return size;
}

bool isdropped(double plp=0.0, double seed=-1.0) {
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

namespace stop_and_wait {

bool recv_ack(const int seqno, const int sockfd, const sockaddr_in* client_addr, const long time_out=100000) {
    set_socket_timeout(sockfd, time_out);

    socklen_t client_addr_len = sizeof(*client_addr);
    char buf[ACK_SIZE];
    int recv_bytes = 0;
    bool acked = true;
    if ((recv_bytes = recvfrom(sockfd, buf, ACK_SIZE, 0, (struct sockaddr *) client_addr,
            &client_addr_len)) != ACK_SIZE) {
        perror("server: recvfrom - ack failed");
        acked = false;
    }
    reset_socket_timeout(sockfd);
    if (acked) {
        ack_packet ack;
        memcpy(&ack, buf, ACK_SIZE);
        return ack.ackno == seqno;
    }
    return false;
}

int sw_sendto(const int sockfd, const sockaddr_in* client_addr, const packet* pckt) {
    int sent;
    int pckt_size = PCKT_HEADER_SIZE + pckt->len;

    do {
        if (isdropped()) {
            cout << pckt->seqno << "- dropped" << endl;
        } else {
            if ((sent = sendto(sockfd, (void *) pckt, pckt_size, 0,
                            (struct sockaddr *) client_addr, sizeof(*client_addr))) == -1) {
                perror("server: error sending pckt!");
                exit(-1);
            }
            cout << pckt->seqno << "- sent " << sent << " bytes" << endl;
        }
    } while(!recv_ack(pckt->seqno, sockfd, client_addr));
    return sent;
}

int send_file(const int sockfd, const sockaddr_in* client_addr, FILE* fd) {
    long file_size = find_file_size(fd);
    packet curr_pckt;
    curr_pckt.seqno = 0;

    cout << "file_size: " << file_size << " bytes" << endl;

    int sent = 0;
    for (long sent_bytes = 0; sent_bytes < file_size; sent_bytes+=curr_pckt.len, curr_pckt.seqno++) {
        curr_pckt.cksum = 1;
		curr_pckt.len = fread(curr_pckt.data, 1, BUFFER_SIZE, fd);
        if ((sent = sw_sendto(sockfd, client_addr, &curr_pckt) < 0)) {
            return sent;
        }
    }

    curr_pckt.len = 0;
    if ((sent = sw_sendto(sockfd, client_addr, &curr_pckt) < 0)) {
        return sent;
    }

    return file_size;
}

} // namespace stop_and_wait

void send_ack(const int sockfd, struct sockaddr_in* client_addr) {

    ack_packet first_ack;
    first_ack.ackno = -1;

    if ((sendto(sockfd, (void *) &first_ack, ACK_SIZE, 0,
                            (struct sockaddr *) client_addr, sizeof(*client_addr))) == -1) {
        perror("server: error sending ACK pckt!");
        exit(-1);
    }
}

namespace selective_repeat {

enum PacketStatus {
    NOT_ACKED,
    ACKED,
    THREAD_FINISHED
};

mutex cout_lock;
mutex status_lock;
vector<packet> pckts;
vector<PacketStatus> status;
const int window_size = 20;
// atomic<int> g_seqno;
atomic<bool> g_finished;

void pckt_thread_handle(const int sockfd, const sockaddr_in* client_addr, const int id, const long time_out) {
    int sent;
    int pckt_size = PCKT_HEADER_SIZE + pckts[id].len;

    while(true) {
        if (isdropped()) {
            cout_lock.lock();
            cout << pckts[id].seqno << "- dropped" << endl;
            cout_lock.unlock();
        } else {
            if ((sent = sendto(sockfd, &pckts[id], pckt_size, 0,
                            (struct sockaddr *) client_addr, sizeof(*client_addr))) == -1) {
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

void ack_listener_thread(const int sockfd, const sockaddr_in* client_addr, const long time_out) {
    set_socket_timeout(sockfd, time_out);
    while(!g_finished) {
        socklen_t client_addr_len = sizeof(*client_addr);
        ack_packet ack;
        int recv_bytes = 0;
        if ((recv_bytes = recvfrom(sockfd, &ack, sizeof ack, 0, (struct sockaddr *) client_addr,
                &client_addr_len)) != sizeof ack) {
            continue;
        }
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
    reset_socket_timeout(sockfd);
}

int send_file(const int sockfd, const sockaddr_in* client_addr, FILE* fd) {
    long file_size = find_file_size(fd);
    long read_data = 0;
    cout << "file_size: " << file_size << " bytes" << endl;

    {
        thread ack_listener(ack_listener_thread, sockfd, client_addr, 10000);
        ack_listener.detach();
    }

    int g_seqno = 0;
    int base = 0;
    bool finished_file = false;
    while(!g_finished) {
        // advance window base to next unACKed seq#
        for (; base < pckts.size(); ++base) {
            status_lock.lock();
            if (status[base] != THREAD_FINISHED) {
                status_lock.unlock();
                break;
            }
            status_lock.unlock();
        }
        // pckts.erase(pckts.begin(), pckts.begin() + base);
        // status.erase(status.begin(), status.begin() + base);

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

            thread th(pckt_thread_handle, sockfd, client_addr, id, 100000);
            th.detach();

            read_data += pckts[id].len;
        }
        g_finished = (base == pckts.size());
    }
    return file_size;
}

} // namespace selective_repeat

/// Return number of bytes sent or -1 if error happened
int send_file(const char* file_name, const int sockfd, struct sockaddr_in* client_addr) {
    FILE* fd = fopen(file_name, "r");
    if (fd == NULL) {
        perror("server: File NOT FOUND 404");
		return -1;
    }

    cout << "server: opened file: \"" << file_name << "\"" << endl;

    int sent = -1;
    if (STOP_AND_WAIT) {
        sent = stop_and_wait::send_file(sockfd, client_addr, fd);
    } else {
        sent = selective_repeat::send_file(sockfd, client_addr, fd);
    }
    fclose(fd);
    return sent;
}

/// TODO test with window = 1

int main()
{
    int sockfd = create_socket(8080);
    /* set PLP and random seed */
    isdropped(0.0);

    while(true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

		/* Block until receiving a request from a client */
		cout << "Server is waiting to receive..." << endl;
		char buf[BUFFER_SIZE];
		int recv_bytes = 0;
		if ((recv_bytes = recvfrom(sockfd, buf, BUFFER_SIZE - 1, 0, (struct sockaddr *) &client_addr,
				&client_addr_len)) < 0) {
			perror("server: main recvfrom failed");
			exit(-1);
        }
		buf[recv_bytes] = '\0';
		cout << "server: received buffer: " << buf << endl;

		send_ack(sockfd, &client_addr);

		send_file(buf, sockfd, &client_addr);
    }
    return 0;
}
