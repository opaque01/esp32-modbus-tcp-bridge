#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Letzter erfolgreicher Poll-Zeitstempel (Unix-Sekunden, 0 = noch keiner) */
extern volatile time_t mb_last_poll_ts;

/* true, wenn die letzte Verbindung zum Inverter erfolgreich war */
extern volatile bool   mb_inverter_connected;

int modbus_client_forward_write(uint8_t unit_id, const uint8_t *pdu, uint16_t pdu_len,
                                uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len);
int modbus_client_forward_read(uint8_t unit_id, const uint8_t *pdu, uint16_t pdu_len,
                               uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len);
int modbus_client_forward_pdu(uint8_t unit_id, const uint8_t *pdu, uint16_t pdu_len,
                              uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len);

void modbus_client_start(void);
