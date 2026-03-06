# ESP32 Modbus TCP Bridge for Huawei Inverter

## Hintergrund
Ein ESP32-S3 ETH Entwicklungsboard soll in einem LAN (PoE) als Modbus TCP Server fungieren. Es agiert lediglich als **Lese-/Proxy-Schicht** – der Huawei Sun 2000 Wechselrichter bleibt das ursprüngliche Modbus‑TCP-Gerät (Master kann nur einen Client bedienen). Der ESP32 liest zyklisch die Werte vom Wechselrichter und stellt sie parallel mehreren anderen Clients zur Verfügung, darunter:

- Technische Alternative Regler (TA)
- Home Assistant
- Weitere Systeme (Solaranzeige.de, SmartMeter, Batterie, Wallbox, etc.)

Probleme:
1. Der Huawei Modbus TCP liefert nicht die gewünschten Daten für TA-Regler.
2. Huawei Modbus TCP kann nur einer Schnittstelle antworten.
3. Mehrere Systeme benötigen gleichzeitig Zugriff auf dieselben Werte.


## Ziel
Ein Programm für das ESP32-S3 ETH, das als Modbus TCP Server arbeitet und:

1. Als Modbus TCP Client die relevanten Register vom Huawei Sun 2000 Wechselrichter liest.
2. Die gelesenen Werte an beliebig viele Modbus TCP Clients weiterreicht (Proxy/Middleware-Funktionalität).
3. Eine Webkonfiguration bereitstellt, die über einen vom ESP32 erzeugten WLAN-Access-Point erreichbar ist.
   - SSID z.B. `ModBus Server`
   - AP aktiv für die ersten ~30 Sekunden nach dem Start, danach deaktiviert.
   - Konfiguration: Huawei-Inverter-IP/Port, LAN-Schnittstelle des ESP, Passwort, etc.
   - Parameter werden in nicht-flüchtigem Speicher gesichert (NVS / SPIFFS / LittleFS).
4. In einem produktiven LAN per PoE betreibbar.
5. Unterstützung von Standardmodbus (TCP) mit mehreren gleichzeitigen Verbindungen.


## Nichtziele
- Keine Unterstützung von anderen Invertersystemen als Huawei Sun 2000.
- Keine Weboberfläche für Auswertung der Daten (nur Konfiguration).
- Keine Integration in andere Ökosysteme außer standardisiertem Modbus TCP.


## Überlegungen
- Der ESP32 wird im AP-Modus für die Konfiguration aufgeweckt, danach im Station/ETH-Modus betrieben.
- Verwendung von ESP-IDF als SDK, Nutzung von lwIP Modbus-TCP-Bibliothek oder eigene einfache Implementierung.
- NVS zur Speicherung der Konfiguration.
- Sicherstellung, dass mehrere Clients gleichzeitig verbunden bleiben können.


---

Dieses Proposal stellt die Basis für die Implementierung im Rahmen dieser Änderung dar.