#ifndef _MSG_H_
#define _MSG_H_

#define MSG_HDR  0xa0f9

struct tgr_msg {
        unsigned short hdr;     /* header */
        unsigned short crc;     /* crc16 */
        unsigned int tsp;       /* timestamp */
	char __reserved[1016];	/* reserved */
};

#endif
