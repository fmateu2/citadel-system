Maester - Multi-Realm Trading Network Simulator
G15 - Francesc Mateu & Jan Porcar
Operating Systems 2025-2026, La Salle (Universitat Ramon Llull)

DESCRIPTION
  Each running instance represents a realm. Realms communicate over TCP
  using a custom fixed-size 320-byte frame protocol. They can form alliances
  (directly or through hop nodes), request product lists, and submit trade
  orders to allied realms. Outgoing missions are delegated to persistent
  child processes (envoys) via pipes.

COMPILE
  make

CLEAN
  make clean

RUN
  ./Maester <config.dat> <inventory.db>

  Example:
    ./Maester data/Stark/maester_Stark.dat data/Stark/stark_stock.db

COMMANDS
  LIST REALMS                          Show all configured realm routes
  LIST PRODUCTS                        Show local inventory
  LIST PRODUCTS <Realm>                Request product list from an allied realm
  PLEDGE <Realm> <Sigil>               Send an alliance request to a realm
  PLEDGE STATUS                        Show the status of all known alliances
  PLEDGE RESPOND <Realm> ACCEPT|REJECT Respond to an incoming pledge request
  ENVOY STATUS                         Show which envoys are free or on mission
  START TRADE <Realm>                  Open an interactive trade session
  EXIT                                 Notify allied realms and shut down cleanly

  EXIT and CTRL+C both notify allied realms before shutting down.

TRADE SESSION COMMANDS (after START TRADE <Realm>)
  ADD <product> <amount>               Add an item to the order
  REMOVE <product> <amount>            Remove an item from the order
  LIST                                 Show the current order
  SEND                                 Transmit the order to the allied realm
  CANCEL                               Abort the trade session

CONFIG FILE FORMAT (data/*.dat)
  Line 1: Realm name
  Line 2: Inventory/sigil directory path
  Line 3: Number of envoys
  Line 4: Listen IP
  Line 5: Listen port
  Line 6: --- ROUTES ---
  Line 7+: <RealmName> <IP> <Port>  or  DEFAULT <IP> <Port>
  Use *.*.*.*  as IP for a realm whose address is unknown (routes via DEFAULT).

PROTOCOL
  Frame: 320 bytes — type(1) + origin(20) + destination(20) + data_length(2) + data(275) + checksum(2)
  Chunked transfer: HEADER frame -> ACK_FILE -> DATA frames -> ACK_MD5
  Pledge header format: RealmName&SigilName&FileSize&MD5SUM
  Order response: OK | REJECT&OUT_OF_STOCK | REJECT&UNKNOWN_PRODUCT

PROJECT STRUCTURE
  src/core/      Main entry point and main loop (maester.c)
  src/config/    Config file parser (config2.c)
  src/state/     Pledge list, alliance table, envoy pool, inventory (pledge.c, alliance2.c, envoy2.c, inventory2.c)
  src/network/   TCP layer, frame protocol, transfer logic, handlers (net_*.c, network2.c, protocol2.c)
  src/commands/  Command dispatcher and trade session (commands2.c, trade2.c)
  src/utils/     String utilities and low-level I/O helpers (utils2.c)
  data/          One subfolder per realm, each containing its .dat config,
                 .db inventory, and sigil .png file
