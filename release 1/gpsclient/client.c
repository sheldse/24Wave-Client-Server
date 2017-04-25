#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <gps.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "utils.h"
#include "msg.h"
#include "crc16.h"
#include "buffer.h"
#include "config.h"

static struct gps_data_t gpsd;
static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

static int read_gpsd(struct gps_fix_t *fix)
{
	int latlon_set;

	pthread_rwlock_rdlock(&rwlock);
	latlon_set = gpsd.set & LATLON_SET;
	memcpy(fix, &gpsd.fix, sizeof(struct gps_fix_t));
	pthread_rwlock_unlock(&rwlock);
	return (latlon_set && fix->mode > MODE_NO_FIX);
}

static int process_msg(const struct tgr_msg *msg,
		       const struct sockaddr_in *addr,
		       size_t msg_len)
{
	struct tgr_msg tmp;
	char ip_str[INET_ADDRSTRLEN];
	const char *ip_ptr;
	unsigned short crc;	

	ip_ptr = inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
	if (ip_ptr == NULL) {
		debug(DEBUG_INFO, "ip_ptr = NULL");
		return 0;
	}

	if (msg_len < sizeof(struct tgr_msg)) {
		debug(DEBUG_WARNING, "invalid msg length len=%i addr=%s", msg_len, ip_ptr);
		return 0;
	}

	if (!config.packet_validation)
		return 1;

	memcpy(&tmp, msg, sizeof(struct tgr_msg));
	tmp.hdr = ntohs(tmp.hdr);
	tmp.crc = ntohs(tmp.crc);
	tmp.tsp = ntohl(tmp.tsp);

	/* Check header */
	if (tmp.hdr != MSG_HDR) {
		debug(DEBUG_WARNING, "invalid header hdr=%.4x addr=%s", tmp.hdr, ip_ptr);
		return 0;
	}

	/* Check crc checksum */
	crc = crc16(0, (char*) &tmp + 4, sizeof(tmp) - 4);
	if (crc != tmp.crc) {
		debug(DEBUG_WARNING, "invalid checksum crc=%.2x addr=%s", tmp.crc, ip_ptr);
		return 0;
	}

	return 1;
}

static void set_sockaddr(struct sockaddr_in *saddr,
			 const char *ipaddr,
			 unsigned short port)
{
	struct in_addr iaddr;
	int ret;

	memset(saddr, 0, sizeof(struct sockaddr_in));
	saddr->sin_family = AF_INET;
	saddr->sin_port = htons(port);
	if (strcmp(ipaddr, "0.0.0.0")) {
		ret = inet_pton(AF_INET, ipaddr, &iaddr);
		if (ret <= 0) {
			debug(DEBUG_WARNING, "invalid address %s", ipaddr);
			saddr->sin_addr.s_addr = htonl(INADDR_ANY);
		} else
			saddr->sin_addr = iaddr;
	} else
		saddr->sin_addr.s_addr = htonl(INADDR_ANY);
}

static int create_socket(int type,
			 const char *addr,
			 unsigned short port)
{
	int sock, ret, val;
	struct sockaddr_in saddr;
	struct in_addr iaddr;
	struct ip_mreq mreq;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		return sock;

	val = 1;
	ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	if (ret == -1)
		return ret;

	/* Specify multicast group */
	if (type == CONFIG_MCAST) {
		ret = inet_pton(AF_INET, config.mcast_gaddr, &iaddr);
		if (!ret) {
			debug(DEBUG_WARNING, "invalid mcast addr %s", addr);
			return 0;
		}
		mreq.imr_multiaddr = iaddr;
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);

		val = 1;
		ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
		if (ret == -1)
			return ret;
	}

	set_sockaddr(&saddr, addr, port);
	ret = bind(sock, (struct sockaddr*) &saddr, sizeof(struct sockaddr_in));
	if (ret == -1)
		return -1;
	return sock;
}

