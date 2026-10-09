# HackchyOS

Mini-OS para el smartwatch [Watchy](https://watchy.sqfmi.com/) (ESP32, e-paper 200×200).
Sobre la esfera normal añade un lanzador con varias apps, todas con estética de
terminal (fuente Spleen) y pensadas para **escuchar, no emitir**.

## Apps

- **2fa / totp** — códigos TOTP (RFC 6238) de tus cuentas. Necesita la hora por NTP.
- **escáner wifi** — escaneo pasivo de redes: SSID, RSSI, canal, seguridad, BSSID,
  fabricante por OUI y ocupación de canales.
- **monitor 802.11** — modo promiscuo pasivo: cuenta beacon/probe/data/deauth y avisa
  de tormentas de deauth. Solo escucha, nunca transmite.
- **bluetooth** — escáner BLE pasivo + detector de trackers (AirTag/Find My, Samsung
  SmartTag, Tile) con aviso de "te están siguiendo", y vista de densidad por RSSI.
- **pomodoro** — temporizador foco/descanso + despertador.

La esfera muestra reloj (digital + binario + semana/día ISO), pasos con histograma
por horas, fase lunar y pasos de ayer.

## Compilar y flashear

Requiere Arduino IDE con:

1. **Core ESP32** (Espressif) 2.0.5+ (probado con 3.3.x).
2. Librerías: **Watchy** (SQFMI) y **Rtc_Pcf8563**. El BLE lo aporta el core, no
   instales NimBLE (choca con el BLE que incluye Watchy).

Ajustes de placa (*Herramientas*):

- **Placa:** Watchy (o `ESP32 Dev Module`).
- **Partition Scheme:** `Huge APP (3MB No OTA)` — imprescindible, WiFi + BLE no caben
  en el esquema por defecto.
- **Flash Size:** 4MB.

Antes de compilar, crea tus ficheros de configuración a partir de las plantillas:

```sh
cp settings.example.h settings.h
cp totp_accounts.example.h totp_accounts.h
# edita settings.h (ciudad + API key de OpenWeatherMap) y totp_accounts.h (tus cuentas)
```

> `settings.h` y `totp_accounts.h` contienen secretos y están en `.gitignore`:
> no se suben al repositorio.

Abre `HackchyOS.ino` y pulsa Upload con la Watchy encendida y conectada.

## Uso

Desde la esfera, **UP** abre el lanzador. Dentro de una app: **UP/DOWN** navega,
**MENU** acciona/cambia de vista, **BACK** sale.
