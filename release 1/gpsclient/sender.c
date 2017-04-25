#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "crc16.h"
#include "msg.h"
#include "config.h"

int main(int argc, char **argv)
{
	int sock, type, ret, sleep_time;
	unsigned long seq = 0;
	struct sockaddr_in saddr;
	struct in_addr addr;
	struct tgr_msg msg;

	if (argc < 5) {
		fprintf(stderr, "usage: %s <client-addr> <client-port> <type> <sleep-second>\n", *argv);
		fprintf(stderr, "       type        : 1=unicast, 2=multicast, 3=broadcast\n");
		fprintf(stderr, "       sleep-second: if sleep is 0, exit directly after sending 1 packet\n\n");
		exit(1);
	}

	type = atoi(argv[3]);
	if (type < CONFIG_UCAST || type > CONFIG_BCAST) {
		fprintf(stderr, "invalid type\n");
		exit(1);
	}	

	ret = inet_pton(AF_INET, argv[1], &addr);
	if (!ret) {
		perror("inet_pton");
		exit(1);
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(atoi(argv[2]));
	saddr.sin_addr = addr;	

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == -1) {
		perror("socket");
		exit(1);
	}

	if (type == CONFIG_BCAST) {
		int opt = 1;
		ret =  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
		if (ret == -1) {
			perror("setsockopt");
			exit(1);
		}
	}

	sleep_time = atoi(argv[4]);
	if (sleep_time < 0)
		sleep_time = 0;

	do {
		msg.hdr = MSG_HDR;
		msg.tsp = time(NULL);
		msg.crc = crc16(0, (char*) &msg + 4, sizeof(msg) - 4);

		fprintf(stderr, "%lu tsp=%u crc=%.4x \n", ++seq, msg.tsp, msg.crc);

		msg.hdr = htons(msg.hdr);
		msg.tsp = htonl(msg.tsp);
		msg.crc = htons(msg.crc);

		ret = sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr*) &saddr, sizeof(saddr));
		if (ret == -1)
			perror("sendto");
		if (sleep_time)
			sleep(sleep_time);
	} while (sleep_time);

	return 0;
}
