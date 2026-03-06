# Beispiel: Python‑Modbus‑Server für SOC

Bevor wir mit der ESP32‑Implementierung begonnen haben, wurde in der Solaranzeige‑Umgebung ein einfacher Modbus‑TCP‑Server in Python betrieben, um den Batterieladezustand (SoC) an einen Technische‑Alternative‑Regler weiterzugeben. Das Script stellt eine gute Referenz für das Verhalten unserer Ziel‑Firmware dar.

## Wesentliche Merkmale des Scripts

*Verwendete Bibliotheken*:
- `pymodbus` für Server‑ und Kontextlogik
- `influxdb` zur Abfrage der SoC‑Messwerte aus einer InfluxDB

*Serverkonfiguration*:
- hor 0.0.0.0:502, Slave‑ID 1
- Halten der Register 1…100 im Holding‑Register‑Block

*Aktualisierungslogik* (`update_soc` task):
1. Verbindung zur InfluxDB aufbauen.
2. SQL‑Query `SELECT LAST("SOC") AS "soc_value" FROM "Batterie"` ausführen.
3. SoC‑Wert (Prozent) in einen 16‑Bit‑Integer umrechnen (`soc*10`) und in Holding‑Register 1 schreiben.
4. Wiederholen alle 10 s.

Serverstart startet gleichzeitig den asyncio‑Modbus‑Server und die Update‑Schleife.

## Bedeutung für das ESP32‑Projekt

* Dieses Python‑Script ist ein Minimal‑Beispiel, wie ein Gerät als Modbus‑Server agieren kann und gleichzeitig dynamisch Daten aktualisiert.
* Für unsere ESP32‑Firmware müssen wir:
  - eine ähnliche Cache‑/Update‑Logik implementieren (z. B. Hintergrundtask, der Werte aus dem Huawei‑Inverter liest und den Cache füttert).
  - Tabellen wie `huawei_registers.md` nutzen, um konkrete Registerregister zu befüllen.
  - die Fähigkeit beibehalten, beliebige Clients (TA‑Regler, HomeAssistant, Solaranzeige etc.) gleichzeitig zu bedienen.
* Die Interaktion mit InfluxDB ist speziell für das Python‑Setup; im ESP32‑Fall ersetzt der Huawei‑Client diese Rolle.

> ✅ Das Python‑Script demonstriert, dass der Grundmechanismus funktioniert und liefert eine Vorlage dafür, wie die Registeraktualisierung ablaufen sollte. Es kann auch als Testserver weiterverwendet werden, während die ESP32‑Firmware entwickelt wird.
