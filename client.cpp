#include <iostream>
#include <string.h>
#include <arpa/inet.h>
#include <fstream>

#include "file-buffer.h"
#include "udp-util.h"

#define ROOT "client_root/"
#define STOP_AND_WAIT 0

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

int send_ack(const int ackno, const int len, const int sockfd, struct sockaddr_in *server_addr) {
    ack_packet ack;
    ack.ackno = ackno;
    ack.len = len;
    cout << "acked " << ackno << "+" << len << endl;
    return sendto(sockfd, &ack, sizeof ack, 0,
                  (struct sockaddr *) server_addr, sizeof(*server_addr));
}

namespace stop_and_wait {

uint32_t receive_file(udp_util::udpsocket* sock, udp_util::addr* s_addr, const char* filename, const uint32_t filesize) {
    packet curr_pckt;
    ofstream of;
    of.open(filename);
    uint32_t curr_pckt_no = 0;

    while(curr_pckt_no < filesize) {
        // Block until receiving packet from the server
        cout << "client: waiting to receive..." << endl;
        uint32_t recv_bytes = 0;
        if ((recv_bytes = udp_util::recvtimed(sock->fd, &curr_pckt, sizeof(curr_pckt), &s_addr->addr, &s_addr->len,
                                              TIME_OUT)) < 0) {
            perror("client: recvfrom failed");
            break;
        }
        cout << "expected packet: " << curr_pckt_no << endl;
        cout << "received packet: " << curr_pckt.seqno << endl;
        cout << "client: received " << recv_bytes << " bytes" << endl;
        cout << "client: data.len = " << curr_pckt.len << " bytes" << endl;

        if (curr_pckt.seqno == curr_pckt_no) {
            of.write(curr_pckt.data, recv_bytes - 8);
            curr_pckt_no += recv_bytes - 8;
        }
        if (curr_pckt.seqno <= curr_pckt_no) {
            send_ack(curr_pckt.seqno, recv_bytes - 8, sock->fd, &s_addr->addr);
        }
    }
    of.close();

    return curr_pckt_no;
}
} // namespace stop_and_wait

namespace selective_repeat {

const unsigned long long TIME_OUT = 500000; // 0.5 sec

uint32_t receive_file(udp_util::udpsocket* sock, udp_util::addr* s_addr, const char* filename, const uint32_t filesize) {
    ofstream of;
    of.open(filename);

    uint32_t window_size = 40 * BUFFER_SIZE;
    Range<uint32_t> window(0, window_size);
    FileBufferedWriter buf(filename, 200 * BUFFER_SIZE);

    while(window.start() < filesize) {
        /// TODO use circular queue
        // advance window base to next unACKed seq#
        window = Range<uint32_t>(buf.adjust(), window_size);

        packet pckt;
        if (udp_util::recvtimed(sock->fd, &pckt, sizeof(pckt), &s_addr->addr, &s_addr->len, TIME_OUT) < 0) {
            perror("client: recvfrom failed");
            break;
        }
        Range<uint32_t> pckt_range(pckt.seqno, pckt.len);
        Range<uint32_t> sect = window.intersect(pckt_range);

        cout << "client: received pckt.no=" << pckt.seqno << "+" << pckt.len << endl;
        cout << "client: expected window=" << window.start() << "+" << window.len() << endl;
        cout << "client: intersection=" << sect.start() << "+" << sect.len() << endl;

        if (sect.len() > 0) {
            Range<uint32_t> written = buf.write(pckt.data, sect);
            send_ack(pckt.seqno, written.end() - pckt.seqno, sock->fd, &s_addr->addr);
        } else if (pckt_range.end() <= window.start()) {
            send_ack(pckt.seqno, pckt.len, sock->fd, &s_addr->addr);
        } else {
            cout << "client: pckt ignored" << endl;
        }
    }
    buf.close();
    return window.start();
}

} // namespace selective_repeat2

/// Keep sending filename to the server till it receives an ACK
/// Returns filesize received from the server
uint32_t request_file(udp_util::udpsocket* sock, udp_util::addr* s_addr, const char* filename) {
    uint32_t filesize = 0;

    for(int i = 0; i < MAX_RETRY; ++i) {
        if (sendto(sock->fd, filename, strlen(filename), 0,
                   (struct sockaddr *) &s_addr->addr, sizeof(s_addr->addr)) == -1) {
            perror("client: error sending pckt!");
            exit(-1);
        }

        char buf[BUFFER_SIZE];
        int received = udp_util::recvtimed(sock->fd, buf, BUFFER_SIZE, &s_addr->addr, &s_addr->len, TIME_OUT);
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

int main() {
    udp_util::udpsocket sock = udp_util::create_socket(5555);

    udp_util::addr server_addr;
    memset((char*) &server_addr, 0, sizeof(server_addr));
    server_addr.addr.sin_family = AF_INET;
    server_addr.addr.sin_port = htons(55555);

    const char *filename = "test.png";
    int filesize = request_file(&sock, &server_addr, filename);

    if (filesize < 0) {
        return -1;
    }

    char full_path[BUFFER_SIZE] = ROOT;
    strncat(full_path, filename, BUFFER_SIZE - strlen(ROOT));
    if (STOP_AND_WAIT) {
        stop_and_wait::receive_file(&sock, &server_addr, full_path, filesize);
    } else {
        selective_repeat::receive_file(&sock, &server_addr, full_path, filesize);
    }

    return 0;
}
