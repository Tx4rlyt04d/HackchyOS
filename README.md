# HackchyOS

Mini-OS para el smartwatch [Watchy](https://watchy.sqfmi.com/) (ESP32, e-paper 200×200).
Sobre la esfera normal añade un lanzador con varias apps, todas con estética de
terminal (fuente Spleen) y pensadas, salvo dos excepciones explícitas, para
**escuchar, no emitir**.

## Apps

- **2fa / totp** — códigos TOTP (RFC 6238) de tus cuentas. Necesita la hora por NTP.
- **escáner wifi** — escaneo pasivo de redes: SSID, RSSI, canal, seguridad, BSSID,
  fabricante por OUI y ocupación de canales.
- **monitor 802.11** — modo promiscuo pasivo: cuenta beacon/probe/data/deauth y avisa
  de tormentas de deauth. Solo escucha, nunca transmite.
- **deauth test** — ⚠️ **única app que transmite.** Envía tramas de deauth 802.11 al
  punto de acceso que elijas de la lista del escáner wifi, para probar cómo
  reacciona tu propia red/dispositivos. Nunca arranca sola (hay que pulsar MENU
  para armarla), se corta a los 60s aunque no la pares tú (`DEAUTH_MAX_SECONDS`)
  y la ráfaga está limitada a propósito. **Úsala solo contra redes y dispositivos
  de tu propiedad o con autorización explícita** (igual que pide el proyecto del
  que toma la idea, [ESP8266 Deauther](https://deauther.com)); es un ataque de
  denegación de servicio y en muchas jurisdicciones usarlo contra terceros es
  ilegal.
- **ble spam** — ⚠️ **también transmite, y sin objetivo.** Emite anuncios BLE que
  imitan un company ID real (Apple/Samsung/Google) para mostrar que cualquiera
  puede anunciarse como un dispositivo BLE sin autenticación — a diferencia del
  deauth, **no eliges a quién afecta**: lo recibe cualquier móvil BLE cercano,
  no solo los tuyos. Mismas barreras (autocorte `BLESPAM_MAX_SECONDS`, armado
  manual con MENU, ráfaga limitada). Para una demo, úsala en una sala
  controlada y avisa antes a quien esté dentro: a varios les saltarán popups
  de emparejamiento sin que lo pidan.
- **bluetooth** — escáner BLE pasivo + detector de trackers (AirTag/Find My, Samsung
  SmartTag, Tile) con aviso de "te están siguiendo", y vista de densidad por RSSI.
- **pomodoro** — temporizador foco/descanso + despertador.

La esfera muestra reloj (digital + binario + semana/día ISO), pasos con histograma
por horas, fase lunar y pasos de ayer.

## Compilar y flashear

Requiere Arduino IDE con:

1. **Core ESP32** (Espressif) 2.0.5+ (probado con 3.3.x; el código soporta la
   diferencia de API de `BLEScan::start()` entre 2.x y 3.x).
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

> **Aviso:** eso protege el repositorio, no el dispositivo. Los secretos TOTP
> se compilan dentro del firmware y quedan en claro en la flash del ESP32; un
> volcado del chip (JTAG/USB con acceso físico) los expone. No hay almacenamiento
> cifrado ni PIN de bloqueo: trata la Watchy como tratarías cualquier dispositivo
> con tus setup keys de 2FA guardadas sin cifrar.

Abre `HackchyOS.ino` y pulsa Upload con la Watchy encendida y conectada.

## Uso

Desde la esfera, **UP** abre el lanzador. Dentro de una app: **UP/DOWN** navega,
**MENU** acciona/cambia de vista, **BACK** sale.
