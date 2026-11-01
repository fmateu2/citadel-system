/* trade2.c — Interactive trade session: build an order, then send it to an allied realm. */
#include "trade2.h"
#include "network2.h"
#include "alliance2.h"
#include "utils2.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>

typedef struct {
    char *name;
    int qty;
} OrderItem;

/* free_order_items — Frees all name strings and the items array. */
static void free_order_items(OrderItem *items, int count) {
    int i;

    for (i = 0; i < count; i++) {
        free(items[i].name);
    }
    free(items);
}

/* parse_product_and_qty — Splits "product name qty" into product and qty, returns -1 on error. */
static int parse_product_and_qty(char *args, char **product, int *qty) {
    char *last_space = NULL;
    char *p;
    int value = 0;
    int i;

    str_trim(args);

    for (p = args; *p != '\0'; p++) {
        if (*p == ' ') {
            last_space = p;
        }
    }

    if (last_space == NULL) {
        return -1;
    }

    *last_space = '\0';
    *product = args;

    for (i = 0; last_space[1 + i] != '\0'; i++) {
        if (!isdigit((unsigned char)last_space[1 + i])) {
            return -1;
        }
        value = value * 10 + (last_space[1 + i] - '0');
    }

    if (value <= 0) {
        return -1;
    }

    *qty = value;
    return 0;
}

/* add_item — Adds qty to an existing item or appends a new one. */
static int add_item(OrderItem **items, int *count, const char *name, int qty) {
    int i;
    OrderItem *tmp;

    for (i = 0; i < *count; i++) {
        if (strcasecmp((*items)[i].name, name) == 0) {
            (*items)[i].qty += qty;
            return 0;
        }
    }

    tmp = realloc(*items, sizeof(OrderItem) * (*count + 1));
    if (tmp == NULL) {
        return -1;
    }

    *items = tmp;
    (*items)[*count].name = strdup(name);
    if ((*items)[*count].name == NULL) {
        return -1;
    }
    (*items)[*count].qty = qty;
    (*count)++;

    return 0;
}

/* remove_item — Subtracts qty from an item, removing it if quantity reaches zero. */
static void remove_item(OrderItem *items, int *count, const char *name, int qty) {
    int i;

    for (i = 0; i < *count; i++) {
        if (strcasecmp(items[i].name, name) == 0) {
            items[i].qty -= qty;
            if (items[i].qty <= 0) {
                int j;
                free(items[i].name);
                for (j = i; j < *count - 1; j++) {
                    items[j] = items[j + 1];
                }
                (*count)--;
            }
            return;
        }
    }
}

/* print_order_items — Prints the current shopping list to stdout. */
static void print_order_items(OrderItem *items, int count) {
    char *line = NULL;
    int i;

    if (count == 0) {
        char *msg = NULL;
        if (asprintf(&msg, "Shopping list is empty.\n") >= 0) {
            printF(msg);
        }
        free(msg);
        return;
    }

    for (i = 0; i < count; i++) {
        asprintf(&line, "- %s x%d\n", items[i].name, items[i].qty);
        if (line != NULL) {
            printF(line);
            free(line);
            line = NULL;
        }
    }
}

/* build_order_payload — Serializes the order list to "product:qty,product:qty,..." format. */
static char *build_order_payload(OrderItem *items, int count) {
    char *payload = strdup("");
    int i;

    if (payload == NULL) {
        return NULL;
    }

    for (i = 0; i < count; i++) {
        char *entry = NULL;
        char *tmp = NULL;

        const char *prefix = "";
        if (i != 0) {
            prefix = ",";
        }
        asprintf(&entry, "%s%s:%d", prefix, items[i].name, items[i].qty);
        if (entry == NULL) {
            free(payload);
            return NULL;
        }

        asprintf(&tmp, "%s%s", payload, entry);
        free(entry);
        free(payload);

        if (tmp == NULL) {
            return NULL;
        }

        payload = tmp;
    }

    return payload;
}

