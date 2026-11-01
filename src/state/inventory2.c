/* inventory2.c — Loads, displays, saves, and frees the binary product inventory. */
#include "inventory2.h"
#include "utils2.h"
#include <stdlib.h>
#include <fcntl.h>

/* load_inventory — Reads Product structs from a binary file into a dynamic array. */
Inventory load_inventory(char *path) {
    Inventory inv = {0};
    Product *product;
    Product temp;
    int fd = open(path, O_RDONLY);
    int len;

    if (fd < 0) {
        char *msg = NULL;
        if (asprintf(&msg, "ERROR: Cannot open inventory binary file.\n") >= 0) {
            printF(msg);
        }
        free(msg);
        return inv;
    }

    len = read(fd, &temp, sizeof(Product));

    while (len > 0) {
        if (len != sizeof(Product)) {
            char *msg = NULL;
            if (asprintf(&msg, "ERROR: Corrupted inventory file.\n") >= 0) {
                printF(msg);
            }
            free(msg);
            free(inv.items);
            inv.items = NULL;
            inv.num_items = 0;
            close(fd);
            return inv;
        }

        product = realloc(inv.items, sizeof(Product) * (inv.num_items + 1));
        if (product == NULL) {
            char *msg = NULL;
            if (asprintf(&msg, "ERROR: Cannot allocate memory for inventory.\n") >= 0) {
                printF(msg);
            }
            free(msg);
            free(inv.items);
            inv.items = NULL;
            inv.num_items = 0;
            close(fd);
            return inv;
        }

        inv.items = product;
        inv.items[inv.num_items] = temp;
        inv.num_items++;

        len = read(fd, &temp, sizeof(Product));
    }

    close(fd);
    return inv;
}

/* display_inventory — Prints the inventory as a formatted table to stdout. */
void display_inventory(Inventory inv) {
    char *line = NULL;

    char *msg = NULL;
    if (asprintf(&msg, "--- Trade Ledger ---\n") >= 0) {
        printF(msg);
    }
    free(msg);
    msg = NULL;
    if (asprintf(&msg, "Item                    | Value (Gold) | Weight (Stone)\n") >= 0) {
        printF(msg);
    }
    free(msg);
    msg = NULL;
    if (asprintf(&msg, "--------------------------------------------------------\n") >= 0) {
        printF(msg);
    }
    free(msg);

    for (int i = 0; i < inv.num_items; i++) {
        asprintf(&line, "%-23s | %12d | %.1f\n", inv.items[i].name, inv.items[i].amount,
                 inv.items[i].weight);
        if (line) {
            printF(line);
            free(line);
            line = NULL;
        }
    }

    if (asprintf(&msg, "--------------------------------------------------------\n") >= 0) {
        printF(msg);
    }
    free(msg);
    msg = NULL;
    asprintf(&line, "Total Entries: %d\n", inv.num_items);
    if (line) {
        printF(line);
        free(line);
    }
}

/* save_inventory — Writes all Product structs back to the given path. */
void save_inventory(Inventory inv, const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        char *msg = NULL;
        if (asprintf(&msg, "ERROR: Cannot open inventory for writing.\n") >= 0) {
            printF(msg);
        }
        free(msg);
        return;
    }
    for (int i = 0; i < inv.num_items; i++) {
        write(fd, &inv.items[i], sizeof(Product));
    }
    close(fd);
}

/* free_inventory — Frees the items array and resets the inventory struct. */
void free_inventory(Inventory *inv) {
    if (!inv) {
        return;
    }
    free(inv->items);
    inv->items = NULL;
    inv->num_items = 0;
}
