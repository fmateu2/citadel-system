#ifndef NET_FRAME_H
#define NET_FRAME_H

#include "protocol2.h"
#include "types.h"
#include <stddef.h>
#include <stdint.h>

int write_full(int fd, const unsigned char *buf, size_t size);
int read_full(int fd, unsigned char *buf, size_t size);
int prepare_frame(Frame *frame, uint8_t type, const char *origin_ip, int origin_port,
                  const char *destination, const void *data, size_t len);
int prepare_ack_frame(Frame *frame, uint8_t type, const char *payload);
int send_frame_on_fd(int fd, const Frame *frame);
int receive_frame_on_fd(int fd, Frame *frame);
int connect_to_ip_port(const char *ip, int port);
int send_one_way_to_ip_port(const char *ip, int port, const Frame *frame);
int send_with_reply_to_ip_port(const char *ip, int port, const Frame *frame, Frame *reply);
int find_pending_outgoing_by_realm(Maester *maester, const char *realm);
int send_frame_to_realm(Maester *maester, const char *realm, const Frame *frame);
void send_unknown_realm_error(Maester *maester, const char *target_realm, const char *origin_ip,
                              int origin_port);
void send_order_response(Maester *maester, const char *ip, int port, const char *destination_realm,
                         const char *payload);
int destination_is_local(Maester *maester, const Frame *frame);

#endif