/* start_trade_session — Runs the interactive trade REPL until SEND or CANCEL. */
void start_trade_session(Maester *maester, char *realm, int *keep_running) {
    OrderItem *items = NULL;
    int item_count = 0;
    char *msg = NULL;

    if (!is_allied(maester->alliances, maester->num_alliances, realm)) {
        char *msg = NULL;
        if (asprintf(&msg, "Trade rejected. Realm is not allied.\n") >= 0) {
            printF(msg);
        }
        free(msg);
        return;
    }

    /* Require that LIST PRODUCTS has been run first */
    if (maester->remote_products_text == NULL || maester->remote_products_realm == NULL ||
        strcasecmp(maester->remote_products_realm, realm) != 0) {
        char *msg = NULL;
        if (asprintf(&msg, "No products available. Use LIST PRODUCTS first.\n") >= 0) {
            printF(msg);
        }
        free(msg);
        return;
    }

    if (asprintf(&msg, "Trade with %s begins.\n", realm) >= 0) {
        printF(msg);
        free(msg);
        msg = NULL;
    }

    if (asprintf(
            &msg,
            "A direct path is open; your houses are allied, and no intermediaries stand in between.\n") >=
        0) {
        printF(msg);
        free(msg);
        msg = NULL;
    }

    /* Display available products from cached list */
    {
        char *line = NULL;
        if (asprintf(&line, "Available products: ") >= 0) {
            printF(line);
        }
        free(line);
        line = NULL;

        /* Parse "name|amount|weight\n" lines and print names */
        char *text_copy = strdup(maester->remote_products_text);
        if (text_copy != NULL) {
            char *p = text_copy;
            int first = 1;
            while (*p != '\0') {
                char *nl = strchr(p, '\n');
                if (nl != NULL) {
                    *nl = '\0';
                }
                char *sep = strchr(p, '|');
                if (sep != NULL) {
                    *sep = '\0';
                }
                if (*p != '\0') {
                    char *item = NULL;
                    if (asprintf(&item, "%s%s", first ? "" : ", ", p) >= 0) {
                        printF(item);
                    }
                    free(item);
                    first = 0;
                }
                if (nl != NULL) {
                    p = nl + 1;
                } else {
                    break;
                }
            }
            free(text_copy);
        }

        char *nl_msg = NULL;
        if (asprintf(&nl_msg, ".\n") >= 0) {
            printF(nl_msg);
        }
        free(nl_msg);
    }

    while (*keep_running) {
        char *line;
        char *upper;

        char *prompt = NULL;
        if (asprintf(&prompt, "(trade)> ") >= 0) {
            printF(prompt);
        }
        free(prompt);
        line = readUntil(STDIN_FILENO, '\n');
        if (line == NULL) {
            break;
        }

        str_trim(line);
        if (line[0] == '\0') {
            free(line);
            continue;
        }

        upper = str_to_upper(line);
        if (upper == NULL) {
            free(line);
            break;
        }

        if (strcmp(upper, "SEND") == 0) {
            char *payload;

            if (item_count == 0) {
                char *msg = NULL;
                if (asprintf(&msg, "Shopping list is empty. Nothing to send.\n") >= 0) {
                    printF(msg);
                }
                free(msg);
            } else {
                payload = build_order_payload(items, item_count);
                if (payload == NULL) {
                    char *msg = NULL;
                    if (asprintf(&msg, "ERROR: Out of memory.\n") >= 0) {
                        printF(msg);
                    }
                    free(msg);
                } else if (send_trade_order(maester, realm, payload) == 0) {
                    char *msg_line = NULL;
                    if (asprintf(&msg_line, "Trade list sent to %s.\n", realm) >= 0) {
                        printF(msg_line);
                    }
                    free(payload);
                    free(msg_line);
                    free(upper);
                    free(line);
                    break;
                } else {
                    char *msg = NULL;
                    if (asprintf(&msg, "ERROR: Could not send trade order.\n") >= 0) {
                        printF(msg);
                    }
                    free(msg);
                    free(payload);
                }
            }
        } else if (strcmp(upper, "LIST") == 0) {
            print_order_items(items, item_count);
        } else if (strncmp(upper, "ADD ", 4) == 0) {
            char *product;
            int qty;

            if (parse_product_and_qty(line + 4, &product, &qty) != 0) {
                char *msg = NULL;
                if (asprintf(&msg, "Usage: ADD <product> <amount>\n") >= 0) {
                    printF(msg);
                }
                free(msg);
            } else if (add_item(&items, &item_count, product, qty) != 0) {
                char *msg = NULL;
                if (asprintf(&msg, "ERROR: Out of memory.\n") >= 0) {
                    printF(msg);
                }
                free(msg);
            } else {
                char *msg = NULL;
                if (asprintf(&msg, "Item added.\n") >= 0) {
                    printF(msg);
                }
                free(msg);
            }
        } else if (strncmp(upper, "REMOVE ", 7) == 0) {
            char *product;
            int qty;

            if (parse_product_and_qty(line + 7, &product, &qty) != 0) {
                char *msg = NULL;
                if (asprintf(&msg, "Usage: REMOVE <product> <amount>\n") >= 0) {
                    printF(msg);
                }
                free(msg);
            } else {
                remove_item(items, &item_count, product, qty);
                char *msg = NULL;
                if (asprintf(&msg, "Item removed.\n") >= 0) {
                    printF(msg);
                }
                free(msg);
            }
        } else if (strcmp(upper, "CANCEL") == 0) {
            char *msg = NULL;
            if (asprintf(&msg, "Trade session cancelled.\n") >= 0) {
                printF(msg);
            }
            free(msg);
            free(upper);
            free(line);
            break;
        } else {
            char *msg = NULL;
            if (asprintf(&msg, "Unknown trade command.\n") >= 0) {
                printF(msg);
            }
            free(msg);
        }

        free(upper);
        free(line);
    }

    free_order_items(items, item_count);
}
