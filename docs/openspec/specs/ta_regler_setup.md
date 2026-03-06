# TA‑Regler: Modbus‑Client‑Konfiguration

Um Werte vom ESP32‑Modbus‑Proxy (der die Daten vom Huawei‑Inverter liefert) einzulesen, muss der Technische‑Alternative‑Regler als Modbus‑Master richtig eingerichtet werden. Das folgende Beispiel orientiert sich an der von dir gezeigten Oberfläche:

![TA Einstellungsseite](attachment://screenshot.png)

## Felder und Hinweise
1. **Bezeichnung** – frei wählbarer Name, z.B. `Huawei-Sun2000`.
2. **IP/Port/Device/Function/Adresse** – diese Einstellungen müssen auf die Werte passen, die der ESP32 bereitstellt:
   - IP: Adresse des ESP32 im LAN (oder über AP bei Konfiguration).
   - Gerät/Slave‑ID: gewöhnlich 1 (unser Proxy verwendet nur eine ID).
   - Funktion: in der Regel `03 - read holding registers` (die meisten Werte stehen dort).
   - Adresse: Startregister, z. B. `1` oder jedes andere in unserer Cache‑Map definierte Register.
   - Datentyp, Byte‑Reihenfolge: je nach konkretem Register (siehe Tabelle `huawei_registers.md`).
3. **Intervall** – wie oft der Regler Abfragen schickt; 10 Sekunden ist üblich.
4. **Timeout** – Wartezeit bei fehlender Antwort; kann 5 Min stehen.
5. **Eingangswert** – nicht relevant für Lese‑Konfiguration (wird angezeigt).
6. **Faktor, Einheit, Wert bei Timeout, Ausgabewert** – interne Skalierung beim Regler, kann je nach Anwendung angepasst werden.

> 🔧 In der Praxis reicht es meist, einen oder mehrere Einträge mit Funktion 03 und passenden Adressen einzurichten, um die gewünschten Werte (z.B. SoC, Leistung, Spannung) zu erhalten.

## Integration in unseren Systemen
* Der ESP32 stellt die Werte in fortlaufenden Registeradressen bereit (beginnend z. B. bei 1). Der Regler kann dann mehrere aufeinanderfolgende Register abfragen, um z.B. Spannung, Strom und Leistung gleichzeitig zu lesen.
* Für spezielle Skalierungen (z. B. %‑Werte/10) muss der TA‑Regler die passenden Faktoren eintragen oder wir legen alternative Register mit vorangepasstem Wert an.

## Dokumentation für Benutzer
Wenn wir den ESP32 deployen, sollten wir in der README bzw. Web‑UI einen Abschnitt aufnehmen, der die erforderlichen Einstellungen am TA‑Regler beschreibt – idealerweise inklusive eines Screenshots wie oben.

---

Diese Anleitung hilft Nutzern, ihren TA‑Regler korrekt zu konfigurieren, damit er problemlos die von unserem ESP32 bereitgestellten Werte auslesen kann.