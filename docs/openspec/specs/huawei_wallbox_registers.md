# Huawei Wallbox Modbus‑Register (SmartHEMS)

Aus der HTML‑Spezifikation "SmartHEMS_Modbus_Interface_Definitions_2024-07-15" wurden folgende relevante Register extrahiert. Diese können später ebenfalls vom ESP32 abgefragt oder weitergereicht werden.

| Register (dez) | Beschreibung                        | Datentyp | Einheit | Skalierung |
|---------------:|-------------------------------------|----------|---------|------------|
| 30322          | Inverter total absorbed energy      | U64 RO   | kWh     | 100        |
| 30324          | Energy charged today                | U32 RO   | kWh     | 100        |
| 30326          | Total charged energy                | U64 RO   | kWh     | 100        |
| 30328          | Energy discharged today             | U32 RO   | kWh     | 100        |
| 30330          | Total discharged energy             | U64 RO   | kWh     | 100        |
| 30332          | ESS chargeable energy               | U32 RO   | kWh     | 100        |
| 30334          | ESS dischargeble energy             | U32 RO   | kWh     | 100        |
| 30336          | Rated ESS capacity                  | U32 RO   | kWh     | 100        |
| 30338          | Consumption today                   | U32 RO   | kWh     | 100        |
| 30340          | Total energy consumption            | U64 RO   | kWh     | 100        |
| 30342          | Feed-in to grid today               | U32 RO   | kWh     | 100        |

> Hinweis: RO = Read‑Only. Die Werte werden in verschiedenen Breiten (32‑bit, 64‑bit) abgelegt und sind mit Faktor 100 skaliert.

Weitere Register bestehen vermutlich im Dokument – hier sind nur diejenigen aufgeführt, die explizit im Dump auftauchten und typische Wallbox‑Kennzahlen repräsentieren.

Diese Tabelle kann bei Bedarf erweitert werden, wenn detailliertere Auszüge aus der Wallbox‑Spec benötigt werden. Die Register dienen als Ergänzung zum Inverter‑Mapping, falls der ESP32 auch eine Huawei‑Wallbox auslesen soll.
