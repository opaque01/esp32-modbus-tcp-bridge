# ESP32-Modbus-TCP-Bridge

Dieses Projekt enthält Firmware für ein ESP32-S3 ETH Entwicklungboard (z.B. Waveshare mit USB-C-Port), das:

- zyklisch Daten von einem Huawei Sun 2000 Wechselrichter per Modbus TCP liest,
- die Werte intern cached,
- als Modbus TCP Server dieselben Daten für einen anderen Master (z.B. TA-Regler, Home Assistant) bereitstellt.

Die Firmware liefert **immer** den vollständigen Cache vom Wechselrichter (Proxy/Pass‑Through). Zusätzlich lässt sich ein **Mapping** aktivieren, das gemappte/alised Werte in der gleichen Verbindung anbietet – ideal, um z. B. den Batteriezustand für einen TA‑Regler bei Adresse 37004 bereitzustellen, während Solaranzeige.de weiterhin alle Rohdaten sehen kann. Im Konfigurationsformular wird beim Hinzufügen einer Mapping-Zeile ein Dropdown mit den bekannten Huawei-Registern angeboten; eine Auswahl befüllt automatisch die zugehörigen Felder (Register‑Nr., Faktor, Einheit). Nach dem ersten Start ist das Mapping eingeschaltet und enthält einen Beispiel‑Eintrag für den BAT‑SOC, damit man sofort ein funktionierendes Setup hat.

## Voraussetzungen

- ESP-IDF (empfohlen: neueste stabile Version) installiert und im `PATH` verfügbar.
- Python 3 (für ESP-IDF-Tools)
- USB-C-Kabel zum Anschluss des Waveshare-Boards an den Rechner (oder bei anderen Boards das passende USB‑Seriell‑Kabel).
- Netzwerkkabel / PoE für die Ethernet-Schnittstelle.

