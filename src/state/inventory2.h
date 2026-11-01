#ifndef INVENTORY2_H
#define INVENTORY2_H

typedef struct {
    char name[100];
    int amount;
    float weight;
} Product;

typedef struct {
    Product *items;
    int num_items;
} Inventory;

Inventory load_inventory(char *path);
void display_inventory(Inventory inv);
void save_inventory(Inventory inv, const char *path);
void free_inventory(Inventory *inv);

#endif
