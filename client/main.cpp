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

#define BUFFER_SIZE 250

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

int send_ack(const int ackno, const int sockfd, struct sockaddr_in *server_addr) {
    ack_packet ack;
    ack.ackno = ackno;
    return sendto(sockfd, &ack, sizeof ack, 0,
			(struct sockaddr *) server_addr, sizeof(*server_addr));
}

int main()
{
    int sockfd = create_socket(5555);

    struct sockaddr_in server_addr;
    socklen_t server_addr_len;
	memset((char*) &server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(8080);

	char *filename = "test.jpg";

	while(true) {
        if (sendto(sockfd, filename, strlen(filename), 0,
                (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
            perror("client: error sending pckt!");
            exit(-1);
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            perror("client: error to set timeout");
            exit(-1);
        }
        char buf[BUFFER_SIZE];
        if ((recvfrom(sockfd, buf, BUFFER_SIZE - 1, 0, (struct sockaddr *) &server_addr,
                    &server_addr_len)) < 0) {
            perror("client: timeout");
            continue;
        }
        cout << "client: received ACK from server" << endl;
        tv.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            perror("client: error to reset timeout");
            exit(-1);
        }
        break;
	}

    packet curr_pckt;
    ofstream of;
    of.open(filename);
    int curr_pckt_no = 0;
    while(true) {
        /* Block until receive a request from a client */
        cout << "client: waiting to receive..." << endl;
        char buf[BUFFER_SIZE];
        int recv_bytes = 0;
        if ((recv_bytes = recvfrom(sockfd, buf, BUFFER_SIZE - 1, 0, (struct sockaddr *) &server_addr,
                &server_addr_len)) < 0) {
            perror("client: recvfrom failed");
        }
        curr_pckt = *(packet*) buf;
        cout << "packet: " << curr_pckt_no << endl;
        cout << "client: received " << recv_bytes << " bytes" << endl;
        cout << "client: data.len = " << curr_pckt.len << " bytes" << endl;
        if (curr_pckt.seqno == curr_pckt_no) {
            of.write(curr_pckt.data, curr_pckt.len);
            ++curr_pckt_no;
        }
        if (curr_pckt.seqno <= curr_pckt_no) {
            send_ack(curr_pckt.seqno, sockfd, &server_addr);
        }
        if (curr_pckt.len == 0) {
            break;
        }
    }
    of.close();
    return 0;
}
