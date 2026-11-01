#include "net_handlers.h"
#include "net_frame.h"
#include "net_transfer.h"
#include "net_utils.h"
#include "config2.h"
#include "alliance2.h"
#include "envoy2.h"
#include "pledge.h"
#include "inventory2.h"
#include "utils2.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef enum { TRANSFER_PRODUCTS = 1, TRANSFER_ORDER = 2 } TransferKind;

static void receive_transfer_stream(Maester *maester, const Frame *first_frame, int client_fd,
                                    TransferKind kind);

static int parse_order_item(char *token, char **name, int *qty) {
    char *sep = strrchr(token, ':');

    if (sep == NULL) {
        return -1;
    }

    *sep = '\0';
    *name = token;
    *qty = atoi(sep + 1);

    if (*qty <= 0) {
        return -1;
    }

    return 0;
}

static int endpoint_matches(const char *a_ip, int a_port, const char *b_ip, int b_port) {
    if (a_ip == NULL || b_ip == NULL) {
        return 0;
    }
    return (strcmp(a_ip, b_ip) == 0 && a_port == b_port);
}

static char *realm_from_origin_alloc(Maester *maester, const char origin[ORIGIN_SIZE]) {
    char *ip = NULL;
    int port = 0;
    char *realm = NULL;
    int i;

    if (maester == NULL || parse_origin_alloc(origin, &ip, &port) != 0) {
        return NULL;
    }

    pthread_mutex_lock(&maester->lock);
    for (i = 0; i < maester->num_alliances; i++) {
        if (endpoint_matches(maester->alliances[i].ip, maester->alliances[i].port, ip, port)) {
            realm = strdup(maester->alliances[i].realm);
            break;
        }
    }
    if (realm == NULL) {
        for (i = 0; i < maester->num_pending_pledges; i++) {
            if (endpoint_matches(maester->pending_pledges[i].ip, maester->pending_pledges[i].port,
                                 ip, port)) {
                realm = strdup(maester->pending_pledges[i].realm);
                break;
            }
        }
    }
    pthread_mutex_unlock(&maester->lock);

    free(ip);
    return realm;
}

static int is_allied_origin(Maester *maester, const char origin[ORIGIN_SIZE], char **realm_out) {
    char *ip = NULL;
    int port = 0;
    int ok = 0;
    int i;

    if (realm_out != NULL) {
        *realm_out = NULL;
    }
    if (maester == NULL || parse_origin_alloc(origin, &ip, &port) != 0) {
        return 0;
    }

    pthread_mutex_lock(&maester->lock);
    for (i = 0; i < maester->num_alliances; i++) {
        if (maester->alliances[i].status == ALLIED &&
            endpoint_matches(maester->alliances[i].ip, maester->alliances[i].port, ip, port)) {
            ok = 1;
            if (realm_out != NULL) {
                *realm_out = strdup(maester->alliances[i].realm);
            }
            break;
        }
    }
    pthread_mutex_unlock(&maester->lock);

    free(ip);
    return ok;
}

