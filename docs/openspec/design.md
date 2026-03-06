## Context

Der ESP32-S3 ETH agiert als Modbus-TCP-Middleware zwischen dem Huawei Sun 2000 Wechselrichter (nur-lesen, ein Client gleichzeitig) und mehreren Abnehmern (TA-Regler, Home Assistant, Solaranzeige.de u.a.). Die Firmware basiert auf ESP-IDF; Netzwerk läuft über das integrierte Ethernet-Interface (PoE).

Referenz-Spezifikationen: `specs/functional.md`, `specs/huawei_registers.md`, `specs/ta_regler_setup.md`.

---

## Modbus TCP Client — keine externe Bibliothek

**Entscheidung**: Eigene schlanke FC03-Implementierung über raw lwIP-Socket. Keine externe Modbus-Bibliothek (kein freemodbus, kein libmodbus-Port).

**Begründung**: Huawei Sun 2000 nutzt ausschließlich Function Code 03 (Read Holding Registers). Das Protokoll (MBAP-Header + PDU) ist in ~50 Zeilen C abbildbar. Keine Dependency, volle Kontrolle über Timeouts und Fehlerbehandlung.

**Alternative verworfen**: `esp-modbus` (freemodbus-Fork) — TCP-Support eingeschränkt, ursprünglich für RTU ausgelegt; Overhead nicht gerechtfertigt.

**Referenz**: Solaranzeige.de (Raspberry Pi) kommuniziert mit dem Huawei SDongle ebenfalls über **TCP/IP** (nicht RS485/USB). Der SDongle stellt das Modbus-TCP-Interface bereit; Solaranzeige.de fragt per `huawei_M1.php` über das Netzwerk ab. Unser ESP32 tut dasselbe — nur direkt in C.

---

## Register-Cache: 4 feste Blöcke

**Entscheidung**: Der Hintergrundtask liest alle 60 s genau **4 zusammenhängende FC03-Blöcke** und speichert die rohen 16-Bit-Werte in statischen Arrays.

```
Block A: Reg 30000–30074   → uint16_t cache_a[75]   (Geräteinfo, Modell, SN, Firmware) — statisch, einmalig beim Start
Block B: Reg 32000–32116   → uint16_t cache_b[117]  (PV-Strings, AC-Leistung, Temperatur…) — alle 60 s
Block C: Reg 37000–37122   → uint16_t cache_c[123]  (Batterie SOC, Leistung, Netzimport…) — alle 60 s
Block D: Reg 38210–38233   → uint16_t cache_d[24]   (Batterie-Pack-Detail) — optional, alle 60 s

Gesamt RAM: (75+117+123+24) × 2 Byte = ~678 Byte + 4 Validity-Flags + 4 Timestamps
```

Jeder Block hat ein `valid`-Flag und einen `last_updated`-Timestamp. Ein separater RW-Mutex schützt den Lese-/Schreibzugriff zwischen Client-Handler-Task und Poll-Task.

**Zugriff**: `reg_cache_lookup(uint16_t address, uint16_t *value)` prüft in welchem Block die Adresse liegt und gibt den Wert zurück. Ist der betreffende Block nicht `valid`, wird kein Wert zurückgegeben (siehe Cache-Stale-Verhalten).

---

## Cache-Stale-Verhalten bei Inverter-Ausfall

**Entscheidung**: Wenn der Wechselrichter nicht erreichbar ist (`mb_inverter_connected = false` bzw. `valid = false`), erhalten anfragende Modbus-Clients **keine** gecachten Werte — stattdessen wird **Modbus Exception 0x04** (Server Device Failure) zurückgegeben.

**Begründung**: Wenn Home Assistant oder andere Automatisierungen auf Basis von PV-Leistung oder Batterie-SOC Geräte schalten, könnten veraltete Werte zu Fehlverhalten führen (z.B. Gerät einschalten, obwohl keine PV-Leistung vorhanden). Keine Daten ist sicherer als falsche Daten.

```
reg_cache_lookup():
  if (!block.valid) return MB_EXCEPTION_DEVICE_FAILURE; // 0x04
  return block.cache[offset];
```

**Proxy-Modus und Mapping-Modus** verhalten sich identisch: beide liefern bei ungültigem Cache eine Exception.

---

## Proxy-Modus vs. Mapping-Modus

**Proxy-Modus** (Mapping-Checkbox aus):
- Alle Cache-Blöcke werden bedient.
- Ein Client fragt Register X → liegt X in Block A/B/C/D? → Wert aus Cache (sofern valid).
- X nicht im Cache → Modbus Exception 0x02 (Illegal Data Address).
- Keine Transformation, rohe Huawei-Werte.