static void fill_db_data(const struct in_addr *addr,
			 const struct gps_fix_t *fix,
			 struct db_data *db,
			 int type)
{
	char ip_str[INET_ADDRSTRLEN];
	const char *ip_ptr;

	if (addr) {
		ip_ptr = inet_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
		sprintf(db->sender_ip, "%s", ip_ptr);
	} else
		memset(db->sender_ip, 0, sizeof(db->sender_ip));

	if (type == CONFIG_UCAST)
		sprintf(db->client_ip, config.ucast_addr, sizeof(db->client_ip));
	else if (type == CONFIG_MCAST)
		sprintf(db->client_ip, config.mcast_addr, sizeof(db->client_ip));
	else if (type == CONFIG_BCAST)
		sprintf(db->client_ip, config.bcast_addr, sizeof(db->client_ip));
	else
		memset(db->client_ip, 0, sizeof(db->client_ip));

	sprintf(db->client_name, "%s", config.client_name);
	db->gps_tsp = fix->time;
	db->gps_lat = fix->latitude;
	db->gps_lon = fix->longitude;
	db->packet_type = type;
}

static void recv_msg(int sock,
		     int type)
{
	struct sockaddr_in addr;
	struct db_data dbdata;
	struct tgr_msg msg;
	struct gps_fix_t fix;
	fd_set rset;
	int ret;
	unsigned socklen;
	char ipstr[INET_ADDRSTRLEN];
	const char *str;

	if (type == CONFIG_UCAST)
		str = "ucast";
	else if (type == CONFIG_MCAST)
		str = "mcast";
	else /* CONFIG_BCAST */
		str = "bcast";

	while (1) {
		FD_ZERO(&rset);
		FD_SET(sock, &rset);
		ret = select(sock + 1, &rset, NULL, NULL, NULL);
		if (ret == -1) {
			debug(DEBUG_ERROR, "select: %s", strerror(errno));
			_exit(EXIT_FAILURE);
		}
		if (!FD_ISSET(sock, &rset))
			continue;
		socklen = sizeof(struct sockaddr_in);
		ret = recvfrom(sock, &msg, sizeof(struct tgr_msg), 0,
			       (struct sockaddr*) &addr, &socklen);
		if (ret == -1) {
			debug(DEBUG_WARNING, "type=%s recvfrom: %s", str, strerror(errno));
			continue;
		}

		ret = process_msg(&msg, &addr, ret);
		if (!ret)
			continue;

		/* Send ack reply to sender for unicast socket */
		if (type == CONFIG_UCAST) {
			socklen = sizeof(struct sockaddr_in);
			ret = sendto(sock, &msg, sizeof(struct tgr_msg), 0,
				     (struct sockaddr*) &addr, socklen);
			if (ret == -1)
				debug(DEBUG_WARNING, "type=%s sendto: %s", str, strerror(errno));
		}

		inet_ntop(AF_INET, &addr.sin_addr, ipstr, sizeof(ipstr));
		debug(DEBUG_INFO, "msg recvd type=%s addr=%s", str, ipstr);

		ret = read_gpsd(&fix);
		if (ret) {
			debug(DEBUG_INFO, "type=%s addr=%s tsp=%f lat=%f lon=%f", 
			      str, ipstr, fix.time, fix.latitude, fix.longitude);
			if (isnan(fix.time) || isnan(fix.latitude) || isnan(fix.longitude)) {
				debug(DEBUG_WARNING, "invalid gps value (NAN)");
				continue;
			}
			fill_db_data(&addr.sin_addr, &fix, &dbdata, type);
			buffer_insert(&dbdata);
		} else
			debug(DEBUG_WARNING, "no data from gpsd type=%s addr=%s", str, ipstr);
	}
}

static void *unicast_routine(void *data)
{
	int sock;

	sock = create_socket(CONFIG_UCAST, config.ucast_addr, config.ucast_port);
	if (sock == -1) {
		debug(DEBUG_ERROR, "could not create unicast socket: %s", strerror(errno));
		_exit(EXIT_FAILURE);
	}

	while (1) {
		recv_msg(sock, CONFIG_UCAST);
	}

	return NULL;
}

