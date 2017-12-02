#include <arpa/inet.h>
#include <atomic>
#include <iostream>
#include <mutex>
#include <string.h>
#include <sys/time.h>
#include <thread>

#include "udp-util.h"

#define ROOT "server_root/"
#define BUFFER_SIZE 200
#define FILE_BUFFER_SIZE 100000
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

long find_file_size(FILE* fd) {
    fseek(fd, 0L, SEEK_END);
    long size = ftell(fd);
    fseek(fd, 0L, SEEK_SET);
    return size;
}

namespace stop_and_wait {

const long TIME_OUT = 100000; // 0.1 sec

bool recv_ack(const int seqno, udp_util::udpsocket* sock, const long time_out = TIME_OUT) {
    ack_packet ack;
    int recv_bytes = 0;
    if ((recv_bytes = udp_util::recvtimed(sock, &ack, sizeof(ack), time_out)) != sizeof(ack)) {
        perror("server: recvfrom - ack failed");
        return false;
    }
    cout << "ack.no=" << ack.ackno << endl;
    return ack.ackno == seqno;
}

int send_packet_till_ack(udp_util::udpsocket* sock, const packet* pckt) {
    int sent;
    int pckt_size = PCKT_HEADER_SIZE + pckt->len;

    do {
        if (udp_util::randrop()) {
            sent = 0;
            cout << pckt->seqno << "- dropped" << endl;
        } else {
            if ((sent = udp_util::send(sock, pckt, pckt_size)) == -1) {
                perror("server: error sending pckt!");
                exit(-1);
            }
            cout << pckt->seqno << "- sent " << sent << " bytes" << endl;
        }
    } while(!recv_ack(pckt->seqno, sock));
    cout << "pckt.seqno=" << pckt->seqno << " Acked" << endl;
    return sent - PCKT_HEADER_SIZE;
}

int send_file(udp_util::udpsocket* sock, FILE* fd, int file_size) {
    packet curr_pckt;
    curr_pckt.seqno = 0;

    for (int tot_bytes = 0, sent = 0; tot_bytes < file_size; tot_bytes += sent) {
        curr_pckt.cksum = 1;
        curr_pckt.len = fread(curr_pckt.data, 1, BUFFER_SIZE, fd);
        curr_pckt.seqno = tot_bytes;
        if ((sent = send_packet_till_ack(sock, &curr_pckt)) < 0) {
            return sent;
        }
    }

    return file_size;
}

} // namespace stop_and_wait

