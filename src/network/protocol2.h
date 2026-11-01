#ifndef PROTOCOL2_H
#define PROTOCOL2_H

#include <stddef.h>
#include <stdint.h>

#define ORIGIN_SIZE 20
#define DESTINATION_SIZE 20
#define DATA_SIZE 275

#define FRAME_SIZE (1 + ORIGIN_SIZE + DESTINATION_SIZE + 2 + DATA_SIZE + 2)

enum {
    TYPE_ALLIANCE_REQUEST = 0x01,
    TYPE_SIGIL_DATA = 0x02,
    TYPE_ALLIANCE_RESPONSE = 0x03,
    TYPE_PRODUCT_LIST_REQUEST = 0x11,
    TYPE_PRODUCT_LIST_HEADER = 0x12,
    TYPE_PRODUCT_LIST_DATA = 0x13,
    TYPE_ORDER_REQUEST_HEADER = 0x14,
    TYPE_ORDER_TRANSFER_DATA = 0x15,
    TYPE_ORDER_RESPONSE = 0x16,
    TYPE_ERROR_UNKNOWN_REALM = 0x21,
    TYPE_ERROR_UNAUTHORIZED = 0x25,
    TYPE_PING = 0x26,
    TYPE_DISCONNECT = 0x27,
    TYPE_ACK_FILE = 0x31,
    TYPE_ACK_MD5 = 0x32,
    TYPE_NACK = 0x69
};

typedef struct {
    uint8_t type;
    char origin[ORIGIN_SIZE];
    char destination[DESTINATION_SIZE];
    uint16_t data_length;
    unsigned char data[DATA_SIZE];
    uint16_t checksum;
} Frame;

void init_frame(Frame *frame);
int set_frame_data(Frame *frame, const void *data, size_t len);
uint16_t compute_frame_checksum(const unsigned char buffer[FRAME_SIZE]);
void serialize_frame(const Frame *frame, unsigned char buffer[FRAME_SIZE]);
int parse_frame(const unsigned char buffer[FRAME_SIZE], Frame *frame);

#endif
