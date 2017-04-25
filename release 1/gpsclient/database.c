#include <libpq-fe.h>
#include <stdio.h>
#include "utils.h"
#include "database.h"
#include "config.h"

typedef PGconn dbctx_t;

dbctx_t *db_connect(void)
{
	dbctx_t *ctx;
	char conn_str[128];

	snprintf(conn_str, sizeof(conn_str),
		 "hostaddr=%s port=%i dbname=%s user=%s password=%s connect_timeout=3",
		 config.db_addr, config.db_port, config.db_name, config.db_user, 
		 config.db_passwd);

	ctx = PQconnectdb(conn_str);
	if (PQstatus (ctx) != CONNECTION_OK) {
		debug(DEBUG_ERROR, "could not connect to database: %s", PQerrorMessage (ctx));
		PQfinish (ctx);
		return NULL;
	}
	return ctx;
}

void db_close(dbctx_t *ctx)
{
	PQfinish(ctx);
}

int db_insert(dbctx_t *ctx,
	      const struct db_data *data)
{
	PGresult *result;
	int ret;
	char cmd[1024];

	snprintf(cmd, sizeof(cmd),
		 "insert into gpsclient(client_name,client_ip,sender_ip,gps_tsp,gps_latitude,"
		 "gps_longitude,packet_type) values('%s','%s','%s',%f,%f,%f,%i)", 
		 data->client_name, data->client_ip, data->sender_ip, data->gps_tsp, 
		 data->gps_lat,data->gps_lon, data->packet_type);
	result = PQexec (ctx, cmd);
	if (result == NULL) {
		debug(DEBUG_ERROR, "%s", PQerrorMessage (ctx));
		return 0;
	}
	if (PQresultStatus (result) == PGRES_COMMAND_OK)
		ret = 1;
	else {
		debug(DEBUG_ERROR, "could not insert to db: %s", PQresultErrorMessage (result));
		ret = 0;
	}
	PQclear (result);
	return ret;
}
