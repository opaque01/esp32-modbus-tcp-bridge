# Tasks

- [x] **Initiales ESP-IDF Projekt anlegen**
   - Grundgerüst mit Ethernet und Wi-Fi.
   - Beispielapp als Basis verwenden.

- [x] **Webserver & AP-Konfiguration**
   - Implementiere AP-Modus beim Boot.
   - Erstelle Webserver für Konfigurationsseite.
   - Formular und NVS-Speicherung.
   - Mapping-Tabelle immer anzeigen, aber per Schalter aktivierbar; fülle bei leerem Speicher ein Beispiel (BAT SOC).
   - Lade/speichere Mapping-Einträge in NVS.

- [x] **NVS Speicher-Management**
   - Lade/ speichere Einstellungen.
   - Werkseinstellungen zurücksetzen.

- [x] **Modbus TCP Client (Huawei)**
   - Eigene FC03-Implementierung über raw lwIP-Socket (keine externe Bibliothek).
   - 4 Cache-Blöcke: A (30000), B (32000), C (37000), D (38210); alle 60 s aktualisiert.
   - `reg_cache.h/c`: Mutex-geschützter Cache mit `valid`-Flag und `last_updated`-Timestamp.
   - Bei Verbindungsfehler: Cache invalidieren → Clients erhalten Exception 0x04.

- [x] **Modbus TCP Server (Multiplexer)**
   - Server-Socket auf Port 502, accept-Loop mit je einem FreeRTOS-Task pro Client.
   - Proxy-Modus: native Huawei-Adressen direkt aus Cache bedienen.
   - Mapping-Modus: Zeilen-Adresse (1…n) → Quell-Register + Faktor-Umrechnung.
   - Exception 0x04 bei stale Cache, 0x02 bei unbekannter Adresse.

- [x] **Fehlerhandling & Logging**
   - ESP_LOGI/W/E-Logging in allen Modulen.
   - Wiederverbindung zum Wechselrichter nach jedem Poll-Intervall (60 s).
   - `/status` HTTP-Endpoint liefert Ringpuffer-Log + Verbindungsstatus (status_log.c).

- [x] **LAN/PoE Netzwerk-Setup**
   - `network.h/c`: Ethernet-Init für W5500 SPI (Waveshare ESP32-S3-ETH), RMII-Variante kommentiert.
   - DHCP oder statische IP aus NVS-Konfiguration.
   - Wi-Fi AP mit 60-s-Timeout, verlängerbar solange STA verbunden (`esp_wifi_ap_get_sta_list`).
   - GPIO0-Task: Langdruck ≥ 10 s → NVS löschen + Neustart.

- [x] **Dokumentation & Build-Anleitung**
   - README.md mit Build/Flash-Anleitung (Waveshare + WT32-ETH01).
   - `CMakeLists.txt` (Projekt) + `src/CMakeLists.txt` (Komponente) angelegt.
   - Board-spezifische SPI-Pin-Konfiguration in `network.c` dokumentiert.

- [x] **Test & Validierung**
   - Verbindung zu Huawei Inverter simulieren.
   - Mehrere Clients verifizieren (TA Regler, Home Assistant).

- [x] **Optional: OTA Update**
   - Firmware-Update via HTTP.
