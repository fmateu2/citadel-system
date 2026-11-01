/* protocol2.c — Frame serialization, deserialization, and checksum computation. */
#include "protocol2.h"

#include <arpa/inet.h>
#include <string.h>

/* init_frame — Zeroes out all fields of a Frame struct. */
void init_frame(Frame *frame) {
    if (frame == NULL) {
        return;
    }

    memset(frame, 0, sizeof(Frame));
}

/* set_frame_data — Copies data into the frame's data field and sets data_length. */
int set_frame_data(Frame *frame, const void *data, size_t len) {
    if (frame == NULL || (data == NULL && len > 0) || len > DATA_SIZE) {
        return -1;
    }

    memset(frame->data, 0, DATA_SIZE);

    if (len > 0) {
        memcpy(frame->data, data, len);
    }

    frame->data_length = (uint16_t)len;
    return 0;
}

/* compute_frame_checksum — Sums all bytes except the last two (checksum slot), mod 65536. */
uint16_t compute_frame_checksum(const unsigned char buffer[FRAME_SIZE]) {
    uint32_t sum = 0;
    int i;

    if (buffer == NULL) {
        return 0;
    }

    for (i = 0; i < FRAME_SIZE - 2; i++) {
        sum += (uint32_t)buffer[i];
    }

    return (uint16_t)(sum % 65536U);
}

/* serialize_frame — Packs a Frame struct into a fixed-size network byte buffer. */
void serialize_frame(const Frame *frame, unsigned char buffer[FRAME_SIZE]) {
    uint16_t net_len;
    uint16_t checksum;
    int offset = 0;

    memset(buffer, 0, FRAME_SIZE);

    if (frame == NULL) {
        return;
    }

    buffer[offset++] = frame->type;

    memcpy(buffer + offset, frame->origin, ORIGIN_SIZE);
    offset += ORIGIN_SIZE;

    memcpy(buffer + offset, frame->destination, DESTINATION_SIZE);
    offset += DESTINATION_SIZE;

    net_len = htons(frame->data_length);
    memcpy(buffer + offset, &net_len, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    memcpy(buffer + offset, frame->data, DATA_SIZE);
    offset += DATA_SIZE;

    checksum = compute_frame_checksum(buffer);
    checksum = htons(checksum);
    memcpy(buffer + offset, &checksum, sizeof(uint16_t));
}

/* parse_frame — Validates checksum then unpacks a network buffer into a Frame struct. */
int parse_frame(const unsigned char buffer[FRAME_SIZE], Frame *frame) {
    uint16_t received_checksum;
    uint16_t calculated_checksum;
    uint16_t net_len;
    int offset = 0;

    if (buffer == NULL || frame == NULL) {
        return -1;
    }

    memcpy(&received_checksum, buffer + FRAME_SIZE - 2, sizeof(uint16_t));
    received_checksum = ntohs(received_checksum);

    calculated_checksum = compute_frame_checksum(buffer);

    if (received_checksum != calculated_checksum) {
        return -1;
    }

    init_frame(frame);

    frame->type = buffer[offset++];

    memcpy(frame->origin, buffer + offset, ORIGIN_SIZE);
    offset += ORIGIN_SIZE;

    memcpy(frame->destination, buffer + offset, DESTINATION_SIZE);
    offset += DESTINATION_SIZE;

    memcpy(&net_len, buffer + offset, sizeof(uint16_t));
    frame->data_length = ntohs(net_len);
    offset += sizeof(uint16_t);

    if (frame->data_length > DATA_SIZE) {
        return -1;
    }

    memcpy(frame->data, buffer + offset, DATA_SIZE);
    frame->checksum = received_checksum;

    return 0;
}
