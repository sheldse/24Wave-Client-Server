#ifndef _DATABASE_H_
#define _DATABASE_H_

#include <libpq-fe.h>
#include <netinet/in.h>

typedef PGconn dbctx_t;

struct db_data {
	char client_name[16];   /* client configured name */
	char client_ip[INET_ADDRSTRLEN];     /* client ip address */
	char sender_ip[INET_ADDRSTRLEN];     /* sender ip address */
	double gps_tsp;         /* gps timestamp */
	double gps_lat;         /* gps latitude */
	double gps_lon;         /* gps longitude */
	int packet_type;        /* type of packet */
};

dbctx_t *db_connect(void);

void db_close(dbctx_t *ctx);

int db_insert(dbctx_t *ctx,
              const struct db_data *data);

#endif /* _DATABASE_H_ */