**Mapping-Modus** (Mapping-Checkbox an):
- Zusätzlich zum Proxy-Cache werden konfigurierte Mapping-Einträge bedient.
- Mapping-Eintrag: `{mapped_register, source_register, factor, type, byte_order}`
- Client fragt mapped_register X → suche source_register im Cache → wende factor an → antworte.
- Proxy-Register bleiben weiterhin verfügbar (beide Modi gleichzeitig aktiv).
- Nicht konfiguriertes Register → Exception 0x02.

---

## Mehrere Modbus-Clients

**Entscheidung**: Mehrere gleichzeitige Clients sind erlaubt (kein künstliches Limit).

**Implementierung**: Ein Accept-Task nimmt neue TCP-Verbindungen an und spawnt pro Client einen eigenen FreeRTOS-Task (Stack ~2 KB). Maximale gleichzeitige Clients durch `CONFIG_LWIP_MAX_SOCKETS` begrenzt (Standard 16).

---

## AP-Timeout & Verbindungserkennung

**Entscheidung**: AP bleibt 60 Sekunden offen. Timer wird immer dann zurückgesetzt, wenn `esp_wifi_ap_get_sta_list()` mindestens eine verbundene Station zurückgibt.

```
Timer-Loop (1s-Tick via esp_timer):
  if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0)
      timer_remaining = 60;   // verlängern
  else
      timer_remaining--;
  if (timer_remaining == 0)
      esp_wifi_stop();        // AP abschalten
```

**Neustart des AP**: Kurzer Reset des ESP32 (EN-Taste) öffnet den AP erneut für 60 s.

---

## Factory Reset via GPIO0 (BOOT-Taste)

**Entscheidung**: Langdruck ≥ 10 s auf GPIO0 (BOOT-Taste) löscht NVS und startet neu.

```
GPIO0-Task (1s-Poll):
  if (gpio_get_level(GPIO_NUM_0) == 0)  // gedrückt = LOW
      press_duration++;
  else
      press_duration = 0;
  if (press_duration >= 10) {
      nvs_flash_erase();
      esp_restart();
  }
```

GPIO0 ist während des normalen Betriebs als normaler Input nutzbar (kein Konflikt mit Boot-Logik nach dem Start).

---

## Wiederverbindung bei Inverter-Ausfall

**Entscheidung**: Festes Retry-Intervall = Poll-Intervall (Standard 60 s). Kein exponential backoff.

**Begründung**: Huawei mag zu häufige Verbindungsversuche nicht. 60 s ist ohnehin das minimale sinnvolle Intervall. Bei Verbindungsfehler wird `mb_inverter_connected = false` gesetzt und alle Block-`valid`-Flags auf `false`. Clients erhalten dann eine Modbus Exception 0x04 (kein stale cache).

---

## AP-Passwort: Eingabe mit Wiederholung

**Entscheidung**: Das AP-Passwort-Feld im Netzwerk-Tab hat ein zweites Feld „Passwort wiederholen". Beim Abspeichern prüft JavaScript clientseitig, ob beide Felder übereinstimmen — nur dann wird das Formular abgeschickt.

```html
<input type="password" id="ap_pw"  name="ap_pw"  placeholder="Neues Passwort">
<input type="password" id="ap_pw2" name="ap_pw2" placeholder="Passwort wiederholen">
```

```js
if (document.getElementById('ap_pw').value !==
    document.getElementById('ap_pw2').value) {
    alert('Passwörter stimmen nicht überein');
    return false;
}
```

Serverseitig wird das zweite Feld ignoriert; die Prüfung ist rein clientseitig (kein sicherheitskritischer Kontext — Konfigurationsnetz).

---

## Risiken

- **Block B zu groß für einen Request**: Huawei limitiert manche FC03-Reads auf max. 125 Register. Block B (117 Reg.) und Block C (123 Reg.) liegen unter dem Limit — bei Problemen in zwei Teilanfragen aufteilen.
- **GPIO0-Langdruck während Flash**: Kein Risiko nach dem Boot; die Boot-ROM-Logik ist nach dem App-Start inaktiv.
- **Task-Stack Overflow bei vielen Clients**: Monitoring via `uxTaskGetStackHighWaterMark()` im Statuslog empfohlen.
- **Stale-Cache und Exception**: Wenn der Wechselrichter kurzzeitig nicht erreichbar ist (z.B. Nacht/Standby), können Modbus-Clients kurz Exceptions erhalten. Das ist gewollt — es ist sicherer als Phantomwerte.
