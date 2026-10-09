#ifndef WATCHY_HACKCHYOS_H
#define WATCHY_HACKCHYOS_H

#include <Watchy.h>
#include "apps.h"

// ---- Ajustes ---------------------------------------------------------------
#ifndef HT_DARKMODE
#define HT_DARKMODE  true
#endif
#ifndef STEP_GOAL
#define STEP_GOAL    10000
#endif
#ifndef MOON_SOUTH
#define MOON_SOUTH   false
#endif
#ifndef POMO_WORK_MIN
#define POMO_WORK_MIN   25
#endif
#ifndef POMO_BREAK_MIN
#define POMO_BREAK_MIN  5
#endif
// ---------------------------------------------------------------------------

class WatchyHackchyOS : public Watchy {
public:
  explicit WatchyHackchyOS(const watchySettings &s) : Watchy(s) {}

  void drawWatchFace() override;             // esfera (y chequeo del despertador)
  void handleButtonPress() override;         // enruta botones hacia la mini-OS

  // Pantallas (públicas para poder renderizarlas en el simulador)
  void drawLauncher();
  void drawTOTP();
  void drawWiFi();
  void drawPomodoro();
  void drawMonitor();
  void drawBluetooth();

private:
  // --- mini-OS ---
  void runApps();                            // bucle interactivo mientras hay una app abierta
  void monitorSample();                       // una ventana de escucha del monitor (firmware)
  void bleScan();                             // una ronda de escaneo BLE pasivo (firmware)
  const char *bleVendor(uint16_t cid);        // fabricante por company id de BLE
  const char *trkLabel(uint8_t kind);         // etiqueta del tipo de tracker
  void openApp(AppId id);
  void renderCurrent();                       // dibuja la pantalla según hackState
  bool alarmDue();                            // ¿toca sonar el despertador?

  // --- esfera (igual que HackTerm) ---
  void updateStepLog(uint32_t steps);
  void drawClockPanel();
  void drawStepsPanel(uint32_t steps);
  void drawMoonPanel();
  void drawYesterdayPanel();
  void binaryClock(int16_t cx, int16_t y);

  // --- helpers compartidos ---
  void panel(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
  int16_t title(int16_t x, int16_t line, const char *s, bool alignRight = false,
                bool inverted = false, int16_t extra = 0);
  void batteryIcon(int16_t x, int16_t y, uint8_t pct);
  void moonIcon(int16_t cx, int16_t cy, int16_t r, double phase);
  void signalBars(int16_t x, int16_t y, int8_t rssi);
  void lockIcon(int16_t x, int16_t y, bool open);
  void securityTag(uint8_t auth, char *out);
  const char *ouiVendor(const uint8_t *mac);
  void textRight(int16_t xRight, int16_t baseline, const char *s);
  int16_t textWidth(const char *s);
  void centerText(int16_t cx, int16_t baseline, const char *s);
  uint8_t batteryPercent(float vbat);
  double moonPhase();
  uint64_t unixUTC();

  uint16_t fg() const { return HT_DARKMODE ? GxEPD_WHITE : GxEPD_BLACK; }
  uint16_t bg() const { return HT_DARKMODE ? GxEPD_BLACK : GxEPD_WHITE; }
};

#endif