static void *multicast_routine(void *data)
{
	int sock;

	sock = create_socket(CONFIG_MCAST, config.mcast_addr, config.mcast_port);
	if (sock == -1) {
		debug(DEBUG_ERROR, "could not create mcast socket: %s", strerror(errno));
		_exit(EXIT_FAILURE);
	}

	while (1) {
		recv_msg(sock, CONFIG_MCAST);
	}

	return NULL;
}
static void *broadcast_routine(void *data)
{
	int sock;

	sock = create_socket(CONFIG_BCAST, config.bcast_addr, config.bcast_port);
	if (sock == -1) {
		if (errno == EADDRNOTAVAIL) {
			debug(DEBUG_ERROR, "could not create broadcast at %s,"
			      " fallback to 0.0.0.0", config.bcast_addr);
			sock = create_socket(CONFIG_BCAST, "0.0.0.0", config.bcast_port);
			if (sock == -1) {
				debug(DEBUG_ERROR, "could not create broadcast socket: %s",
				      strerror(errno));
				_exit(EXIT_FAILURE);
			}
		}
	}

	while (1) {
		recv_msg(sock, CONFIG_BCAST);
	}

	return NULL;
}

static void *manual_routine(void *data)
{
	int ret;
	struct gps_fix_t fix;
	struct db_data db;

	while (1) {
		ret = read_gpsd(&fix);
		if (ret) {
			fill_db_data(NULL, &fix, &db, CONFIG_MANUAL);
			buffer_insert(&db);
		}
		msleep(5000);
	}
	return NULL;
}

int main(int argc,
	 char **argv)
{
	pthread_t thread[4];
	char gpsd_port[5];
	int ret;
	char *progname, *tmp;

	progname = argv[0];
	if ((tmp = strstr(argv[0], "/")))
		progname = ++tmp;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <config-file>\n", progname);
		exit(EXIT_SUCCESS);
	}

	ret = config_read(argv[1]);
	if (!ret) {
		debug(DEBUG_ERROR, "could not read config file");
		exit(EXIT_FAILURE);
	}

	/* Initialize GPSD connection */
	sprintf(gpsd_port, "%i", config.gpsd_port);
	ret = gps_open(config.gpsd_addr, gpsd_port, &gpsd);
	if (ret == -1) {
		debug(DEBUG_ERROR, "could not connect to gpsd");
		exit(EXIT_FAILURE);
	}

	/* Enable streaming */
	ret = gps_stream(&gpsd, WATCH_ENABLE, NULL);
	if (ret == -1) {
		debug(DEBUG_ERROR, "could not enable gpsd streaming: %s", gps_errstr(errno));
		exit(EXIT_FAILURE);
	}

	/* Initialize buffer */
	ret = buffer_init();
	if (!ret) {
		debug(DEBUG_ERROR, "could not initialize buffer file");
		exit(EXIT_FAILURE);
	}

	ret = pthread_create(&thread[0], NULL, unicast_routine, NULL);
	if (ret) {
		debug(DEBUG_ERROR, "could not create unicast thread");
		_exit(EXIT_FAILURE);
	}

	ret = pthread_create(&thread[1], NULL, broadcast_routine, NULL);
	if (ret) {
		debug(DEBUG_ERROR, "could not create broadcast thread");
		_exit(EXIT_FAILURE);
	}

	ret = pthread_create(&thread[2], NULL, multicast_routine, NULL);
	if (ret) {
		debug(DEBUG_ERROR, "could not create multicast thread\n");
		_exit(EXIT_FAILURE);
	}

	ret = pthread_create(&thread[3], NULL, manual_routine, NULL);
	if (ret) {
		debug(DEBUG_ERROR, "could not create manual thread\n");
		_exit(EXIT_FAILURE);
	}

	/* Loop forever to check new data then read  */
	while (1) {
		ret = gps_waiting(&gpsd, 1000);
		if (ret) {
			pthread_rwlock_wrlock(&rwlock);
			ret = gps_read(&gpsd);
			pthread_rwlock_unlock(&rwlock);
			if (ret == -1) {
				debug(DEBUG_ERROR, "could not read gpsd: %s", gps_errstr(errno));
				_exit(EXIT_FAILURE);
			}
		}
	}

	/* Not reached */
	gps_close(&gpsd);
	pthread_join(thread[0], NULL);
	pthread_join(thread[1], NULL);
	pthread_join(thread[2], NULL);
	pthread_join(thread[3], NULL);	
	_exit(EXIT_SUCCESS);
}
