# Vollständige Registerliste Huawei Sun2000 Wechselrichter

Diese Datei fasst die wichtigsten Modbus‑Holding‑Register zusammen, die in der offiziellen Huawei‑Spezifikation (Solar_Inverter_Modbus_Interface_Definitions_V5) und im `huawei_M1.php`‑Skript verwendet werden. Die Adressen stimmen überein und bilden die Grundlage für den ESP32‑Cache.

## Wichtige Registerbereiche

- **30000–300??** – Geräte‑Informationen
  - 30000 … Firmware‑Bezeichnung (String, Länge 15)
  - 30015 … Seriennummer (String, Länge 10)
  - 30070 … Modell‑ID
  - 30071 … Anzahl PV‑Strings
  - 30072 … Anzahl MPP‑Tracker
  - 30073 … Nennleistung (W)

- **32000–3200A** – Status/Alarm
  - 32000 Status1
  - 32002 Status2
  - 32003 Status3 (2 Registers)
  - 32008 Alarm1, 32009 Alarm2, 32010 Alarm3

- **32014+ / 32015+** – PV‑String Spannungen/Strom (je 1 Register pro String)

- **32064–32090** – AC/Allgemeine Messdaten
  - 32064 AC‑Eingangsleistung (2 Reg)
  - 32066‑71 AC‑Spannung R/S/T
  - 32072 AC‑Strom R
  - 32080 AC‑Leistung
  - 32085 AC‑Frequenz
  - 32086 Effizienz
  - 32087 Temperatur
  - 32088 Isolation
  - 32089 DeviceStatus
  - 32090 Fehlercode

- **32106–32114** – Energiezähler (Wh Gesamt, heute)

- **37000–37004 und 37113–37121** – Batterie und Energiefluss
  - 37000 Batterie‑Status
  - 37001 Batterie‑Leistung (signiert)
  - 37004 SOC (%/10)
  - 37113 Einspeisung/Bezug
  - 37119 Export Wh*10
  - 37121 Import Wh*10

- **38210–382??** – Batterie‑Datenblock (Firmware etc., 24 Reg)

> Diese Liste entspricht direkt den Exemplaren im PHP‑Script und den offiziellen Huawei‑Docs. Weitere Register sind in der kompletten Spezifikation enthalten, wir konzentrieren uns jedoch auf die tatsächlich verwendeten.

---

Das Modul kann bei Bedarf erweitert werden, wenn später zusätzliche Register aus Huawei‑Dokumenten bekannt werden. Für den ESP32‑Cache genügt die obenstehende Auswahl.
