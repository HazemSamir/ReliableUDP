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

int main()
{
    int sockfd = create_socket(5555);

    struct sockaddr_in server_addr;
    socklen_t server_addr_len;
	memset((char*) &server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(8080);

	char *filename = "test.png";
	int sendFlag = sendto(sockfd, filename, strlen(filename), 0,
			(struct sockaddr *) &server_addr, sizeof(server_addr));

    packet curr_pckt;
    ofstream of;
    of.open(filename);
    while(true) {
        /* Block until receive a request from a client */
        cout << "client: waiting to receive..." << endl;
        char buf[BUFFER_SIZE];
        int recv_bytes = 0;
        if ((recv_bytes = recvfrom(sockfd, buf, BUFFER_SIZE - 1, 0, (struct sockaddr *) &server_addr,
                &server_addr_len)) < 0) {
            perror("client: recvfrom failed");
        }
        memcpy(&curr_pckt, buf, recv_bytes);
        if (curr_pckt.len == 0) {
            break;
        }
        cout << "client: received " << recv_bytes << " bytes" << endl;
        cout << "client: data.len= " << curr_pckt.len << " bytes" << endl;
        of.write(curr_pckt.data, curr_pckt.len);
    }
    of.close();
    return 0;
}
