#include <iostream>
#include <string.h>
#include <arpa/inet.h>
#include <fstream>

#include "udp-util.h"

#define ROOT "client_root/"
#define STOP_AND_WAIT 0

#define FILE_BUFFER_SIZE 100000
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

const unsigned long long TIME_OUT = 999999;

int send_ack(udp_util::udpsocket* sock, const int ackno, const int len) {
    ack_packet ack;
    ack.ackno = ackno;
    ack.len = len;
    cout << "acked " << ackno << "+" << len << endl;
    return udp_util::send(sock, &ack, sizeof ack);
}

namespace stop_and_wait {

int receive_file(udp_util::udpsocket* sock, const char* filename, const int filesize) {
    packet curr_pckt;
    ofstream of;
    of.open(filename);
    int curr_pckt_no = 0;

    while(curr_pckt_no < filesize) {
        // Block until receiving packet from the server
        cout << "client: waiting to receive..." << endl;
        int recv_bytes = 0;
        if ((recv_bytes = udp_util::recvtimed(sock, &curr_pckt, sizeof(curr_pckt), TIME_OUT)) < 0) {
            perror("client: recvfrom failed");
            break;
        }
        cout << "expected packet: " << curr_pckt_no << endl;
        cout << "received packet: " << curr_pckt.seqno << endl;
        cout << "client: received " << recv_bytes << " bytes" << endl;
        cout << "client: data.len = " << curr_pckt.len << " bytes" << endl;

        if (curr_pckt.seqno == curr_pckt_no) {
            of.write(curr_pckt.data, recv_bytes-8);
            curr_pckt_no += recv_bytes-8;
        }
        if (curr_pckt.seqno <= curr_pckt_no) {
            send_ack(sock, curr_pckt.seqno, recv_bytes-8);
        }
    }
    of.close();

    return curr_pckt_no;
}
} // namespace stop_and_wait

namespace selective_repeat {

const unsigned long long TIME_OUT = 500000; // 0.5 sec

int window_size;

void decrease_window() {
    window_size /= 2;
    window_size = max(window_size, BUFFER_SIZE);
}

void increase_window() {
    window_size += BUFFER_SIZE;
    window_size = min(window_size, FILE_BUFFER_SIZE);
}

int receive_file(udp_util::udpsocket* sock, const char* filename, const int filesize) {
    ofstream of;
    of.open(filename);

    bool acked[FILE_BUFFER_SIZE];
    char file_data[FILE_BUFFER_SIZE];

    int buf_base = 0, recvbase = 0;
    window_size = 5 * BUFFER_SIZE;
    memset(acked, 0, sizeof(acked));
    while(recvbase + buf_base < filesize) {
        /// TODO use circular queue
        // advance window base to next unACKed seq#
        for (; buf_base < FILE_BUFFER_SIZE && acked[buf_base]; ++buf_base);

        if (buf_base == FILE_BUFFER_SIZE) {
            of.write(file_data, FILE_BUFFER_SIZE);
            memset(acked, 0, FILE_BUFFER_SIZE);
            buf_base = 0;
            recvbase += FILE_BUFFER_SIZE;
            cout << "reset!" << endl;
        }

        packet curr_pckt;
        if (udp_util::recvtimed(sock, &curr_pckt, sizeof(curr_pckt), TIME_OUT) < 0) {
            perror("client: recvfrom failed");
            break;
        }

        int window_start = recvbase + buf_base;
        int window_len = min(window_size, FILE_BUFFER_SIZE - buf_base);

        int pckt_start = max((int) curr_pckt.seqno, window_start);
        int pckt_end = curr_pckt.seqno + curr_pckt.len;
        pckt_end = max(window_start, pckt_end);
        pckt_end = min(window_start + window_len, pckt_end);
        pckt_end = min(pckt_start + window_len, pckt_end);
        int pckt_len = pckt_end - pckt_start;
        cout << "client: received pckt.no=" << curr_pckt.seqno << "+" << curr_pckt.len << endl;
        cout << "client: expected window=" << window_start << "+" << window_len << endl;
        cout << "client: will write " << pckt_start << "+" << pckt_len << endl;

        if (pckt_len > 0) {
            int start_in_buf = pckt_start - recvbase;
            int start_in_pckt = pckt_start - curr_pckt.seqno;
            memcpy(file_data + start_in_buf, curr_pckt.data + start_in_pckt, pckt_len);
            memset(acked + start_in_buf, 1, pckt_len);

            int ack_start = min((int)curr_pckt.seqno, pckt_start);
            send_ack(sock, ack_start, pckt_end - ack_start);
        } else if (curr_pckt.seqno + curr_pckt.len <= recvbase + buf_base) {
            send_ack(sock, curr_pckt.seqno, curr_pckt.len);
        } else {
            cout << "ignored" << endl;
        }
    }

    if (buf_base > 0) {
        of.write(file_data, buf_base);
        recvbase += buf_base;
    }
    of.close();
    return recvbase;
}

} // namespace selective_repeat2

/// Keep sending filename to the server till it receives an ACK
/// Returns filesize received from the server
int request_file(udp_util::udpsocket* sock, const char* filename) {
    int filesize = 0;

    for(int i = 0; i < MAX_RETRY; ++i) {
        if (udp_util::send(sock, filename, strlen(filename)) == -1) {
            perror("client: error sending pckt!");
            exit(-1);
        }

        char buf[BUFFER_SIZE];
        int received = udp_util::recvtimed(sock, buf, BUFFER_SIZE, TIME_OUT);
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

    return filesize;
}

int main(int argc, char* argv[]) {
    udp_util::udpsocket sock = udp_util::create_socket(atoi(argv[2]), 4444);

    int filesize = request_file(&sock, argv[1]);

    if (filesize < 0) {
        return -1;
    }

    char full_path[BUFFER_SIZE] = ROOT;
    if (argc >= 2) {
        strncat(full_path, argv[1], BUFFER_SIZE - strlen(ROOT));
    }
    if (STOP_AND_WAIT) {
        stop_and_wait::receive_file(&sock, full_path, filesize);
    } else {
        selective_repeat::receive_file(&sock, full_path, filesize);
    }

    return 0;
}
