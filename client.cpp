#include <iostream>
#include <string.h>
#include <arpa/inet.h>
#include <fstream>

#include "udp-util.h"

#define ROOT "client_root/"
#define STOP_AND_WAIT true

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

    int receive_file(udp_util::udpsocket* sock, udp_util::addr* s_addr, const char* filename, const int filesize) {
        packet curr_pckt;
        ofstream of;
        of.open(filename);
        int curr_pckt_no = 0;

        while(curr_pckt_no < filesize) {
            // Block until receiving packet from the server
            cout << "client: waiting to receive..." << endl;
            char buf[BUFFER_SIZE];
            int recv_bytes = 0;
            if ((recv_bytes = udp_util::recvtimed(sock->fd, buf, BUFFER_SIZE - 1, &s_addr->addr, &s_addr->len, TIME_OUT)) < 0) {
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
                send_ack(curr_pckt.seqno, recv_bytes-8, sock->fd, &s_addr->addr);
                if (curr_pckt.len == 0) {
                    break;
                }
            }
        }
        of.close();

        return curr_pckt_no;
    }
} // namespace stop_and_wait

namespace selective_repeat {
    int receive_file(udp_util::udpsocket* sock, udp_util::addr* s_addr, const char* filename, const int filesize) {
        FILE* fd = fopen(filename, "w");

        fseek(fd, filesize - 1, SEEK_SET);
        fputc('\0', fd);

        int window_size = 20 * BUFFER_SIZE;
        packet curr_pckt;
        int curr_pckt_no = 0;
        bool receive_status[filesize];
        unsigned long bytes_count = 0;

        while(bytes_count < filesize) {
            // Block until receiving packet from the server
            cout << "client: waiting to receive..." << endl;
            char buf[BUFFER_SIZE];
            int recv_bytes = 0;
            if ((recv_bytes = udp_util::recvtimed(sock->fd, buf, BUFFER_SIZE - 1, &s_addr->addr, &s_addr->len, TIME_OUT)) < 0) {
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
                fwrite(curr_pckt.data, recv_bytes - 8, 1, fd);

                for (int i = curr_pckt.seqno; i < (curr_pckt.seqno + curr_pckt.len); i++) {
                    receive_status[i] = true;
                    bytes_count++;
                }

                if (curr_pckt.seqno == curr_pckt_no) {
                    while(receive_status[curr_pckt_no]) {
                        curr_pckt_no++;
                    }
                }

                send_ack(curr_pckt.seqno, recv_bytes - 8, sock->fd, &s_addr->addr);
            } else if (curr_pckt.seqno < curr_pckt_no) {
                send_ack(curr_pckt.seqno, recv_bytes - 8, sock->fd, &s_addr->addr);
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

        return curr_pckt_no;
    }
} // namespace selective_repeat

/// Keep sending filename to the server till it receives an ACK
/// Returns filesize received from the server
int request_file(udp_util::udpsocket* sock, udp_util::addr* s_addr, const char* filename) {
    int filesize = 0;

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

    char *filename = "test.jpg";
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
