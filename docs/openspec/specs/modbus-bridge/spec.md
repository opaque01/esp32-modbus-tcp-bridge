## ADDED Requirements

### Requirement: ESP32 stellt Modbus-TCP-Bridge bereit
Das System SHALL auf dem ESP32 als Modbus-TCP-Server auf Port 502 laufen und Anfragen von mehreren Clients gleichzeitig annehmen.

#### Scenario: Mehrere Clients lesen parallel
- **WHEN** mindestens zwei Modbus-TCP-Clients gleichzeitig FC03-Leseanfragen senden
- **THEN** beantwortet der Server beide Verbindungen ohne Neustart oder Verbindungsabbruch

### Requirement: Zyklisches Lesen der Huawei-Register
Das System SHALL als Modbus-TCP-Client zyklisch die definierten Huawei-Registerblöcke lesen und die Werte im lokalen Cache speichern.

#### Scenario: Erfolgreicher Poll aktualisiert Cache
- **WHEN** der Poll-Task erfolgreich mit dem Huawei-Inverter kommuniziert
- **THEN** werden die zugehörigen Cache-Blöcke als gültig markiert und mit aktuellen Registerwerten überschrieben

### Requirement: Schutz vor veralteten Messwerten
Das System SHALL bei ungültigem oder veraltetem Cache keine alten Prozesswerte ausgeben und stattdessen Modbus Exception `0x04` zurückgeben.

#### Scenario: Inverter nicht erreichbar
- **WHEN** ein Client ein Register anfragt und der zugehörige Cache-Block ungültig ist
- **THEN** antwortet der Server mit Modbus Exception `0x04` (Server Device Failure)

### Requirement: Unbekannte Register führen zu Illegal Data Address
Das System SHALL für nicht unterstützte oder nicht gemappte Register Modbus Exception `0x02` liefern.

#### Scenario: Register außerhalb des unterstützten Bereichs
- **WHEN** ein Client ein Register liest, das weder im Proxy-Cache noch im aktivierten Mapping definiert ist
- **THEN** antwortet der Server mit Modbus Exception `0x02` (Illegal Data Address)

### Requirement: AP-basierte Web-Konfiguration beim Start
Das System SHALL beim Boot einen temporären Wi-Fi-Access-Point zur Konfiguration bereitstellen und die Einstellungen persistent in NVS speichern.

#### Scenario: AP-Timeout ohne verbundene Station
- **WHEN** der AP gestartet wurde und innerhalb des konfigurierten Zeitfensters keine Station verbunden ist
- **THEN** wird der AP automatisch deaktiviert

#### Scenario: Konfiguration bleibt nach Neustart erhalten
- **WHEN** Netzwerk- oder Mapping-Einstellungen über die Weboberfläche gespeichert wurden und das Gerät neu startet
- **THEN** werden die gespeicherten Werte aus NVS geladen und erneut verwendet
