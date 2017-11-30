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

#define BUFFER_SIZE 200
#define PCKT_HEADER_SIZE 8

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

/// Return number of bytes sent
/// -1 if error happened
int send_file(const char* file_name, const int sockfd, struct sockaddr_in* client_addr) {
    FILE* fd = fopen(file_name, "r");
    if (fd == NULL) {
        perror("server: File NOT FOUND 404");
		exit(-1);
    }

    long file_size = find_file_size(fd);
    char buf[sizeof(packet)];
    packet curr_pckt;
    curr_pckt.seqno = 0;

    for (long sent_bytes = 0; sent_bytes < file_size; sent_bytes+=curr_pckt.len, curr_pckt.seqno++) {
        curr_pckt.cksum = 1;
		curr_pckt.len = fread(curr_pckt.data, 1, BUFFER_SIZE, fd);

        int pckt_size = PCKT_HEADER_SIZE + curr_pckt.len;
        memcpy(buf, &curr_pckt, pckt_size);

        int sent;
        if ((sent = sendto(sockfd, buf, pckt_size, 0,
                        (struct sockaddr *) client_addr, sizeof(*client_addr))) == -1) {
            perror("server: error sending pckt!");
            exit(-1);
        }
    }

    return file_size;
}

int main()
{
    int sockfd = create_socket(8080);

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

		char* ack = "e4ta!";
		int sent;
		if ((sent = sendto(sockfd, ack, strlen(ack), 0,
                        (struct sockaddr *) &client_addr, sizeof(client_addr))) == -1) {
            perror("server: error sending ack!");
            exit(-1);
		}

		send_file(buf, sockfd, &client_addr);
    }
    return 0;
}
