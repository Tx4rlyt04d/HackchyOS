#include "Watchy_HackchyOS.h"
#include "settings.h"

WatchyHackchyOS watchy(settings);

void setup() {
  Serial.begin(115200);   // para ver los logs del driver WiFi al depurar el deauth
  watchy.init();
}

void loop() {}
