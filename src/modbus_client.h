#pragma once

#include <stdbool.h>
#include <time.h>

/* Letzter erfolgreicher Poll-Zeitstempel (Unix-Sekunden, 0 = noch keiner) */
extern volatile time_t mb_last_poll_ts;

/* true, wenn die letzte Verbindung zum Inverter erfolgreich war */
extern volatile bool   mb_inverter_connected;

void modbus_client_start(void);
