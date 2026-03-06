# ESP32 Modbus Bridge: Terminal-Kurzstart

Diese Datei ist eine praktische Anleitung fuer den Alltag:
- Firmware bauen
- auf den ESP flashen
- Log ansehen
- OTA-Update einspielen

Die Beispiele sind fuer macOS/Linux geschrieben.

## 1) Projektordner

```sh
cd <dein-clone>/esp32-modbus-tcp-bridge
```

## 2) ESP-IDF Umgebung laden

```sh
source /Users/opaque/esp/v5.5.1/esp-idf/export.sh
```

Danach stehen `idf.py` und die Toolchain zur Verfuegung.

## 3) Seriellen Port finden

```sh
ls /dev/cu.usbmodem*
```

Beispiel-Port:
- `/dev/cu.usbmodem12201`

## 4) Firmware bauen

```sh
idf.py build
```

Das erzeugt die Firmware-Datei:
- `build/modbus_bridge.bin`

## 5) Firmware per USB flashen

```sh
idf.py -p /dev/cu.usbmodem12201 flash
```

Wenn dein Port anders heisst, den Portnamen ersetzen.

## 6) Boot-Log / Laufzeit-Log ansehen

```sh
idf.py -p /dev/cu.usbmodem12201 monitor
```

Monitor beenden mit:
- `Ctrl + ]`

## 7) Nur Build+Flash in einem Schritt

```sh
idf.py -p /dev/cu.usbmodem12201 build flash
```

## 8) Status vom Geraet per HTTP pruefen

Beispiel-IP vom ESP: `192.168.1.15`

```sh
curl -sS --max-time 8 http://192.168.1.15/status
curl -sS --max-time 8 http://192.168.1.15/config
curl -sS --max-time 8 http://192.168.1.15/registers
```

## 9) Konfiguration per HTTP setzen

Beispiel: Huawei-IP, Port, Geraete-ID, Poll-Intervall setzen.

```sh
curl -sS --max-time 8 -X POST http://192.168.1.15/config \
  -H "Content-Type: application/json" \
  --data "{\"huaweiIp\":\"192.168.1.101\",\"huaweiPort\":502,\"huaweiDev\":1,\"pollInterval\":60}"
```

## 10) Vor Modbus-Abfragen immer Port pruefen (sDongle)

```sh
nc -z -G 1 192.168.1.101 502 && echo "open" || echo "closed"
```

Hinweis: Wenn zu viele Abfragen kommen, kann der sDongle 502 kurzzeitig blockieren.

## 11) OTA-Update (ohne USB)

1. Neue Firmware bauen (`idf.py build`)
2. Bin-Datei per HTTP hochladen:

```sh
curl -X POST --data-binary @build/modbus_bridge.bin \
  -H "Content-Type: application/octet-stream" \
  http://192.168.1.15/ota
```

Bei Erfolg rebootet das Geraet automatisch.

## 12) Nützliche Fehlerhilfe

- Build-Ordner neu erzeugen:
```sh
idf.py fullclean
idf.py build
```

- Wenn `idf.py` nicht gefunden wird:
```sh
source /Users/opaque/esp/v5.5.1/esp-idf/export.sh
```

- Wenn Flash-Port nicht gefunden wird:
  - USB-Kabel pruefen (Datenkabel, nicht nur Ladekabel)
  - Board neu verbinden
  - `ls /dev/cu.*` erneut pruefen

## 13) Wichtiger OTA-Hinweis fuer dieses Projekt

Das Projekt nutzt aktuell 2MB Flash mit OTA-Partitionierung.
Die Firmware ist relativ gross und hat wenig Reserve pro OTA-Slot.
Wenn spaeter Features dazukommen, kann ein Wechsel auf 4MB-Layout sinnvoll werden.
