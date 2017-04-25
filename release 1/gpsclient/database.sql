CREATE TABLE gpsclient (
	uid SERIAL PRIMARY KEY,    -- unique id
	client_name VARCHAR(100),  -- client name
	client_ip VARCHAR(15),     -- client ip address
	sender_ip VARCHAR(15),     -- sender ip address
	gps_tsp FLOAT,             -- gps timestamp
	gps_latitude FLOAT,        -- gps latitude
	gps_longitude FLOAT,       -- gps longitude
	packet_type CHAR           -- type of packet
);
