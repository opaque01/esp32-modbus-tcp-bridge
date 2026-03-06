# SDongle Modbus Interface (Version 200R022CC10)

Das bereitgestellte Dokument beschreibt das Modbus‑TCP‑Protokoll in allgemeinen Begriffen und enthält spezifische Informationen zum Huawei **SDongleA** (in Version V200R022CC10). Im Rahmen unseres Projektes sind vor allem folgende Punkte relevant:

## Allgemeine Protokollhinweise
- Modbus TCP verwendet MBAP-Kopf (7 Bytes) mit den Feldern Master, Protokolltyp, Längen- und Logik‑Geräte‑ID.
- Big‑Endian Kodierung für Adressen und Daten.
- Unterstützte Funktionscodes: 0x03 (Lesen), 0x06/0x10 (Schreiben), 0x2B (Geräte‑Identifizierer) u. a.
- Ausnahme‑Codes 0x01…0x04, 0x06, 0x80 etc.

## SDongle-spezifische Erweiterungen
- Logic Device ID 100 wird für das SDongleA selbst verwendet.
- Adressen 1…247 stehen für angeschlossene Wechselrichter oder andere Geräte.

### Device‑Identifier (Function Code 0x2B / MEI 0x0E)
- Objekt‑IDs:
  - 0x00 – Herstellername (ASCII, "HUAWEI")
  - 0x01 – Produktcode (ASCII, "SDongleA-WLAN")
  - 0x02 – Hauptrevision (Software‑Version)
- Bei Abfrage der Geräteliste (Sub‑code 0x03, ObjectID 0x87) kann die Anzahl und Informationen der über RS485 angeschlossenen Geräte abgefragt werden.

### Register‑Beispiele aus dem Dokument
- Beispielhafte Adressen:
  - 40118 (0x9CB6) – aktives Leistungssteuerungs‑Modus
  - 40119 (0x9CB7) – aktive Leistungs‑Derate (%)
  - 40200 (0x9D08) – Power-On‑Befehl
- Diese Beispiele dienen zur Implementierung von Schreibzugriffen und Tests.

### Wichtige Notizen
- Die Dokumentation enthält keine vollständige Registerliste, nur Beispiele und allgemeines Frame‑Layout.
- Für Huawei‑Inverter (Sun2000) müssen wir separate Modbus‑Definitionen beschaffen.

---

> **Weiteres Vorgehen:**
> - Einbinden der obigen Informationen in unsere Firmware als Kommentar/Referenz.
> - Weitere Dokumente (falls vorhanden) hinzufügen, sobald sie verfügbar sind.
