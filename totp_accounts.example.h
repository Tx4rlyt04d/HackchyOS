#ifndef TOTP_ACCOUNTS_H
#define TOTP_ACCOUNTS_H

// PLANTILLA. Copia este fichero a "totp_accounts.h" y pon tus cuentas reales.
// totp_accounts.h está en .gitignore: nunca se sube al repositorio.
//
// El secreto es la clave en Base32 que te da el servicio al activar el 2FA
// (el texto que hay detrás del código QR, "setup key"). Trátalo como
// información sensible.
//
// OJO: el TOTP necesita la hora bien puesta. Sincroniza por NTP
// (menú de Watchy) o los códigos no coincidirán.

struct TotpAccount { const char *name; const char *secret; };

static const TotpAccount TOTP_ACCOUNTS[] = {
  {"Ejemplo", "JBSWY3DPEHPK3PXP"},   // <- sustituye por tus cuentas
};
static const int TOTP_COUNT = sizeof(TOTP_ACCOUNTS) / sizeof(TOTP_ACCOUNTS[0]);

#endif
