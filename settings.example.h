#ifndef SETTINGS_H
#define SETTINGS_H

// PLANTILLA. Copia este fichero a "settings.h" y pon tus datos reales.
// settings.h está en .gitignore: nunca se sube al repositorio.

// --- Tiempo (OpenWeatherMap) ---------------------------------------------
// Busca el ID de tu ciudad en https://openweathermap.org/find
#define CITY_ID "5128581" // Nueva York (cámbialo por el tuyo)

// También puedes usar LAT/LON en lugar de CITY_ID (pero no ambos)
//#define LAT "40.7127"
//#define LON "-74.0059"

#ifdef CITY_ID
    #define OPENWEATHERMAP_URL "http://api.openweathermap.org/data/2.5/weather?id={cityID}&lang={lang}&units={units}&appid={apiKey}"
#else
    #define OPENWEATHERMAP_URL "http://api.openweathermap.org/data/2.5/weather?lat={lat}&lon={lon}&lang={lang}&units={units}&appid={apiKey}"
#endif

#define OPENWEATHERMAP_APIKEY "PON_AQUI_TU_API_KEY" // tu propia API key de OpenWeatherMap
#define TEMP_UNIT "metric"          // metric = Celsius, imperial = Fahrenheit
#define TEMP_LANG "en"
#define WEATHER_UPDATE_INTERVAL 30  // minutos, mayor que 5

// --- NTP ---------------------------------------------------------------
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 3600 * -5    // se sobrescribe con la zona del tiempo

watchySettings settings{
    #ifdef CITY_ID
        .cityID = CITY_ID,
    #else
        .cityID = "",
        .lat = LAT,
        .lon = LON,
    #endif
    .weatherAPIKey = OPENWEATHERMAP_APIKEY,
    .weatherURL = OPENWEATHERMAP_URL,
    .weatherUnit = TEMP_UNIT,
    .weatherLang = TEMP_LANG,
    .weatherUpdateInterval = WEATHER_UPDATE_INTERVAL,
    .ntpServer = NTP_SERVER,
    .gmtOffset = GMT_OFFSET_SEC,
    .vibrateOClock = true,
};

#endif
