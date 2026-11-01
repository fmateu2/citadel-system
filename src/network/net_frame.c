#include "net_frame.h"
#include "net_utils.h"
#include "utils2.h"
#include "config2.h"
#include "alliance2.h"
#include "pledge.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

int write_full(int fd, const unsigned char *buf, size_t size) {
    size_t total = 0;

    while (total < size) {
        ssize_t n = write(fd, buf + total, size - total);
        if (n <= 0) {
            return -1;
        }
        total += (size_t)n;
    }

    return 0;
}

int read_full(int fd, unsigned char *buf, size_t size) {
    size_t total = 0;

    while (total < size) {
        ssize_t n = read(fd, buf + total, size - total);
        if (n <= 0) {
            return -1;
        }
        total += (size_t)n;
    }

    return 0;
}

int prepare_frame(Frame *frame, uint8_t type, const char *origin_ip, int origin_port,
                  const char *destination, const void *data, size_t len) {
    char *origin_string;

    if (frame == NULL || origin_ip == NULL || destination == NULL || (data == NULL && len > 0)) {
        return -1;
    }

    init_frame(frame);
    frame->type = type;

    origin_string = build_origin_string(origin_ip, origin_port);
    if (origin_string == NULL) {
        return -1;
    }

    if (fill_fixed_field(frame->origin, ORIGIN_SIZE, origin_string) != 0) {
        free(origin_string);
        return -1;
    }
    free(origin_string);

    if (fill_fixed_field(frame->destination, DESTINATION_SIZE, destination) != 0) {
        return -1;
    }

    return set_frame_data(frame, data, len);
}

int prepare_ack_frame(Frame *frame, uint8_t type, const char *payload) {
    if (frame == NULL || payload == NULL) {
        return -1;
    }

    init_frame(frame);
    frame->type = type;

    memset(frame->origin, 0, ORIGIN_SIZE);
    memset(frame->destination, 0, DESTINATION_SIZE);

    return set_frame_data(frame, payload, strlen(payload));
}

int send_frame_on_fd(int fd, const Frame *frame) {
    unsigned char buffer[FRAME_SIZE];

    if (frame == NULL) {
        return -1;
    }

    serialize_frame(frame, buffer);
    return write_full(fd, buffer, FRAME_SIZE);
}

int receive_frame_on_fd(int fd, Frame *frame) {
    unsigned char buffer[FRAME_SIZE];

    if (read_full(fd, buffer, FRAME_SIZE) != 0) {
        return -1;
    }

    return parse_frame(buffer, frame);
}

int connect_to_ip_port(const char *ip, int port) {
    int sockfd;
    struct sockaddr_in addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int send_one_way_to_ip_port(const char *ip, int port, const Frame *frame) {
    int sockfd;
    int rc;

    sockfd = connect_to_ip_port(ip, port);
    if (sockfd < 0) {
        return -1;
    }

    rc = send_frame_on_fd(sockfd, frame);
    close(sockfd);

    return rc;
}

/* Kept for potential future use; currently intermediary forwarding uses proxy_chunked_transfer. */
int send_with_reply_to_ip_port(const char *ip, int port, const Frame *frame, Frame *reply) {
    int sockfd;
    int rc;

    sockfd = connect_to_ip_port(ip, port);
    if (sockfd < 0) {
        return -1;
    }

    if (send_frame_on_fd(sockfd, frame) != 0) {
        close(sockfd);
        return -1;
    }

    rc = receive_frame_on_fd(sockfd, reply);
    close(sockfd);

    return rc;
}

int find_pending_outgoing_by_realm(Maester *maester, const char *realm) {
    int i;

    for (i = 0; i < maester->num_pending_pledges; i++) {
        if (maester->pending_pledges[i].type == PENDING_OUTGOING &&
            maester->pending_pledges[i].realm != NULL &&
            strcasecmp(maester->pending_pledges[i].realm, realm) == 0) {
            return i;
        }
    }

    return -1;
}

int send_frame_to_realm(Maester *maester, const char *realm, const Frame *frame) {
    int idx;
    Route *route;

    if (maester == NULL || realm == NULL || frame == NULL) {
        return -1;
    }

    idx = find_alliance_index(maester->alliances, maester->num_alliances, realm);
    if (idx >= 0 && maester->alliances[idx].status == ALLIED &&
        maester->alliances[idx].ip != NULL && maester->alliances[idx].port > 0) {
        return send_one_way_to_ip_port(maester->alliances[idx].ip, maester->alliances[idx].port,
                                       frame);
    }

    route = find_route(&maester->config, realm);
    if (route == NULL) {
        return -1;
    }

    return send_one_way_to_ip_port(route->ip, route->port, frame);
}

void send_unknown_realm_error(Maester *maester, const char *target_realm, const char *origin_ip,
                              int origin_port) {
    Frame error_frame;
    char *payload = NULL;

    if (asprintf(&payload, "UNKNOWN_REALM&%s", target_realm) < 0) {
        return;
    }

    if (prepare_frame(&error_frame, TYPE_ERROR_UNKNOWN_REALM, maester->config.ip,
                      maester->config.port, "", payload, strlen(payload)) == 0) {
        send_one_way_to_ip_port(origin_ip, origin_port, &error_frame);
    }

    free(payload);
}

void send_order_response(Maester *maester, const char *ip, int port, const char *destination_realm,
                         const char *payload) {
    Frame frame;
    char *out_payload = NULL;

    if (asprintf(&out_payload, "%s&%s", payload, maester->config.realm) < 0) {
        return;
    }

    if (prepare_frame(&frame, TYPE_ORDER_RESPONSE, maester->config.ip, maester->config.port,
                      destination_realm, out_payload, strlen(out_payload)) == 0) {
        send_one_way_to_ip_port(ip, port, &frame);
    }

    free(out_payload);
}

int destination_is_local(Maester *maester, const Frame *frame) {
    char *destination;
    int result = 0;

    destination = frame_destination_alloc(frame);
    if (destination == NULL) {
        return 0;
    }

    result = (strcasecmp(destination, maester->config.realm) == 0 || destination[0] == '\0');
    free(destination);
    return result;
}
