#ifndef HACKCHYOS_APPS_H
#define HACKCHYOS_APPS_H

// Estados de la "mini-OS" que viven sobre WATCHFACE_STATE / APP_STATE de Watchy.
enum HackState {
  HS_WATCHFACE = 0,   // esfera normal
  HS_LAUNCHER,        // lanzador de apps
  HS_TOTP,            // autenticador 2FA
  HS_WIFI,            // escáner WiFi
  HS_MON,             // monitor pasivo de tramas
  HS_DEAUTH,          // test de deauth (activo: transmite) — solo en tu propia red
  HS_BLESPAM,         // spam BLE (activo: transmite) — indiscriminado, cualquiera cerca
  HS_POMO,            // pomodoro + despertador
  HS_BLE,             // escáner Bluetooth LE + detector de trackers
};

// Apps del lanzador
enum AppId { APP_TOTP = 0, APP_WIFI, APP_MON, APP_DEAUTH, APP_BLESPAM, APP_BLE, APP_POMO, APP_COUNT };

#endif
