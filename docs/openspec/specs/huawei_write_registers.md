# Huawei Write-Register Referenzen

Diese Datei sammelt die im Projekt bisher belegten schreibbaren Huawei-Register, die fuer transparentes Modbus-Write-Forwarding relevant sind.

## Feldbefund zum Kommunikationspfad

- Im vorliegenden Aufbau ist kein `EMMA` vorhanden. Der aktive Huawei-Modbus-TCP-Endpunkt der Bridge ist der `SUN2000` bzw. `sDongle` auf `192.168.1.101:502`.
- Auf diesem Pfad sind Inverter- und Batterie-Register wie `37004` und `37006` normal lesbar.
- Die im SmartHEMS-PDF dokumentierten Wallbox-/SmartHEMS-Write-Register (`40000`, `40002`, `40107`) waren auf diesem Pfad waehrend der Live-Tests nicht direkt adressierbar und antworteten mit `Illegal Data Address`.
- Die Wallbox-IP `192.168.1.233:502` war zwar erreichbar, lieferte in den Live-Tests aber keine standardkonformen Modbus-TCP-Antworten. Selbst dokumentierte `0x2B/0x0E`-Requests fuer Device-ID und Device-List ergaben dort keinen gueltigen Huawei-Modbus-Response.
- Fuer diesen Aufbau ist deshalb der wahrscheinlich korrekte Integrationspfad weiterhin `sDongle`/`Inverter`, nicht die direkte Wallbox-IP. Ob die Wallbox-Register ueber den sDongle nach aussen exponiert werden, ist aktuell nicht belegt.

## Relevante Huawei-Dokumentation

- SmartHEMS/Wallbox: `SmartHEMS_Modbus_Interface_Definitions_2024-07-15.pdf`
  - Ethernet-Port `502`
  - Geraeteadressierung ueber `logical device ID`
  - `0x2B/0x0E` fuer Device-ID und Device-List
- Inverter: `Solar_Inverter_Modbus_Interface_Definitions_V5_2023-02-16.pdf`

Hinweis:
Die Feldtests widersprechen nicht den Registerdefinitionen selbst, sondern zeigen, dass der reale Zugriffspfad im sDongle-Szenario nicht automatisch alle dokumentierten Wallbox-/SmartHEMS-Register als direkt lesbaren Modbus-TCP-Endpunkt bereitstellt.

## Wallbox / SmartHEMS

- `40000` - `ESS control mode` (`RW`, ENUM16)
  Quelle: `Wallbox_SmartHEMS_Modbus_Interface_Definitions_2024-07-15.html`
- `40002` - `[Time of Use mode] Preferred use of surplus PV power` (`RW`, ENUM16)
  Quelle: `Wallbox_SmartHEMS_Modbus_Interface_Definitions_2024-07-15.html`
- `40107` - `Maximum grid feed-in power (kW)` (`RW`, I32)
  Quelle: `Wallbox_SmartHEMS_Modbus_Interface_Definitions_2024-07-15.html`

## Batterie / System

- `37006` - `[System level] Charge/Discharge mode`
  Dokumentierte Modi umfassen unter anderem `Forced charge/discharge`, `TOU`, `Max. self-consumption` und `AI control`.
  Quelle: `Inverter_Solar_Inverter_Modbus_Interface_Definitions_V5_2023-02-16.html`
- `47082` - `Discharge cutoff capacity`
  Live ueber Home Assistant und die Bridge verifiziert. Werte wie `15.0 %`, `16.0 %` und `19.5 %` wurden als `FC06` erfolgreich an Huawei weitergeleitet; `19.5 %` wurde anschliessend in FusionSolar bestaetigt.
  Praktische Skalierung: Prozentwert `x.y` wird als Rohwert `xy*10` geschrieben, z. B. `19.5 % -> 195`.
  Quelle: Live-Test gegen `192.168.1.101:502` ueber die ESP32-Bridge sowie `Solar_Inverter_Modbus_Interface_Definitions_V5_2023-02-16.html`
- `47088` - `Maximum discharging power`
  Live ueber Home Assistant und die Bridge verifiziert. Eine Aenderung von `5000 W` auf `4567 W` wurde als `FC06` erfolgreich an Huawei weitergeleitet und bestaetigt.
  Quelle: Live-Test gegen `192.168.1.101:502` ueber die ESP32-Bridge sowie `Solar_Inverter_Modbus_Interface_Definitions_V5_2023-02-16.html`

## Wechselrichter / Inverter

- `47589` - `[Inverter level] Remote charge/discharge control mode` (`RW`, ENUM16)
  Dokumentierte Modi umfassen `Local control`, `Remote control - Max. self-consumption`, `Remote control - Fully fed to grid`, `Remote control - TOU` und `Remote control - AI control`.
  Quelle: `Inverter_Solar_Inverter_Modbus_Interface_Definitions_V5_2023-02-16.html`
- `47674` - `Scheduled task` (`RW`, ENUM16)
  Quelle: `Inverter_Solar_Inverter_Modbus_Interface_Definitions_V5_2023-02-16.html`
- `47675` - `Default maximum feed-in power` (`RW`, INT32)
  Quelle: `Inverter_Solar_Inverter_Modbus_Interface_Definitions_V5_2023-02-16.html`

Hinweis:
Diese Liste ist kein Vollauszug der Huawei-Dokumentation. Sie dient als verifizierte Referenz fuer Tests und fuer die Begruendung, warum die Bridge `FC06` und `FC10` transparent weiterleiten muss. Die reine Dokumentation eines Registers bedeutet nicht automatisch, dass es ueber jeden realen Huawei-Zugriffspfad direkt erreichbar ist. Wo explizit als Live-Test markiert, ist der Write-Pfad nicht nur dokumentiert, sondern auf der realen Anlage praktisch bestaetigt.
