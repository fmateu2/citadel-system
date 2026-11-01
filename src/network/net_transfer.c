#include "net_transfer.h"
#include "net_frame.h"
#include "net_utils.h"
#include "utils2.h"
#include "config2.h"
#include "alliance2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

char *serialize_inventory_text(const Inventory *inventory) {
    char *result = NULL;
    int i;

    if (inventory == NULL) {
        return dup_string("");
    }

    result = dup_string("");
    if (result == NULL) {
        return NULL;
    }

    for (i = 0; i < inventory->num_items; i++) {
        char *line = NULL;
        char *tmp = NULL;

        if (asprintf(&line, "%s|%d|%.1f\n", inventory->items[i].name, inventory->items[i].amount,
                     inventory->items[i].weight) < 0) {
            free(result);
            return NULL;
        }

        if (asprintf(&tmp, "%s%s", result, line) < 0) {
            free(line);
            free(result);
            return NULL;
        }

        free(line);
        free(result);
        result = tmp;
    }

    return result;
}

/* Sends payload to an already-resolved target address without needing a Maester*. */
int send_chunked_transfer_direct(const char *target_ip, int target_port, const char *sender_ip,
                                 int sender_port, const char *sender_realm, const char *dest_realm,
                                 uint8_t header_type, uint8_t data_type, const char *filename,
                                 const unsigned char *payload, size_t payload_len) {
    Frame header;
    Frame reply;
    char *md5;
    char *header_data = NULL;
    size_t offset = 0;
    int rc = -1;
    int sockfd;

    if (target_ip == NULL || filename == NULL || payload == NULL || dest_realm == NULL) {
        return -1;
    }

    md5 = md5_bytes_alloc(payload, payload_len);
    if (md5 == NULL) {
        return -1;
    }

    if (header_type == TYPE_ALLIANCE_REQUEST) {
        const char *base = strrchr(filename, '/');
        base = (base != NULL) ? base + 1 : filename;
        if (sender_realm == NULL) {
            free(md5);
            return -1;
        }
        if (asprintf(&header_data, "%s&%s&%zu&%s", sender_realm, base, payload_len, md5) < 0) {
            free(md5);
            return -1;
        }
    } else {
        const char *base = strrchr(filename, '/');
        base = (base != NULL) ? base + 1 : filename;
        if (asprintf(&header_data, "%s&%zu&%s", base, payload_len, md5) < 0) {
            free(md5);
            return -1;
        }
    }
    free(md5);

    if (prepare_frame(&header, header_type, sender_ip, sender_port, dest_realm, header_data,
                      strlen(header_data)) != 0) {
        free(header_data);
        return -1;
    }
    free(header_data);

    sockfd = connect_to_ip_port(target_ip, target_port);
    if (sockfd < 0) {
        return -1;
    }

    if (send_frame_on_fd(sockfd, &header) != 0) {
        close(sockfd);
        return -1;
    }

    if (receive_frame_on_fd(sockfd, &reply) != 0 || reply.type != TYPE_ACK_FILE) {
        close(sockfd);
        return -1;
    }
    {
        char *ack_data = frame_data_to_string_alloc(&reply);
        if (ack_data == NULL || strncmp(ack_data, "OK", 2) != 0) {
            free(ack_data);
            close(sockfd);
            return -1;
        }
        free(ack_data);
    }

    while (offset < payload_len) {
        Frame data_frame;
        size_t chunk_len = payload_len - offset;

        if (chunk_len > DATA_SIZE) {
            chunk_len = DATA_SIZE;
        }

        if (prepare_frame(&data_frame, data_type, sender_ip, sender_port, dest_realm,
                          payload + offset, chunk_len) != 0) {
            close(sockfd);
            return -1;
        }

        if (send_frame_on_fd(sockfd, &data_frame) != 0) {
            close(sockfd);
            return -1;
        }

        offset += chunk_len;
    }

    if (receive_frame_on_fd(sockfd, &reply) == 0 && reply.type == TYPE_ACK_MD5) {
        char *ack_data = frame_data_to_string_alloc(&reply);
        if (ack_data != NULL && strncmp(ack_data, "CHECK_OK", 8) == 0) {
            rc = 0;
        }
        free(ack_data);
    }

    close(sockfd);
    return rc;
}

/* Sends payload in DATA_SIZE chunks with MD5 integrity check over a single persistent TCP
 * connection. */
int send_chunked_transfer(Maester *maester, const char *realm, uint8_t header_type,
                          uint8_t data_type, const char *filename, const unsigned char *payload,
                          size_t payload_len, const char *sender_realm) {
    const char *target_ip = NULL;
    int target_port = 0;
    int idx;
    Route *route = NULL;

    if (maester == NULL || realm == NULL || filename == NULL || payload == NULL) {
        return -1;
    }

    idx = find_alliance_index(maester->alliances, maester->num_alliances, realm);
    if (idx >= 0 && maester->alliances[idx].status == ALLIED &&
        maester->alliances[idx].ip != NULL && maester->alliances[idx].port > 0) {
        target_ip = maester->alliances[idx].ip;
        target_port = maester->alliances[idx].port;
    } else {
        route = find_route(&maester->config, realm);
        if (route != NULL) {
            target_ip = route->ip;
            target_port = route->port;
        }
    }

    if (target_ip == NULL || target_port <= 0) {
        return -1;
    }

    return send_chunked_transfer_direct(target_ip, target_port, maester->config.ip,
                                        maester->config.port, sender_realm, realm, header_type,
                                        data_type, filename, payload, payload_len);
}

/* Relays a full header+data+ACK exchange between sender and destination (intermediary proxy). */
void proxy_chunked_transfer(int sender_fd, const Frame *header_frame, const char *dest_ip,
                            int dest_port) {
    Frame ack;
    int dest_fd;

    dest_fd = connect_to_ip_port(dest_ip, dest_port);
    if (dest_fd < 0) {
        return;
    }

    if (send_frame_on_fd(dest_fd, header_frame) != 0) {
        close(dest_fd);
        return;
    }

    if (receive_frame_on_fd(dest_fd, &ack) != 0) {
        close(dest_fd);
        return;
    }
    send_frame_on_fd(sender_fd, &ack);

    if (ack.type != TYPE_ACK_FILE) {
        close(dest_fd);
        return;
    }

    while (1) {
        fd_set rfds;
        int maxfd = (sender_fd > dest_fd ? sender_fd : dest_fd) + 1;

        FD_ZERO(&rfds);
        FD_SET(sender_fd, &rfds);
        FD_SET(dest_fd, &rfds);

        if (select(maxfd, &rfds, NULL, NULL, NULL) < 0) {
            break;
        }

        if (FD_ISSET(dest_fd, &rfds)) {
            Frame final_ack;
            if (receive_frame_on_fd(dest_fd, &final_ack) == 0) {
                send_frame_on_fd(sender_fd, &final_ack);
            }
            break;
        }

        if (FD_ISSET(sender_fd, &rfds)) {
            Frame chunk;
            if (receive_frame_on_fd(sender_fd, &chunk) != 0) {
                break;
            }
            if (send_frame_on_fd(dest_fd, &chunk) != 0) {
                break;
            }
        }
    }

    close(dest_fd);
}
