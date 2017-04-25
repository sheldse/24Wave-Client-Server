#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"
#include "config.h"
#include "database.h"
#include "utils.h"

static sqlite3 *bufdb;

static int buffer_delete(unsigned uid)
{
	char *cmd;
	int ret;

	cmd = sqlite3_mprintf("DELETE FROM buffer WHERE uid=%u", uid);
	ret = sqlite3_exec(bufdb, cmd, NULL, NULL, NULL);
	sqlite3_free(cmd);

	if (ret != SQLITE_OK) {
		debug(DEBUG_ERROR, "could not delete buffer: %s",
		      sqlite3_errmsg(bufdb));
		return 0;
	}
	return 1;
}

static void buffer_process(dbctx_t *dbctx)
{
	int ret, row, col;
	int i, j;
	unsigned uid;
	char **table;
	struct db_data dbdata;

	ret = sqlite3_get_table(bufdb, "SELECT * FROM buffer LIMIT 100",
				&table, &row, &col, NULL);
	if (ret != SQLITE_OK) {
		debug(DEBUG_WARNING, "could not get table: %s", sqlite3_errmsg(bufdb));
		return;
	}

	sqlite3_exec(bufdb, "BEGIN", NULL, NULL, NULL);

	for (i = 0, j = col; i < row; i++, j += col) {
		uid = atoi(table[j]);
		snprintf(dbdata.client_name, sizeof(dbdata.client_name), "%s", table[j + 1]);
		snprintf(dbdata.client_ip, sizeof(dbdata.client_ip), "%s", table[j + 2]);
		snprintf(dbdata.sender_ip, sizeof(dbdata.sender_ip), "%s", table[j + 3]);
		dbdata.gps_tsp = atof(table[j + 4]);
		dbdata.gps_lat = atof(table[j + 5]);
		dbdata.gps_lon = atof(table[j + 6]);
		dbdata.packet_type = atoi(table[j + 7]);

		ret = db_insert(dbctx, &dbdata);
		if (!ret)
			break;
		ret = buffer_delete(uid);
		if (!ret)
			break;
	}

	sqlite3_exec(bufdb, "COMMIT", NULL, NULL, NULL);

	if (i)
		debug(DEBUG_INFO, "processed %i records to db", i);
	sqlite3_free_table(table);
}

static void *buffer_routine(void *data)
{
	dbctx_t *ctx;
	int sleepms = config.buffer_interval * 1000;

	while (1) {
		ctx = db_connect();
		if (ctx) {
			buffer_process(ctx);
			db_close(ctx);
		}
		msleep(sleepms);
	}
	return NULL;
}

static int buffer_start(void)
{
	pthread_t thread;
	int ret;

	ret = pthread_create(&thread, NULL, &buffer_routine, NULL);
	if (ret) {
		debug(DEBUG_ERROR, "could not create buffer thread: %s",
		      strerror(errno));
		return 0;
	}
	return 1;
}

int buffer_init(void)
{
	int ret;
	const char *cmd;

	ret = sqlite3_open(config.buffer_file, &bufdb);
	if (ret != SQLITE_OK) {
		debug(DEBUG_ERROR, "could not open buffer file: %s", sqlite3_errmsg(bufdb));
		sqlite3_close(bufdb);
		return 0;
	}

	ret = sqlite3_exec(bufdb, "PRAGMA synchronous = 1", NULL, NULL, NULL);
	if (ret != SQLITE_OK) {
		debug(DEBUG_ERROR, "could not set buffer option: %s", sqlite3_errmsg(bufdb));
		sqlite3_close(bufdb);
		return 0;
	}

	/* Create buffer table */
	cmd = "CREATE TABLE IF NOT EXISTS buffer("
	      "uid INTEGER PRIMARY KEY,"
	      "client_name TEXT,"
	      "client_ip TEXT,"
              "sender_ip TEXT,"
              "gps_tsp REAL,"
	      "gps_lat REAL,"
	      "gps_lon REAL,"
	      "packet_type INTEGER)";
	ret = sqlite3_exec(bufdb, cmd, NULL, NULL, NULL);
	if (ret != SQLITE_OK) {
		debug(DEBUG_ERROR, "could not create table: %s", sqlite3_errmsg(bufdb));
		return 0;
	}

	/* Start buffer consumer and writer thread */
	ret = buffer_start();
	return ret;
}

int buffer_insert(const struct db_data *db)
{
	char *cmd;
	int ret;

	cmd = sqlite3_mprintf("INSERT INTO buffer VALUES(NULL,'%q','%q','%q',%f,%f,%f,%i)",
			      db->client_name, db->client_ip, db->sender_ip,
			      db->gps_tsp, db->gps_lat, db->gps_lon, db->packet_type);
	ret = sqlite3_exec(bufdb, cmd, NULL, NULL, NULL);
	sqlite3_free(cmd);
	if (ret != SQLITE_OK) {
		debug(DEBUG_ERROR, "could not insert buffer: %s", sqlite3_errmsg(bufdb));
		return 0;
	}
	return 1;
}
