#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <netinet/in.h>

#define CONFIG_MANUAL 0
#define CONFIG_UCAST  1
#define CONFIG_MCAST  2
#define CONFIG_BCAST  3

struct config {
	char client_name[16];
	char ucast_addr[INET_ADDRSTRLEN];
	unsigned short ucast_port;
	char mcast_addr[INET_ADDRSTRLEN];
	unsigned short mcast_port;
	char mcast_gaddr[INET_ADDRSTRLEN];
	char bcast_addr[INET_ADDRSTRLEN];
	unsigned short bcast_port;
	int packet_validation;
	char gpsd_addr[INET_ADDRSTRLEN];
	unsigned short gpsd_port;
	char db_addr[INET_ADDRSTRLEN];
	unsigned short db_port;
	char db_name[16];
	char db_user[16];
	char db_passwd[16];
	char buffer_file[256];
	int buffer_interval;
};

/* Globally accessed configuration */
struct config config;

int config_read(const char *file);

#endif /* _CONFIG_H_ */
