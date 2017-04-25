# gpsclient Makefile

SOURCES = sqlite3.c utils.c crc16.c database.c config.c buffer.c client.c 
OBJECTS = ${SOURCES:.c=.o}
CFLAGS  = -Wall -g -fstack-protector -I/usr/include/postgresql -DSQLITE_THREADSAFE=1
LIBS    = -lm -lpthread -lgps -lpq
TARGET  = gpsclient

${TARGET}: ${OBJECTS}
	${CC} ${OBJECTS} ${LIBS} -o ${TARGET}

sender: sender.o crc16.o
	${CC} ${LIBS} sender.o crc16.o -o sender

.c.o:
	${CC} ${CFLAGS} -c $<

clean:
	rm -rf *.o ${TARGET} sender