static float remote_weight_for_product(Maester *maester, const char *name) {
    char *copy;
    char *saveptr = NULL;
    char *line;
    float weight = 0.0f;

    if (maester == NULL || maester->remote_products_text == NULL || name == NULL) {
        return 0.0f;
    }

    copy = strdup(maester->remote_products_text);
    if (copy == NULL) {
        return 0.0f;
    }

    line = strtok_r(copy, "\n", &saveptr);
    while (line != NULL) {
        char **fields = NULL;
        int count = 0;

        fields = split_fields_alloc(line, '|', &count);
        if (fields != NULL && count >= 3 && strcasecmp(fields[0], name) == 0) {
            weight = (float)atof(fields[2]);
            free_split_fields(fields, count);
            break;
        }
        free_split_fields(fields, count);
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(copy);
    return weight;
}

static void apply_buyer_order_text(Maester *maester, const char *order_text) {
    char *order_copy;
    char *saveptr = NULL;
    char *token;

    if (maester == NULL || order_text == NULL) {
        return;
    }

    order_copy = strdup(order_text);
    if (order_copy == NULL) {
        return;
    }

    token = strtok_r(order_copy, ",", &saveptr);
    while (token != NULL) {
        char *name = NULL;
        int qty = 0;
        char *tok_copy = strdup(token);

        if (tok_copy != NULL && parse_order_item(tok_copy, &name, &qty) == 0) {
            int j;
            int found = 0;

            for (j = 0; j < maester->inventory.num_items; j++) {
                if (strcasecmp(maester->inventory.items[j].name, name) == 0) {
                    maester->inventory.items[j].amount += qty;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                Product *tmp =
                    realloc(maester->inventory.items,
                            sizeof(Product) * ((size_t)maester->inventory.num_items + 1));
                if (tmp != NULL) {
                    maester->inventory.items = tmp;
                    memset(&maester->inventory.items[maester->inventory.num_items], 0,
                           sizeof(Product));
                    strncpy(maester->inventory.items[maester->inventory.num_items].name, name,
                            sizeof(maester->inventory.items[0].name) - 1);
                    maester->inventory.items[maester->inventory.num_items].amount = qty;
                    maester->inventory.items[maester->inventory.num_items].weight =
                        remote_weight_for_product(maester, name);
                    maester->inventory.num_items++;
                }
            }
        }
        free(tok_copy);
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(order_copy);
}

static void display_remote_products(const char *realm, const char *text) {
    char *copy;
    char *p;
    int n = 1;
    char *line = NULL;

    if (asprintf(&line, "Listing products from %s:\n", realm) >= 0) {
        printF(line);
    }
    free(line);
    line = NULL;

    copy = strdup(text);
    if (copy == NULL) {
        return;
    }

    p = copy;
    while (*p != '\0') {
        char *nl = strchr(p, '\n');
        char *sep = strchr(p, '|');

        if (nl != NULL) {
            *nl = '\0';
        }
        if (sep != NULL) {
            char *sep2 = strchr(sep + 1, '|');
            if (sep2 != NULL) {
                *sep2 = '\0';
            }
            *sep = '\0';
            int amount = atoi(sep + 1);
            if (*p != '\0') {
                if (asprintf(&line, "%d. %s (%d units)\n", n++, p, amount) >= 0) {
                    printF(line);
                }
                free(line);
                line = NULL;
            }
        }

        if (nl != NULL) {
            p = nl + 1;
        } else {
            break;
        }
    }

    free(copy);
}

void handle_local_alliance_request(Maester *maester, const Frame *frame, int client_fd) {
    Frame ack;
    char *payload;
    char **fields = NULL;
    int count = 0;
    char *origin_ip = NULL;
    int origin_port = 0;
    char *ack_payload = NULL;

    payload = frame_data_to_string_alloc(frame);
    if (payload == NULL) {
        return;
    }

    fields = split_fields_alloc(payload, '&', &count);
    free(payload);

    if (fields == NULL || count < 4) {
        free_split_fields(fields, count);
        return;
    }

    if (parse_origin_alloc(frame->origin, &origin_ip, &origin_port) != 0) {
        free_split_fields(fields, count);
        return;
    }

    pthread_mutex_lock(&maester->lock);
    free(maester->pending_rx_md5);
    free(maester->pending_rx_realm);
    free(maester->pending_rx_origin_ip);
    maester->pending_rx_size = (size_t)atol(fields[2]);
    maester->pending_rx_md5 = strdup(fields[3]);
    maester->pending_rx_realm = strdup(fields[0]);
    maester->pending_rx_origin_ip = strdup(origin_ip);
    maester->pending_rx_origin_port = origin_port;
    maester->pending_rx_kind = 0;
    pthread_mutex_unlock(&maester->lock);

    if (asprintf(&ack_payload, "OK&%s", maester->config.realm) >= 0) {
        if (prepare_ack_frame(&ack, TYPE_ACK_FILE, ack_payload) == 0) {
            send_frame_on_fd(client_fd, &ack);
        }
    }
    free(ack_payload);

    free_split_fields(fields, count);
    free(origin_ip);

    {
        Frame first_data;
        if (receive_frame_on_fd(client_fd, &first_data) == 0) {
            receive_transfer_stream(maester, &first_data, client_fd, 0);
        }
    }
}

void handle_local_alliance_response(Maester *maester, const Frame *frame) {
    char *payload;
    char *decision = NULL;
    char *realm_name = NULL;
    char *origin_ip = NULL;
    int origin_port = 0;
    int idx;
    int envoy_id = -1;
    char *line = NULL;

    payload = frame_data_to_string_alloc(frame);
    if (payload == NULL) {
        return;
    }

    split_two_fields_alloc(payload, &decision, &realm_name);
    parse_origin_alloc(frame->origin, &origin_ip, &origin_port);

    if (realm_name == NULL || decision == NULL) {
        free(payload);
        free(decision);
        free(realm_name);
        free(origin_ip);
        return;
    }

    pthread_mutex_lock(&maester->lock);
    idx = find_pending_outgoing_by_realm(maester, realm_name);
    if (idx < 0) {
        /* No matching outgoing pledge: already timed out or processed — discard late response */
        pthread_mutex_unlock(&maester->lock);
        free(payload);
        free(decision);
        free(realm_name);
        free(origin_ip);
        return;
    }
    envoy_id = maester->pending_pledges[idx].envoy_id;
    remove_pending_pledge_by_request(maester, maester->pending_pledges[idx].request_id);

    if (strcasecmp(decision, "ACCEPT") == 0) {
        upsert_alliance(&maester->alliances, &maester->num_alliances, realm_name, origin_ip,
                        origin_port, ALLIED);
        asprintf(&line, "\n>>> Alliance with %s forged successfully!\n$ ", realm_name);
    } else {
        upsert_alliance(&maester->alliances, &maester->num_alliances, realm_name, origin_ip,
                        origin_port, REJECTED);
        asprintf(&line, "\n>>> Alliance with %s rejected.\n$ ", realm_name);
    }
    pthread_mutex_unlock(&maester->lock);

    if (envoy_id >= 0) {
        release_envoy(maester->envoys, maester->num_envoys, envoy_id);
    }

    if (line != NULL) {
        printF(line);
    }

    free(line);
    free(payload);
    free(decision);
    free(realm_name);
    free(origin_ip);
}

void handle_local_product_list_request(Maester *maester, const Frame *frame) {
    char *requester_realm = NULL;
    char *text = NULL;
    char *line = NULL;

    requester_realm = frame_data_to_string_alloc(frame);
    if (requester_realm == NULL) {
        return;
    }

    if (!is_allied(maester->alliances, maester->num_alliances, requester_realm)) {
        Frame error;
        char *origin_ip = NULL;
        int origin_port = 0;
        char *edata = NULL;

        if (parse_origin_alloc(frame->origin, &origin_ip, &origin_port) == 0 &&
            asprintf(&edata, "AUTH&%s", maester->config.realm) >= 0 &&
            prepare_frame(&error, TYPE_ERROR_UNAUTHORIZED, maester->config.ip, maester->config.port,
                          requester_realm, edata, strlen(edata)) == 0) {
            send_one_way_to_ip_port(origin_ip, origin_port, &error);
        }
        free(origin_ip);
        free(edata);
        free(requester_realm);
        return;
    }

    if (asprintf(&line, ">>> LIST PRODUCTS request from %s.\nUnveiling the vault of goods...\n",
                 requester_realm) >= 0) {
        printF(line);
    }
    free(line);
    line = NULL;

    text = serialize_inventory_text(&maester->inventory);
    if (text == NULL) {
        free(requester_realm);
        return;
    }

    if (send_chunked_transfer(maester, requester_realm, TYPE_PRODUCT_LIST_HEADER,
                              TYPE_PRODUCT_LIST_DATA, "products.txt", (const unsigned char *)text,
                              strlen(text), maester->config.realm) == 0) {
        if (asprintf(&line, "The list has been sealed and sent forth.\n") >= 0) {
            printF(line);
        }
        free(line);
        line = NULL;
    }

    free(text);
    free(requester_realm);
}

void handle_local_product_list_header(Maester *maester, const Frame *frame, int client_fd) {
    char *payload;
    char **fields = NULL;
    int count = 0;
    char *ack_payload = NULL;
    Frame ack;

    payload = frame_data_to_string_alloc(frame);
    if (payload == NULL) {
        return;
    }

    fields = split_fields_alloc(payload, '&', &count);
    free(payload);

    if (fields == NULL || count < 3) {
        free_split_fields(fields, count);
        return;
    }

    {
        char *sender_realm = NULL;

        if (count >= 4) {
            sender_realm = strdup(fields[3]);
        } else {
            sender_realm = realm_from_origin_alloc(maester, frame->origin);
        }
        if (sender_realm == NULL) {
            sender_realm = strdup("");
        }

        pthread_mutex_lock(&maester->lock);
        free(maester->pending_rx_md5);
        free(maester->pending_rx_realm);
        free(maester->pending_rx_origin_ip);
        maester->pending_rx_size = (size_t)atol(fields[1]);
        maester->pending_rx_md5 = strdup(fields[2]);
        maester->pending_rx_realm = sender_realm;
        maester->pending_rx_origin_ip = NULL;
        maester->pending_rx_origin_port = 0;
        maester->pending_rx_kind = 1;
        pthread_mutex_unlock(&maester->lock);
    }

    free_split_fields(fields, count);

    if (asprintf(&ack_payload, "OK&%s", maester->config.realm) >= 0) {
        if (prepare_ack_frame(&ack, TYPE_ACK_FILE, ack_payload) == 0) {
            send_frame_on_fd(client_fd, &ack);
        }
    }
    free(ack_payload);

    {
        Frame first_data;
        if (receive_frame_on_fd(client_fd, &first_data) == 0) {
            receive_transfer_stream(maester, &first_data, client_fd, 1);
        }
    }
}

void handle_local_order_header(Maester *maester, const Frame *frame, int client_fd) {
    char *payload;
    char **fields = NULL;
    int count = 0;
    char *ack_payload = NULL;
    Frame ack;

    payload = frame_data_to_string_alloc(frame);
    if (payload == NULL) {
        return;
    }

    fields = split_fields_alloc(payload, '&', &count);
    free(payload);

    if (fields == NULL || count < 3) {
        free_split_fields(fields, count);
        return;
    }

    {
        char *sender_realm = NULL;

        if (count >= 4) {
            sender_realm = strdup(fields[3]);
        } else {
            sender_realm = realm_from_origin_alloc(maester, frame->origin);
        }
        if (sender_realm == NULL || !is_allied_origin(maester, frame->origin, NULL)) {
            if (asprintf(&ack_payload, "KO&%s", maester->config.realm) >= 0) {
                if (prepare_ack_frame(&ack, TYPE_ACK_FILE, ack_payload) == 0) {
                    send_frame_on_fd(client_fd, &ack);
                }
            }
            free(ack_payload);
            free(sender_realm);
            free_split_fields(fields, count);
            return;
        }

        pthread_mutex_lock(&maester->lock);
        free(maester->pending_rx_md5);
        free(maester->pending_rx_realm);
        free(maester->pending_rx_origin_ip);
        maester->pending_rx_size = (size_t)atol(fields[1]);
        maester->pending_rx_md5 = strdup(fields[2]);
        maester->pending_rx_realm = sender_realm;
        maester->pending_rx_origin_ip = NULL;
        maester->pending_rx_origin_port = 0;
        maester->pending_rx_kind = 2;
        pthread_mutex_unlock(&maester->lock);
    }

    free_split_fields(fields, count);

    if (asprintf(&ack_payload, "OK&%s", maester->config.realm) >= 0) {
        if (prepare_ack_frame(&ack, TYPE_ACK_FILE, ack_payload) == 0) {
            send_frame_on_fd(client_fd, &ack);
        }
    }
    free(ack_payload);

    {
        Frame first_data;
        if (receive_frame_on_fd(client_fd, &first_data) == 0) {
            receive_transfer_stream(maester, &first_data, client_fd, 2);
        }
    }
}

static void receive_transfer_stream(Maester *maester, const Frame *first_frame, int client_fd,
                                    TransferKind kind) {
    size_t expected_size;
    char *expected_md5 = NULL;
    char *sender_realm = NULL;
    char *origin_ip = NULL;
    int origin_port;
    int rx_kind;
    unsigned char *buf = NULL;
    size_t total = 0;
    size_t chunk;
    char *actual_md5 = NULL;
    Frame ack;
    char *ack_payload = NULL;
    int ok = 0;
    int ack_sent = 0;

    (void)kind;

    pthread_mutex_lock(&maester->lock);
    expected_size = maester->pending_rx_size;
    expected_md5 = maester->pending_rx_md5 ? strdup(maester->pending_rx_md5) : NULL;
    sender_realm = maester->pending_rx_realm ? strdup(maester->pending_rx_realm) : NULL;
    origin_ip = maester->pending_rx_origin_ip ? strdup(maester->pending_rx_origin_ip) : NULL;
    origin_port = maester->pending_rx_origin_port;
    rx_kind = maester->pending_rx_kind;
    free(maester->pending_rx_md5);
    maester->pending_rx_md5 = NULL;
    free(maester->pending_rx_realm);
    maester->pending_rx_realm = NULL;
    free(maester->pending_rx_origin_ip);
    maester->pending_rx_origin_ip = NULL;
    maester->pending_rx_size = 0;
    pthread_mutex_unlock(&maester->lock);

    if (expected_size > 0 && expected_md5 != NULL) {
        uint8_t expected_data_type;
        if (rx_kind == 0) {
            expected_data_type = TYPE_SIGIL_DATA;
        } else if (rx_kind == 1) {
            expected_data_type = TYPE_PRODUCT_LIST_DATA;
        } else {
            expected_data_type = TYPE_ORDER_TRANSFER_DATA;
        }

        buf = malloc(expected_size);
        if (buf != NULL && first_frame->type == expected_data_type) {
            chunk = first_frame->data_length;
            if (chunk > expected_size) {
                chunk = expected_size;
            }
            memcpy(buf, first_frame->data, chunk);
            total += chunk;

            while (total < expected_size) {
                Frame next;

                if (receive_frame_on_fd(client_fd, &next) != 0) {
                    break;
                }
                if (next.type != expected_data_type) {
                    break;
                }
                chunk = next.data_length;
                if (total + chunk > expected_size) {
                    chunk = expected_size - total;
                }
                memcpy(buf + total, next.data, chunk);
                total += chunk;
            }

            actual_md5 = md5_bytes_alloc(buf, total);
            if (actual_md5 != NULL && strcmp(actual_md5, expected_md5) == 0) {
                ok = 1;
            }
            free(actual_md5);
        }
    }

    if (ok) {
        if (rx_kind == 0) {
            pthread_mutex_lock(&maester->lock);
            add_pending_pledge(maester, maester->next_request_id++, sender_realm, origin_ip,
                               origin_port, -1, PENDING_INCOMING);
            upsert_alliance(&maester->alliances, &maester->num_alliances, sender_realm, origin_ip,
                            origin_port, ON_PENDING);
            pthread_mutex_unlock(&maester->lock);

            print_dynamic_line(">>> Alliance request received from %s.\n", sender_realm);
            printF("$ ");

        } else if (rx_kind == 1) {
            char *text = malloc(total + 1);

            if (text != NULL) {
                char *realm_copy = NULL;

                memcpy(text, buf, total);
                text[total] = '\0';

                pthread_mutex_lock(&maester->lock);
                free(maester->remote_products_text);
                free(maester->remote_products_realm);
                maester->remote_products_text = text;
                maester->remote_products_realm = strdup(sender_realm ? sender_realm : "");
                realm_copy =
                    maester->remote_products_realm ? strdup(maester->remote_products_realm) : NULL;
                pthread_mutex_unlock(&maester->lock);

                if (realm_copy != NULL) {
                    display_remote_products(realm_copy, text);
                    free(realm_copy);
                }

                pthread_mutex_lock(&maester->lock);
                {
                    int pidx;
                    int released = 0;
                    for (pidx = 0; pidx < maester->num_pending_pledges; pidx++) {
                        if (maester->pending_pledges[pidx].type == PENDING_PRODUCTS &&
                            maester->pending_pledges[pidx].realm != NULL && sender_realm != NULL &&
                            strcasecmp(maester->pending_pledges[pidx].realm, sender_realm) == 0) {
                            if (maester->pending_pledges[pidx].envoy_id >= 0) {
                                release_envoy(maester->envoys, maester->num_envoys,
                                              maester->pending_pledges[pidx].envoy_id);
                                released = 1;
                            }
                            remove_pending_pledge_by_request(
                                maester, maester->pending_pledges[pidx].request_id);
                            break;
                        }
                    }
                    (void)released;
                }
                pthread_mutex_unlock(&maester->lock);

                printF("$ ");
            }

        } else if (rx_kind == 2) {
            int all_ok = 1;
            int i;
            char *copy;
            char *token;
            char *saveptr = NULL;
            char *order_text = malloc(total + 1);
            char *line = NULL;
            int item_count = 0;
            char **item_names = NULL;
            int *item_qtys = NULL;
            int *inv_idx = NULL;

            if (order_text != NULL) {
                memcpy(order_text, buf, total);
                order_text[total] = '\0';

                print_dynamic_line(">>> Trade request received from %s.\n", sender_realm);

                /* Save order to order_<realm>.txt */
                {
                    char *fname = NULL;
                    const char *base =
                        (maester->config.path != NULL && maester->config.path[0] != '\0')
                            ? maester->config.path
                            : ".";
                    if (asprintf(&fname, "%s/order_%s.txt", base,
                                 sender_realm ? sender_realm : "unknown") >= 0) {
                        int ofd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (ofd >= 0) {
                            write_full(ofd, (const unsigned char *)order_text, total);
                            close(ofd);
                        }
                        free(fname);
                    }
                }

                copy = strdup(order_text);
                if (copy != NULL) {
                    token = strtok_r(copy, ",", &saveptr);
                    while (token != NULL) {
                        item_count++;
                        token = strtok_r(NULL, ",", &saveptr);
                    }
                    free(copy);
                }

                item_names = calloc((size_t)item_count, sizeof(char *));
                item_qtys = calloc((size_t)item_count, sizeof(int));
                inv_idx = calloc((size_t)item_count, sizeof(int));

                if (item_names != NULL && item_qtys != NULL && inv_idx != NULL) {
                    copy = strdup(order_text);
                    saveptr = NULL;
                    i = 0;
                    if (copy != NULL) {
                        token = strtok_r(copy, ",", &saveptr);
                        while (token != NULL && i < item_count) {
                            char *name = NULL;
                            int qty = 0;
                            char *tok_copy = strdup(token);

                            if (tok_copy != NULL && parse_order_item(tok_copy, &name, &qty) == 0) {
                                item_names[i] = strdup(name);
                                item_qtys[i] = qty;
                            }
                            free(tok_copy);
                            i++;
                            token = strtok_r(NULL, ",", &saveptr);
                        }
                        free(copy);
                    }

                    {
                        int all_found = 1;
                        pthread_mutex_lock(&maester->lock);
                        for (i = 0; i < item_count; i++) {
                            int j;
                            int found = 0;

                            if (item_names[i] == NULL) {
                                all_ok = 0;
                                all_found = 0;
                                continue;
                            }

                            for (j = 0; j < maester->inventory.num_items; j++) {
                                if (strcasecmp(maester->inventory.items[j].name, item_names[i]) ==
                                    0) {
                                    found = 1;
                                    inv_idx[i] = j;
                                    if (maester->inventory.items[j].amount < item_qtys[i]) {
                                        all_ok = 0;
                                    }
                                    break;
                                }
                            }
                            if (!found) {
                                all_ok = 0;
                                all_found = 0;
                            }
                        }

                        if (all_ok) {
                            for (i = 0; i < item_count; i++) {
                                if (item_names[i] != NULL) {
                                    maester->inventory.items[inv_idx[i]].amount -= item_qtys[i];
                                }
                            }
                            save_inventory(maester->inventory, maester->inventory_path);
                        }
                        pthread_mutex_unlock(&maester->lock);

                        if (ok) {
                            if (asprintf(&ack_payload, "CHECK_OK&%s", maester->config.realm) >= 0) {
                                if (prepare_ack_frame(&ack, TYPE_ACK_MD5, ack_payload) == 0) {
                                    send_frame_on_fd(client_fd, &ack);
                                }
                            }
                            free(ack_payload);
                            ack_payload = NULL;
                            ack_sent = 1;
                        }

                        if (all_ok) {
                            Frame resp;
                            char *resp_payload = NULL;
                            if (asprintf(&resp_payload, "OK") >= 0) {
                                if (prepare_frame(&resp, TYPE_ORDER_RESPONSE, maester->config.ip,
                                                  maester->config.port, sender_realm, resp_payload,
                                                  strlen(resp_payload)) == 0) {
                                    send_frame_to_realm(maester, sender_realm, &resp);
                                }
                            }
                            free(resp_payload);
                            if (asprintf(&line, "Order processed successfully. Stock updated.\n") >=
                                0) {
                                printF(line);
                            }
                        } else {
                            Frame resp;
                            char *resp_payload = NULL;
                            const char *reject_reason =
                                all_found ? "OUT_OF_STOCK" : "UNKNOWN_PRODUCT";
                            if (asprintf(&resp_payload, "REJECT&%s", reject_reason) >= 0) {
                                if (prepare_frame(&resp, TYPE_ORDER_RESPONSE, maester->config.ip,
                                                  maester->config.port, sender_realm, resp_payload,
                                                  strlen(resp_payload)) == 0) {
                                    send_frame_to_realm(maester, sender_realm, &resp);
                                }
                            }
                            free(resp_payload);
                            if (asprintf(
                                    &line,
                                    "The vaults stand empty; the order cannot be fulfilled.\n") >=
                                0) {
                                printF(line);
                            }
                        }
                    }
                } else {
                    all_ok = 0;
                }

                if (ok && !ack_sent) {
                    if (asprintf(&ack_payload, "CHECK_OK&%s", maester->config.realm) >= 0) {
                        if (prepare_ack_frame(&ack, TYPE_ACK_MD5, ack_payload) == 0) {
                            send_frame_on_fd(client_fd, &ack);
                        }
                    }
                    free(ack_payload);
                    ack_payload = NULL;
                    ack_sent = 1;
                }

                free(line);
                if (item_names != NULL) {
                    for (i = 0; i < item_count; i++) {
                        free(item_names[i]);
                    }
                }
                free(item_names);
                free(item_qtys);
                free(inv_idx);
                printF("$ ");
            }
            free(order_text);
        }
    }

    free(buf);

    if (!ack_sent) {
        if (ok) {
            if (asprintf(&ack_payload, "CHECK_OK&%s", maester->config.realm) >= 0) {
                if (prepare_ack_frame(&ack, TYPE_ACK_MD5, ack_payload) == 0) {
                    send_frame_on_fd(client_fd, &ack);
                }
            }
            free(ack_payload);
        } else {
            if (asprintf(&ack_payload, "CHECK_KO&%s", maester->config.realm) >= 0) {
                if (prepare_ack_frame(&ack, TYPE_ACK_MD5, ack_payload) == 0) {
                    send_frame_on_fd(client_fd, &ack);
                }
            }
            free(ack_payload);
        }
    }

    free(expected_md5);
    free(sender_realm);
    free(origin_ip);
}

void handle_local_disconnect(Maester *maester, const Frame *frame) {
    char *origin_ip = NULL;
    int origin_port = 0;
    char *realm = NULL;
    char *line = NULL;

    parse_origin_alloc(frame->origin, &origin_ip, &origin_port);
    realm = frame_data_to_string_alloc(frame);

    if (realm != NULL) {
        pthread_mutex_lock(&maester->lock);
        upsert_alliance(&maester->alliances, &maester->num_alliances, realm, origin_ip, origin_port,
                        FAILED);
        pthread_mutex_unlock(&maester->lock);

        if (asprintf(&line, ">>> %s has signed off.\n", realm) >= 0) {
            printF(line);
        }
    }

    free(origin_ip);
    free(realm);
    free(line);
}

void handle_local_info_frame(Maester *maester, const Frame *frame) {
    char *payload;
    char *line = NULL;

    payload = frame_data_to_string_alloc(frame);
    if (payload == NULL) {
        return;
    }

    switch (frame->type) {
    case TYPE_ERROR_UNKNOWN_REALM:
        if (asprintf(&line, "ERROR: %s\n", payload) >= 0) {
            printF(line);
        }
        break;
    case TYPE_ERROR_UNAUTHORIZED: {
        char *orig_ip = NULL;
        int orig_port = 0;
        int k;

        if (parse_origin_alloc(frame->origin, &orig_ip, &orig_port) == 0) {
            pthread_mutex_lock(&maester->lock);
            for (k = 0; k < maester->num_pending_pledges; k++) {
                if ((maester->pending_pledges[k].type == PENDING_PRODUCTS ||
                     maester->pending_pledges[k].type == PENDING_TRADE) &&
                    endpoint_matches(maester->pending_pledges[k].ip,
                                     maester->pending_pledges[k].port, orig_ip, orig_port)) {
                    if (maester->pending_pledges[k].envoy_id >= 0) {
                        release_envoy(maester->envoys, maester->num_envoys,
                                      maester->pending_pledges[k].envoy_id);
                    }
                    remove_pending_pledge_by_request(maester,
                                                     maester->pending_pledges[k].request_id);
                    break;
                }
            }
            pthread_mutex_unlock(&maester->lock);
        }
        free(orig_ip);
    }
        if (asprintf(&line, "ERROR: %s\n", payload) >= 0) {
            printF(line);
        }
        break;
    case TYPE_ORDER_RESPONSE: {
        char **fields = NULL;
        int count = 0;
        char *orig_ip = NULL;
        int orig_port = 0;
        int trade_idx = -1;
        int trade_envoy_id = -1;
        char *realm = NULL;
        char *order_text = NULL;
        int accepted = 0;
        int k;

        fields = split_fields_alloc(payload, '&', &count);
        parse_origin_alloc(frame->origin, &orig_ip, &orig_port);

        if (fields != NULL && count >= 1 &&
            (strcmp(fields[0], "OK") == 0 || strcmp(fields[0], "ACCEPTED") == 0)) {
            accepted = 1;
        }

        pthread_mutex_lock(&maester->lock);
        for (k = 0; k < maester->num_pending_pledges; k++) {
            if (maester->pending_pledges[k].type == PENDING_TRADE &&
                endpoint_matches(maester->pending_pledges[k].ip, maester->pending_pledges[k].port,
                                 orig_ip, orig_port)) {
                trade_idx = k;
                break;
            }
        }
        if (trade_idx < 0) {
            for (k = 0; k < maester->num_pending_pledges; k++) {
                if (maester->pending_pledges[k].type == PENDING_TRADE &&
                    maester->pending_pledges[k].realm != NULL) {
                    trade_idx = k;
                    break;
                }
            }
        }

        if (trade_idx >= 0) {
            realm = strdup(maester->pending_pledges[trade_idx].realm);
            order_text = maester->pending_pledges[trade_idx].order_text
                             ? strdup(maester->pending_pledges[trade_idx].order_text)
                             : NULL;
            trade_envoy_id = maester->pending_pledges[trade_idx].envoy_id;
            remove_pending_pledge_by_request(maester,
                                             maester->pending_pledges[trade_idx].request_id);
        }

        if (accepted && order_text != NULL) {
            apply_buyer_order_text(maester, order_text);
            save_inventory(maester->inventory, maester->inventory_path);
        }
        if (trade_envoy_id >= 0) {
            release_envoy(maester->envoys, maester->num_envoys, trade_envoy_id);
        }
        pthread_mutex_unlock(&maester->lock);

        if (realm == NULL) {
            realm = realm_from_origin_alloc(maester, frame->origin);
        }
        if (realm == NULL) {
            realm = strdup("(unknown)");
        }

        if (accepted) {
            if (asprintf(&line, "\n>>> Order accepted by %s. Stock updated.\n$ ", realm) >= 0) {
                printF(line);
            }
        } else {
            if (asprintf(&line, "\n>>> Order rejected by %s.\n$ ", realm) >= 0) {
                printF(line);
            }
        }

        free(order_text);
        free(realm);
        free(orig_ip);
        free_split_fields(fields, count);
        break;
    }
    case TYPE_NACK:
        if (asprintf(&line, "ERROR: NACK from remote node (%s)\n", payload) >= 0) {
            printF(line);
        }
        break;
    default:
        break;
    }

    free(payload);
    free(line);
}

void process_incoming_frame(Maester *maester, int client_fd, const Frame *frame) {
    char *destination;
    char *origin_ip = NULL;
    int origin_port = 0;
    Route *route;
    Frame reply;

    destination = frame_destination_alloc(frame);
    if (destination == NULL) {
        return;
    }

    if (destination[0] != '\0' && !destination_is_local(maester, frame)) {
        route = find_route(&maester->config, destination);
        if (route == NULL) {
            if (parse_origin_alloc(frame->origin, &origin_ip, &origin_port) == 0) {
                send_unknown_realm_error(maester, destination, origin_ip, origin_port);
            }
            free(destination);
            free(origin_ip);
            return;
        }

        if (frame->type == TYPE_ALLIANCE_REQUEST) {
            char *payload = frame_data_to_string_alloc(frame);
            char *line = NULL;
            if (payload != NULL) {
                char **fields = NULL;
                int count = 0;
                fields = split_fields_alloc(payload, '&', &count);
                const char *sender = (fields != NULL && count >= 1) ? fields[0] : payload;
                if (asprintf(&line, ">>> Received hop: %s -> %s\n", sender, destination) >= 0) {
                    printF(line);
                }
                free_split_fields(fields, count);
                free(line);
                line = NULL;
            }
            free(payload);
            const char *rip = "(unknown)";
            if (route->ip != NULL) {
                rip = route->ip;
            }
            if (asprintf(&line, "Found route: %s -> %s:%d\nForwarding...\n", destination, rip,
                         route->port) >= 0) {
                printF(line);
            }
            free(line);
        }

        if (frame->type == TYPE_ALLIANCE_REQUEST || frame->type == TYPE_PRODUCT_LIST_HEADER ||
            frame->type == TYPE_ORDER_REQUEST_HEADER) {
            proxy_chunked_transfer(client_fd, frame, route->ip, route->port);
            free(destination);
            return;
        }

        send_one_way_to_ip_port(route->ip, route->port, frame);
        free(destination);
        return;
    }

    switch (frame->type) {
    case TYPE_ALLIANCE_REQUEST:
        handle_local_alliance_request(maester, frame, client_fd);
        break;
    case TYPE_ALLIANCE_RESPONSE:
        handle_local_alliance_response(maester, frame);
        break;
    case TYPE_PRODUCT_LIST_REQUEST:
        handle_local_product_list_request(maester, frame);
        break;
    case TYPE_PRODUCT_LIST_HEADER:
        handle_local_product_list_header(maester, frame, client_fd);
        break;
    case TYPE_ORDER_REQUEST_HEADER:
        handle_local_order_header(maester, frame, client_fd);
        break;
    case TYPE_DISCONNECT:
        handle_local_disconnect(maester, frame);
        break;
    case TYPE_ERROR_UNKNOWN_REALM:
    case TYPE_ERROR_UNAUTHORIZED:
    case TYPE_ORDER_RESPONSE:
    case TYPE_NACK:
        handle_local_info_frame(maester, frame);
        break;
    default:
        if (prepare_ack_frame(&reply, TYPE_NACK, maester->config.realm) == 0) {
            send_frame_on_fd(client_fd, &reply);
        }
        break;
    }

    free(destination);
    free(origin_ip);
}
