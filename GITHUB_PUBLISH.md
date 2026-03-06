# GitHub Publish Anleitung

Diese Schritte fuehrst du lokal im Terminal aus.

## 1) In den GitHub-Ready Ordner wechseln

```sh
cd /Users/opaque/Documents/ESP32ModBus_OpenSpec/github-ready/esp32-modbus-tcp-bridge
```

## 2) Optional: Lizenzdatei anlegen

Empfehlung fuer Open-Source: MIT.

```sh
cat > LICENSE <<'EOF'
MIT License

Copyright (c) 2026

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
EOF
```

## 3) Git initialisieren und ersten Commit machen

```sh
git init
git add .
git commit -m "Initial release v1.0.0"
```

## 4) GitHub Repo anlegen

Auf GitHub ein neues leeres Repo erstellen, z.B.:
- `esp32-modbus-tcp-bridge`

Dann Remote setzen:

```sh
git remote add origin git@github.com:<dein-user>/esp32-modbus-tcp-bridge.git
```

Oder via HTTPS:

```sh
git remote add origin https://github.com/<dein-user>/esp32-modbus-tcp-bridge.git
```

## 5) Main-Branch pushen

```sh
git branch -M main
git push -u origin main
```

## 6) Version-Tag setzen (v1.0.0)

```sh
git tag -a v1.0.0 -m "Release 1.0.0"
git push origin v1.0.0
```

## 7) Spaetere Releases

```sh
git add .
git commit -m "..."
git push
git tag -a v1.0.1 -m "Release 1.0.1"
git push origin v1.0.1
```
