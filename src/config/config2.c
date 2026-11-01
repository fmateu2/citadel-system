/* config2.c — Parses .dat realm configuration files. */
#include "utils2.h"
#include "config2.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* remove_ampersand — Removes all '&' characters from the string in-place. */
void remove_ampersand(char *s) {
    int i;
    int j;
    int len;

    if (s == NULL) {
        return;
    }

    len = (int)strlen(s);
    i = 0;
    while (i < len) {
        if (s[i] == '&') {
            j = i;
            while (j < len) {
                s[j] = s[j + 1];
                j++;
            }
            len--;
        } else {
            i++;
        }
    }
}

static char *read_clean_line(int fd) {
    char *line;

    line = readUntil(fd, '\n');
    if (line != NULL) {
        str_trim(line);
    }
    return line;
}

static int route_is_known(const char *ip, int port) {
    if (ip == NULL || port <= 0) {
        return 0;
    }
    if (strcmp(ip, "*.*.*.*") == 0) {
        return 0;
    }
    return 1;
}

static int append_route(Config *cfg, const char *name, const char *ip, int port) {
    Route *tmp;

    tmp = realloc(cfg->routes, sizeof(Route) * (cfg->num_routes + 1));
    if (tmp == NULL) {
        return -1;
    }
    cfg->routes = tmp;
    cfg->routes[cfg->num_routes].name = strdup(name);
    cfg->routes[cfg->num_routes].ip = strdup(ip);
    cfg->routes[cfg->num_routes].port = port;
    cfg->routes[cfg->num_routes].known = route_is_known(ip, port);
    if (cfg->routes[cfg->num_routes].name == NULL || cfg->routes[cfg->num_routes].ip == NULL) {
        return -1;
    }
    cfg->num_routes++;
    return 0;
}

/* load_file — Opens and parses a realm config file, returns a populated Config struct. */
Config load_file(char *path) {
    Config cfg = {0};
    char *line;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        char *msg = NULL;
        if (asprintf(&msg, "ERROR: Cannot open configuration file.\n") >= 0) {
            printF(msg);
        }
        free(msg);
        return cfg;
    }

    line = read_clean_line(fd);
    remove_ampersand(line);
    cfg.realm = strdup(line != NULL ? line : "");
    free(line);

    line = read_clean_line(fd);
    cfg.path = strdup(line != NULL ? line : ".");
    free(line);

    line = read_clean_line(fd);
    cfg.num_envoys = atoi(line != NULL ? line : "0");
    free(line);

    line = read_clean_line(fd);
    cfg.ip = strdup(line != NULL ? line : "");
    free(line);

    line = read_clean_line(fd);
    cfg.port = atoi(line != NULL ? line : "0");
    free(line);

    /* Search the official route marker explicitly. */
    line = read_clean_line(fd);
    while (line != NULL && strcmp(line, "--- ROUTES ---") != 0) {
        free(line);
        line = read_clean_line(fd);
    }
    free(line);

    line = read_clean_line(fd);
    while (line != NULL) {
        char *name;
        char *ip;
        char *port_text;
        int port;

        if (line[0] != '\0') {
            name = strtok(line, " \t");
            ip = strtok(NULL, " \t");
            port_text = strtok(NULL, " \t");
            if (name != NULL && ip != NULL && port_text != NULL) {
                port = atoi(port_text);
                remove_ampersand(name);
                if (strcmp(name, "DEFAULT") == 0) {
                    cfg.has_default = 1;
                    cfg.default_route.name = strdup(name);
                    cfg.default_route.ip = strdup(ip);
                    cfg.default_route.port = port;
                    cfg.default_route.known = route_is_known(ip, port);
                } else {
                    append_route(&cfg, name, ip, port);
                }
            }
        }
        free(line);
        line = read_clean_line(fd);
    }
    close(fd);

    return cfg;
}

/* free_config — Frees all heap memory inside a Config struct. */
void free_config(Config *cfg) {
    int i;

    if (cfg == NULL) {
        return;
    }

    free(cfg->realm);
    free(cfg->path);
    free(cfg->ip);

    if (cfg->has_default) {
        free(cfg->default_route.name);
        free(cfg->default_route.ip);
    }

    i = 0;
    while (i < cfg->num_routes) {
        free(cfg->routes[i].name);
        free(cfg->routes[i].ip);
        i++;
    }
    free(cfg->routes);

    memset(cfg, 0, sizeof(Config));
}

/* find_route — Returns a usable direct route or DEFAULT route. */
Route *find_route(Config *cfg, const char *realm) {
    int i;

    if (cfg == NULL || realm == NULL) {
        return NULL;
    }

    i = 0;
    while (i < cfg->num_routes) {
        if (cfg->routes[i].name != NULL && strcasecmp(cfg->routes[i].name, realm) == 0) {
            if (cfg->routes[i].known) {
                return &cfg->routes[i];
            }
            break;
        }
        i++;
    }
    if (cfg->has_default && cfg->default_route.known) {
        return &cfg->default_route;
    }
    return NULL;
}

/* get_next_route — Returns the following known route, or DEFAULT route. */
Route *get_next_route(Config *cfg, const char *realm) {
    int i;

    if (cfg == NULL || realm == NULL) {
        return NULL;
    }

    i = 0;
    while (i < cfg->num_routes) {
        if (cfg->routes[i].name != NULL && strcasecmp(cfg->routes[i].name, realm) == 0) {
            i++;
            while (i < cfg->num_routes) {
                if (cfg->routes[i].known) {
                    return &cfg->routes[i];
                }
                i++;
            }
            break;
        }
        i++;
    }

    if (cfg->has_default && cfg->default_route.known) {
        return &cfg->default_route;
    }
    return NULL;
}
