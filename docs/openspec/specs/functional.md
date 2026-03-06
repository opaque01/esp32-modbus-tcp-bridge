# Funktionale Spezifikation

## 1. Initialer Betriebsmodus
- Nach dem Einschalten startet der ESP32-S3 als Access Point (SSID z.B. `ModBus Server`).
- Der AP bleibt für **60 Sekunden** aktiv (1 Minute), verlängert sich aber automatisch immer wieder, solange mindestens ein Client verbunden ist.
- Der ESP32 implementiert ein einfaches Captive‑Portal: sobald sich ein neues Gerät (z. B. ein Handy) verbindet, wird durch DNS‑/HTTP‑Umleitungen im Client‑WLAN automatisch der Browser zur Konfigurationsseite (`http://192.168.4.1`) geöffnet, ähnlich wie beim Captive‑Portal von Captiva.
- Der Nutzer kann die Einstellungen ändern und speichern; danach oder nach Ablauf der letzten Minute ohne verbundene Clients deaktiviert sich der AP.

## 2. Konfigurations-UI
- Die Seite ist in **drei Tab‑Bereiche** gegliedert:
  - **Netzwerkeinstellungen** – Alle LAN/PoE‑Optionen, DHCP vs statisch, IP/Mask/Gateway/DNS, MAC‑Anzeige.
  - **Modbus‑Einstellungen** – Parameter zur Kommunikation mit dem Huawei‑Inverter (IP, Port, Slave‑ID) sowie Registerauswahl/Funktion.
  - **Status** – Debug und Logging Seite.
- Formularfelder (primär im Modbus‑Tab):
  - Huawei Sun 2000 Inverter IP-Adresse
  - Port (Standard 502)
  - Auswahl LAN‑Schnittstelle des ESP32 (Ethernet/PoE)
  - **Netzwerkkonfiguration**
    - DHCP oder statische Vergabe
    - Bei statisch: IP, Subnetzmaske, Gateway, DNS 1, DNS 2
    - Anzeige der aktuellen MAC‑Adresse des Ethernet‑Interfaces (nicht veränderbar)
  - **AP‑Passwort für Konfigurationsseite** – kann der Nutzer ändern; Standardeinstellung ist `ModBus` (gleiches wie WLAN‑Passwort). Diese Einstellung wird ebenfalls persistent gespeichert.
  - Optional: Admin‑Passwort für zukünftige Webzugriffe

- **Doppelbetrieb**: Der Server liefert stets den kompletten, vom Inverter gelesenen Cache (Proxy). Zusätzlich kann der Nutzer eine *Mapping‑Tabelle* aktivieren – die dort eingetragenen Werte werden ebenfalls angeboten (alias, umbenannte oder umskalierte Register). Auf diese Weise ist ein gleichzeitiger Betrieb möglich: Solaranzeige.de läuft im Proxy‑Modus, während der TA‑Regler nur den BAT‑SOC unter der ihm bekannten Register‑Adresse erhält.

- **UI-Struktur**: Im Modbus-Tab gibt es einen Schalter "Mapping aktivieren". Wenn gesetzt erscheint die Tabelle zur Konfiguration. Der Proxy‑Datendurchsatz ist unabhängig davon immer verfügbar; bei deaktivierter Checkbox werden sämtliche Anfragen unverändert aus dem Cache beantwortet.

  - **Dropdown für Registerauswahl**: Die Spalte "Name" ist ein Auswahlfeld, das alle bekannten Huawei‑Register schlüsselt (Name+Adresse). Nach Auswahl werden die Spalten "Register", "Funktion", "Typ", "Endian", "Faktor" und "Einheit" automatisch ausgefüllt. Die Liste stammt aus einem vom ESP32 bereitgestellten JSON‑Endpunkt (/registers).

