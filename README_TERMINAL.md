# ESP32 Modbus Bridge: Terminal-Kurzstart

Diese Datei ist die praktische Schritt-fuer-Schritt-Anleitung fuer:
- neues ESP per USB komplett flashen
- Bin-Dateien erzeugen (OTA und Full-Flash)
- Log ansehen
- OTA-Update einspielen

Die Beispiele sind fuer macOS/Linux.

## 1) Projektordner

```sh
cd <dein-clone>/esp32-modbus-tcp-bridge
```

## 2) Einmalig: ESP-IDF Tools + Python-Umgebung einrichten

Wenn du den Fehler siehst
`ERROR: ESP-IDF Python virtual environment "...idf5.5_py3.14_env/bin/python" not found`,
dann wurde die IDF-Python-Umgebung fuer diese Python-Version noch nicht angelegt.

Fuehre einmal aus:

```sh
cd /Users/opaque/esp/v5.5.1/esp-idf
./install.sh esp32s3
```

Optional (falls du gezielt Python 3.12/3.11 nutzen willst):

```sh
export ESP_PYTHON=$(command -v python3.12)
./install.sh esp32s3
```

Wichtig: Danach wieder in den Projektordner wechseln.

```sh
cd <dein-clone>/esp32-modbus-tcp-bridge
```

## 3) In jedem neuen Terminal: ESP-IDF Umgebung laden

```sh
source /Users/opaque/esp/v5.5.1/esp-idf/export.sh
```

Danach stehen `idf.py`, Toolchain und `esptool` zur Verfuegung.

## 4) Seriellen Port finden

```sh
ls /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* 2>/dev/null
```

Beispiel-Port:
- `/dev/cu.usbmodem12201`

## 5) Neues ESP komplett per USB flashen

Fuer ein frisches Board ist dieser Ablauf robust:

```sh
idf.py -p /dev/cu.usbmodem12201 erase-flash flash
```

Optional direkt mit Monitor:

```sh
idf.py -p /dev/cu.usbmodem12201 erase-flash flash monitor
```

Monitor beenden mit Ctrl + T, dann X (nacheinander, nicht gleichzeitig).

## 6) Nur bauen

```sh
idf.py build
```

Versionshinweis:
- Die angezeigte Firmware-Version kommt aus `src/config.h` (`APP_FIRMWARE_VERSION`).
- Nur `ui.html` zu aendern reicht nicht.

## 7) Welche Bin-Dateien entstehen?

Nach dem Build sind typischerweise wichtig:
- `build/modbus_bridge.bin` (App-Bin fuer OTA)
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
- `build/ota_data_initial.bin`

## 8) Eine Full-Flash-Bin (eine Datei) erzeugen

Wenn du eine einzelne Bin fuer komplettes Flashen haben willst:

```sh
idf.py merge-bin -o modbus_bridge_full.bin -f raw
```

Hinweis: `-o` ist bei `idf.py merge-bin` relativ zum `build/`-Ordner.
Die Datei liegt danach unter `build/modbus_bridge_full.bin`.

Diese Datei kannst du spaeter ohne Projektkontext direkt flashen:

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem12201 --baud 460800 write_flash 0x0 build/modbus_bridge_full.bin
```

## 9) OTA-Update (ohne USB)

1. Neue Firmware bauen (`idf.py build`)
2. OTA-App-Bin hochladen:

```sh
curl -X POST --data-binary @build/modbus_bridge.bin \
  -H "Content-Type: application/octet-stream" \
  http://192.168.1.15/ota
```

Bei Erfolg rebootet das Geraet automatisch.

## 10) Status vom Geraet per HTTP pruefen

Beispiel-IP vom ESP: `192.168.1.15`

```sh
curl -sS --max-time 8 http://192.168.1.15/status
curl -sS --max-time 8 http://192.168.1.15/config
curl -sS --max-time 8 http://192.168.1.15/registers
```

## 11) Konfiguration per HTTP setzen

Beispiel: Huawei-IP, Port, Geraete-ID, Poll-Intervall setzen.

```sh
curl -sS --max-time 8 -X POST http://192.168.1.15/config \
  -H "Content-Type: application/json" \
  --data "{\"huaweiIp\":\"192.168.1.101\",\"huaweiPort\":502,\"huaweiDev\":1,\"pollInterval\":60}"
```

## 12) Vor Modbus-Abfragen immer Port pruefen (sDongle)

```sh
nc -z -G 1 192.168.1.101 502 && echo "open" || echo "closed"
```

Hinweis: Wenn zu viele Abfragen kommen, kann der sDongle Port 502 kurzzeitig blockieren.

## 13) Fehlerhilfe kurz

- Build-Ordner neu erzeugen:
```sh
idf.py fullclean
idf.py build
```

- `idf.py` nicht gefunden:
```sh
source /Users/opaque/esp/v5.5.1/esp-idf/export.sh
```

- Python-Env fehlt (dein aktueller Fehler):
```sh
cd /Users/opaque/esp/v5.5.1/esp-idf
./install.sh esp32s3
source ./export.sh
```

- Flash-Port nicht gefunden:
  - USB-Kabel pruefen (Datenkabel, nicht nur Ladekabel)
  - Board neu verbinden
  - `ls /dev/cu.*` erneut pruefen

## 14) OTA-Hinweis fuer dieses Projekt

Das Projekt nutzt 16MB Flash mit OTA-Partitionierung.
Die Partitionstabelle hat zwei grosse OTA-Slots (`ota_0`, `ota_1`).