namespace selective_repeat {
const unsigned long long TIME_OUT = 500000; // 0.5 sec

mutex cout_lock;
mutex ack_lock;
bool acked[FILE_BUFFER_SIZE];
timeval time_sent[FILE_BUFFER_SIZE];
char file_data[FILE_BUFFER_SIZE];

atomic<int> first_byte_seqno;

mutex window_lock;
int window_size = BUFFER_SIZE;
atomic<bool> g_finished;

void reset_global() {
    g_finished = false;
    window_lock.lock();
    window_size = BUFFER_SIZE;
    window_lock.unlock();
    first_byte_seqno = 0;
}

void decrease_window() {
    window_lock.lock();
    window_size /= 2;
    window_size = max(window_size, BUFFER_SIZE);
    window_lock.unlock();

    cout_lock.lock();
    cout << "window size decreased: " << window_size << endl;
    cout_lock.unlock();
}

void increase_window() {
    window_lock.lock();
    window_size += BUFFER_SIZE;
    window_size = min(window_size, FILE_BUFFER_SIZE);
    window_lock.unlock();

    cout_lock.lock();
    cout << "window size increased: " << window_size << endl;
    cout_lock.unlock();
}

void send_packet(udp_util::udpsocket* sock, int seqno, int len) {
    packet pckt;
    pckt.seqno = seqno;
    pckt.len = len;
    pckt.cksum = 1;

    int pbase = seqno - first_byte_seqno;
    memcpy(pckt.data, file_data + pbase, len);

    int pckt_size = PCKT_HEADER_SIZE + pckt.len;
    if (udp_util::randrop()) {
        cout_lock.lock();
        cout << pckt.seqno << " is dropped" << endl;
        cout_lock.unlock();
    } else {
        int sent;
        if ((sent = udp_util::send(sock, &pckt, pckt_size)) == -1) {
            perror("server: error sending pckt!");
            exit(-1);
        }
        cout_lock.lock();
        cout << pckt.seqno << " is sent with " << sent << " bytes" << endl;
        cout_lock.unlock();

        timeval time_now;
        gettimeofday(&time_now, NULL);
        for (int i = 0; i < len; ++i) {
            time_sent[pbase + i] = time_now;
        }
    }
}

void ack_listener_thread(udp_util::udpsocket* sock, const long time_out) {
    while(!g_finished) {
        ack_packet ack;
        if (udp_util::recvtimed(sock, &ack, sizeof(ack), time_out) == sizeof(ack)) {
            ack_lock.lock();
            int pbase = ack.ackno - first_byte_seqno;
            int pfin = max(0, pbase + ack.len);
            pbase = max(0, pbase);
            memset(acked + pbase, 1, pfin - pbase);
            ack_lock.unlock();

            if (pfin - pbase > 0) {
                increase_window();
            }

            cout_lock.lock();
            cout << "bytes " << pbase << ":" << pfin << " acked!" << endl;
            cout << "AKA bytes " << ack.ackno << "+" << ack.len << " acked!" << endl;
            cout_lock.unlock();
        } else {
            decrease_window();
        }
    }
}

int send_file(udp_util::udpsocket* sock, FILE* fd, int file_size) {
    long read_data = 0;
    {
        /* Launch a listener thread for ACKs */
        thread ack_listener(ack_listener_thread, sock, TIME_OUT);
        ack_listener.detach();
    }

    int base = 0;
    int buf_size = 0;
    reset_global();
    while(!g_finished) {
        /// TODO use circular queue
        // advance window base to next unACKed seq#
        ack_lock.lock();
        for (; base < buf_size && acked[base]; ++base);
        ack_lock.unlock();

        if (base == buf_size) {
            first_byte_seqno += buf_size;
            buf_size = fread(file_data, 1, FILE_BUFFER_SIZE, fd);
            memset(acked, 0, buf_size);
            memset(time_sent, 0, sizeof time_sent);
            base = 0;
            read_data += buf_size;
        }

        timeval time_now;
        gettimeofday(&time_now, NULL);
        int l = base, r = base;
        ack_lock.lock();
        unsigned long long time_now_micro = time_now.tv_sec * 1000000 + time_now.tv_usec;
        for (; r - base < window_size && r < buf_size; r++) {
            unsigned long long time_sent_micro = time_sent[r].tv_sec * 1000000 + time_sent[r].tv_usec;
            unsigned long long time_passed = time_now_micro - time_sent_micro;
            if (acked[r] || time_passed < TIME_OUT || r - l == BUFFER_SIZE) {
                if (l < r) {
                    send_packet(sock, l + first_byte_seqno, r - l);
                }
                l = r + 1;
            } /*else if (time_passed >= TIME_OUT) {
                decrease_window();
            }*/
        }
        ack_lock.unlock();
        if (l < r) {
            send_packet(sock, l + first_byte_seqno, r - l);
        }
        g_finished = (base == buf_size);
    }
    return file_size;
}

} // namespace selective_repeat

void send_first_ack(udp_util::udpsocket* sock, int filesize) {
    ack_packet ack;
    ack.ackno = filesize;

    if (udp_util::send(sock, &ack, sizeof(ack)) == -1) {
        perror("server: error sending first ACK pckt!");
        exit(-1);
    }
}

/// Return number of bytes sent or -1 if error happened
int send_file(const char* file_name, udp_util::udpsocket* sock) {
    FILE* fd = fopen(file_name, "r");
    if (fd == NULL) {
        cerr << "File " << file_name << " NOT FOUND 404" << endl;
        perror("server: ");
        send_first_ack(sock, -1);
        return -1;
    }
    int file_size = find_file_size(fd);

    cout << "server: opened file: \"" << file_name << "\"" << endl;
    cout << "file_size: " << file_size << " bytes" << endl;

    send_first_ack(sock, file_size);
    int sent = -1;
    if (STOP_AND_WAIT) {
        sent = stop_and_wait::send_file(sock, fd, file_size);
    } else {
        sent = selective_repeat::send_file(sock, fd, file_size);
    }
    fclose(fd);
    return sent;
}

int main() {
    udp_util::udpsocket sock = udp_util::create_socket(55555);
    /* set PLP and random seed */
    udp_util::randrop(0.01);

    while(true) {
        /* Block until receiving a request from a client */
        cout << "Server is waiting to receive..." << endl;
        char filename[BUFFER_SIZE];
        int recv_bytes = 0;
        udp_util::reset_socket_timeout(sock.fd);
        if ((recv_bytes = recvfrom(sock.fd, filename, BUFFER_SIZE - 1, 0, (sockaddr*) &sock.myaddr.addr,
                                   &sock.myaddr.len)) < 0) {
            perror("server: main recvfrom failed");
            exit(-1);
        }
        filename[recv_bytes] = '\0';
        cout << "server: received filename: " << filename << endl;

        char full_path[BUFFER_SIZE] = ROOT;
        strncat(full_path, filename, BUFFER_SIZE - strlen(ROOT));
        cout << "Sent " << send_file(full_path, &sock) << endl;
    }
    cout << "Finished" << endl;
    return 0;
}
