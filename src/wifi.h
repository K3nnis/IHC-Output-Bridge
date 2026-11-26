// Wi-Fi: forbindelse til routeren, opsætnings-AP og nødbetjenings-AP.
//
// Beslutningerne træffes af netvaerk::WifiStyring (lib/netvaerk), som er
// testet på PC. Dette modul udfører dem med ESP-IDF's Wi-Fi-driver i en
// task på kerne 0. Bussen og reglerne på kerne 1 påvirkes ikke.
#pragma once

#include <cstdint>

#include "wifi_styring.h"

namespace wifi {

constexpr const char *kOpsaetningSsid = "IHC-Bridge-Opsætning";
constexpr const char *kNoedbetjeningSsid = "IHC-Bridge-Nødbetjening";
constexpr const char *kStandardVaertsnavn = "ihc-bridge";

struct Konfig {
  char ssid[33] = {};
  char kode[65] = {};
  char vaertsnavn[33] = {};
  bool fast_ip = false;
  // IPv4-adresser i netværksbyte-orden (som esp_ip4_addr_t.addr).
  uint32_t ip = 0;
  uint32_t maske = 0;
  uint32_t gateway = 0;
  uint32_t dns = 0;
};

struct Status {
  bool forbundet = false;
  char ssid[33] = {};
  char ip[16] = {};
  int8_t rssi = 0;  // signalstyrke i dBm (kun når forbundet)
  netvaerk::ApTilstand ap = netvaerk::ApTilstand::kSlukket;
  uint32_t forsoeg = 0;          // forsøg siden forbindelsen blev tabt
  int64_t ms_til_naeste = 0;     // tid til næste forsøg
  char vaertsnavn[33] = {};
};

// Starter Wi-Fi. NVS skal være initialiseret (tilstand::init()).
void start();

Status status();

// Henter gemt konfiguration (uden at afsløre koden, hvis skjul_kode).
bool hent_konfig(Konfig &k, bool skjul_kode = true);
// Gemmer ny konfiguration og forbinder med den med det samme.
bool gem_konfig(const Konfig &k);

// Kaldes, når UI-adgangskoden (og dermed nødbetjenings-AP'ets kode) er ændret.
void noed_kode_aendret();

}  // namespace wifi
