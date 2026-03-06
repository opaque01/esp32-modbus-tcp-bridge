#pragma once
#include <stddef.h>

void modbus_server_start(void);

/* Gibt die Anzahl aktuell verbundener Modbus-Clients zurück. */
int modbus_server_get_client_count(void);

/* Kopiert aktive Client-IPs in einen flachen Puffer (out_item_len je Eintrag). */
int modbus_server_get_client_ips(char *out, size_t out_item_len, int max_items);