- **Register‑Mapping**: Im Mapping‑Modus wird im Modbus‑Tab eine Tabelle angeboten, ähnlich einer Firewall‑Konfiguration. Jede Zeile entspricht einem Wert, den der ESP32 aus dem internen Cache (siehe `huawei_registers.md`) liefert. Benutzer können mit „+" neue Einträge hinzufügen, bestehende Zeilen editieren oder per „–" löschen. Für jede Zeile werden folgende Felder angezeigt:
  1. **Bezeichnung** – frei wählbarer Name (z.B. "PV Spannung 1").
  2. **Registeradresse** – automatisch befüllte Holding‑Registernummer (Zeile 1 → Adresse 1 etc.); nicht änderbar. Ein Dropdown neben der Adresse erlaubt die Auswahl eines Werts aus der Liste der verfügbaren Huawei‑Register (Beschriftung + Hex‑Adresse).
  3. **Funktion** – Standardeinstellung `03 – read holding registers`; weitere Funktionstypen optional.
  4. **Datentyp** – 16‑bit signed/unsigned, 32‑bit float, … (voreingestellt gemäß ausgewähltem Register).
  5. **Byte‑Reihenfolge** – Big‑Endian/Little‑Endian.
  6. **Faktor/Skalierung** – Multiplikator zur Umrechnung (z. B. 0,1 für Spannung).
  7. **Einheit** – Anzeige­einheit (V, A, W, %, °C, …).
  8. **Wert bei Timeout** – optional (z. B. „Benutzerdefiniert").

  Die Zeilen werden in einer Tabelle angezeigt und lassen sich per Stift‑Icon bearbeiten; oberhalb/unterhalb stehen Schaltflächen zum Hinzufügen (`+`) und Entfernen (`–`) von Einträgen. Dieses Mapping dient dazu, den Datenstrom flexibel an verschiedene Client‑Anforderungen anzupassen (TA‑Regler, HA, Solaranzeige etc.).

- **Persistenz**: Alle oben genannten Konfigurationen werden in nicht‑flüchtigem Speicher (NVS / LittleFS) abgelegt. Beim Start lädt das Gerät diese Einstellungen und nutzt sie für Netzwerk und Huawei‑Anbindung. Ein Menüpunkt „Werkseinstellungen wiederherstellen" setzt den Speicher zurück.

- **Hardware‑Reset**: Ein Langdruck ≥ 10 s auf die **BOOT-Taste (GPIO0)** versetzt das Gerät in den Auslieferungszustand (NVS löschen + Neustart), falls die Web‑UI nicht erreichbar ist. Der RESET-Taster startet den ESP32 neu ohne NVS zu löschen — der AP öffnet sich dann erneut für 60 s.

- **UI‑Framework**: Die Weboberfläche basiert auf einem schlanken CSS‑Framework (z. B. Bootstrap), wobei nur die benötigten Komponenten eingebunden werden – Formulare, Checkboxen, Inputs, Selects, Fonts sowie Reset/CSS‑Baseline. Ziel ist es, den Code klein zu halten und auf dem ESP32 unkompliziert zu hosten. Die Seite muss **responsive** sein, da die Konfiguration in der Regel per Smartphone oder Tablet erfolgt. Das Framework sollte daher mobile‑freundliche Komponenten und ein flexibles Grid mitbringen.
- Schaltflächen: `Speichern`, `Zurücksetzen auf Werkseinstellungen`.
- Nach dem Speichern Verwendung von NVS oder LittleFS, Einstellungen persistieren.
- AP deaktiviert sich nach erfolgreichem Speichern oder Timeout.

## 3. Modbus TCP Kommunikation

**Terminologie**: In Modbus TCP ist der *Client* der Abfrager (Master, initiiert Verbindung), der *Server* der Datenlieferant (Slave, antwortet). Der ESP32 ist beides gleichzeitig — Client gegenüber Huawei, Server gegenüber TA-Regler & Co.

**ESP32 als Modbus TCP Client (→ Huawei SDongle):**
- Der ESP32 baut aktiv eine TCP-Verbindung zum Huawei SDongle auf und liest Register via FC03.
- Ein Hintergrundtask liest zyklisch (Standard 60 s) drei zusammenhängende Register-Blöcke. Der Huawei SDongle akzeptiert nur eine gleichzeitige Verbindung — der ESP32 hält diese exklusiv.
- Bei Kommunikationsfehlern erneuter Verbindungsaufbau nach dem nächsten Poll-Intervall.
- Gelesene Werte werden in einem internen Cache (3 statische Arrays, ~674 Byte) gehalten.

**ESP32 als Modbus TCP Server (→ TA-Regler, Home Assistant, Solaranzeige …):**
- Der ESP32 nimmt gleichzeitig **mehrere** Verbindungen an (kein künstliches Limit).
- Bei FC03-Leseanfragen liefert der Server Werte aus dem Cache; unbekannte Register → Exception 0x02.
- Da der TA-Regler andere Register-Adressen und Skalierungen erwartet als Huawei nativ liefert, übernimmt das **Register-Mapping** diese Übersetzung (z.B. Huawei Register 37004 ×0.1 % → TA-Register 1 ×1 %). Ohne Mapping wären die Rohdaten für den TA nicht verwertbar — dies war der ursprüngliche Grund für das Python-Script.

## 4. Mehrere Clients / Multiplexer
- Da der Huawei-Wechselrichter nur eine gleichzeitige Verbindung zulässt, bildet der ESP32 eine Multiplexer-Schicht.
- Nur ein interner Konnex zum Wechselrichter (ESP32 als Modbus-Client), extern beliebige Anzahl Modbus-Clients (TA-Regler, Home Assistant, Solaranzeige etc.).
- Anfragen werden nicht direkt transparent weitergeleitet, sondern aus dem lokalen Cache des ESP32 beantwortet.

## 5. LAN & PoE Betrieb
- Der ESP32-S3 ETH Board nutzt die integrierte Ethernet-Schnittstelle.
- Unterstützung für DHCP und statische Konfiguration über WebUI.
- Bei Einsatz in PoE-Umgebung genügt Strom über RJ45.

## 6. Nicht-funktionale Anforderungen
- Stabiler Dauerbetrieb (24/7) mit geringem Speicher-/CPU-Footprint.
- Robust gegenüber Netzwerkausfällen am Inverter.
- Einfaches Firmware-Update via OTA (optional später).