> **Hinweis zu alternativen Boards:**
> Das Projekt ist primär für ein ESP32-S3 ETH Development Board (z.B. Waveshare mit USB-C) gedacht. Es sollte aber prinzipiell auch auf anderen ESP32-Varianten laufen, die Ethernet unterstützen, z.B. das WT32-ETH01 (ESP32 mit eingebettetem PHY).
> 
> **WT32-ETH01 Flash-Anleitung:**
> Diese Variante besitzt kein PoE; Stromversorgung erfolgt über 5 V an den 5V-Pin oder über die USB‑UART‑Seriell‑Schnittstelle. Das Flashen geschieht mittels eines externen USB‑UART-Adapters und folgender Verkabelung (Siehe https://wolf-u.li/flashen-des-wt32-eth01-ein-esp32-mit-lan-wifiwlan/):
> 1. Verbinde TX des Adapters mit RX des WT32-ETH01 (GPIO3), RX zu TX (GPIO1).
> 2. Verbinde GND des Adapters mit GND des WT32-ETH01.
> 3. Verbinde 5V vom Adapter (falls verfügbar) mit 5V des WT32-ETH01 zur Stromversorgung oder speise extern 5 V ein.
> 4. Zum Entsperren in den Boot‑Modus GPIO0 auf GND ziehen, ggf. EN/RST für Reset.
> 5. In der ESP-IDF: `idf.py -p /dev/ttyUSB0 flash` (Port entsprechend anpassen).
> 
> Der Rest des Build‑Prozesses bleibt identisch zum Waveshare‑Board. Nach dem Flashen kann das WT32-ETH01 wie gewohnt per Ethernet verbunden werden.

## Projekt initialisieren

```sh
cd <dein-clone>/esp32-modbus-tcp-bridge
```

## Bauen & Flashen

Die folgenden Schritte zeigen das typische Arbeiten mit ESP-IDF:

1. **Umgebung laden** (abhängig von Installation):

   ```sh
   . $HOME/esp/esp-idf/export.sh
   ```

   _Alternativ:_ Für Nutzer der Arduino‑IDE ist kein ESP‑IDF‑Setup erforderlich. Installiere das "ESP32"‑Boardpaket über den Boards Manager (URL `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`) und wähle im Menü "Tools → Board → ESP32S3 Dev Module".

2. **Konfigurieren**

   ```sh
   idf.py menuconfig
   # hier evtl. Anpassungen an Target (esp32s3), Partitionstabelle etc.
   ```

   Oder in der Arduino IDE: konfiguriere unter "Tools" Baudrate (115200), Flash Mode, Flash Size usw.

3. **Bauen**

   ```sh
   idf.py build
   ```

   In Arduino: Projekt öffnen, Sketch wählen und über "Sketch → Kompilieren" bauen.

4. **Flashen**

   ```sh
   idf.py -p /dev/ttyUSB0 -b 115200 flash
   # ersetze /dev/ttyUSB0 durch den seriellen Port des USB-C-Anschlusses
   ```

   In der Arduino IDE: Board anschließen, Port auswählen und "Sketch → Upload" drücken. Beim Waveshare-Board wird über USB‑C automatisch mit 115200 baud geflasht.

   Beim Waveshare-Board erscheint ein virtuelles USB-Seriell-Gerät, häufig `/dev/ttyUSB0` oder `/dev/ttyACM0` (macOS z.B. `/dev/cu.SLAB_USBtoUART`).

5. **Monitor starten** (optional)

   ```sh
   idf.py monitor
   ```

   Mit `Ctrl+]` verlässt man den Monitor.

## Tipps

- Bei Problemen mit Berechtigungen unter Linux kann ein `sudo chmod` auf den seriellen Port helfen.
- Für OTA-Updates benötigt die Partitionstabelle zwei OTA-Slots (`ota_0`, `ota_1`).

## Test & Validierung

### 1) Huawei-Verbindung simulieren

Im Ordner `tests/` liegt ein einfacher Mock-Modbus-Server, der die relevanten Huawei-Registerblöcke bedient:

```sh
cd <dein-clone>/esp32-modbus-tcp-bridge
python3 tests/mock_huawei_server.py --host 0.0.0.0 --port 1502
```

Offenen Port prüfen: nc -z -G 1 192.168.1.101 502 && echo "open" || echo "closed"

Konfiguriere anschließend die ESP32-Bridge auf die IP des Testrechners und Port `1502`.

### 2) Mehrere Modbus-Clients validieren

Mit `pymodbus` lässt sich Parallelzugriff prüfen:

```sh
pip install pymodbus
python3 tests/validate_multi_client.py --host <ESP32-IP> --port 502 --clients 3 --iterations 20 --register 37004
```

Erwartung: `Fail: 0`.

### 3) Status-Endpoint prüfen

```sh
curl http://<ESP32-IP>/status
```

Erwartung: JSON mit `uptime_s`, `inverter_connected`, `last_poll_ts`, `modbus_clients`, `log`.

## OTA Update (HTTP)

Die Firmware bietet `POST /ota` für Binär-Uploads (`application/octet-stream`) und startet danach automatisch neu.

### Via Web-UI

1. Tab `Status` öffnen
2. Im Bereich `OTA Update` die `.bin`-Datei auswählen
3. `Firmware hochladen` klicken

### Via curl

```sh
curl -X POST --data-binary @build/modbus_bridge.bin \
  -H "Content-Type: application/octet-stream" \
  http://<ESP32-IP>/ota
```

Bei Erfolg antwortet der Server mit `OK` und rebootet.

---

Die Firmware-Dateien und weitere Anleitungen (WebUI, register mapping etc.) sind in diesem Repository enthalten. Die OpenSpec-Historie liegt unter `docs/openspec/`.

## Terminal-Kurzstart

Für einen einfachen Einstieg ohne Vorkenntnisse gibt es eine separate Schritt-für-Schritt-Anleitung:

- `README_TERMINAL.md`
- `GITHUB_PUBLISH.md` (Repo veroeffentlichen und taggen)
