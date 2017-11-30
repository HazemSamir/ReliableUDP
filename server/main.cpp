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
#include <random>

#define BUFFER_SIZE 200
#define PCKT_HEADER_SIZE 8
#define ACK_SIZE 8

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

long find_file_size(FILE* fd) {
    fseek(fd, 0L, SEEK_END);
	long size = ftell(fd);
	fseek(fd, 0L, SEEK_SET);
	return size;
}

bool recvack(const int seqno, const int sockfd, struct sockaddr_in* client_addr, const long time_out=100000) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = time_out;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("server: error to set timeout");
        exit(-1);
    }

    socklen_t client_addr_len = sizeof(*client_addr);
    char buf[ACK_SIZE];
    int recv_bytes = 0;
    bool acked = true;
    if ((recv_bytes = recvfrom(sockfd, buf, ACK_SIZE, 0, (struct sockaddr *) client_addr,
            &client_addr_len)) != ACK_SIZE) {
        perror("server: recvfrom - ack failed");
        acked = false;
    }
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("server: error to reset timeout");
        exit(-1);
    }
    if (acked) {
        ack_packet ack;
        memcpy(&ack, buf, ACK_SIZE);
        return ack.ackno == seqno;
    }
    return false;
}

bool isdropped(double plp=0.0, double seed=-1.0) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    if (seed > 0.0) {
        gen.seed(seed);
    }
    static std::discrete_distribution<> d({1.0 - plp, plp});
    return d(gen);
}

int stopwait_sendto(const int sockfd, const packet* pckt, struct sockaddr_in* client_addr) {
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
    } while(!recvack(pckt->seqno, sockfd, client_addr));
    return sent;
}

void send_ack(const int sockfd, struct sockaddr_in* client_addr) {

    ack_packet first_ack;
    first_ack.ackno = -1;

    if ((sendto(sockfd, (void *) &first_ack, ACK_SIZE, 0,
                            (struct sockaddr *) client_addr, sizeof(*client_addr))) == -1) {
        perror("server: error sending ACK pckt!");
        exit(-1);
    }
}

/// Return number of bytes sent
/// -1 if error happened
int send_file(const char* file_name, const int sockfd, struct sockaddr_in* client_addr) {
    FILE* fd = fopen(file_name, "r");
    if (fd == NULL) {
        perror("server: File NOT FOUND 404");
		exit(-1);
    }

    long file_size = find_file_size(fd);
    packet curr_pckt;
    curr_pckt.seqno = 0;

    cout << "server: opened file: \"" << file_name << "\"" << endl;
    cout << "file_size: " << file_size << " bytes" << endl;

    for (long sent_bytes = 0; sent_bytes < file_size; sent_bytes+=curr_pckt.len, curr_pckt.seqno++) {
        curr_pckt.cksum = 1;
		curr_pckt.len = fread(curr_pckt.data, 1, BUFFER_SIZE, fd);
        stopwait_sendto(sockfd, &curr_pckt, client_addr);
    }

    curr_pckt.len = 0;
    stopwait_sendto(sockfd, &curr_pckt, client_addr);

    return file_size;
}

int main()
{
    int sockfd = create_socket(8080);
    /* set PLP and random seed */
    isdropped(0.1);
    while(true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);


		/* Block until receive a request from a client */
		cout << "Server is waiting to receive..." << endl;
		char buf[BUFFER_SIZE];
		int recv_bytes = 0;
		if ((recv_bytes = recvfrom(sockfd, buf, BUFFER_SIZE - 1, 0, (struct sockaddr *) &client_addr,
				&client_addr_len)) < 0) {
			perror("server: recvfrom failed");
			exit(-1);
        }
		buf[recv_bytes] = '\0';
		cout << "server: received buffer: " << buf << endl;

		send_ack(sockfd, &client_addr);

		send_file(buf, sockfd, &client_addr);
    }
    return 0;
}
