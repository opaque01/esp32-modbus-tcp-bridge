# Huawei Sun 2000 Modbus‑Register (aus Solaranzeige PHP‑Script)

Das Backup‑PHP‑Skript `huawei_M1.php` aus dem Solaranzeige‑Projekt liefert eine praktische Liste von Registeradressen, die beim Auslesen eines Huawei Sun‑2000 Wechselrichters verwendet werden. Diese Adressen bilden die Grundlage für unseren ESP32‑Client.

## Struktur
- Viele Register werden über Modbus‑Funktion 0x03 (Lesen) abgefragt.
- Einige schreiben Funktionen (0x06/0x10) sind in Beispielen, aber nicht im Skript implementiert.
- Adressen werden oft als Dezimalwerte genutzt, teilweise mit Offsets für PV‑Strings.

## Wichtigste Register (Deklarationen in Skript in Dezimalform)

| Register (dez) | Hex       | Bedeutung / Verwendungszweck                  | Notizen |
|---------------:|-----------|----------------------------------------------|--------|
| 30000          | 0x7530    | Firmware‑Name (mehrere Register, L=15)       | String |
| 30015          | 0x753F    | Seriennummer (L=10)                          | String |
| 30070          | 0x7556    | Modell‑ID                                     | Device Typ |
| 30071          | 0x7557    | Anzahl PV‑Strings                             | Integer |
| 30072          | 0x7558    | Anzahl MPP‑Tracker                            | Integer |
| 30073          | 0x7559    | Nennleistung (W)                              | Integer |
| 32000          | 0x7D00    | Status1                                       | Bitmask |
| 32002          | 0x7D02    | Status2                                       | Bitmask |
| 32003          | 0x7D03    | Status3 (2 Register)                          | Bitmask |
| 32008          | 0x7D08    | Alarm1                                        | Bitmask |
| 32009          | 0x7D09    | Alarm2                                        | Bitmask |
| 32010          | 0x7D0A    | Alarm3                                        | Bitmask |
| 32014+2*j      | 0x7D0E+2*j| PV‑String j Spannung                          | j=1..Anz_PV_Strings |
| 32015+2*j      | 0x7D0F+2*j| PV‑String j Strom                             | j=1..Anz_PV_Strings |
| 32064          | 0x7D40    | AC‑Eingangsleistung (2 Reg.)                  | Watt   |
| 32066 (1‑Ph)   | 0x7D42    | AC‑Spannung R (1‑Ph)                          | V/10   |
| 32069/70/71    | 0x7D45-47 | AC‑Spannung R/S/T (3‑Ph)                      | V/10   |
| 32072          | 0x7D48    | AC‑Strom R (1‑Ph oder erster Kanal 3‑Ph)      | A/1000 |
| 32080          | 0x7D50    | AC‑Leistung                                   | W      |
| 32085          | 0x7D55    | AC‑Frequenz                                   | Hz/100 |
| 32086          | 0x7D56    | Effizienz                                     | %/100  |
| 32087          | 0x7D57    | Temperatur                                     | °C/10  |
| 32088          | 0x7D58    | Isolation                                     | ?      |
| 32089          | 0x7D59    | DeviceStatus                                  | Integer |
| 32090          | 0x7D5A    | Fehlercode                                    | Integer |
| 32106          | 0x7D6A    | Wattstunden Gesamt (2 Reg.)                   | Wh*10  |
| 32114          | 0x7D72    | Wattstunden heute (2 Reg.)                    | Wh*10  |
| 37000          | 0x9068    | Batterie‑Status                               | Integer |
| 37001 (2 Reg)  | 0x9069    | Batterie‑Leistung (signiert)                  | ?      |
| 37004          | 0x906C    | SOC (%/10)                                    | %      |
| 37113          | 0x9121    | Einspeisung\/Bezug (signiert)                | ?      |
| 37119          | 0x9127    | Wattstunden Gesamt Export (2 Reg)             | Wh*10  |
| 37121          | 0x9129    | Wattstunden Gesamt Import (2 Reg)             | Wh*10  |
| 38210 (24 Reg) | 0x9512    | Batterie‑Datenblock (Firmware etc.)           | String+… |

> **Hinweis:** Werte werden in mehreren Registern in verschiedenen Skalen übertragen (siehe Kommentare im PHP‑Script). Das Mapping oben übersetzt begleitend.

### Nutzung in unserem Projekt
- Diese Tabelle wird als Basis für den Cache und für das spätere Mapping an Clients wie TA‑Regler erstellt.
- Bei Bedarf sollen weitere Register aus originalen Huawei‑Dokumenten ergänzt werden. Zusätzliche Gerätespezifische Listen (z. B. für Huawei‑Wallboxen) sind in separaten Dateien wie `huawei_wallbox_registers.md` dokumentiert.

---

Die extrahierten Adressen geben uns einen realistischen Satz an Werten, die wirklich beim Sun2000‑Inverter verfügbar sind. Damit ist die Entwicklung des Modbus‑Clients wesentlich zielgerichteter.