#ifndef NET_TRANSFER_H
#define NET_TRANSFER_H

#include "protocol2.h"
#include "types.h"
#include "inventory2.h"
#include <stddef.h>
#include <stdint.h>

char *serialize_inventory_text(const Inventory *inventory);
int send_chunked_transfer(Maester *maester, const char *realm, uint8_t header_type,
                          uint8_t data_type, const char *filename, const unsigned char *payload,
                          size_t payload_len, const char *sender_realm);
int send_chunked_transfer_direct(const char *target_ip, int target_port, const char *sender_ip,
                                 int sender_port, const char *sender_realm, const char *dest_realm,
                                 uint8_t header_type, uint8_t data_type, const char *filename,
                                 const unsigned char *payload, size_t payload_len);
void proxy_chunked_transfer(int sender_fd, const Frame *header_frame, const char *dest_ip,
                            int dest_port);

#endif
